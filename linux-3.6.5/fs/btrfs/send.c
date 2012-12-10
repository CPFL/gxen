/*
 * Copyright (C) 2012 Alexander Block.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/bsearch.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/sort.h>
#include <linux/mount.h>
#include <linux/xattr.h>
#include <linux/posix_acl_xattr.h>
#include <linux/radix-tree.h>
#include <linux/crc32c.h>
#include <linux/vmalloc.h>

#include "send.h"
#include "backref.h"
#include "locking.h"
#include "disk-io.h"
#include "btrfs_inode.h"
#include "transaction.h"

static int g_verbose = 0;

#define verbose_printk(...) if (g_verbose) printk(__VA_ARGS__)

/*
 * A fs_path is a helper to dynamically build path names with unknown size.
 * It reallocates the internal buffer on demand.
 * It allows fast adding of path elements on the right side (normal path) and
 * fast adding to the left side (reversed path). A reversed path can also be
 * unreversed if needed.
 */
struct fs_path {
	union {
		struct {
			char *start;
			char *end;
			char *prepared;

			char *buf;
			int buf_len;
			int reversed:1;
			int virtual_mem:1;
			char inline_buf[];
		};
		char pad[PAGE_SIZE];
	};
};
#define FS_PATH_INLINE_SIZE \
	(sizeof(struct fs_path) - offsetof(struct fs_path, inline_buf))


/* reused for each extent */
struct clone_root {
	struct btrfs_root *root;
	u64 ino;
	u64 offset;

	u64 found_refs;
};

#define SEND_CTX_MAX_NAME_CACHE_SIZE 128
#define SEND_CTX_NAME_CACHE_CLEAN_SIZE (SEND_CTX_MAX_NAME_CACHE_SIZE * 2)

struct send_ctx {
	struct file *send_filp;
	loff_t send_off;
	char *send_buf;
	u32 send_size;
	u32 send_max_size;
	u64 total_send_size;
	u64 cmd_send_size[BTRFS_SEND_C_MAX + 1];

	struct vfsmount *mnt;

	struct btrfs_root *send_root;
	struct btrfs_root *parent_root;
	struct clone_root *clone_roots;
	int clone_roots_cnt;

	/* current state of the compare_tree call */
	struct btrfs_path *left_path;
	struct btrfs_path *right_path;
	struct btrfs_key *cmp_key;

	/*
	 * infos of the currently processed inode. In case of deleted inodes,
	 * these are the values from the deleted inode.
	 */
	u64 cur_ino;
	u64 cur_inode_gen;
	int cur_inode_new;
	int cur_inode_new_gen;
	int cur_inode_deleted;
	int cur_inode_first_ref_orphan;
	u64 cur_inode_size;
	u64 cur_inode_mode;

	u64 send_progress;

	struct list_head new_refs;
	struct list_head deleted_refs;

	struct radix_tree_root name_cache;
	struct list_head name_cache_list;
	int name_cache_size;

	struct file *cur_inode_filp;
	char *read_buf;
};

struct name_cache_entry {
	struct list_head list;
	struct list_head use_list;
	u64 ino;
	u64 gen;
	u64 parent_ino;
	u64 parent_gen;
	int ret;
	int need_later_update;
	int name_len;
	char name[];
};

static void fs_path_reset(struct fs_path *p)
{
	if (p->reversed) {
		p->start = p->buf + p->buf_len - 1;
		p->end = p->start;
		*p->start = 0;
	} else {
		p->start = p->buf;
		p->end = p->start;
		*p->start = 0;
	}
}

static struct fs_path *fs_path_alloc(struct send_ctx *sctx)
{
	struct fs_path *p;

	p = kmalloc(sizeof(*p), GFP_NOFS);
	if (!p)
		return NULL;
	p->reversed = 0;
	p->virtual_mem = 0;
	p->buf = p->inline_buf;
	p->buf_len = FS_PATH_INLINE_SIZE;
	fs_path_reset(p);
	return p;
}

static struct fs_path *fs_path_alloc_reversed(struct send_ctx *sctx)
{
	struct fs_path *p;

	p = fs_path_alloc(sctx);
	if (!p)
		return NULL;
	p->reversed = 1;
	fs_path_reset(p);
	return p;
}

static void fs_path_free(struct send_ctx *sctx, struct fs_path *p)
{
	if (!p)
		return;
	if (p->buf != p->inline_buf) {
		if (p->virtual_mem)
			vfree(p->buf);
		else
			kfree(p->buf);
	}
	kfree(p);
}

static int fs_path_len(struct fs_path *p)
{
	return p->end - p->start;
}

static int fs_path_ensure_buf(struct fs_path *p, int len)
{
	char *tmp_buf;
	int path_len;
	int old_buf_len;

	len++;

	if (p->buf_len >= len)
		return 0;

	path_len = p->end - p->start;
	old_buf_len = p->buf_len;
	len = PAGE_ALIGN(len);

	if (p->buf == p->inline_buf) {
		tmp_buf = kmalloc(len, GFP_NOFS);
		if (!tmp_buf) {
			tmp_buf = vmalloc(len);
			if (!tmp_buf)
				return -ENOMEM;
			p->virtual_mem = 1;
		}
		memcpy(tmp_buf, p->buf, p->buf_len);
		p->buf = tmp_buf;
		p->buf_len = len;
	} else {
		if (p->virtual_mem) {
			tmp_buf = vmalloc(len);
			if (!tmp_buf)
				return -ENOMEM;
			memcpy(tmp_buf, p->buf, p->buf_len);
			vfree(p->buf);
		} else {
			tmp_buf = krealloc(p->buf, len, GFP_NOFS);
			if (!tmp_buf) {
				tmp_buf = vmalloc(len);
				if (!tmp_buf)
					return -ENOMEM;
				memcpy(tmp_buf, p->buf, p->buf_len);
				kfree(p->buf);
				p->virtual_mem = 1;
			}
		}
		p->buf = tmp_buf;
		p->buf_len = len;
	}
	if (p->reversed) {
		tmp_buf = p->buf + old_buf_len - path_len - 1;
		p->end = p->buf + p->buf_len - 1;
		p->start = p->end - path_len;
		memmove(p->start, tmp_buf, path_len + 1);
	} else {
		p->start = p->buf;
		p->end = p->start + path_len;
	}
	return 0;
}

static int fs_path_prepare_for_add(struct fs_path *p, int name_len)
{
	int ret;
	int new_len;

	new_len = p->end - p->start + name_len;
	if (p->start != p->end)
		new_len++;
	ret = fs_path_ensure_buf(p, new_len);
	if (ret < 0)
		goto out;

	if (p->reversed) {
		if (p->start != p->end)
			*--p->start = '/';
		p->start -= name_len;
		p->prepared = p->start;
	} else {
		if (p->start != p->end)
			*p->end++ = '/';
		p->prepared = p->end;
		p->end += name_len;
		*p->end = 0;
	}

out:
	return ret;
}

static int fs_path_add(struct fs_path *p, const char *name, int name_len)
{
	int ret;

	ret = fs_path_prepare_for_add(p, name_len);
	if (ret < 0)
		goto out;
	memcpy(p->prepared, name, name_len);
	p->prepared = NULL;

out:
	return ret;
}

static int fs_path_add_path(struct fs_path *p, struct fs_path *p2)
{
	int ret;

	ret = fs_path_prepare_for_add(p, p2->end - p2->start);
	if (ret < 0)
		goto out;
	memcpy(p->prepared, p2->start, p2->end - p2->start);
	p->prepared = NULL;

out:
	return ret;
}

static int fs_path_add_from_extent_buffer(struct fs_path *p,
					  struct extent_buffer *eb,
					  unsigned long off, int len)
{
	int ret;

	ret = fs_path_prepare_for_add(p, len);
	if (ret < 0)
		goto out;

	read_extent_buffer(eb, p->prepared, off, len);
	p->prepared = NULL;

out:
	return ret;
}

static void fs_path_remove(struct fs_path *p)
{
	BUG_ON(p->reversed);
	while (p->start != p->end && *p->end != '/')
		p->end--;
	*p->end = 0;
}

static int fs_path_copy(struct fs_path *p, struct fs_path *from)
{
	int ret;

	p->reversed = from->reversed;
	fs_path_reset(p);

	ret = fs_path_add_path(p, from);

	return ret;
}


static void fs_path_unreverse(struct fs_path *p)
{
	char *tmp;
	int len;

	if (!p->reversed)
		return;

	tmp = p->start;
	len = p->end - p->start;
	p->start = p->buf;
	p->end = p->start + len;
	memmove(p->start, tmp, len + 1);
	p->reversed = 0;
}

static struct btrfs_path *alloc_path_for_send(void)
{
	struct btrfs_path *path;

	path = btrfs_alloc_path();
	if (!path)
		return NULL;
	path->search_commit_root = 1;
	path->skip_locking = 1;
	return path;
}

static int write_buf(struct send_ctx *sctx, const void *buf, u32 len)
{
	int ret;
	mm_segment_t old_fs;
	u32 pos = 0;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	while (pos < len) {
		ret = vfs_write(sctx->send_filp, (char *)buf + pos, len - pos,
				&sctx->send_off);
		/* TODO handle that correctly */
		/*if (ret == -ERESTARTSYS) {
			continue;
		}*/
		if (ret < 0)
			goto out;
		if (ret == 0) {
			ret = -EIO;
			goto out;
		}
		pos += ret;
	}

	ret = 0;

out:
	set_fs(old_fs);
	return ret;
}

static int tlv_put(struct send_ctx *sctx, u16 attr, const void *data, int len)
{
	struct btrfs_tlv_header *hdr;
	int total_len = sizeof(*hdr) + len;
	int left = sctx->send_max_size - sctx->send_size;

	if (unlikely(left < total_len))
		return -EOVERFLOW;

	hdr = (struct btrfs_tlv_header *) (sctx->send_buf + sctx->send_size);
	hdr->tlv_type = cpu_to_le16(attr);
	hdr->tlv_len = cpu_to_le16(len);
	memcpy(hdr + 1, data, len);
	sctx->send_size += total_len;

	return 0;
}

#if 0
static int tlv_put_u8(struct send_ctx *sctx, u16 attr, u8 value)
{
	return tlv_put(sctx, attr, &value, sizeof(value));
}

static int tlv_put_u16(struct send_ctx *sctx, u16 attr, u16 value)
{
	__le16 tmp = cpu_to_le16(value);
	return tlv_put(sctx, attr, &tmp, sizeof(tmp));
}

static int tlv_put_u32(struct send_ctx *sctx, u16 attr, u32 value)
{
	__le32 tmp = cpu_to_le32(value);
	return tlv_put(sctx, attr, &tmp, sizeof(tmp));
}
#endif

static int tlv_put_u64(struct send_ctx *sctx, u16 attr, u64 value)
{
	__le64 tmp = cpu_to_le64(value);
	return tlv_put(sctx, attr, &tmp, sizeof(tmp));
}

static int tlv_put_string(struct send_ctx *sctx, u16 attr,
			  const char *str, int len)
{
	if (len == -1)
		len = strlen(str);
	return tlv_put(sctx, attr, str, len);
}

static int tlv_put_uuid(struct send_ctx *sctx, u16 attr,
			const u8 *uuid)
{
	return tlv_put(sctx, attr, uuid, BTRFS_UUID_SIZE);
}

#if 0
static int tlv_put_timespec(struct send_ctx *sctx, u16 attr,
			    struct timespec *ts)
{
	struct btrfs_timespec bts;
	bts.sec = cpu_to_le64(ts->tv_sec);
	bts.nsec = cpu_to_le32(ts->tv_nsec);
	return tlv_put(sctx, attr, &bts, sizeof(bts));
}
#endif

static int tlv_put_btrfs_timespec(struct send_ctx *sctx, u16 attr,
				  struct extent_buffer *eb,
				  struct btrfs_timespec *ts)
{
	struct btrfs_timespec bts;
	read_extent_buffer(eb, &bts, (unsigned long)ts, sizeof(bts));
	return tlv_put(sctx, attr, &bts, sizeof(bts));
}


#define TLV_PUT(sctx, attrtype, attrlen, data) \
	do { \
		ret = tlv_put(sctx, attrtype, attrlen, data); \
		if (ret < 0) \
			goto tlv_put_failure; \
	} while (0)

#define TLV_PUT_INT(sctx, attrtype, bits, value) \
	do { \
		ret = tlv_put_u##bits(sctx, attrtype, value); \
		if (ret < 0) \
			goto tlv_put_failure; \
	} while (0)

#define TLV_PUT_U8(sctx, attrtype, data) TLV_PUT_INT(sctx, attrtype, 8, data)
#define TLV_PUT_U16(sctx, attrtype, data) TLV_PUT_INT(sctx, attrtype, 16, data)
#define TLV_PUT_U32(sctx, attrtype, data) TLV_PUT_INT(sctx, attrtype, 32, data)
#define TLV_PUT_U64(sctx, attrtype, data) TLV_PUT_INT(sctx, attrtype, 64, data)
#define TLV_PUT_STRING(sctx, attrtype, str, len) \
	do { \
		ret = tlv_put_string(sctx, attrtype, str, len); \
		if (ret < 0) \
			goto tlv_put_failure; \
	} while (0)
#define TLV_PUT_PATH(sctx, attrtype, p) \
	do { \
		ret = tlv_put_string(sctx, attrtype, p->start, \
			p->end - p->start); \
		if (ret < 0) \
			goto tlv_put_failure; \
	} while(0)
#define TLV_PUT_UUID(sctx, attrtype, uuid) \
	do { \
		ret = tlv_put_uuid(sctx, attrtype, uuid); \
		if (ret < 0) \
			goto tlv_put_failure; \
	} while (0)
#define TLV_PUT_TIMESPEC(sctx, attrtype, ts) \
	do { \
		ret = tlv_put_timespec(sctx, attrtype, ts); \
		if (ret < 0) \
			goto tlv_put_failure; \
	} while (0)
#define TLV_PUT_BTRFS_TIMESPEC(sctx, attrtype, eb, ts) \
	do { \
		ret = tlv_put_btrfs_timespec(sctx, attrtype, eb, ts); \
		if (ret < 0) \
			goto tlv_put_failure; \
	} while (0)

static int send_header(struct send_ctx *sctx)
{
	struct btrfs_stream_header hdr;

	strcpy(hdr.magic, BTRFS_SEND_STREAM_MAGIC);
	hdr.version = cpu_to_le32(BTRFS_SEND_STREAM_VERSION);

	return write_buf(sctx, &hdr, sizeof(hdr));
}

/*
 * For each command/item we want to send to userspace, we call this function.
 */
static int begin_cmd(struct send_ctx *sctx, int cmd)
{
	struct btrfs_cmd_header *hdr;

	if (!sctx->send_buf) {
		WARN_ON(1);
		return -EINVAL;
	}

	BUG_ON(sctx->send_size);

	sctx->send_size += sizeof(*hdr);
	hdr = (struct btrfs_cmd_header *)sctx->send_buf;
	hdr->cmd = cpu_to_le16(cmd);

	return 0;
}

static int send_cmd(struct send_ctx *sctx)
{
	int ret;
	struct btrfs_cmd_header *hdr;
	u32 crc;

	hdr = (struct btrfs_cmd_header *)sctx->send_buf;
	hdr->len = cpu_to_le32(sctx->send_size - sizeof(*hdr));
	hdr->crc = 0;

	crc = crc32c(0, (unsigned char *)sctx->send_buf, sctx->send_size);
	hdr->crc = cpu_to_le32(crc);

	ret = write_buf(sctx, sctx->send_buf, sctx->send_size);

	sctx->total_send_size += sctx->send_size;
	sctx->cmd_send_size[le16_to_cpu(hdr->cmd)] += sctx->send_size;
	sctx->send_size = 0;

	return ret;
}

/*
 * Sends a move instruction to user space
 */
static int send_rename(struct send_ctx *sctx,
		     struct fs_path *from, struct fs_path *to)
{
	int ret;

verbose_printk("btrfs: send_rename %s -> %s\n", from->start, to->start);

	ret = begin_cmd(sctx, BTRFS_SEND_C_RENAME);
	if (ret < 0)
		goto out;

	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, from);
	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH_TO, to);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	return ret;
}

/*
 * Sends a link instruction to user space
 */
static int send_link(struct send_ctx *sctx,
		     struct fs_path *path, struct fs_path *lnk)
{
	int ret;

verbose_printk("btrfs: send_link %s -> %s\n", path->start, lnk->start);

	ret = begin_cmd(sctx, BTRFS_SEND_C_LINK);
	if (ret < 0)
		goto out;

	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, path);
	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH_LINK, lnk);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	return ret;
}

/*
 * Sends an unlink instruction to user space
 */
static int send_unlink(struct send_ctx *sctx, struct fs_path *path)
{
	int ret;

verbose_printk("btrfs: send_unlink %s\n", path->start);

	ret = begin_cmd(sctx, BTRFS_SEND_C_UNLINK);
	if (ret < 0)
		goto out;

	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, path);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	return ret;
}

/*
 * Sends a rmdir instruction to user space
 */
static int send_rmdir(struct send_ctx *sctx, struct fs_path *path)
{
	int ret;

verbose_printk("btrfs: send_rmdir %s\n", path->start);

	ret = begin_cmd(sctx, BTRFS_SEND_C_RMDIR);
	if (ret < 0)
		goto out;

	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, path);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	return ret;
}

/*
 * Helper function to retrieve some fields from an inode item.
 */
static int get_inode_info(struct btrfs_root *root,
			  u64 ino, u64 *size, u64 *gen,
			  u64 *mode, u64 *uid, u64 *gid)
{
	int ret;
	struct btrfs_inode_item *ii;
	struct btrfs_key key;
	struct btrfs_path *path;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	key.objectid = ino;
	key.type = BTRFS_INODE_ITEM_KEY;
	key.offset = 0;
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	if (ret) {
		ret = -ENOENT;
		goto out;
	}

	ii = btrfs_item_ptr(path->nodes[0], path->slots[0],
			struct btrfs_inode_item);
	if (size)
		*size = btrfs_inode_size(path->nodes[0], ii);
	if (gen)
		*gen = btrfs_inode_generation(path->nodes[0], ii);
	if (mode)
		*mode = btrfs_inode_mode(path->nodes[0], ii);
	if (uid)
		*uid = btrfs_inode_uid(path->nodes[0], ii);
	if (gid)
		*gid = btrfs_inode_gid(path->nodes[0], ii);

out:
	btrfs_free_path(path);
	return ret;
}

typedef int (*iterate_inode_ref_t)(int num, u64 dir, int index,
				   struct fs_path *p,
				   void *ctx);

/*
 * Helper function to iterate the entries in ONE btrfs_inode_ref.
 * The iterate callback may return a non zero value to stop iteration. This can
 * be a negative value for error codes or 1 to simply stop it.
 *
 * path must point to the INODE_REF when called.
 */
static int iterate_inode_ref(struct send_ctx *sctx,
			     struct btrfs_root *root, struct btrfs_path *path,
			     struct btrfs_key *found_key, int resolve,
			     iterate_inode_ref_t iterate, void *ctx)
{
	struct extent_buffer *eb;
	struct btrfs_item *item;
	struct btrfs_inode_ref *iref;
	struct btrfs_path *tmp_path;
	struct fs_path *p;
	u32 cur;
	u32 len;
	u32 total;
	int slot;
	u32 name_len;
	char *start;
	int ret = 0;
	int num;
	int index;

	p = fs_path_alloc_reversed(sctx);
	if (!p)
		return -ENOMEM;

	tmp_path = alloc_path_for_send();
	if (!tmp_path) {
		fs_path_free(sctx, p);
		return -ENOMEM;
	}

	eb = path->nodes[0];
	slot = path->slots[0];
	item = btrfs_item_nr(eb, slot);
	iref = btrfs_item_ptr(eb, slot, struct btrfs_inode_ref);
	cur = 0;
	len = 0;
	total = btrfs_item_size(eb, item);

	num = 0;
	while (cur < total) {
		fs_path_reset(p);

		name_len = btrfs_inode_ref_name_len(eb, iref);
		index = btrfs_inode_ref_index(eb, iref);
		if (resolve) {
			start = btrfs_iref_to_path(root, tmp_path, iref, eb,
						found_key->offset, p->buf,
						p->buf_len);
			if (IS_ERR(start)) {
				ret = PTR_ERR(start);
				goto out;
			}
			if (start < p->buf) {
				/* overflow , try again with larger buffer */
				ret = fs_path_ensure_buf(p,
						p->buf_len + p->buf - start);
				if (ret < 0)
					goto out;
				start = btrfs_iref_to_path(root, tmp_path, iref,
						eb, found_key->offset, p->buf,
						p->buf_len);
				if (IS_ERR(start)) {
					ret = PTR_ERR(start);
					goto out;
				}
				BUG_ON(start < p->buf);
			}
			p->start = start;
		} else {
			ret = fs_path_add_from_extent_buffer(p, eb,
					(unsigned long)(iref + 1), name_len);
			if (ret < 0)
				goto out;
		}


		len = sizeof(*iref) + name_len;
		iref = (struct btrfs_inode_ref *)((char *)iref + len);
		cur += len;

		ret = iterate(num, found_key->offset, index, p, ctx);
		if (ret)
			goto out;

		num++;
	}

out:
	btrfs_free_path(tmp_path);
	fs_path_free(sctx, p);
	return ret;
}

typedef int (*iterate_dir_item_t)(int num, struct btrfs_key *di_key,
				  const char *name, int name_len,
				  const char *data, int data_len,
				  u8 type, void *ctx);

/*
 * Helper function to iterate the entries in ONE btrfs_dir_item.
 * The iterate callback may return a non zero value to stop iteration. This can
 * be a negative value for error codes or 1 to simply stop it.
 *
 * path must point to the dir item when called.
 */
static int iterate_dir_item(struct send_ctx *sctx,
			    struct btrfs_root *root, struct btrfs_path *path,
			    struct btrfs_key *found_key,
			    iterate_dir_item_t iterate, void *ctx)
{
	int ret = 0;
	struct extent_buffer *eb;
	struct btrfs_item *item;
	struct btrfs_dir_item *di;
	struct btrfs_path *tmp_path = NULL;
	struct btrfs_key di_key;
	char *buf = NULL;
	char *buf2 = NULL;
	int buf_len;
	int buf_virtual = 0;
	u32 name_len;
	u32 data_len;
	u32 cur;
	u32 len;
	u32 total;
	int slot;
	int num;
	u8 type;

	buf_len = PAGE_SIZE;
	buf = kmalloc(buf_len, GFP_NOFS);
	if (!buf) {
		ret = -ENOMEM;
		goto out;
	}

	tmp_path = alloc_path_for_send();
	if (!tmp_path) {
		ret = -ENOMEM;
		goto out;
	}

	eb = path->nodes[0];
	slot = path->slots[0];
	item = btrfs_item_nr(eb, slot);
	di = btrfs_item_ptr(eb, slot, struct btrfs_dir_item);
	cur = 0;
	len = 0;
	total = btrfs_item_size(eb, item);

	num = 0;
	while (cur < total) {
		name_len = btrfs_dir_name_len(eb, di);
		data_len = btrfs_dir_data_len(eb, di);
		type = btrfs_dir_type(eb, di);
		btrfs_dir_item_key_to_cpu(eb, di, &di_key);

		if (name_len + data_len > buf_len) {
			buf_len = PAGE_ALIGN(name_len + data_len);
			if (buf_virtual) {
				buf2 = vmalloc(buf_len);
				if (!buf2) {
					ret = -ENOMEM;
					goto out;
				}
				vfree(buf);
			} else {
				buf2 = krealloc(buf, buf_len, GFP_NOFS);
				if (!buf2) {
					buf2 = vmalloc(buf_len);
					if (!buf2) {
						ret = -ENOMEM;
						goto out;
					}
					kfree(buf);
					buf_virtual = 1;
				}
			}

			buf = buf2;
			buf2 = NULL;
		}

		read_extent_buffer(eb, buf, (unsigned long)(di + 1),
				name_len + data_len);

		len = sizeof(*di) + name_len + data_len;
		di = (struct btrfs_dir_item *)((char *)di + len);
		cur += len;

		ret = iterate(num, &di_key, buf, name_len, buf + name_len,
				data_len, type, ctx);
		if (ret < 0)
			goto out;
		if (ret) {
			ret = 0;
			goto out;
		}

		num++;
	}

out:
	btrfs_free_path(tmp_path);
	if (buf_virtual)
		vfree(buf);
	else
		kfree(buf);
	return ret;
}

static int __copy_first_ref(int num, u64 dir, int index,
			    struct fs_path *p, void *ctx)
{
	int ret;
	struct fs_path *pt = ctx;

	ret = fs_path_copy(pt, p);
	if (ret < 0)
		return ret;

	/* we want the first only */
	return 1;
}

/*
 * Retrieve the first path of an inode. If an inode has more then one
 * ref/hardlink, this is ignored.
 */
static int get_inode_path(struct send_ctx *sctx, struct btrfs_root *root,
			  u64 ino, struct fs_path *path)
{
	int ret;
	struct btrfs_key key, found_key;
	struct btrfs_path *p;

	p = alloc_path_for_send();
	if (!p)
		return -ENOMEM;

	fs_path_reset(path);

	key.objectid = ino;
	key.type = BTRFS_INODE_REF_KEY;
	key.offset = 0;

	ret = btrfs_search_slot_for_read(root, &key, p, 1, 0);
	if (ret < 0)
		goto out;
	if (ret) {
		ret = 1;
		goto out;
	}
	btrfs_item_key_to_cpu(p->nodes[0], &found_key, p->slots[0]);
	if (found_key.objectid != ino ||
		found_key.type != BTRFS_INODE_REF_KEY) {
		ret = -ENOENT;
		goto out;
	}

	ret = iterate_inode_ref(sctx, root, p, &found_key, 1,
			__copy_first_ref, path);
	if (ret < 0)
		goto out;
	ret = 0;

out:
	btrfs_free_path(p);
	return ret;
}

struct backref_ctx {
	struct send_ctx *sctx;

	/* number of total found references */
	u64 found;

	/*
	 * used for clones found in send_root. clones found behind cur_objectid
	 * and cur_offset are not considered as allowed clones.
	 */
	u64 cur_objectid;
	u64 cur_offset;

	/* may be truncated in case it's the last extent in a file */
	u64 extent_len;

	/* Just to check for bugs in backref resolving */
	int found_in_send_root;
};

static int __clone_root_cmp_bsearch(const void *key, const void *elt)
{
	u64 root = (u64)key;
	struct clone_root *cr = (struct clone_root *)elt;

	if (root < cr->root->objectid)
		return -1;
	if (root > cr->root->objectid)
		return 1;
	return 0;
}

static int __clone_root_cmp_sort(const void *e1, const void *e2)
{
	struct clone_root *cr1 = (struct clone_root *)e1;
	struct clone_root *cr2 = (struct clone_root *)e2;

	if (cr1->root->objectid < cr2->root->objectid)
		return -1;
	if (cr1->root->objectid > cr2->root->objectid)
		return 1;
	return 0;
}

/*
 * Called for every backref that is found for the current extent.
 */
static int __iterate_backrefs(u64 ino, u64 offset, u64 root, void *ctx_)
{
	struct backref_ctx *bctx = ctx_;
	struct clone_root *found;
	int ret;
	u64 i_size;

	/* First check if the root is in the list of accepted clone sources */
	found = bsearch((void *)root, bctx->sctx->clone_roots,
			bctx->sctx->clone_roots_cnt,
			sizeof(struct clone_root),
			__clone_root_cmp_bsearch);
	if (!found)
		return 0;

	if (found->root == bctx->sctx->send_root &&
	    ino == bctx->cur_objectid &&
	    offset == bctx->cur_offset) {
		bctx->found_in_send_root = 1;
	}

	/*
	 * There are inodes that have extents that lie behind it's i_size. Don't
	 * accept clones from these extents.
	 */
	ret = get_inode_info(found->root, ino, &i_size, NULL, NULL, NULL, NULL);
	if (ret < 0)
		return ret;

	if (offset + bctx->extent_len > i_size)
		return 0;

	/*
	 * Make sure we don't consider clones from send_root that are
	 * behind the current inode/offset.
	 */
	if (found->root == bctx->sctx->send_root) {
		/*
		 * TODO for the moment we don't accept clones from the inode
		 * that is currently send. We may change this when
		 * BTRFS_IOC_CLONE_RANGE supports cloning from and to the same
		 * file.
		 */
		if (ino >= bctx->cur_objectid)
			return 0;
		/*if (ino > ctx->cur_objectid)
			return 0;
		if (offset + ctx->extent_len > ctx->cur_offset)
			return 0;*/

		bctx->found++;
		found->found_refs++;
		found->ino = ino;
		found->offset = offset;
		return 0;
	}

	bctx->found++;
	found->found_refs++;
	if (ino < found->ino) {
		found->ino = ino;
		found->offset = offset;
	} else if (found->ino == ino) {
		/*
		 * same extent found more then once in the same file.
		 */
		if (found->offset > offset + bctx->extent_len)
			found->offset = offset;
	}

	return 0;
}

/*
 * path must point to the extent item when called.
 */
static int find_extent_clone(struct send_ctx *sctx,
			     struct btrfs_path *path,
			     u64 ino, u64 data_offset,
			     u64 ino_size,
			     struct clone_root **found)
{
	int ret;
	int extent_type;
	u64 logical;
	u64 num_bytes;
	u64 extent_item_pos;
	struct btrfs_file_extent_item *fi;
	struct extent_buffer *eb = path->nodes[0];
	struct backref_ctx backref_ctx;
	struct clone_root *cur_clone_root;
	struct btrfs_key found_key;
	struct btrfs_path *tmp_path;
	u32 i;

	tmp_path = alloc_path_for_send();
	if (!tmp_path)
		return -ENOMEM;

	if (data_offset >= ino_size) {
		/*
		 * There may be extents that lie behind the file's size.
		 * I at least had this in combination with snapshotting while
		 * writing large files.
		 */
		ret = 0;
		goto out;
	}

	fi = btrfs_item_ptr(eb, path->slots[0],
			struct btrfs_file_extent_item);
	extent_type = btrfs_file_extent_type(eb, fi);
	if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
		ret = -ENOENT;
		goto out;
	}

	num_bytes = btrfs_file_extent_num_bytes(eb, fi);
	logical = btrfs_file_extent_disk_bytenr(eb, fi);
	if (logical == 0) {
		ret = -ENOENT;
		goto out;
	}
	logical += btrfs_file_extent_offset(eb, fi);

	ret = extent_from_logical(sctx->send_root->fs_info,
			logical, tmp_path, &found_key);
	btrfs_release_path(tmp_path);

	if (ret < 0)
		goto out;
	if (ret & BTRFS_EXTENT_FLAG_TREE_BLOCK) {
		ret = -EIO;
		goto out;
	}

	/*
	 * Setup the clone roots.
	 */
	for (i = 0; i < sctx->clone_roots_cnt; i++) {
		cur_clone_root = sctx->clone_roots + i;
		cur_clone_root->ino = (u64)-1;
		cur_clone_root->offset = 0;
		cur_clone_root->found_refs = 0;
	}

	backref_ctx.sctx = sctx;
	backref_ctx.found = 0;
	backref_ctx.cur_objectid = ino;
	backref_ctx.cur_offset = data_offset;
	backref_ctx.found_in_send_root = 0;
	backref_ctx.extent_len = num_bytes;

	/*
	 * The last extent of a file may be too large due to page alignment.
	 * We need to adjust extent_len in this case so that the checks in
	 * __iterate_backrefs work.
	 */
	if (data_offset + num_bytes >= ino_size)
		backref_ctx.extent_len = ino_size - data_offset;

	/*
	 * Now collect all backrefs.
	 */
	extent_item_pos = logical - found_key.objectid;
	ret = iterate_extent_inodes(sctx->send_root->fs_info,
					found_key.objectid, extent_item_pos, 1,
					__iterate_backrefs, &backref_ctx);
	if (ret < 0)
		goto out;

	if (!backref_ctx.found_in_send_root) {
		/* found a bug in backref code? */
		ret = -EIO;
		printk(KERN_ERR "btrfs: ERROR did not find backref in "
				"send_root. inode=%llu, offset=%llu, "
				"logical=%llu\n",
				ino, data_offset, logical);
		goto out;
	}

verbose_printk(KERN_DEBUG "btrfs: find_extent_clone: data_offset=%llu, "
		"ino=%llu, "
		"num_bytes=%llu, logical=%llu\n",
		data_offset, ino, num_bytes, logical);

	if (!backref_ctx.found)
		verbose_printk("btrfs:    no clones found\n");

	cur_clone_root = NULL;
	for (i = 0; i < sctx->clone_roots_cnt; i++) {
		if (sctx->clone_roots[i].found_refs) {
			if (!cur_clone_root)
				cur_clone_root = sctx->clone_roots + i;
			else if (sctx->clone_roots[i].root == sctx->send_root)
				/* prefer clones from send_root over others */
				cur_clone_root = sctx->clone_roots + i;
			break;
		}

	}

	if (cur_clone_root) {
		*found = cur_clone_root;
		ret = 0;
	} else {
		ret = -ENOENT;
	}

out:
	btrfs_free_path(tmp_path);
	return ret;
}

static int read_symlink(struct send_ctx *sctx,
			struct btrfs_root *root,
			u64 ino,
			struct fs_path *dest)
{
	int ret;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_file_extent_item *ei;
	u8 type;
	u8 compression;
	unsigned long off;
	int len;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	key.objectid = ino;
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = 0;
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	BUG_ON(ret);

	ei = btrfs_item_ptr(path->nodes[0], path->slots[0],
			struct btrfs_file_extent_item);
	type = btrfs_file_extent_type(path->nodes[0], ei);
	compression = btrfs_file_extent_compression(path->nodes[0], ei);
	BUG_ON(type != BTRFS_FILE_EXTENT_INLINE);
	BUG_ON(compression);

	off = btrfs_file_extent_inline_start(ei);
	len = btrfs_file_extent_inline_len(path->nodes[0], ei);

	ret = fs_path_add_from_extent_buffer(dest, path->nodes[0], off, len);
	if (ret < 0)
		goto out;

out:
	btrfs_free_path(path);
	return ret;
}

/*
 * Helper function to generate a file name that is unique in the root of
 * send_root and parent_root. This is used to generate names for orphan inodes.
 */
static int gen_unique_name(struct send_ctx *sctx,
			   u64 ino, u64 gen,
			   struct fs_path *dest)
{
	int ret = 0;
	struct btrfs_path *path;
	struct btrfs_dir_item *di;
	char tmp[64];
	int len;
	u64 idx = 0;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	while (1) {
		len = snprintf(tmp, sizeof(tmp) - 1, "o%llu-%llu-%llu",
				ino, gen, idx);
		if (len >= sizeof(tmp)) {
			/* should really not happen */
			ret = -EOVERFLOW;
			goto out;
		}

		di = btrfs_lookup_dir_item(NULL, sctx->send_root,
				path, BTRFS_FIRST_FREE_OBJECTID,
				tmp, strlen(tmp), 0);
		btrfs_release_path(path);
		if (IS_ERR(di)) {
			ret = PTR_ERR(di);
			goto out;
		}
		if (di) {
			/* not unique, try again */
			idx++;
			continue;
		}

		if (!sctx->parent_root) {
			/* unique */
			ret = 0;
			break;
		}

		di = btrfs_lookup_dir_item(NULL, sctx->parent_root,
				path, BTRFS_FIRST_FREE_OBJECTID,
				tmp, strlen(tmp), 0);
		btrfs_release_path(path);
		if (IS_ERR(di)) {
			ret = PTR_ERR(di);
			goto out;
		}
		if (di) {
			/* not unique, try again */
			idx++;
			continue;
		}
		/* unique */
		break;
	}

	ret = fs_path_add(dest, tmp, strlen(tmp));

out:
	btrfs_free_path(path);
	return ret;
}

enum inode_state {
	inode_state_no_change,
	inode_state_will_create,
	inode_state_did_create,
	inode_state_will_delete,
	inode_state_did_delete,
};

static int get_cur_inode_state(struct send_ctx *sctx, u64 ino, u64 gen)
{
	int ret;
	int left_ret;
	int right_ret;
	u64 left_gen;
	u64 right_gen;

	ret = get_inode_info(sctx->send_root, ino, NULL, &left_gen, NULL, NULL,
			NULL);
	if (ret < 0 && ret != -ENOENT)
		goto out;
	left_ret = ret;

	if (!sctx->parent_root) {
		right_ret = -ENOENT;
	} else {
		ret = get_inode_info(sctx->parent_root, ino, NULL, &right_gen,
				NULL, NULL, NULL);
		if (ret < 0 && ret != -ENOENT)
			goto out;
		right_ret = ret;
	}

	if (!left_ret && !right_ret) {
		if (left_gen == gen && right_gen == gen)
			ret = inode_state_no_change;
		else if (left_gen == gen) {
			if (ino < sctx->send_progress)
				ret = inode_state_did_create;
			else
				ret = inode_state_will_create;
		} else if (right_gen == gen) {
			if (ino < sctx->send_progress)
				ret = inode_state_did_delete;
			else
				ret = inode_state_will_delete;
		} else  {
			ret = -ENOENT;
		}
	} else if (!left_ret) {
		if (left_gen == gen) {
			if (ino < sctx->send_progress)
				ret = inode_state_did_create;
			else
				ret = inode_state_will_create;
		} else {
			ret = -ENOENT;
		}
	} else if (!right_ret) {
		if (right_gen == gen) {
			if (ino < sctx->send_progress)
				ret = inode_state_did_delete;
			else
				ret = inode_state_will_delete;
		} else {
			ret = -ENOENT;
		}
	} else {
		ret = -ENOENT;
	}

out:
	return ret;
}

static int is_inode_existent(struct send_ctx *sctx, u64 ino, u64 gen)
{
	int ret;

	ret = get_cur_inode_state(sctx, ino, gen);
	if (ret < 0)
		goto out;

	if (ret == inode_state_no_change ||
	    ret == inode_state_did_create ||
	    ret == inode_state_will_delete)
		ret = 1;
	else
		ret = 0;

out:
	return ret;
}

/*
 * Helper function to lookup a dir item in a dir.
 */
static int lookup_dir_item_inode(struct btrfs_root *root,
				 u64 dir, const char *name, int name_len,
				 u64 *found_inode,
				 u8 *found_type)
{
	int ret = 0;
	struct btrfs_dir_item *di;
	struct btrfs_key key;
	struct btrfs_path *path;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	di = btrfs_lookup_dir_item(NULL, root, path,
			dir, name, name_len, 0);
	if (!di) {
		ret = -ENOENT;
		goto out;
	}
	if (IS_ERR(di)) {
		ret = PTR_ERR(di);
		goto out;
	}
	btrfs_dir_item_key_to_cpu(path->nodes[0], di, &key);
	*found_inode = key.objectid;
	*found_type = btrfs_dir_type(path->nodes[0], di);

out:
	btrfs_free_path(path);
	return ret;
}

static int get_first_ref(struct send_ctx *sctx,
			 struct btrfs_root *root, u64 ino,
			 u64 *dir, u64 *dir_gen, struct fs_path *name)
{
	int ret;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_path *path;
	struct btrfs_inode_ref *iref;
	int len;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	key.objectid = ino;
	key.type = BTRFS_INODE_REF_KEY;
	key.offset = 0;

	ret = btrfs_search_slot_for_read(root, &key, path, 1, 0);
	if (ret < 0)
		goto out;
	if (!ret)
		btrfs_item_key_to_cpu(path->nodes[0], &found_key,
				path->slots[0]);
	if (ret || found_key.objectid != key.objectid ||
	    found_key.type != key.type) {
		ret = -ENOENT;
		goto out;
	}

	iref = btrfs_item_ptr(path->nodes[0], path->slots[0],
			struct btrfs_inode_ref);
	len = btrfs_inode_ref_name_len(path->nodes[0], iref);
	ret = fs_path_add_from_extent_buffer(name, path->nodes[0],
			(unsigned long)(iref + 1), len);
	if (ret < 0)
		goto out;
	btrfs_release_path(path);

	ret = get_inode_info(root, found_key.offset, NULL, dir_gen, NULL, NULL,
			NULL);
	if (ret < 0)
		goto out;

	*dir = found_key.offset;

out:
	btrfs_free_path(path);
	return ret;
}

static int is_first_ref(struct send_ctx *sctx,
			struct btrfs_root *root,
			u64 ino, u64 dir,
			const char *name, int name_len)
{
	int ret;
	struct fs_path *tmp_name;
	u64 tmp_dir;
	u64 tmp_dir_gen;

	tmp_name = fs_path_alloc(sctx);
	if (!tmp_name)
		return -ENOMEM;

	ret = get_first_ref(sctx, root, ino, &tmp_dir, &tmp_dir_gen, tmp_name);
	if (ret < 0)
		goto out;

	if (name_len != fs_path_len(tmp_name)) {
		ret = 0;
		goto out;
	}

	ret = memcmp(tmp_name->start, name, name_len);
	if (ret)
		ret = 0;
	else
		ret = 1;

out:
	fs_path_free(sctx, tmp_name);
	return ret;
}

static int will_overwrite_ref(struct send_ctx *sctx, u64 dir, u64 dir_gen,
			      const char *name, int name_len,
			      u64 *who_ino, u64 *who_gen)
{
	int ret = 0;
	u64 other_inode = 0;
	u8 other_type = 0;

	if (!sctx->parent_root)
		goto out;

	ret = is_inode_existent(sctx, dir, dir_gen);
	if (ret <= 0)
		goto out;

	ret = lookup_dir_item_inode(sctx->parent_root, dir, name, name_len,
			&other_inode, &other_type);
	if (ret < 0 && ret != -ENOENT)
		goto out;
	if (ret) {
		ret = 0;
		goto out;
	}

	if (other_inode > sctx->send_progress) {
		ret = get_inode_info(sctx->parent_root, other_inode, NULL,
				who_gen, NULL, NULL, NULL);
		if (ret < 0)
			goto out;

		ret = 1;
		*who_ino = other_inode;
	} else {
		ret = 0;
	}

out:
	return ret;
}

static int did_overwrite_ref(struct send_ctx *sctx,
			    u64 dir, u64 dir_gen,
			    u64 ino, u64 ino_gen,
			    const char *name, int name_len)
{
	int ret = 0;
	u64 gen;
	u64 ow_inode;
	u8 other_type;

	if (!sctx->parent_root)
		goto out;

	ret = is_inode_existent(sctx, dir, dir_gen);
	if (ret <= 0)
		goto out;

	/* check if the ref was overwritten by another ref */
	ret = lookup_dir_item_inode(sctx->send_root, dir, name, name_len,
			&ow_inode, &other_type);
	if (ret < 0 && ret != -ENOENT)
		goto out;
	if (ret) {
		/* was never and will never be overwritten */
		ret = 0;
		goto out;
	}

	ret = get_inode_info(sctx->send_root, ow_inode, NULL, &gen, NULL, NULL,
			NULL);
	if (ret < 0)
		goto out;

	if (ow_inode == ino && gen == ino_gen) {
		ret = 0;
		goto out;
	}

	/* we know that it is or will be overwritten. check this now */
	if (ow_inode < sctx->send_progress)
		ret = 1;
	else
		ret = 0;

out:
	return ret;
}

static int did_overwrite_first_ref(struct send_ctx *sctx, u64 ino, u64 gen)
{
	int ret = 0;
	struct fs_path *name = NULL;
	u64 dir;
	u64 dir_gen;

	if (!sctx->parent_root)
		goto out;

	name = fs_path_alloc(sctx);
	if (!name)
		return -ENOMEM;

	ret = get_first_ref(sctx, sctx->parent_root, ino, &dir, &dir_gen, name);
	if (ret < 0)
		goto out;

	ret = did_overwrite_ref(sctx, dir, dir_gen, ino, gen,
			name->start, fs_path_len(name));
	if (ret < 0)
		goto out;

out:
	fs_path_free(sctx, name);
	return ret;
}

static int name_cache_insert(struct send_ctx *sctx,
			     struct name_cache_entry *nce)
{
	int ret = 0;
	struct name_cache_entry **ncea;

	ncea = radix_tree_lookup(&sctx->name_cache, nce->ino);
	if (ncea) {
		if (!ncea[0])
			ncea[0] = nce;
		else if (!ncea[1])
			ncea[1] = nce;
		else
			BUG();
	} else {
		ncea = kmalloc(sizeof(void *) * 2, GFP_NOFS);
		if (!ncea)
			return -ENOMEM;

		ncea[0] = nce;
		ncea[1] = NULL;
		ret = radix_tree_insert(&sctx->name_cache, nce->ino, ncea);
		if (ret < 0)
			return ret;
	}
	list_add_tail(&nce->list, &sctx->name_cache_list);
	sctx->name_cache_size++;

	return ret;
}

static void name_cache_delete(struct send_ctx *sctx,
			      struct name_cache_entry *nce)
{
	struct name_cache_entry **ncea;

	ncea = radix_tree_lookup(&sctx->name_cache, nce->ino);
	BUG_ON(!ncea);

	if (ncea[0] == nce)
		ncea[0] = NULL;
	else if (ncea[1] == nce)
		ncea[1] = NULL;
	else
		BUG();

	if (!ncea[0] && !ncea[1]) {
		radix_tree_delete(&sctx->name_cache, nce->ino);
		kfree(ncea);
	}

	list_del(&nce->list);

	sctx->name_cache_size--;
}

static struct name_cache_entry *name_cache_search(struct send_ctx *sctx,
						    u64 ino, u64 gen)
{
	struct name_cache_entry **ncea;

	ncea = radix_tree_lookup(&sctx->name_cache, ino);
	if (!ncea)
		return NULL;

	if (ncea[0] && ncea[0]->gen == gen)
		return ncea[0];
	else if (ncea[1] && ncea[1]->gen == gen)
		return ncea[1];
	return NULL;
}

static void name_cache_used(struct send_ctx *sctx, struct name_cache_entry *nce)
{
	list_del(&nce->list);
	list_add_tail(&nce->list, &sctx->name_cache_list);
}

static void name_cache_clean_unused(struct send_ctx *sctx)
{
	struct name_cache_entry *nce;

	if (sctx->name_cache_size < SEND_CTX_NAME_CACHE_CLEAN_SIZE)
		return;

	while (sctx->name_cache_size > SEND_CTX_MAX_NAME_CACHE_SIZE) {
		nce = list_entry(sctx->name_cache_list.next,
				struct name_cache_entry, list);
		name_cache_delete(sctx, nce);
		kfree(nce);
	}
}

static void name_cache_free(struct send_ctx *sctx)
{
	struct name_cache_entry *nce;
	struct name_cache_entry *tmp;

	list_for_each_entry_safe(nce, tmp, &sctx->name_cache_list, list) {
		name_cache_delete(sctx, nce);
	}
}

static int __get_cur_name_and_parent(struct send_ctx *sctx,
				     u64 ino, u64 gen,
				     u64 *parent_ino,
				     u64 *parent_gen,
				     struct fs_path *dest)
{
	int ret;
	int nce_ret;
	struct btrfs_path *path = NULL;
	struct name_cache_entry *nce = NULL;

	nce = name_cache_search(sctx, ino, gen);
	if (nce) {
		if (ino < sctx->send_progress && nce->need_later_update) {
			name_cache_delete(sctx, nce);
			kfree(nce);
			nce = NULL;
		} else {
			name_cache_used(sctx, nce);
			*parent_ino = nce->parent_ino;
			*parent_gen = nce->parent_gen;
			ret = fs_path_add(dest, nce->name, nce->name_len);
			if (ret < 0)
				goto out;
			ret = nce->ret;
			goto out;
		}
	}

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	ret = is_inode_existent(sctx, ino, gen);
	if (ret < 0)
		goto out;

	if (!ret) {
		ret = gen_unique_name(sctx, ino, gen, dest);
		if (ret < 0)
			goto out;
		ret = 1;
		goto out_cache;
	}

	if (ino < sctx->send_progress)
		ret = get_first_ref(sctx, sctx->send_root, ino,
				parent_ino, parent_gen, dest);
	else
		ret = get_first_ref(sctx, sctx->parent_root, ino,
				parent_ino, parent_gen, dest);
	if (ret < 0)
		goto out;

	ret = did_overwrite_ref(sctx, *parent_ino, *parent_gen, ino, gen,
			dest->start, dest->end - dest->start);
	if (ret < 0)
		goto out;
	if (ret) {
		fs_path_reset(dest);
		ret = gen_unique_name(sctx, ino, gen, dest);
		if (ret < 0)
			goto out;
		ret = 1;
	}

out_cache:
	nce = kmalloc(sizeof(*nce) + fs_path_len(dest) + 1, GFP_NOFS);
	if (!nce) {
		ret = -ENOMEM;
		goto out;
	}

	nce->ino = ino;
	nce->gen = gen;
	nce->parent_ino = *parent_ino;
	nce->parent_gen = *parent_gen;
	nce->name_len = fs_path_len(dest);
	nce->ret = ret;
	strcpy(nce->name, dest->start);
	memset(&nce->use_list, 0, sizeof(nce->use_list));

	if (ino < sctx->send_progress)
		nce->need_later_update = 0;
	else
		nce->need_later_update = 1;

	nce_ret = name_cache_insert(sctx, nce);
	if (nce_ret < 0)
		ret = nce_ret;
	name_cache_clean_unused(sctx);

out:
	btrfs_free_path(path);
	return ret;
}

/*
 * Magic happens here. This function returns the first ref to an inode as it
 * would look like while receiving the stream at this point in time.
 * We walk the path up to the root. For every inode in between, we check if it
 * was already processed/sent. If yes, we continue with the parent as found
 * in send_root. If not, we continue with the parent as found in parent_root.
 * If we encounter an inode that was deleted at this point in time, we use the
 * inodes "orphan" name instead of the real name and stop. Same with new inodes
 * that were not created yet and overwritten inodes/refs.
 *
 * When do we have have orphan inodes:
 * 1. When an inode is freshly created and thus no valid refs are available yet
 * 2. When a directory lost all it's refs (deleted) but still has dir items
 *    inside which were not processed yet (pending for move/delete). If anyone
 *    tried to get the path to the dir items, it would get a path inside that
 *    orphan directory.
 * 3. When an inode is moved around or gets new links, it may overwrite the ref
 *    of an unprocessed inode. If in that case the first ref would be
 *    overwritten, the overwritten inode gets "orphanized". Later when we
 *    process this overwritten inode, it is restored at a new place by moving
 *    the orphan inode.
 *
 * sctx->send_progress tells this function at which point in time receiving
 * would be.
 */
static int get_cur_path(struct send_ctx *sctx, u64 ino, u64 gen,
			struct fs_path *dest)
{
	int ret = 0;
	struct fs_path *name = NULL;
	u64 parent_inode = 0;
	u64 parent_gen = 0;
	int stop = 0;

	name = fs_path_alloc(sctx);
	if (!name) {
		ret = -ENOMEM;
		goto out;
	}

	dest->reversed = 1;
	fs_path_reset(dest);

	while (!stop && ino != BTRFS_FIRST_FREE_OBJECTID) {
		fs_path_reset(name);

		ret = __get_cur_name_and_parent(sctx, ino, gen,
				&parent_inode, &parent_gen, name);
		if (ret < 0)
			goto out;
		if (ret)
			stop = 1;

		ret = fs_path_add_path(dest, name);
		if (ret < 0)
			goto out;

		ino = parent_inode;
		gen = parent_gen;
	}

out:
	fs_path_free(sctx, name);
	if (!ret)
		fs_path_unreverse(dest);
	return ret;
}

/*
 * Called for regular files when sending extents data. Opens a struct file
 * to read from the file.
 */
static int open_cur_inode_file(struct send_ctx *sctx)
{
	int ret = 0;
	struct btrfs_key key;
	struct path path;
	struct inode *inode;
	struct dentry *dentry;
	struct file *filp;
	int new = 0;

	if (sctx->cur_inode_filp)
		goto out;

	key.objectid = sctx->cur_ino;
	key.type = BTRFS_INODE_ITEM_KEY;
	key.offset = 0;

	inode = btrfs_iget(sctx->send_root->fs_info->sb, &key, sctx->send_root,
			&new);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		goto out;
	}

	dentry = d_obtain_alias(inode);
	inode = NULL;
	if (IS_ERR(dentry)) {
		ret = PTR_ERR(dentry);
		goto out;
	}

	path.mnt = sctx->mnt;
	path.dentry = dentry;
	filp = dentry_open(&path, O_RDONLY | O_LARGEFILE, current_cred());
	dput(dentry);
	dentry = NULL;
	if (IS_ERR(filp)) {
		ret = PTR_ERR(filp);
		goto out;
	}
	sctx->cur_inode_filp = filp;

out:
	/*
	 * no xxxput required here as every vfs op
	 * does it by itself on failure
	 */
	return ret;
}

/*
 * Closes the struct file that was created in open_cur_inode_file
 */
static int close_cur_inode_file(struct send_ctx *sctx)
{
	int ret = 0;

	if (!sctx->cur_inode_filp)
		goto out;

	ret = filp_close(sctx->cur_inode_filp, NULL);
	sctx->cur_inode_filp = NULL;

out:
	return ret;
}

/*
 * Sends a BTRFS_SEND_C_SUBVOL command/item to userspace
 */
static int send_subvol_begin(struct send_ctx *sctx)
{
	int ret;
	struct btrfs_root *send_root = sctx->send_root;
	struct btrfs_root *parent_root = sctx->parent_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_root_ref *ref;
	struct extent_buffer *leaf;
	char *name = NULL;
	int namelen;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	name = kmalloc(BTRFS_PATH_NAME_MAX, GFP_NOFS);
	if (!name) {
		btrfs_free_path(path);
		return -ENOMEM;
	}

	key.objectid = send_root->objectid;
	key.type = BTRFS_ROOT_BACKREF_KEY;
	key.offset = 0;

	ret = btrfs_search_slot_for_read(send_root->fs_info->tree_root,
				&key, path, 1, 0);
	if (ret < 0)
		goto out;
	if (ret) {
		ret = -ENOENT;
		goto out;
	}

	leaf = path->nodes[0];
	btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
	if (key.type != BTRFS_ROOT_BACKREF_KEY ||
	    key.objectid != send_root->objectid) {
		ret = -ENOENT;
		goto out;
	}
	ref = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_root_ref);
	namelen = btrfs_root_ref_name_len(leaf, ref);
	read_extent_buffer(leaf, name, (unsigned long)(ref + 1), namelen);
	btrfs_release_path(path);

	if (ret < 0)
		goto out;

	if (parent_root) {
		ret = begin_cmd(sctx, BTRFS_SEND_C_SNAPSHOT);
		if (ret < 0)
			goto out;
	} else {
		ret = begin_cmd(sctx, BTRFS_SEND_C_SUBVOL);
		if (ret < 0)
			goto out;
	}

	TLV_PUT_STRING(sctx, BTRFS_SEND_A_PATH, name, namelen);
	TLV_PUT_UUID(sctx, BTRFS_SEND_A_UUID,
			sctx->send_root->root_item.uuid);
	TLV_PUT_U64(sctx, BTRFS_SEND_A_CTRANSID,
			sctx->send_root->root_item.ctransid);
	if (parent_root) {
		TLV_PUT_UUID(sctx, BTRFS_SEND_A_CLONE_UUID,
				sctx->parent_root->root_item.uuid);
		TLV_PUT_U64(sctx, BTRFS_SEND_A_CLONE_CTRANSID,
				sctx->parent_root->root_item.ctransid);
	}

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	btrfs_free_path(path);
	kfree(name);
	return ret;
}

static int send_truncate(struct send_ctx *sctx, u64 ino, u64 gen, u64 size)
{
	int ret = 0;
	struct fs_path *p;

verbose_printk("btrfs: send_truncate %llu size=%llu\n", ino, size);

	p = fs_path_alloc(sctx);
	if (!p)
		return -ENOMEM;

	ret = begin_cmd(sctx, BTRFS_SEND_C_TRUNCATE);
	if (ret < 0)
		goto out;

	ret = get_cur_path(sctx, ino, gen, p);
	if (ret < 0)
		goto out;
	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, p);
	TLV_PUT_U64(sctx, BTRFS_SEND_A_SIZE, size);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	fs_path_free(sctx, p);
	return ret;
}

static int send_chmod(struct send_ctx *sctx, u64 ino, u64 gen, u64 mode)
{
	int ret = 0;
	struct fs_path *p;

verbose_printk("btrfs: send_chmod %llu mode=%llu\n", ino, mode);

	p = fs_path_alloc(sctx);
	if (!p)
		return -ENOMEM;

	ret = begin_cmd(sctx, BTRFS_SEND_C_CHMOD);
	if (ret < 0)
		goto out;

	ret = get_cur_path(sctx, ino, gen, p);
	if (ret < 0)
		goto out;
	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, p);
	TLV_PUT_U64(sctx, BTRFS_SEND_A_MODE, mode & 07777);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	fs_path_free(sctx, p);
	return ret;
}

static int send_chown(struct send_ctx *sctx, u64 ino, u64 gen, u64 uid, u64 gid)
{
	int ret = 0;
	struct fs_path *p;

verbose_printk("btrfs: send_chown %llu uid=%llu, gid=%llu\n", ino, uid, gid);

	p = fs_path_alloc(sctx);
	if (!p)
		return -ENOMEM;

	ret = begin_cmd(sctx, BTRFS_SEND_C_CHOWN);
	if (ret < 0)
		goto out;

	ret = get_cur_path(sctx, ino, gen, p);
	if (ret < 0)
		goto out;
	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, p);
	TLV_PUT_U64(sctx, BTRFS_SEND_A_UID, uid);
	TLV_PUT_U64(sctx, BTRFS_SEND_A_GID, gid);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	fs_path_free(sctx, p);
	return ret;
}

static int send_utimes(struct send_ctx *sctx, u64 ino, u64 gen)
{
	int ret = 0;
	struct fs_path *p = NULL;
	struct btrfs_inode_item *ii;
	struct btrfs_path *path = NULL;
	struct extent_buffer *eb;
	struct btrfs_key key;
	int slot;

verbose_printk("btrfs: send_utimes %llu\n", ino);

	p = fs_path_alloc(sctx);
	if (!p)
		return -ENOMEM;

	path = alloc_path_for_send();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	key.objectid = ino;
	key.type = BTRFS_INODE_ITEM_KEY;
	key.offset = 0;
	ret = btrfs_search_slot(NULL, sctx->send_root, &key, path, 0, 0);
	if (ret < 0)
		goto out;

	eb = path->nodes[0];
	slot = path->slots[0];
	ii = btrfs_item_ptr(eb, slot, struct btrfs_inode_item);

	ret = begin_cmd(sctx, BTRFS_SEND_C_UTIMES);
	if (ret < 0)
		goto out;

	ret = get_cur_path(sctx, ino, gen, p);
	if (ret < 0)
		goto out;
	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, p);
	TLV_PUT_BTRFS_TIMESPEC(sctx, BTRFS_SEND_A_ATIME, eb,
			btrfs_inode_atime(ii));
	TLV_PUT_BTRFS_TIMESPEC(sctx, BTRFS_SEND_A_MTIME, eb,
			btrfs_inode_mtime(ii));
	TLV_PUT_BTRFS_TIMESPEC(sctx, BTRFS_SEND_A_CTIME, eb,
			btrfs_inode_ctime(ii));
	/* TODO otime? */

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	fs_path_free(sctx, p);
	btrfs_free_path(path);
	return ret;
}

/*
 * Sends a BTRFS_SEND_C_MKXXX or SYMLINK command to user space. We don't have
 * a valid path yet because we did not process the refs yet. So, the inode
 * is created as orphan.
 */
static int send_create_inode(struct send_ctx *sctx, struct btrfs_path *path,
			     struct btrfs_key *key)
{
	int ret = 0;
	struct extent_buffer *eb = path->nodes[0];
	struct btrfs_inode_item *ii;
	struct fs_path *p;
	int slot = path->slots[0];
	int cmd;
	u64 mode;

verbose_printk("btrfs: send_create_inode %llu\n", sctx->cur_ino);

	p = fs_path_alloc(sctx);
	if (!p)
		return -ENOMEM;

	ii = btrfs_item_ptr(eb, slot, struct btrfs_inode_item);
	mode = btrfs_inode_mode(eb, ii);

	if (S_ISREG(mode))
		cmd = BTRFS_SEND_C_MKFILE;
	else if (S_ISDIR(mode))
		cmd = BTRFS_SEND_C_MKDIR;
	else if (S_ISLNK(mode))
		cmd = BTRFS_SEND_C_SYMLINK;
	else if (S_ISCHR(mode) || S_ISBLK(mode))
		cmd = BTRFS_SEND_C_MKNOD;
	else if (S_ISFIFO(mode))
		cmd = BTRFS_SEND_C_MKFIFO;
	else if (S_ISSOCK(mode))
		cmd = BTRFS_SEND_C_MKSOCK;
	else {
		printk(KERN_WARNING "btrfs: unexpected inode type %o",
				(int)(mode & S_IFMT));
		ret = -ENOTSUPP;
		goto out;
	}

	ret = begin_cmd(sctx, cmd);
	if (ret < 0)
		goto out;

	ret = gen_unique_name(sctx, sctx->cur_ino, sctx->cur_inode_gen, p);
	if (ret < 0)
		goto out;

	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, p);
	TLV_PUT_U64(sctx, BTRFS_SEND_A_INO, sctx->cur_ino);

	if (S_ISLNK(mode)) {
		fs_path_reset(p);
		ret = read_symlink(sctx, sctx->send_root, sctx->cur_ino, p);
		if (ret < 0)
			goto out;
		TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH_LINK, p);
	} else if (S_ISCHR(mode) || S_ISBLK(mode) ||
		   S_ISFIFO(mode) || S_ISSOCK(mode)) {
		TLV_PUT_U64(sctx, BTRFS_SEND_A_RDEV, btrfs_inode_rdev(eb, ii));
	}

	ret = send_cmd(sctx);
	if (ret < 0)
		goto out;


tlv_put_failure:
out:
	fs_path_free(sctx, p);
	return ret;
}

struct recorded_ref {
	struct list_head list;
	char *dir_path;
	char *name;
	struct fs_path *full_path;
	u64 dir;
	u64 dir_gen;
	int dir_path_len;
	int name_len;
};

/*
 * We need to process new refs before deleted refs, but compare_tree gives us
 * everything mixed. So we first record all refs and later process them.
 * This function is a helper to record one ref.
 */
static int record_ref(struct list_head *head, u64 dir,
		      u64 dir_gen, struct fs_path *path)
{
	struct recorded_ref *ref;
	char *tmp;

	ref = kmalloc(sizeof(*ref), GFP_NOFS);
	if (!ref)
		return -ENOMEM;

	ref->dir = dir;
	ref->dir_gen = dir_gen;
	ref->full_path = path;

	tmp = strrchr(ref->full_path->start, '/');
	if (!tmp) {
		ref->name_len = ref->full_path->end - ref->full_path->start;
		ref->name = ref->full_path->start;
		ref->dir_path_len = 0;
		ref->dir_path = ref->full_path->start;
	} else {
		tmp++;
		ref->name_len = ref->full_path->end - tmp;
		ref->name = tmp;
		ref->dir_path = ref->full_path->start;
		ref->dir_path_len = ref->full_path->end -
				ref->full_path->start - 1 - ref->name_len;
	}

	list_add_tail(&ref->list, head);
	return 0;
}

static void __free_recorded_refs(struct send_ctx *sctx, struct list_head *head)
{
	struct recorded_ref *cur;
	struct recorded_ref *tmp;

	list_for_each_entry_safe(cur, tmp, head, list) {
		fs_path_free(sctx, cur->full_path);
		kfree(cur);
	}
	INIT_LIST_HEAD(head);
}

static void free_recorded_refs(struct send_ctx *sctx)
{
	__free_recorded_refs(sctx, &sctx->new_refs);
	__free_recorded_refs(sctx, &sctx->deleted_refs);
}

/*
 * Renames/moves a file/dir to it's orphan name. Used when the first
 * ref of an unprocessed inode gets overwritten and for all non empty
 * directories.
 */
static int orphanize_inode(struct send_ctx *sctx, u64 ino, u64 gen,
			  struct fs_path *path)
{
	int ret;
	struct fs_path *orphan;

	orphan = fs_path_alloc(sctx);
	if (!orphan)
		return -ENOMEM;

	ret = gen_unique_name(sctx, ino, gen, orphan);
	if (ret < 0)
		goto out;

	ret = send_rename(sctx, path, orphan);

out:
	fs_path_free(sctx, orphan);
	return ret;
}

/*
 * Returns 1 if a directory can be removed at this point in time.
 * We check this by iterating all dir items and checking if the inode behind
 * the dir item was already processed.
 */
static int can_rmdir(struct send_ctx *sctx, u64 dir, u64 send_progress)
{
	int ret = 0;
	struct btrfs_root *root = sctx->parent_root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_key loc;
	struct btrfs_dir_item *di;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	key.objectid = dir;
	key.type = BTRFS_DIR_INDEX_KEY;
	key.offset = 0;

	while (1) {
		ret = btrfs_search_slot_for_read(root, &key, path, 1, 0);
		if (ret < 0)
			goto out;
		if (!ret) {
			btrfs_item_key_to_cpu(path->nodes[0], &found_key,
					path->slots[0]);
		}
		if (ret || found_key.objectid != key.objectid ||
		    found_key.type != key.type) {
			break;
		}

		di = btrfs_item_ptr(path->nodes[0], path->slots[0],
				struct btrfs_dir_item);
		btrfs_dir_item_key_to_cpu(path->nodes[0], di, &loc);

		if (loc.objectid > send_progress) {
			ret = 0;
			goto out;
		}

		btrfs_release_path(path);
		key.offset = found_key.offset + 1;
	}

	ret = 1;

out:
	btrfs_free_path(path);
	return ret;
}

struct finish_unordered_dir_ctx {
	struct send_ctx *sctx;
	struct fs_path *cur_path;
	struct fs_path *dir_path;
	u64 dir_ino;
	int need_delete;
	int delete_pass;
};

int __finish_unordered_dir(int num, struct btrfs_key *di_key,
			   const char *name, int name_len,
			   const char *data, int data_len,
			   u8 type, void *ctx)
{
	int ret = 0;
	struct finish_unordered_dir_ctx *fctx = ctx;
	struct send_ctx *sctx = fctx->sctx;
	u64 di_gen;
	u64 di_mode;
	int is_orphan = 0;

	if (di_key->objectid >= fctx->dir_ino)
		goto out;

	fs_path_reset(fctx->cur_path);

	ret = get_inode_info(sctx->send_root, di_key->objectid,
			NULL, &di_gen, &di_mode, NULL, NULL);
	if (ret < 0)
		goto out;

	ret = is_first_ref(sctx, sctx->send_root, di_key->objectid,
			fctx->dir_ino, name, name_len);
	if (ret < 0)
		goto out;
	if (ret) {
		is_orphan = 1;
		ret = gen_unique_name(sctx, di_key->objectid, di_gen,
				fctx->cur_path);
	} else {
		ret = get_cur_path(sctx, di_key->objectid, di_gen,
				fctx->cur_path);
	}
	if (ret < 0)
		goto out;

	ret = fs_path_add(fctx->dir_path, name, name_len);
	if (ret < 0)
		goto out;

	if (!fctx->delete_pass) {
		if (S_ISDIR(di_mode)) {
			ret = send_rename(sctx, fctx->cur_path,
					fctx->dir_path);
		} else {
			ret = send_link(sctx, fctx->dir_path,
					fctx->cur_path);
			if (is_orphan)
				fctx->need_delete = 1;
		}
	} else if (!S_ISDIR(di_mode)) {
		ret = send_unlink(sctx, fctx->cur_path);
	} else {
		ret = 0;
	}

	fs_path_remove(fctx->dir_path);

out:
	return ret;
}

/*
 * Go through all dir items and see if we find refs which could not be created
 * in the past because the dir did not exist at that time.
 */
static int finish_outoforder_dir(struct send_ctx *sctx, u64 dir, u64 dir_gen)
{
	int ret = 0;
	struct btrfs_path *path = NULL;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct extent_buffer *eb;
	struct finish_unordered_dir_ctx fctx;
	int slot;

	path = alloc_path_for_send();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	memset(&fctx, 0, sizeof(fctx));
	fctx.sctx = sctx;
	fctx.cur_path = fs_path_alloc(sctx);
	fctx.dir_path = fs_path_alloc(sctx);
	if (!fctx.cur_path || !fctx.dir_path) {
		ret = -ENOMEM;
		goto out;
	}
	fctx.dir_ino = dir;

	ret = get_cur_path(sctx, dir, dir_gen, fctx.dir_path);
	if (ret < 0)
		goto out;

	/*
	 * We do two passes. The first links in the new refs and the second
	 * deletes orphans if required. Deletion of orphans is not required for
	 * directory inodes, as we always have only one ref and use rename
	 * instead of link for those.
	 */

again:
	key.objectid = dir;
	key.type = BTRFS_DIR_ITEM_KEY;
	key.offset = 0;
	while (1) {
		ret = btrfs_search_slot_for_read(sctx->send_root, &key, path,
				1, 0);
		if (ret < 0)
			goto out;
		eb = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(eb, &found_key, slot);

		if (found_key.objectid != key.objectid ||
		    found_key.type != key.type) {
			btrfs_release_path(path);
			break;
		}

		ret = iterate_dir_item(sctx, sctx->send_root, path,
				&found_key, __finish_unordered_dir,
				&fctx);
		if (ret < 0)
			goto out;

		key.offset = found_key.offset + 1;
		btrfs_release_path(path);
	}

	if (!fctx.delete_pass && fctx.need_delete) {
		fctx.delete_pass = 1;
		goto again;
	}

out:
	btrfs_free_path(path);
	fs_path_free(sctx, fctx.cur_path);
	fs_path_free(sctx, fctx.dir_path);
	return ret;
}

/*
 * This does all the move/link/unlink/rmdir magic.
 */
static int process_recorded_refs(struct send_ctx *sctx)
{
	int ret = 0;
	struct recorded_ref *cur;
	struct ulist *check_dirs = NULL;
	struct ulist_iterator uit;
	struct ulist_node *un;
	struct fs_path *valid_path = NULL;
	u64 ow_inode = 0;
	u64 ow_gen;
	int did_overwrite = 0;
	int is_orphan = 0;

verbose_printk("btrfs: process_recorded_refs %llu\n", sctx->cur_ino);

	valid_path = fs_path_alloc(sctx);
	if (!valid_path) {
		ret = -ENOMEM;
		goto out;
	}

	check_dirs = ulist_alloc(GFP_NOFS);
	if (!check_dirs) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * First, check if the first ref of the current inode was overwritten
	 * before. If yes, we know that the current inode was already orphanized
	 * and thus use the orphan name. If not, we can use get_cur_path to
	 * get the path of the first ref as it would like while receiving at
	 * this point in time.
	 * New inodes are always orphan at the beginning, so force to use the
	 * orphan name in this case.
	 * The first ref is stored in valid_path and will be updated if it
	 * gets moved around.
	 */
	if (!sctx->cur_inode_new) {
		ret = did_overwrite_first_ref(sctx, sctx->cur_ino,
				sctx->cur_inode_gen);
		if (ret < 0)
			goto out;
		if (ret)
			did_overwrite = 1;
	}
	if (sctx->cur_inode_new || did_overwrite) {
		ret = gen_unique_name(sctx, sctx->cur_ino,
				sctx->cur_inode_gen, valid_path);
		if (ret < 0)
			goto out;
		is_orphan = 1;
	} else {
		ret = get_cur_path(sctx, sctx->cur_ino, sctx->cur_inode_gen,
				valid_path);
		if (ret < 0)
			goto out;
	}

	list_for_each_entry(cur, &sctx->new_refs, list) {
		/*
		 * Check if this new ref would overwrite the first ref of
		 * another unprocessed inode. If yes, orphanize the
		 * overwritten inode. If we find an overwritten ref that is
		 * not the first ref, simply unlink it.
		 */
		ret = will_overwrite_ref(sctx, cur->dir, cur->dir_gen,
				cur->name, cur->name_len,
				&ow_inode, &ow_gen);
		if (ret < 0)
			goto out;
		if (ret) {
			ret = is_first_ref(sctx, sctx->parent_root,
					ow_inode, cur->dir, cur->name,
					cur->name_len);
			if (ret < 0)
				goto out;
			if (ret) {
				ret = orphanize_inode(sctx, ow_inode, ow_gen,
						cur->full_path);
				if (ret < 0)
					goto out;
			} else {
				ret = send_unlink(sctx, cur->full_path);
				if (ret < 0)
					goto out;
			}
		}

		/*
		 * link/move the ref to the new place. If we have an orphan
		 * inode, move it and update valid_path. If not, link or move
		 * it depending on the inode mode.
		 */
		if (is_orphan && !sctx->cur_inode_first_ref_orphan) {
			ret = send_rename(sctx, valid_path, cur->full_path);
			if (ret < 0)
				goto out;
			is_orphan = 0;
			ret = fs_path_copy(valid_path, cur->full_path);
			if (ret < 0)
				goto out;
		} else {
			if (S_ISDIR(sctx->cur_inode_mode)) {
				/*
				 * Dirs can't be linked, so move it. For moved
				 * dirs, we always have one new and one deleted
				 * ref. The deleted ref is ignored later.
				 */
				ret = send_rename(sctx, valid_path,
						cur->full_path);
				if (ret < 0)
					goto out;
				ret = fs_path_copy(valid_path, cur->full_path);
				if (ret < 0)
					goto out;
			} else {
				ret = send_link(sctx, cur->full_path,
						valid_path);
				if (ret < 0)
					goto out;
			}
		}
		ret = ulist_add(check_dirs, cur->dir, cur->dir_gen,
				GFP_NOFS);
		if (ret < 0)
			goto out;
	}

	if (S_ISDIR(sctx->cur_inode_mode) && sctx->cur_inode_deleted) {
		/*
		 * Check if we can already rmdir the directory. If not,
		 * orphanize it. For every dir item inside that gets deleted
		 * later, we do this check again and rmdir it then if possible.
		 * See the use of check_dirs for more details.
		 */
		ret = can_rmdir(sctx, sctx->cur_ino, sctx->cur_ino);
		if (ret < 0)
			goto out;
		if (ret) {
			ret = send_rmdir(sctx, valid_path);
			if (ret < 0)
				goto out;
		} else if (!is_orphan) {
			ret = orphanize_inode(sctx, sctx->cur_ino,
					sctx->cur_inode_gen, valid_path);
			if (ret < 0)
				goto out;
			is_orphan = 1;
		}

		list_for_each_entry(cur, &sctx->deleted_refs, list) {
			ret = ulist_add(check_dirs, cur->dir, cur->dir_gen,
					GFP_NOFS);
			if (ret < 0)
				goto out;
		}
	} else if (!S_ISDIR(sctx->cur_inode_mode)) {
		/*
		 * We have a non dir inode. Go through all deleted refs and
		 * unlink them if they were not already overwritten by other
		 * inodes.
		 */
		list_for_each_entry(cur, &sctx->deleted_refs, list) {
			ret = did_overwrite_ref(sctx, cur->dir, cur->dir_gen,
					sctx->cur_ino, sctx->cur_inode_gen,
					cur->name, cur->name_len);
			if (ret < 0)
				goto out;
			if (!ret) {
				/*
				 * In case the inode was moved to a directory
				 * that was not created yet (see
				 * __record_new_ref), we can not unlink the ref
				 * as it will be needed later when the parent
				 * directory is created, so that we can move in
				 * the inode to the new dir.
				 */
				if (!is_orphan &&
				    sctx->cur_inode_first_ref_orphan) {
					ret = orphanize_inode(sctx,
							sctx->cur_ino,
							sctx->cur_inode_gen,
							cur->full_path);
					if (ret < 0)
						goto out;
					ret = gen_unique_name(sctx,
							sctx->cur_ino,
							sctx->cur_inode_gen,
							valid_path);
					if (ret < 0)
						goto out;
					is_orphan = 1;

				} else {
					ret = send_unlink(sctx, cur->full_path);
					if (ret < 0)
						goto out;
				}
			}
			ret = ulist_add(check_dirs, cur->dir, cur->dir_gen,
					GFP_NOFS);
			if (ret < 0)
				goto out;
		}

		/*
		 * If the inode is still orphan, unlink the orphan. This may
		 * happen when a previous inode did overwrite the first ref
		 * of this inode and no new refs were added for the current
		 * inode.
		 * We can however not delete the orphan in case the inode relies
		 * in a directory that was not created yet (see
		 * __record_new_ref)
		 */
		if (is_orphan && !sctx->cur_inode_first_ref_orphan) {
			ret = send_unlink(sctx, valid_path);
			if (ret < 0)
				goto out;
		}
	}

	/*
	 * We did collect all parent dirs where cur_inode was once located. We
	 * now go through all these dirs and check if they are pending for
	 * deletion and if it's finally possible to perform the rmdir now.
	 * We also update the inode stats of the parent dirs here.
	 */
	ULIST_ITER_INIT(&uit);
	while ((un = ulist_next(check_dirs, &uit))) {
		if (un->val > sctx->cur_ino)
			continue;

		ret = get_cur_inode_state(sctx, un->val, un->aux);
		if (ret < 0)
			goto out;

		if (ret == inode_state_did_create ||
		    ret == inode_state_no_change) {
			/* TODO delayed utimes */
			ret = send_utimes(sctx, un->val, un->aux);
			if (ret < 0)
				goto out;
		} else if (ret == inode_state_did_delete) {
			ret = can_rmdir(sctx, un->val, sctx->cur_ino);
			if (ret < 0)
				goto out;
			if (ret) {
				ret = get_cur_path(sctx, un->val, un->aux,
						valid_path);
				if (ret < 0)
					goto out;
				ret = send_rmdir(sctx, valid_path);
				if (ret < 0)
					goto out;
			}
		}
	}

	/*
	 * Current inode is now at it's new position, so we must increase
	 * send_progress
	 */
	sctx->send_progress = sctx->cur_ino + 1;

	/*
	 * We may have a directory here that has pending refs which could not
	 * be created before (because the dir did not exist before, see
	 * __record_new_ref). finish_outoforder_dir will link/move the pending
	 * refs.
	 */
	if (S_ISDIR(sctx->cur_inode_mode) && sctx->cur_inode_new) {
		ret = finish_outoforder_dir(sctx, sctx->cur_ino,
				sctx->cur_inode_gen);
		if (ret < 0)
			goto out;
	}

	ret = 0;

out:
	free_recorded_refs(sctx);
	ulist_free(check_dirs);
	fs_path_free(sctx, valid_path);
	return ret;
}

static int __record_new_ref(int num, u64 dir, int index,
			    struct fs_path *name,
			    void *ctx)
{
	int ret = 0;
	struct send_ctx *sctx = ctx;
	struct fs_path *p;
	u64 gen;

	p = fs_path_alloc(sctx);
	if (!p)
		return -ENOMEM;

	ret = get_inode_info(sctx->send_root, dir, NULL, &gen, NULL, NULL,
			NULL);
	if (ret < 0)
		goto out;

	/*
	 * The parent may be non-existent at this point in time. This happens
	 * if the ino of the parent dir is higher then the current ino. In this
	 * case, we can not process this ref until the parent dir is finally
	 * created. If we reach the parent dir later, process_recorded_refs
	 * will go through all dir items and process the refs that could not be
	 * processed before. In case this is the first ref, we set
	 * cur_inode_first_ref_orphan to 1 to inform process_recorded_refs to
	 * keep an orphan of the inode so that it later can be used for
	 * link/move
	 */
	ret = is_inode_existent(sctx, dir, gen);
	if (ret < 0)
		goto out;
	if (!ret) {
		ret = is_first_ref(sctx, sctx->send_root, sctx->cur_ino, dir,
				name->start, fs_path_len(name));
		if (ret < 0)
			goto out;
		if (ret)
			sctx->cur_inode_first_ref_orphan = 1;
		ret = 0;
		goto out;
	}

	ret = get_cur_path(sctx, dir, gen, p);
	if (ret < 0)
		goto out;
	ret = fs_path_add_path(p, name);
	if (ret < 0)
		goto out;

	ret = record_ref(&sctx->new_refs, dir, gen, p);

out:
	if (ret)
		fs_path_free(sctx, p);
	return ret;
}

static int __record_deleted_ref(int num, u64 dir, int index,
				struct fs_path *name,
				void *ctx)
{
	int ret = 0;
	struct send_ctx *sctx = ctx;
	struct fs_path *p;
	u64 gen;

	p = fs_path_alloc(sctx);
	if (!p)
		return -ENOMEM;

	ret = get_inode_info(sctx->parent_root, dir, NULL, &gen, NULL, NULL,
			NULL);
	if (ret < 0)
		goto out;

	ret = get_cur_path(sctx, dir, gen, p);
	if (ret < 0)
		goto out;
	ret = fs_path_add_path(p, name);
	if (ret < 0)
		goto out;

	ret = record_ref(&sctx->deleted_refs, dir, gen, p);

out:
	if (ret)
		fs_path_free(sctx, p);
	return ret;
}

static int record_new_ref(struct send_ctx *sctx)
{
	int ret;

	ret = iterate_inode_ref(sctx, sctx->send_root, sctx->left_path,
			sctx->cmp_key, 0, __record_new_ref, sctx);
	if (ret < 0)
		goto out;
	ret = 0;

out:
	return ret;
}

static int record_deleted_ref(struct send_ctx *sctx)
{
	int ret;

	ret = iterate_inode_ref(sctx, sctx->parent_root, sctx->right_path,
			sctx->cmp_key, 0, __record_deleted_ref, sctx);
	if (ret < 0)
		goto out;
	ret = 0;

out:
	return ret;
}

struct find_ref_ctx {
	u64 dir;
	struct fs_path *name;
	int found_idx;
};

static int __find_iref(int num, u64 dir, int index,
		       struct fs_path *name,
		       void *ctx_)
{
	struct find_ref_ctx *ctx = ctx_;

	if (dir == ctx->dir && fs_path_len(name) == fs_path_len(ctx->name) &&
	    strncmp(name->start, ctx->name->start, fs_path_len(name)) == 0) {
		ctx->found_idx = num;
		return 1;
	}
	return 0;
}

static int find_iref(struct send_ctx *sctx,
		     struct btrfs_root *root,
		     struct btrfs_path *path,
		     struct btrfs_key *key,
		     u64 dir, struct fs_path *name)
{
	int ret;
	struct find_ref_ctx ctx;

	ctx.dir = dir;
	ctx.name = name;
	ctx.found_idx = -1;

	ret = iterate_inode_ref(sctx, root, path, key, 0, __find_iref, &ctx);
	if (ret < 0)
		return ret;

	if (ctx.found_idx == -1)
		return -ENOENT;

	return ctx.found_idx;
}

static int __record_changed_new_ref(int num, u64 dir, int index,
				    struct fs_path *name,
				    void *ctx)
{
	int ret;
	struct send_ctx *sctx = ctx;

	ret = find_iref(sctx, sctx->parent_root, sctx->right_path,
			sctx->cmp_key, dir, name);
	if (ret == -ENOENT)
		ret = __record_new_ref(num, dir, index, name, sctx);
	else if (ret > 0)
		ret = 0;

	return ret;
}

static int __record_changed_deleted_ref(int num, u64 dir, int index,
					struct fs_path *name,
					void *ctx)
{
	int ret;
	struct send_ctx *sctx = ctx;

	ret = find_iref(sctx, sctx->send_root, sctx->left_path, sctx->cmp_key,
			dir, name);
	if (ret == -ENOENT)
		ret = __record_deleted_ref(num, dir, index, name, sctx);
	else if (ret > 0)
		ret = 0;

	return ret;
}

static int record_changed_ref(struct send_ctx *sctx)
{
	int ret = 0;

	ret = iterate_inode_ref(sctx, sctx->send_root, sctx->left_path,
			sctx->cmp_key, 0, __record_changed_new_ref, sctx);
	if (ret < 0)
		goto out;
	ret = iterate_inode_ref(sctx, sctx->parent_root, sctx->right_path,
			sctx->cmp_key, 0, __record_changed_deleted_ref, sctx);
	if (ret < 0)
		goto out;
	ret = 0;

out:
	return ret;
}

/*
 * Record and process all refs at once. Needed when an inode changes the
 * generation number, which means that it was deleted and recreated.
 */
static int process_all_refs(struct send_ctx *sctx,
			    enum btrfs_compare_tree_result cmd)
{
	int ret;
	struct btrfs_root *root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct extent_buffer *eb;
	int slot;
	iterate_inode_ref_t cb;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	if (cmd == BTRFS_COMPARE_TREE_NEW) {
		root = sctx->send_root;
		cb = __record_new_ref;
	} else if (cmd == BTRFS_COMPARE_TREE_DELETED) {
		root = sctx->parent_root;
		cb = __record_deleted_ref;
	} else {
		BUG();
	}

	key.objectid = sctx->cmp_key->objectid;
	key.type = BTRFS_INODE_REF_KEY;
	key.offset = 0;
	while (1) {
		ret = btrfs_search_slot_for_read(root, &key, path, 1, 0);
		if (ret < 0) {
			btrfs_release_path(path);
			goto out;
		}
		if (ret) {
			btrfs_release_path(path);
			break;
		}

		eb = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(eb, &found_key, slot);

		if (found_key.objectid != key.objectid ||
		    found_key.type != key.type) {
			btrfs_release_path(path);
			break;
		}

		ret = iterate_inode_ref(sctx, sctx->parent_root, path,
				&found_key, 0, cb, sctx);
		btrfs_release_path(path);
		if (ret < 0)
			goto out;

		key.offset = found_key.offset + 1;
	}

	ret = process_recorded_refs(sctx);

out:
	btrfs_free_path(path);
	return ret;
}

static int send_set_xattr(struct send_ctx *sctx,
			  struct fs_path *path,
			  const char *name, int name_len,
			  const char *data, int data_len)
{
	int ret = 0;

	ret = begin_cmd(sctx, BTRFS_SEND_C_SET_XATTR);
	if (ret < 0)
		goto out;

	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, path);
	TLV_PUT_STRING(sctx, BTRFS_SEND_A_XATTR_NAME, name, name_len);
	TLV_PUT(sctx, BTRFS_SEND_A_XATTR_DATA, data, data_len);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	return ret;
}

static int send_remove_xattr(struct send_ctx *sctx,
			  struct fs_path *path,
			  const char *name, int name_len)
{
	int ret = 0;

	ret = begin_cmd(sctx, BTRFS_SEND_C_REMOVE_XATTR);
	if (ret < 0)
		goto out;

	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, path);
	TLV_PUT_STRING(sctx, BTRFS_SEND_A_XATTR_NAME, name, name_len);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	return ret;
}

static int __process_new_xattr(int num, struct btrfs_key *di_key,
			       const char *name, int name_len,
			       const char *data, int data_len,
			       u8 type, void *ctx)
{
	int ret;
	struct send_ctx *sctx = ctx;
	struct fs_path *p;
	posix_acl_xattr_header dummy_acl;

	p = fs_path_alloc(sctx);
	if (!p)
		return -ENOMEM;

	/*
	 * This hack is needed because empty acl's are stored as zero byte
	 * data in xattrs. Problem with that is, that receiving these zero byte
	 * acl's will fail later. To fix this, we send a dummy acl list that
	 * only contains the version number and no entries.
	 */
	if (!strncmp(name, XATTR_NAME_POSIX_ACL_ACCESS, name_len) ||
	    !strncmp(name, XATTR_NAME_POSIX_ACL_DEFAULT, name_len)) {
		if (data_len == 0) {
			dummy_acl.a_version =
					cpu_to_le32(POSIX_ACL_XATTR_VERSION);
			data = (char *)&dummy_acl;
			data_len = sizeof(dummy_acl);
		}
	}

	ret = get_cur_path(sctx, sctx->cur_ino, sctx->cur_inode_gen, p);
	if (ret < 0)
		goto out;

	ret = send_set_xattr(sctx, p, name, name_len, data, data_len);

out:
	fs_path_free(sctx, p);
	return ret;
}

static int __process_deleted_xattr(int num, struct btrfs_key *di_key,
				   const char *name, int name_len,
				   const char *data, int data_len,
				   u8 type, void *ctx)
{
	int ret;
	struct send_ctx *sctx = ctx;
	struct fs_path *p;

	p = fs_path_alloc(sctx);
	if (!p)
		return -ENOMEM;

	ret = get_cur_path(sctx, sctx->cur_ino, sctx->cur_inode_gen, p);
	if (ret < 0)
		goto out;

	ret = send_remove_xattr(sctx, p, name, name_len);

out:
	fs_path_free(sctx, p);
	return ret;
}

static int process_new_xattr(struct send_ctx *sctx)
{
	int ret = 0;

	ret = iterate_dir_item(sctx, sctx->send_root, sctx->left_path,
			sctx->cmp_key, __process_new_xattr, sctx);

	return ret;
}

static int process_deleted_xattr(struct send_ctx *sctx)
{
	int ret;

	ret = iterate_dir_item(sctx, sctx->parent_root, sctx->right_path,
			sctx->cmp_key, __process_deleted_xattr, sctx);

	return ret;
}

struct find_xattr_ctx {
	const char *name;
	int name_len;
	int found_idx;
	char *found_data;
	int found_data_len;
};

static int __find_xattr(int num, struct btrfs_key *di_key,
			const char *name, int name_len,
			const char *data, int data_len,
			u8 type, void *vctx)
{
	struct find_xattr_ctx *ctx = vctx;

	if (name_len == ctx->name_len &&
	    strncmp(name, ctx->name, name_len) == 0) {
		ctx->found_idx = num;
		ctx->found_data_len = data_len;
		ctx->found_data = kmalloc(data_len, GFP_NOFS);
		if (!ctx->found_data)
			return -ENOMEM;
		memcpy(ctx->found_data, data, data_len);
		return 1;
	}
	return 0;
}

static int find_xattr(struct send_ctx *sctx,
		      struct btrfs_root *root,
		      struct btrfs_path *path,
		      struct btrfs_key *key,
		      const char *name, int name_len,
		      char **data, int *data_len)
{
	int ret;
	struct find_xattr_ctx ctx;

	ctx.name = name;
	ctx.name_len = name_len;
	ctx.found_idx = -1;
	ctx.found_data = NULL;
	ctx.found_data_len = 0;

	ret = iterate_dir_item(sctx, root, path, key, __find_xattr, &ctx);
	if (ret < 0)
		return ret;

	if (ctx.found_idx == -1)
		return -ENOENT;
	if (data) {
		*data = ctx.found_data;
		*data_len = ctx.found_data_len;
	} else {
		kfree(ctx.found_data);
	}
	return ctx.found_idx;
}


static int __process_changed_new_xattr(int num, struct btrfs_key *di_key,
				       const char *name, int name_len,
				       const char *data, int data_len,
				       u8 type, void *ctx)
{
	int ret;
	struct send_ctx *sctx = ctx;
	char *found_data = NULL;
	int found_data_len  = 0;
	struct fs_path *p = NULL;

	ret = find_xattr(sctx, sctx->parent_root, sctx->right_path,
			sctx->cmp_key, name, name_len, &found_data,
			&found_data_len);
	if (ret == -ENOENT) {
		ret = __process_new_xattr(num, di_key, name, name_len, data,
				data_len, type, ctx);
	} else if (ret >= 0) {
		if (data_len != found_data_len ||
		    memcmp(data, found_data, data_len)) {
			ret = __process_new_xattr(num, di_key, name, name_len,
					data, data_len, type, ctx);
		} else {
			ret = 0;
		}
	}

	kfree(found_data);
	fs_path_free(sctx, p);
	return ret;
}

static int __process_changed_deleted_xattr(int num, struct btrfs_key *di_key,
					   const char *name, int name_len,
					   const char *data, int data_len,
					   u8 type, void *ctx)
{
	int ret;
	struct send_ctx *sctx = ctx;

	ret = find_xattr(sctx, sctx->send_root, sctx->left_path, sctx->cmp_key,
			name, name_len, NULL, NULL);
	if (ret == -ENOENT)
		ret = __process_deleted_xattr(num, di_key, name, name_len, data,
				data_len, type, ctx);
	else if (ret >= 0)
		ret = 0;

	return ret;
}

static int process_changed_xattr(struct send_ctx *sctx)
{
	int ret = 0;

	ret = iterate_dir_item(sctx, sctx->send_root, sctx->left_path,
			sctx->cmp_key, __process_changed_new_xattr, sctx);
	if (ret < 0)
		goto out;
	ret = iterate_dir_item(sctx, sctx->parent_root, sctx->right_path,
			sctx->cmp_key, __process_changed_deleted_xattr, sctx);

out:
	return ret;
}

static int process_all_new_xattrs(struct send_ctx *sctx)
{
	int ret;
	struct btrfs_root *root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct extent_buffer *eb;
	int slot;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	root = sctx->send_root;

	key.objectid = sctx->cmp_key->objectid;
	key.type = BTRFS_XATTR_ITEM_KEY;
	key.offset = 0;
	while (1) {
		ret = btrfs_search_slot_for_read(root, &key, path, 1, 0);
		if (ret < 0)
			goto out;
		if (ret) {
			ret = 0;
			goto out;
		}

		eb = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(eb, &found_key, slot);

		if (found_key.objectid != key.objectid ||
		    found_key.type != key.type) {
			ret = 0;
			goto out;
		}

		ret = iterate_dir_item(sctx, root, path, &found_key,
				__process_new_xattr, sctx);
		if (ret < 0)
			goto out;

		btrfs_release_path(path);
		key.offset = found_key.offset + 1;
	}

out:
	btrfs_free_path(path);
	return ret;
}

/*
 * Read some bytes from the current inode/file and send a write command to
 * user space.
 */
static int send_write(struct send_ctx *sctx, u64 offset, u32 len)
{
	int ret = 0;
	struct fs_path *p;
	loff_t pos = offset;
	int readed = 0;
	mm_segment_t old_fs;

	p = fs_path_alloc(sctx);
	if (!p)
		return -ENOMEM;

	/*
	 * vfs normally only accepts user space buffers for security reasons.
	 * we only read from the file and also only provide the read_buf buffer
	 * to vfs. As this buffer does not come from a user space call, it's
	 * ok to temporary allow kernel space buffers.
	 */
	old_fs = get_fs();
	set_fs(KERNEL_DS);

verbose_printk("btrfs: send_write offset=%llu, len=%d\n", offset, len);

	ret = open_cur_inode_file(sctx);
	if (ret < 0)
		goto out;

	ret = vfs_read(sctx->cur_inode_filp, sctx->read_buf, len, &pos);
	if (ret < 0)
		goto out;
	readed = ret;
	if (!readed)
		goto out;

	ret = begin_cmd(sctx, BTRFS_SEND_C_WRITE);
	if (ret < 0)
		goto out;

	ret = get_cur_path(sctx, sctx->cur_ino, sctx->cur_inode_gen, p);
	if (ret < 0)
		goto out;

	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, p);
	TLV_PUT_U64(sctx, BTRFS_SEND_A_FILE_OFFSET, offset);
	TLV_PUT(sctx, BTRFS_SEND_A_DATA, sctx->read_buf, readed);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	fs_path_free(sctx, p);
	set_fs(old_fs);
	if (ret < 0)
		return ret;
	return readed;
}

/*
 * Send a clone command to user space.
 */
static int send_clone(struct send_ctx *sctx,
		      u64 offset, u32 len,
		      struct clone_root *clone_root)
{
	int ret = 0;
	struct btrfs_root *clone_root2 = clone_root->root;
	struct fs_path *p;
	u64 gen;

verbose_printk("btrfs: send_clone offset=%llu, len=%d, clone_root=%llu, "
	       "clone_inode=%llu, clone_offset=%llu\n", offset, len,
		clone_root->root->objectid, clone_root->ino,
		clone_root->offset);

	p = fs_path_alloc(sctx);
	if (!p)
		return -ENOMEM;

	ret = begin_cmd(sctx, BTRFS_SEND_C_CLONE);
	if (ret < 0)
		goto out;

	ret = get_cur_path(sctx, sctx->cur_ino, sctx->cur_inode_gen, p);
	if (ret < 0)
		goto out;

	TLV_PUT_U64(sctx, BTRFS_SEND_A_FILE_OFFSET, offset);
	TLV_PUT_U64(sctx, BTRFS_SEND_A_CLONE_LEN, len);
	TLV_PUT_PATH(sctx, BTRFS_SEND_A_PATH, p);

	if (clone_root2 == sctx->send_root) {
		ret = get_inode_info(sctx->send_root, clone_root->ino, NULL,
				&gen, NULL, NULL, NULL);
		if (ret < 0)
			goto out;
		ret = get_cur_path(sctx, clone_root->ino, gen, p);
	} else {
		ret = get_inode_path(sctx, clone_root2, clone_root->ino, p);
	}
	if (ret < 0)
		goto out;

	TLV_PUT_UUID(sctx, BTRFS_SEND_A_CLONE_UUID,
			clone_root2->root_item.uuid);
	TLV_PUT_U64(sctx, BTRFS_SEND_A_CLONE_CTRANSID,
			clone_root2->root_item.ctransid);
	TLV_PUT_PATH(sctx, BTRFS_SEND_A_CLONE_PATH, p);
	TLV_PUT_U64(sctx, BTRFS_SEND_A_CLONE_OFFSET,
			clone_root->offset);

	ret = send_cmd(sctx);

tlv_put_failure:
out:
	fs_path_free(sctx, p);
	return ret;
}

static int send_write_or_clone(struct send_ctx *sctx,
			       struct btrfs_path *path,
			       struct btrfs_key *key,
			       struct clone_root *clone_root)
{
	int ret = 0;
	struct btrfs_file_extent_item *ei;
	u64 offset = key->offset;
	u64 pos = 0;
	u64 len;
	u32 l;
	u8 type;

	ei = btrfs_item_ptr(path->nodes[0], path->slots[0],
			struct btrfs_file_extent_item);
	type = btrfs_file_extent_type(path->nodes[0], ei);
	if (type == BTRFS_FILE_EXTENT_INLINE)
		len = btrfs_file_extent_inline_len(path->nodes[0], ei);
	else
		len = btrfs_file_extent_num_bytes(path->nodes[0], ei);

	if (offset + len > sctx->cur_inode_size)
		len = sctx->cur_inode_size - offset;
	if (len == 0) {
		ret = 0;
		goto out;
	}

	if (!clone_root) {
		while (pos < len) {
			l = len - pos;
			if (l > BTRFS_SEND_READ_SIZE)
				l = BTRFS_SEND_READ_SIZE;
			ret = send_write(sctx, pos + offset, l);
			if (ret < 0)
				goto out;
			if (!ret)
				break;
			pos += ret;
		}
		ret = 0;
	} else {
		ret = send_clone(sctx, offset, len, clone_root);
	}

out:
	return ret;
}

static int is_extent_unchanged(struct send_ctx *sctx,
			       struct btrfs_path *left_path,
			       struct btrfs_key *ekey)
{
	int ret = 0;
	struct btrfs_key key;
	struct btrfs_path *path = NULL;
	struct extent_buffer *eb;
	int slot;
	struct btrfs_key found_key;
	struct btrfs_file_extent_item *ei;
	u64 left_disknr;
	u64 right_disknr;
	u64 left_offset;
	u64 right_offset;
	u64 left_offset_fixed;
	u64 left_len;
	u64 right_len;
	u8 left_type;
	u8 right_type;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	eb = left_path->nodes[0];
	slot = left_path->slots[0];

	ei = btrfs_item_ptr(eb, slot, struct btrfs_file_extent_item);
	left_type = btrfs_file_extent_type(eb, ei);
	left_disknr = btrfs_file_extent_disk_bytenr(eb, ei);
	left_len = btrfs_file_extent_num_bytes(eb, ei);
	left_offset = btrfs_file_extent_offset(eb, ei);

	if (left_type != BTRFS_FILE_EXTENT_REG) {
		ret = 0;
		goto out;
	}

	/*
	 * Following comments will refer to these graphics. L is the left
	 * extents which we are checking at the moment. 1-8 are the right
	 * extents that we iterate.
	 *
	 *       |-----L-----|
	 * |-1-|-2a-|-3-|-4-|-5-|-6-|
	 *
	 *       |-----L-----|
	 * |--1--|-2b-|...(same as above)
	 *
	 * Alternative situation. Happens on files where extents got split.
	 *       |-----L-----|
	 * |-----------7-----------|-6-|
	 *
	 * Alternative situation. Happens on files which got larger.
	 *       |-----L-----|
	 * |-8-|
	 * Nothing follows after 8.
	 */

	key.objectid = ekey->objectid;
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = ekey->offset;
	ret = btrfs_search_slot_for_read(sctx->parent_root, &key, path, 0, 0);
	if (ret < 0)
		goto out;
	if (ret) {
		ret = 0;
		goto out;
	}

	/*
	 * Handle special case where the right side has no extents at all.
	 */
	eb = path->nodes[0];
	slot = path->slots[0];
	btrfs_item_key_to_cpu(eb, &found_key, slot);
	if (found_key.objectid != key.objectid ||
	    found_key.type != key.type) {
		ret = 0;
		goto out;
	}

	/*
	 * We're now on 2a, 2b or 7.
	 */
	key = found_key;
	while (key.offset < ekey->offset + left_len) {
		ei = btrfs_item_ptr(eb, slot, struct btrfs_file_extent_item);
		right_type = btrfs_file_extent_type(eb, ei);
		right_disknr = btrfs_file_extent_disk_bytenr(eb, ei);
		right_len = btrfs_file_extent_num_bytes(eb, ei);
		right_offset = btrfs_file_extent_offset(eb, ei);

		if (right_type != BTRFS_FILE_EXTENT_REG) {
			ret = 0;
			goto out;
		}

		/*
		 * Are we at extent 8? If yes, we know the extent is changed.
		 * This may only happen on the first iteration.
		 */
		if (found_key.offset + right_len < ekey->offset) {
			ret = 0;
			goto out;
		}

		left_offset_fixed = left_offset;
		if (key.offset < ekey->offset) {
			/* Fix the right offset for 2a and 7. */
			right_offset += ekey->offset - key.offset;
		} else {
			/* Fix the left offset for all behind 2a and 2b */
			left_offset_fixed += key.offset - ekey->offset;
		}

		/*
		 * Check if we have the same extent.
		 */
		if (left_disknr + left_offset_fixed !=
				right_disknr + right_offset) {
			ret = 0;
			goto out;
		}

		/*
		 * Go to the next extent.
		 */
		ret = btrfs_next_item(sctx->parent_root, path);
		if (ret < 0)
			goto out;
		if (!ret) {
			eb = path->nodes[0];
			slot = path->slots[0];
			btrfs_item_key_to_cpu(eb, &found_key, slot);
		}
		if (ret || found_key.objectid != key.objectid ||
		    found_key.type != key.type) {
			key.offset += right_len;
			break;
		} else {
			if (found_key.offset != key.offset + right_len) {
				/* Should really not happen */
				ret = -EIO;
				goto out;
			}
		}
		key = found_key;
	}

	/*
	 * We're now behind the left extent (treat as unchanged) or at the end
	 * of the right side (treat as changed).
	 */
	if (key.offset >= ekey->offset + left_len)
		ret = 1;
	else
		ret = 0;


out:
	btrfs_free_path(path);
	return ret;
}

static int process_extent(struct send_ctx *sctx,
			  struct btrfs_path *path,
			  struct btrfs_key *key)
{
	int ret = 0;
	struct clone_root *found_clone = NULL;

	if (S_ISLNK(sctx->cur_inode_mode))
		return 0;

	if (sctx->parent_root && !sctx->cur_inode_new) {
		ret = is_extent_unchanged(sctx, path, key);
		if (ret < 0)
			goto out;
		if (ret) {
			ret = 0;
			goto out;
		}
	}

	ret = find_extent_clone(sctx, path, key->objectid, key->offset,
			sctx->cur_inode_size, &found_clone);
	if (ret != -ENOENT && ret < 0)
		goto out;

	ret = send_write_or_clone(sctx, path, key, found_clone);

out:
	return ret;
}

static int process_all_extents(struct send_ctx *sctx)
{
	int ret;
	struct btrfs_root *root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct extent_buffer *eb;
	int slot;

	root = sctx->send_root;
	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	key.objectid = sctx->cmp_key->objectid;
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = 0;
	while (1) {
		ret = btrfs_search_slot_for_read(root, &key, path, 1, 0);
		if (ret < 0)
			goto out;
		if (ret) {
			ret = 0;
			goto out;
		}

		eb = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(eb, &found_key, slot);

		if (found_key.objectid != key.objectid ||
		    found_key.type != key.type) {
			ret = 0;
			goto out;
		}

		ret = process_extent(sctx, path, &found_key);
		if (ret < 0)
			goto out;

		btrfs_release_path(path);
		key.offset = found_key.offset + 1;
	}

out:
	btrfs_free_path(path);
	return ret;
}

static int process_recorded_refs_if_needed(struct send_ctx *sctx, int at_end)
{
	int ret = 0;

	if (sctx->cur_ino == 0)
		goto out;
	if (!at_end && sctx->cur_ino == sctx->cmp_key->objectid &&
	    sctx->cmp_key->type <= BTRFS_INODE_REF_KEY)
		goto out;
	if (list_empty(&sctx->new_refs) && list_empty(&sctx->deleted_refs))
		goto out;

	ret = process_recorded_refs(sctx);

out:
	return ret;
}

static int finish_inode_if_needed(struct send_ctx *sctx, int at_end)
{
	int ret = 0;
	u64 left_mode;
	u64 left_uid;
	u64 left_gid;
	u64 right_mode;
	u64 right_uid;
	u64 right_gid;
	int need_chmod = 0;
	int need_chown = 0;

	ret = process_recorded_refs_if_needed(sctx, at_end);
	if (ret < 0)
		goto out;

	if (sctx->cur_ino == 0 || sctx->cur_inode_deleted)
		goto out;
	if (!at_end && sctx->cmp_key->objectid == sctx->cur_ino)
		goto out;

	ret = get_inode_info(sctx->send_root, sctx->cur_ino, NULL, NULL,
			&left_mode, &left_uid, &left_gid);
	if (ret < 0)
		goto out;

	if (!S_ISLNK(sctx->cur_inode_mode)) {
		if (!sctx->parent_root || sctx->cur_inode_new) {
			need_chmod = 1;
			need_chown = 1;
		} else {
			ret = get_inode_info(sctx->parent_root, sctx->cur_ino,
					NULL, NULL, &right_mode, &right_uid,
					&right_gid);
			if (ret < 0)
				goto out;

			if (left_uid != right_uid || left_gid != right_gid)
				need_chown = 1;
			if (left_mode != right_mode)
				need_chmod = 1;
		}
	}

	if (S_ISREG(sctx->cur_inode_mode)) {
		ret = send_truncate(sctx, sctx->cur_ino, sctx->cur_inode_gen,
				sctx->cur_inode_size);
		if (ret < 0)
			goto out;
	}

	if (need_chown) {
		ret = send_chown(sctx, sctx->cur_ino, sctx->cur_inode_gen,
				left_uid, left_gid);
		if (ret < 0)
			goto out;
	}
	if (need_chmod) {
		ret = send_chmod(sctx, sctx->cur_ino, sctx->cur_inode_gen,
				left_mode);
		if (ret < 0)
			goto out;
	}

	/*
	 * Need to send that every time, no matter if it actually changed
	 * between the two trees as we have done changes to the inode before.
	 */
	ret = send_utimes(sctx, sctx->cur_ino, sctx->cur_inode_gen);
	if (ret < 0)
		goto out;

out:
	return ret;
}

static int changed_inode(struct send_ctx *sctx,
			 enum btrfs_compare_tree_result result)
{
	int ret = 0;
	struct btrfs_key *key = sctx->cmp_key;
	struct btrfs_inode_item *left_ii = NULL;
	struct btrfs_inode_item *right_ii = NULL;
	u64 left_gen = 0;
	u64 right_gen = 0;

	ret = close_cur_inode_file(sctx);
	if (ret < 0)
		goto out;

	sctx->cur_ino = key->objectid;
	sctx->cur_inode_new_gen = 0;
	sctx->cur_inode_first_ref_orphan = 0;
	sctx->send_progress = sctx->cur_ino;

	if (result == BTRFS_COMPARE_TREE_NEW ||
	    result == BTRFS_COMPARE_TREE_CHANGED) {
		left_ii = btrfs_item_ptr(sctx->left_path->nodes[0],
				sctx->left_path->slots[0],
				struct btrfs_inode_item);
		left_gen = btrfs_inode_generation(sctx->left_path->nodes[0],
				left_ii);
	} else {
		right_ii = btrfs_item_ptr(sctx->right_path->nodes[0],
				sctx->right_path->slots[0],
				struct btrfs_inode_item);
		right_gen = btrfs_inode_generation(sctx->right_path->nodes[0],
				right_ii);
	}
	if (result == BTRFS_COMPARE_TREE_CHANGED) {
		right_ii = btrfs_item_ptr(sctx->right_path->nodes[0],
				sctx->right_path->slots[0],
				struct btrfs_inode_item);

		right_gen = btrfs_inode_generation(sctx->right_path->nodes[0],
				right_ii);
		if (left_gen != right_gen)
			sctx->cur_inode_new_gen = 1;
	}

	if (result == BTRFS_COMPARE_TREE_NEW) {
		sctx->cur_inode_gen = left_gen;
		sctx->cur_inode_new = 1;
		sctx->cur_inode_deleted = 0;
		sctx->cur_inode_size = btrfs_inode_size(
				sctx->left_path->nodes[0], left_ii);
		sctx->cur_inode_mode = btrfs_inode_mode(
				sctx->left_path->nodes[0], left_ii);
		if (sctx->cur_ino != BTRFS_FIRST_FREE_OBJECTID)
			ret = send_create_inode(sctx, sctx->left_path,
					sctx->cmp_key);
	} else if (result == BTRFS_COMPARE_TREE_DELETED) {
		sctx->cur_inode_gen = right_gen;
		sctx->cur_inode_new = 0;
		sctx->cur_inode_deleted = 1;
		sctx->cur_inode_size = btrfs_inode_size(
				sctx->right_path->nodes[0], right_ii);
		sctx->cur_inode_mode = btrfs_inode_mode(
				sctx->right_path->nodes[0], right_ii);
	} else if (result == BTRFS_COMPARE_TREE_CHANGED) {
		if (sctx->cur_inode_new_gen) {
			sctx->cur_inode_gen = right_gen;
			sctx->cur_inode_new = 0;
			sctx->cur_inode_deleted = 1;
			sctx->cur_inode_size = btrfs_inode_size(
					sctx->right_path->nodes[0], right_ii);
			sctx->cur_inode_mode = btrfs_inode_mode(
					sctx->right_path->nodes[0], right_ii);
			ret = process_all_refs(sctx,
					BTRFS_COMPARE_TREE_DELETED);
			if (ret < 0)
				goto out;

			sctx->cur_inode_gen = left_gen;
			sctx->cur_inode_new = 1;
			sctx->cur_inode_deleted = 0;
			sctx->cur_inode_size = btrfs_inode_size(
					sctx->left_path->nodes[0], left_ii);
			sctx->cur_inode_mode = btrfs_inode_mode(
					sctx->left_path->nodes[0], left_ii);
			ret = send_create_inode(sctx, sctx->left_path,
					sctx->cmp_key);
			if (ret < 0)
				goto out;

			ret = process_all_refs(sctx, BTRFS_COMPARE_TREE_NEW);
			if (ret < 0)
				goto out;
			ret = process_all_extents(sctx);
			if (ret < 0)
				goto out;
			ret = process_all_new_xattrs(sctx);
			if (ret < 0)
				goto out;
		} else {
			sctx->cur_inode_gen = left_gen;
			sctx->cur_inode_new = 0;
			sctx->cur_inode_new_gen = 0;
			sctx->cur_inode_deleted = 0;
			sctx->cur_inode_size = btrfs_inode_size(
					sctx->left_path->nodes[0], left_ii);
			sctx->cur_inode_mode = btrfs_inode_mode(
					sctx->left_path->nodes[0], left_ii);
		}
	}

out:
	return ret;
}

static int changed_ref(struct send_ctx *sctx,
		       enum btrfs_compare_tree_result result)
{
	int ret = 0;

	BUG_ON(sctx->cur_ino != sctx->cmp_key->objectid);

	if (!sctx->cur_inode_new_gen &&
	    sctx->cur_ino != BTRFS_FIRST_FREE_OBJECTID) {
		if (result == BTRFS_COMPARE_TREE_NEW)
			ret = record_new_ref(sctx);
		else if (result == BTRFS_COMPARE_TREE_DELETED)
			ret = record_deleted_ref(sctx);
		else if (result == BTRFS_COMPARE_TREE_CHANGED)
			ret = record_changed_ref(sctx);
	}

	return ret;
}

static int changed_xattr(struct send_ctx *sctx,
			 enum btrfs_compare_tree_result result)
{
	int ret = 0;

	BUG_ON(sctx->cur_ino != sctx->cmp_key->objectid);

	if (!sctx->cur_inode_new_gen && !sctx->cur_inode_deleted) {
		if (result == BTRFS_COMPARE_TREE_NEW)
			ret = process_new_xattr(sctx);
		else if (result == BTRFS_COMPARE_TREE_DELETED)
			ret = process_deleted_xattr(sctx);
		else if (result == BTRFS_COMPARE_TREE_CHANGED)
			ret = process_changed_xattr(sctx);
	}

	return ret;
}

static int changed_extent(struct send_ctx *sctx,
			  enum btrfs_compare_tree_result result)
{
	int ret = 0;

	BUG_ON(sctx->cur_ino != sctx->cmp_key->objectid);

	if (!sctx->cur_inode_new_gen && !sctx->cur_inode_deleted) {
		if (result != BTRFS_COMPARE_TREE_DELETED)
			ret = process_extent(sctx, sctx->left_path,
					sctx->cmp_key);
	}

	return ret;
}


static int changed_cb(struct btrfs_root *left_root,
		      struct btrfs_root *right_root,
		      struct btrfs_path *left_path,
		      struct btrfs_path *right_path,
		      struct btrfs_key *key,
		      enum btrfs_compare_tree_result result,
		      void *ctx)
{
	int ret = 0;
	struct send_ctx *sctx = ctx;

	sctx->left_path = left_path;
	sctx->right_path = right_path;
	sctx->cmp_key = key;

	ret = finish_inode_if_needed(sctx, 0);
	if (ret < 0)
		goto out;

	if (key->type == BTRFS_INODE_ITEM_KEY)
		ret = changed_inode(sctx, result);
	else if (key->type == BTRFS_INODE_REF_KEY)
		ret = changed_ref(sctx, result);
	else if (key->type == BTRFS_XATTR_ITEM_KEY)
		ret = changed_xattr(sctx, result);
	else if (key->type == BTRFS_EXTENT_DATA_KEY)
		ret = changed_extent(sctx, result);

out:
	return ret;
}

static int full_send_tree(struct send_ctx *sctx)
{
	int ret;
	struct btrfs_trans_handle *trans = NULL;
	struct btrfs_root *send_root = sctx->send_root;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_path *path;
	struct extent_buffer *eb;
	int slot;
	u64 start_ctransid;
	u64 ctransid;

	path = alloc_path_for_send();
	if (!path)
		return -ENOMEM;

	spin_lock(&send_root->root_times_lock);
	start_ctransid = btrfs_root_ctransid(&send_root->root_item);
	spin_unlock(&send_root->root_times_lock);

	key.objectid = BTRFS_FIRST_FREE_OBJECTID;
	key.type = BTRFS_INODE_ITEM_KEY;
	key.offset = 0;

join_trans:
	/*
	 * We need to make sure the transaction does not get committed
	 * while we do anything on commit roots. Join a transaction to prevent
	 * this.
	 */
	trans = btrfs_join_transaction(send_root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out;
	}

	/*
	 * Make sure the tree has not changed
	 */
	spin_lock(&send_root->root_times_lock);
	ctransid = btrfs_root_ctransid(&send_root->root_item);
	spin_unlock(&send_root->root_times_lock);

	if (ctransid != start_ctransid) {
		WARN(1, KERN_WARNING "btrfs: the root that you're trying to "
				     "send was modified in between. This is "
				     "probably a bug.\n");
		ret = -EIO;
		goto out;
	}

	ret = btrfs_search_slot_for_read(send_root, &key, path, 1, 0);
	if (ret < 0)
		goto out;
	if (ret)
		goto out_finish;

	while (1) {
		/*
		 * When someone want to commit while we iterate, end the
		 * joined transaction and rejoin.
		 */
		if (btrfs_should_end_transaction(trans, send_root)) {
			ret = btrfs_end_transaction(trans, send_root);
			trans = NULL;
			if (ret < 0)
				goto out;
			btrfs_release_path(path);
			goto join_trans;
		}

		eb = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(eb, &found_key, slot);

		ret = changed_cb(send_root, NULL, path, NULL,
				&found_key, BTRFS_COMPARE_TREE_NEW, sctx);
		if (ret < 0)
			goto out;

		key.objectid = found_key.objectid;
		key.type = found_key.type;
		key.offset = found_key.offset + 1;

		ret = btrfs_next_item(send_root, path);
		if (ret < 0)
			goto out;
		if (ret) {
			ret  = 0;
			break;
		}
	}

out_finish:
	ret = finish_inode_if_needed(sctx, 1);

out:
	btrfs_free_path(path);
	if (trans) {
		if (!ret)
			ret = btrfs_end_transaction(trans, send_root);
		else
			btrfs_end_transaction(trans, send_root);
	}
	return ret;
}

static int send_subvol(struct send_ctx *sctx)
{
	int ret;

	ret = send_header(sctx);
	if (ret < 0)
		goto out;

	ret = send_subvol_begin(sctx);
	if (ret < 0)
		goto out;

	if (sctx->parent_root) {
		ret = btrfs_compare_trees(sctx->send_root, sctx->parent_root,
				changed_cb, sctx);
		if (ret < 0)
			goto out;
		ret = finish_inode_if_needed(sctx, 1);
		if (ret < 0)
			goto out;
	} else {
		ret = full_send_tree(sctx);
		if (ret < 0)
			goto out;
	}

out:
	if (!ret)
		ret = close_cur_inode_file(sctx);
	else
		close_cur_inode_file(sctx);

	free_recorded_refs(sctx);
	return ret;
}

long btrfs_ioctl_send(struct file *mnt_file, void __user *arg_)
{
	int ret = 0;
	struct btrfs_root *send_root;
	struct btrfs_root *clone_root;
	struct btrfs_fs_info *fs_info;
	struct btrfs_ioctl_send_args *arg = NULL;
	struct btrfs_key key;
	struct file *filp = NULL;
	struct send_ctx *sctx = NULL;
	u32 i;
	u64 *clone_sources_tmp = NULL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	send_root = BTRFS_I(fdentry(mnt_file)->d_inode)->root;
	fs_info = send_root->fs_info;

	arg = memdup_user(arg_, sizeof(*arg));
	if (IS_ERR(arg)) {
		ret = PTR_ERR(arg);
		arg = NULL;
		goto out;
	}

	if (!access_ok(VERIFY_READ, arg->clone_sources,
			sizeof(*arg->clone_sources *
			arg->clone_sources_count))) {
		ret = -EFAULT;
		goto out;
	}

	sctx = kzalloc(sizeof(struct send_ctx), GFP_NOFS);
	if (!sctx) {
		ret = -ENOMEM;
		goto out;
	}

	INIT_LIST_HEAD(&sctx->new_refs);
	INIT_LIST_HEAD(&sctx->deleted_refs);
	INIT_RADIX_TREE(&sctx->name_cache, GFP_NOFS);
	INIT_LIST_HEAD(&sctx->name_cache_list);

	sctx->send_filp = fget(arg->send_fd);
	if (IS_ERR(sctx->send_filp)) {
		ret = PTR_ERR(sctx->send_filp);
		goto out;
	}

	sctx->mnt = mnt_file->f_path.mnt;

	sctx->send_root = send_root;
	sctx->clone_roots_cnt = arg->clone_sources_count;

	sctx->send_max_size = BTRFS_SEND_BUF_SIZE;
	sctx->send_buf = vmalloc(sctx->send_max_size);
	if (!sctx->send_buf) {
		ret = -ENOMEM;
		goto out;
	}

	sctx->read_buf = vmalloc(BTRFS_SEND_READ_SIZE);
	if (!sctx->read_buf) {
		ret = -ENOMEM;
		goto out;
	}

	sctx->clone_roots = vzalloc(sizeof(struct clone_root) *
			(arg->clone_sources_count + 1));
	if (!sctx->clone_roots) {
		ret = -ENOMEM;
		goto out;
	}

	if (arg->clone_sources_count) {
		clone_sources_tmp = vmalloc(arg->clone_sources_count *
				sizeof(*arg->clone_sources));
		if (!clone_sources_tmp) {
			ret = -ENOMEM;
			goto out;
		}

		ret = copy_from_user(clone_sources_tmp, arg->clone_sources,
				arg->clone_sources_count *
				sizeof(*arg->clone_sources));
		if (ret) {
			ret = -EFAULT;
			goto out;
		}

		for (i = 0; i < arg->clone_sources_count; i++) {
			key.objectid = clone_sources_tmp[i];
			key.type = BTRFS_ROOT_ITEM_KEY;
			key.offset = (u64)-1;
			clone_root = btrfs_read_fs_root_no_name(fs_info, &key);
			if (!clone_root) {
				ret = -EINVAL;
				goto out;
			}
			if (IS_ERR(clone_root)) {
				ret = PTR_ERR(clone_root);
				goto out;
			}
			sctx->clone_roots[i].root = clone_root;
		}
		vfree(clone_sources_tmp);
		clone_sources_tmp = NULL;
	}

	if (arg->parent_root) {
		key.objectid = arg->parent_root;
		key.type = BTRFS_ROOT_ITEM_KEY;
		key.offset = (u64)-1;
		sctx->parent_root = btrfs_read_fs_root_no_name(fs_info, &key);
		if (!sctx->parent_root) {
			ret = -EINVAL;
			goto out;
		}
	}

	/*
	 * Clones from send_root are allowed, but only if the clone source
	 * is behind the current send position. This is checked while searching
	 * for possible clone sources.
	 */
	sctx->clone_roots[sctx->clone_roots_cnt++].root = sctx->send_root;

	/* We do a bsearch later */
	sort(sctx->clone_roots, sctx->clone_roots_cnt,
			sizeof(*sctx->clone_roots), __clone_root_cmp_sort,
			NULL);

	ret = send_subvol(sctx);
	if (ret < 0)
		goto out;

	ret = begin_cmd(sctx, BTRFS_SEND_C_END);
	if (ret < 0)
		goto out;
	ret = send_cmd(sctx);
	if (ret < 0)
		goto out;

out:
	if (filp)
		fput(filp);
	kfree(arg);
	vfree(clone_sources_tmp);

	if (sctx) {
		if (sctx->send_filp)
			fput(sctx->send_filp);

		vfree(sctx->clone_roots);
		vfree(sctx->send_buf);
		vfree(sctx->read_buf);

		name_cache_free(sctx);

		kfree(sctx);
	}

	return ret;
}
