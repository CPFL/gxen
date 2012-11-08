/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 1999, 2001, 2003  Free Software Foundation, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <fsimage_grub.h>

#define	mapblock1 (*fsig_int1(ffi))
#define	mapblock2 (*fsig_int2(ffi))

/* sizes are always in bytes, BLOCK values are always in DEV_BSIZE (sectors) */
#define DEV_BSIZE 512

/* include/linux/fs.h */
#define BLOCK_SIZE 1024		/* initial block size for superblock read */
/* made up, defaults to 1 but can be passed via mount_opts */
#define WHICH_SUPER 1
/* kind of from fs/ext2/super.c */
#define SBLOCK (WHICH_SUPER * BLOCK_SIZE / DEV_BSIZE)	/* = 2 */

/* include/asm-i386/types.h */
typedef __signed__ char __s8;
typedef unsigned char __u8;
typedef __signed__ short __s16;
typedef unsigned short __u16;
typedef __signed__ int __s32;
typedef unsigned int __u32;

/*
 * Constants relative to the data blocks, from ext2_fs.h
 */
#define EXT2_NDIR_BLOCKS                12
#define EXT2_IND_BLOCK                  EXT2_NDIR_BLOCKS
#define EXT2_DIND_BLOCK                 (EXT2_IND_BLOCK + 1)
#define EXT2_TIND_BLOCK                 (EXT2_DIND_BLOCK + 1)
#define EXT2_N_BLOCKS                   (EXT2_TIND_BLOCK + 1)

/* Inode flags */
#define EXT4_EXTENTS_FL                 0x00080000 /* Inode uses extents */

/* include/linux/ext2_fs.h */
struct ext2_super_block
  {
    __u32 s_inodes_count;	/* Inodes count */
    __u32 s_blocks_count;	/* Blocks count */
    __u32 s_r_blocks_count;	/* Reserved blocks count */
    __u32 s_free_blocks_count;	/* Free blocks count */
    __u32 s_free_inodes_count;	/* Free inodes count */
    __u32 s_first_data_block;	/* First Data Block */
    __u32 s_log_block_size;	/* Block size */
    __s32 s_log_frag_size;	/* Fragment size */
    __u32 s_blocks_per_group;	/* # Blocks per group */
    __u32 s_frags_per_group;	/* # Fragments per group */
    __u32 s_inodes_per_group;	/* # Inodes per group */
    __u32 s_mtime;		/* Mount time */
    __u32 s_wtime;		/* Write time */
    __u16 s_mnt_count;		/* Mount count */
    __s16 s_max_mnt_count;	/* Maximal mount count */
    __u16 s_magic;		/* Magic signature */
    __u16 s_state;		/* File system state */
    __u16 s_errors;		/* Behaviour when detecting errors */
    __u16 s_pad;
    __u32 s_lastcheck;		/* time of last check */
    __u32 s_checkinterval;	/* max. time between checks */
    __u32 s_creator_os;		/* OS */
    __u32 s_rev_level;		/* Revision level */
    __u16 s_def_resuid;		/* Default uid for reserved blocks */
    __u16 s_def_resgid;		/* Default gid for reserved blocks */
    /*
     * These fields are for EXT2_DYNAMIC_REV superblocks only.
     *
     * Note: the difference between the compatible feature set and
     * the incompatible feature set is that if there is a bit set
     * in the incompatible feature set that the kernel doesn't
     * know about, it should refuse to mount the filesystem.
     *
     * e2fsck's requirements are more strict; if it doesn't know
     * about a feature in either the compatible or incompatible
     * feature set, it must abort and not try to meddle with
     * things it doesn't understand...
     */
    __u32 s_first_ino;		/* First non-reserved inode */
    __u16 s_inode_size;		/* size of inode structure */
    __u16 s_block_group_nr;	/* block group # of this superblock */
    __u32 s_feature_compat;	/* compatible feature set */
    __u32 s_feature_incompat;	/* incompatible feature set */
    __u32 s_feature_ro_compat;	/* readonly-compatible feature set */
    __u8  s_uuid[16];		/* 128-bit uuid for volume */
    char  s_volume_name[16];	/* volume name */
    char  s_last_mounted[64];	/* directory where last mounted */
    __u32 s_algorithm_usage_bitmap; /* For compression */
    /*
     * Performance hints.  Directory preallocation should only
     * happen if the EXT2_FEATURE_COMPAT_DIR_PREALLOC flag is on.
     */
    __u8  s_prealloc_blocks;	/* Nr of blocks to try to preallocate*/
    __u8  s_prealloc_dir_blocks;	/* Nr to preallocate for dirs */
    __u16 s_reserved_gdt_blocks;/* Per group table for online growth */
    /*
     * Journaling support valid if EXT2_FEATURE_COMPAT_HAS_JOURNAL set.
     */
    __u8 s_journal_uuid[16];	/* uuid of journal superblock */
    __u32 s_journal_inum;	/* inode number of journal file */
    __u32 s_journal_dev;	/* device number of journal file */
    __u32 s_last_orphan;	/* start of list of inodes to delete */
    __u32 s_hash_seed[4];	/* HTREE hash seed */
    __u8  s_def_hash_version;	/* Default hash version to use */
    __u8  s_jnl_backup_type; 	/* Default type of journal backup */
    __u16 s_reserved_word_pad;
    __u32 s_default_mount_opts;
    __u32 s_first_meta_bg;	/* First metablock group */
    __u32 s_mkfs_time;		/* When the filesystem was created */
    __u32 s_jnl_blocks[17]; 	/* Backup of the journal inode */
    __u32 s_reserved[172];	/* Padding to the end of the block */
  };

struct ext2_group_desc
  {
    __u32 bg_block_bitmap;	/* Blocks bitmap block */
    __u32 bg_inode_bitmap;	/* Inodes bitmap block */
    __u32 bg_inode_table;	/* Inodes table block */
    __u16 bg_free_blocks_count;	/* Free blocks count */
    __u16 bg_free_inodes_count;	/* Free inodes count */
    __u16 bg_used_dirs_count;	/* Directories count */
    __u16 bg_pad;
    __u32 bg_reserved[3];
  };

struct ext2_inode
  {
    __u16 i_mode;		/* File mode */
    __u16 i_uid;		/* Owner Uid */
    __u32 i_size;		/* 4: Size in bytes */
    __u32 i_atime;		/* Access time */
    __u32 i_ctime;		/* 12: Creation time */
    __u32 i_mtime;		/* Modification time */
    __u32 i_dtime;		/* 20: Deletion Time */
    __u16 i_gid;		/* Group Id */
    __u16 i_links_count;	/* 24: Links count */
    __u32 i_blocks;		/* Blocks count */
    __u32 i_flags;		/* 32: File flags */
    union
      {
	struct
	  {
	    __u32 l_i_reserved1;
	  }
	linux1;
	struct
	  {
	    __u32 h_i_translator;
	  }
	hurd1;
	struct
	  {
	    __u32 m_i_reserved1;
	  }
	masix1;
      }
    osd1;			/* OS dependent 1 */
    __u32 i_block[EXT2_N_BLOCKS];	/* 40: Pointers to blocks */
    __u32 i_version;		/* File version (for NFS) */
    __u32 i_file_acl;		/* File ACL */
    __u32 i_dir_acl;		/* Directory ACL */
    __u32 i_faddr;		/* Fragment address */
    union
      {
	struct
	  {
	    __u8 l_i_frag;	/* Fragment number */
	    __u8 l_i_fsize;	/* Fragment size */
	    __u16 i_pad1;
	    __u32 l_i_reserved2[2];
	  }
	linux2;
	struct
	  {
	    __u8 h_i_frag;	/* Fragment number */
	    __u8 h_i_fsize;	/* Fragment size */
	    __u16 h_i_mode_high;
	    __u16 h_i_uid_high;
	    __u16 h_i_gid_high;
	    __u32 h_i_author;
	  }
	hurd2;
	struct
	  {
	    __u8 m_i_frag;	/* Fragment number */
	    __u8 m_i_fsize;	/* Fragment size */
	    __u16 m_pad1;
	    __u32 m_i_reserved2[2];
	  }
	masix2;
      }
    osd2;			/* OS dependent 2 */
  };

/* linux/limits.h */
#define NAME_MAX         255	/* # chars in a file name */

/* linux/posix_type.h */
typedef long linux_off_t;

/* linux/ext2fs.h */
#define EXT2_NAME_LEN 255
struct ext2_dir_entry
  {
    __u32 inode;		/* Inode number */
    __u16 rec_len;		/* Directory entry length */
    __u8 name_len;		/* Name length */
    __u8 file_type;
    char name[EXT2_NAME_LEN];	/* File name */
  };

/* linux/ext2fs.h */
/*
 * EXT2_DIR_PAD defines the directory entries boundaries
 *
 * NOTE: It must be a multiple of 4
 */
#define EXT2_DIR_PAD                    4
#define EXT2_DIR_ROUND                  (EXT2_DIR_PAD - 1)
#define EXT2_DIR_REC_LEN(name_len)      (((name_len) + 8 + EXT2_DIR_ROUND) & \
                                         ~EXT2_DIR_ROUND)

/* linux/ext4_fs_extents.h */
/*
 * This is the extent on-disk structure.
 * It's used at the bottom of the tree.
 */
struct ext4_extent {
    __u32 ee_block;       /* first logical block extent covers */
    __u16 ee_len;         /* number of blocks covered by extent */
    __u16 ee_start_hi;    /* high 16 bits of physical block */
    __u32 ee_start;       /* low 32 bits of physical block */
};

/*
 * This is index on-disk structure.
 * It's used at all the levels except the bottom.
 */
struct ext4_extent_idx {
    __u32 ei_block;       /* index covers logical blocks from 'block' */
    __u32 ei_leaf;        /* pointer to the physical block of the next *
                                 * level. leaf or next index could be there */
    __u16 ei_leaf_hi;     /* high 16 bits of physical block */
    __u16 ei_unused;
};

/*
 * Each block (leaves and indexes), even inode-stored has header.
 */
struct ext4_extent_header {
    __u16  eh_magic;       /* probably will support different formats */
    __u16  eh_entries;     /* number of valid entries */
    __u16  eh_max;         /* capacity of store in entries */
    __u16  eh_depth;       /* has tree real underlying blocks? */
    __u32  eh_generation;  /* generation of the tree */
};

#define EXT4_EXT_MAGIC          0xf30a

/* ext2/super.c */
#define log2(n) grub_log2(n)

#define EXT2_SUPER_MAGIC      0xEF53	/* include/linux/ext2_fs.h */
#define EXT2_ROOT_INO              2	/* include/linux/ext2_fs.h */
#define PATH_MAX                1024	/* include/linux/limits.h */
#define MAX_LINK_COUNT             5	/* number of symbolic links to follow */

/* made up, these are pointers into FSYS_BUF */
/* read once, always stays there: */
#define SUPERBLOCK \
    ((struct ext2_super_block *)(FSYS_BUF))
#define GROUP_DESC \
    ((struct ext2_group_desc *) \
     ((char *)SUPERBLOCK + sizeof(struct ext2_super_block)))
#define INODE \
    ((struct ext2_inode *)((caddr_t)GROUP_DESC + EXT2_BLOCK_SIZE(SUPERBLOCK)))
#define DATABLOCK1 \
    ((char *)((caddr_t)INODE + sizeof(struct ext2_inode)))
#define DATABLOCK2 \
    ((char *)((caddr_t)DATABLOCK1 + EXT2_BLOCK_SIZE(SUPERBLOCK)))

/* linux/ext2_fs.h */
#define EXT2_ADDR_PER_BLOCK(s)          (EXT2_BLOCK_SIZE(s) / sizeof (__u32))
#define EXT2_ADDR_PER_BLOCK_BITS(s)		(log2(EXT2_ADDR_PER_BLOCK(s)))

#define EXT2_INODE_SIZE(s)		(SUPERBLOCK->s_inode_size)
#define EXT2_INODES_PER_BLOCK(s)	(EXT2_BLOCK_SIZE(s)/EXT2_INODE_SIZE(s))

/* linux/ext2_fs.h */
#define EXT2_BLOCK_SIZE_BITS(s)        ((s)->s_log_block_size + 10)
/* kind of from ext2/super.c */
#define EXT2_BLOCK_SIZE(s)	(1 << EXT2_BLOCK_SIZE_BITS(s))
/* linux/ext2fs.h */
#define EXT2_DESC_PER_BLOCK(s) \
     (EXT2_BLOCK_SIZE(s) / sizeof (struct ext2_group_desc))
/* linux/stat.h */
#define S_IFMT  00170000
#define S_IFLNK  0120000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_ISLNK(m)	(((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m)      (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)      (((m) & S_IFMT) == S_IFDIR)

/* check filesystem types and read superblock into memory buffer */
static int
ext2fs_mount (fsi_file_t *ffi, const char *options)
{
  int retval = 1;

  if (/*(((current_drive & 0x80) || (current_slice != 0))
       && (current_slice != PC_SLICE_TYPE_EXT2FS)
       && (current_slice != PC_SLICE_TYPE_LINUX_RAID)
       && (! IS_PC_SLICE_TYPE_BSD_WITH_FS (current_slice, FS_EXT2FS))
       && (! IS_PC_SLICE_TYPE_BSD_WITH_FS (current_slice, FS_OTHER)))
      || part_length < (SBLOCK + (sizeof (struct ext2_super_block) / DEV_BSIZE))
      || */ !devread (ffi, SBLOCK, 0, sizeof (struct ext2_super_block),
		   (char *) SUPERBLOCK)
      || SUPERBLOCK->s_magic != EXT2_SUPER_MAGIC)
      retval = 0;

  return retval;
}

/* Takes a file system block number and reads it into BUFFER. */
static int
ext2_rdfsb (fsi_file_t *ffi, int fsblock, char *buffer)
{
#ifdef E2DEBUG
  printf ("fsblock %d buffer %d\n", fsblock, buffer);
#endif /* E2DEBUG */
  return devread (ffi, fsblock * (EXT2_BLOCK_SIZE (SUPERBLOCK) / DEV_BSIZE), 0,
		  EXT2_BLOCK_SIZE (SUPERBLOCK), (char *) buffer);
}

/* Walk through extents index tree to find the good leaf */
static struct ext4_extent_header *
ext4_recurse_extent_index(fsi_file_t *ffi, struct ext4_extent_header *extent_block, int logical_block)
{
  int i;
  struct ext4_extent_idx *index = (struct ext4_extent_idx *) (extent_block + 1);
  if (extent_block->eh_magic != EXT4_EXT_MAGIC)
    return NULL;
  if (extent_block->eh_depth == 0)
    return extent_block;
  for (i = 0; i < extent_block->eh_entries; i++)
    {
      if (logical_block < index[i].ei_block)
        break;
    }
  if (i == 0 || !ext2_rdfsb(ffi, index[i-1].ei_leaf, DATABLOCK1))
    return NULL;
  return (ext4_recurse_extent_index(ffi, (struct ext4_extent_header *) DATABLOCK1, logical_block));
}


/* from
  ext2/inode.c:ext2_bmap()
*/
/* Maps LOGICAL_BLOCK (the file offset divided by the blocksize) into
   a physical block (the location in the file system) via an inode. */
static int
ext2fs_block_map (fsi_file_t *ffi, int logical_block)
{
#ifdef E2DEBUG
  unsigned char *i;
  for (i = (unsigned char *) INODE;
       i < ((unsigned char *) INODE + sizeof (struct ext2_inode));
       i++)
    {
      printf ("%c", "0123456789abcdef"[*i >> 4]);
      printf ("%c", "0123456789abcdef"[*i % 16]);
      if (!((i + 1 - (unsigned char *) INODE) % 16))
	{
	  printf ("\n");
	}
      else
	{
	  printf (" ");
	}
    }
  printf ("logical block %d\n", logical_block);
#endif /* E2DEBUG */

  if (!(INODE->i_flags & EXT4_EXTENTS_FL))
      {
      /* if it is directly pointed to by the inode, return that physical addr */
      if (logical_block < EXT2_NDIR_BLOCKS)
        {
#ifdef E2DEBUG
          printf ("returning %d\n", (unsigned char *) (INODE->i_block[logical_block]));
          printf ("returning %d\n", INODE->i_block[logical_block]);
#endif /* E2DEBUG */
          return INODE->i_block[logical_block];
        }
      /* else */
      logical_block -= EXT2_NDIR_BLOCKS;
      /* try the indirect block */
      if (logical_block < EXT2_ADDR_PER_BLOCK (SUPERBLOCK))
        {
          if (mapblock1 != 1 && !ext2_rdfsb (ffi, INODE->i_block[EXT2_IND_BLOCK], DATABLOCK1))
            {
              errnum = ERR_FSYS_CORRUPT;
              return -1;
            }
          mapblock1 = 1;
          return ((__u32 *) DATABLOCK1)[logical_block];
        }
      /* else */
      logical_block -= EXT2_ADDR_PER_BLOCK (SUPERBLOCK);
      /* now try the double indirect block */
      if (logical_block < (1 << (EXT2_ADDR_PER_BLOCK_BITS (SUPERBLOCK) * 2)))
        {
          int bnum;
          if (mapblock1 != 2 && !ext2_rdfsb (ffi, INODE->i_block[EXT2_DIND_BLOCK], DATABLOCK1))
            {
              errnum = ERR_FSYS_CORRUPT;
              return -1;
            }
          mapblock1 = 2;
          if ((bnum = (((__u32 *) DATABLOCK1)
                  [logical_block >> EXT2_ADDR_PER_BLOCK_BITS (SUPERBLOCK)]))
         != mapblock2
         && !ext2_rdfsb (ffi, bnum, DATABLOCK2))
           {
             errnum = ERR_FSYS_CORRUPT;
             return -1;
           }
          mapblock2 = bnum;
          return ((__u32 *) DATABLOCK2)
            [logical_block & (EXT2_ADDR_PER_BLOCK (SUPERBLOCK) - 1)];
        }
      /* else */
      mapblock2 = -1;
      logical_block -= (1 << (EXT2_ADDR_PER_BLOCK_BITS (SUPERBLOCK) * 2));
      if (mapblock1 != 3
          && !ext2_rdfsb (ffi, INODE->i_block[EXT2_TIND_BLOCK], DATABLOCK1))
        {
          errnum = ERR_FSYS_CORRUPT;
          return -1;
        }
      mapblock1 = 3;
      if (!ext2_rdfsb (ffi, ((__u32 *) DATABLOCK1)
                  [logical_block >> (EXT2_ADDR_PER_BLOCK_BITS (SUPERBLOCK)
                                     * 2)],
                  DATABLOCK2))
        {
          errnum = ERR_FSYS_CORRUPT;
          return -1;
        }
      if (!ext2_rdfsb (ffi, ((__u32 *) DATABLOCK2)
                  [(logical_block >> EXT2_ADDR_PER_BLOCK_BITS (SUPERBLOCK))
                   & (EXT2_ADDR_PER_BLOCK (SUPERBLOCK) - 1)],
                  DATABLOCK2))
        {
          errnum = ERR_FSYS_CORRUPT;
          return -1;
        }

      return ((__u32 *) DATABLOCK2)
       [logical_block & (EXT2_ADDR_PER_BLOCK (SUPERBLOCK) - 1)];
    }
    /* inode is in extents format */
    else
    {
      int i;
      struct ext4_extent_header *extent_hdr =
         ext4_recurse_extent_index(ffi, (struct ext4_extent_header *) INODE->i_block, logical_block);
      struct ext4_extent *extent = (struct ext4_extent *) (extent_hdr + 1);
      if ( extent_hdr == NULL || extent_hdr->eh_magic != EXT4_EXT_MAGIC)
        {
          errnum = ERR_FSYS_CORRUPT;
          return -1;
        }
      for (i = 0; i<extent_hdr->eh_entries; i++)
        {
          if (extent[i].ee_block <= logical_block && logical_block < extent[i].ee_block + extent[i].ee_len && !(extent[i].ee_len>>15))
            return (logical_block - extent[i].ee_block + extent[i].ee_start);
        }
      /* We should not arrive here */

      errnum = ERR_FSYS_CORRUPT;
      return -1;
    }
}

/* preconditions: all preconds of ext2fs_block_map */
static int
ext2fs_read (fsi_file_t *ffi, char *buf, int len)
{
  int logical_block;
  int offset;
  int map;
  int ret = 0;
  int size = 0;

#ifdef E2DEBUG
  static char hexdigit[] = "0123456789abcdef";
  unsigned char *i;
  for (i = (unsigned char *) INODE;
       i < ((unsigned char *) INODE + sizeof (struct ext2_inode));
       i++)
    {
      printf ("%c", hexdigit[*i >> 4]);
      printf ("%c", hexdigit[*i % 16]);
      if (!((i + 1 - (unsigned char *) INODE) % 16))
	{
	  printf ("\n");
	}
      else
	{
	  printf (" ");
	}
    }
#endif /* E2DEBUG */
  while (len > 0)
    {
      /* find the (logical) block component of our location */
      logical_block = filepos >> EXT2_BLOCK_SIZE_BITS (SUPERBLOCK);
      offset = filepos & (EXT2_BLOCK_SIZE (SUPERBLOCK) - 1);
      map = ext2fs_block_map (ffi, logical_block);
#ifdef E2DEBUG
      printf ("map=%d\n", map);
#endif /* E2DEBUG */
      if (map < 0)
	break;

      size = EXT2_BLOCK_SIZE (SUPERBLOCK);
      size -= offset;
      if (size > len)
	size = len;

      if (map == 0) {
        memset ((char *) buf, 0, size);
      } else {
        disk_read_func = disk_read_hook;

        devread (ffi, map * (EXT2_BLOCK_SIZE (SUPERBLOCK) / DEV_BSIZE),
	         offset, size, buf);

        disk_read_func = NULL;
      }

      buf += size;
      len -= size;
      filepos += size;
      ret += size;
    }

  if (errnum)
    ret = 0;

  return ret;
}


/* Based on:
   def_blk_fops points to
   blkdev_open, which calls (I think):
   sys_open()
   do_open()
   open_namei()
   dir_namei() which accesses current->fs->root
     fs->root was set during original mount:
     (something)... which calls (I think):
     ext2_read_super()
     iget()
     __iget()
     read_inode()
     ext2_read_inode()
       uses desc_per_block_bits, which is set in ext2_read_super()
       also uses group descriptors loaded during ext2_read_super()
   lookup()
   ext2_lookup()
   ext2_find_entry()
   ext2_getblk()

*/

static inline
int ext2_is_fast_symlink (fsi_file_t *ffi)
{
  int ea_blocks;
  ea_blocks = INODE->i_file_acl ? EXT2_BLOCK_SIZE (SUPERBLOCK) / DEV_BSIZE : 0;
  return INODE->i_blocks == ea_blocks;
}

/* preconditions: ext2fs_mount already executed, therefore supblk in buffer
 *   known as SUPERBLOCK
 * returns: 0 if error, nonzero iff we were able to find the file successfully
 * postconditions: on a nonzero return, buffer known as INODE contains the
 *   inode of the file we were trying to look up
 * side effects: messes up GROUP_DESC buffer area
 */
static int
ext2fs_dir (fsi_file_t *ffi, char *dirname)
{
  int current_ino = EXT2_ROOT_INO;	/* start at the root */
  int updir_ino = current_ino;	/* the parent of the current directory */
  int group_id;			/* which group the inode is in */
  int group_desc;		/* fs pointer to that group */
  int desc;			/* index within that group */
  int ino_blk;			/* fs pointer of the inode's information */
  int str_chk = 0;		/* used to hold the results of a string compare */
  struct ext2_group_desc *gdp;
  struct ext2_inode *raw_inode;	/* inode info corresponding to current_ino */

  char linkbuf[PATH_MAX];	/* buffer for following symbolic links */
  int link_count = 0;

  char *rest;
  char ch;			/* temp char holder */

  int off;			/* offset within block of directory entry (off mod blocksize) */
  int loc;			/* location within a directory */
  int blk;			/* which data blk within dir entry (off div blocksize) */
  int inodes_per_block;		/* number of inodes in each block */
  int inode_offset;		/* inode offset in block */
  long map;			/* fs pointer of a particular block from dir entry */
  struct ext2_dir_entry *dp;	/* pointer to directory entry */
#ifdef E2DEBUG
  unsigned char *i;
#endif	/* E2DEBUG */

  /* loop invariants:
     current_ino = inode to lookup
     dirname = pointer to filename component we are cur looking up within
     the directory known pointed to by current_ino (if any)
   */

  while (1)
    {
#ifdef E2DEBUG
      printf ("inode %d\n", current_ino);
      printf ("dirname=%s\n", dirname);
#endif /* E2DEBUG */

      /* look up an inode */
      group_id = (current_ino - 1) / (SUPERBLOCK->s_inodes_per_group);
      group_desc = group_id >> log2 (EXT2_DESC_PER_BLOCK (SUPERBLOCK));
      desc = group_id & (EXT2_DESC_PER_BLOCK (SUPERBLOCK) - 1);
#ifdef E2DEBUG
      printf ("ipg=%d, dpb=%d\n", SUPERBLOCK->s_inodes_per_group,
	      EXT2_DESC_PER_BLOCK (SUPERBLOCK));
      printf ("group_id=%d group_desc=%d desc=%d\n", group_id, group_desc, desc);
#endif /* E2DEBUG */
      if (!ext2_rdfsb (ffi, 
			(WHICH_SUPER + group_desc + SUPERBLOCK->s_first_data_block),
			(char *)GROUP_DESC))
	{
	  return 0;
	}
      gdp = GROUP_DESC;
      inodes_per_block =  EXT2_BLOCK_SIZE (SUPERBLOCK) / EXT2_INODE_SIZE(SUPERBLOCK);
      inode_offset = ((current_ino - 1) % (SUPERBLOCK->s_inodes_per_group));
      ino_blk = gdp[desc].bg_inode_table + (inode_offset / inodes_per_block);
#ifdef E2DEBUG
      printf ("inode table fsblock=%d\n", ino_blk);
#endif /* E2DEBUG */
      if (!ext2_rdfsb (ffi, ino_blk, (char *)INODE))
	{
	  return 0;
	}

      /* reset indirect blocks! */
      mapblock2 = mapblock1 = -1;

      raw_inode = (struct ext2_inode *)((char *)INODE +
	((current_ino - 1) & (EXT2_INODES_PER_BLOCK (SUPERBLOCK) - 1)) *
	EXT2_INODE_SIZE (SUPERBLOCK));
#ifdef E2DEBUG
      printf ("ipb=%d, sizeof(inode)=%d\n",
	      EXT2_INODES_PER_BLOCK (SUPERBLOCK), EXT2_INODE_SIZE (SUPERBLOCK));
      printf ("inode=%x, raw_inode=%x\n", INODE, raw_inode);
      printf ("offset into inode table block=%d\n", (int) raw_inode - (int) INODE);
      for (i = (unsigned char *) INODE; i <= (unsigned char *) raw_inode;
	   i++)
	{
	  printf ("%c", "0123456789abcdef"[*i >> 4]);
	  printf ("%c", "0123456789abcdef"[*i % 16]);
	  if (!((i + 1 - (unsigned char *) INODE) % 16))
	    {
	      printf ("\n");
	    }
	  else
	    {
	      printf (" ");
	    }
	}
      printf ("first word=%x\n", *((int *) raw_inode));
#endif /* E2DEBUG */

      /* copy inode to fixed location */
      memmove ((void *) INODE, (void *) raw_inode, sizeof (struct ext2_inode));

#ifdef E2DEBUG
      printf ("first word=%x\n", *((int *) INODE));
#endif /* E2DEBUG */

      /* If we've got a symbolic link, then chase it. */
      if (S_ISLNK (INODE->i_mode))
	{
	  int len;
	  if (++link_count > MAX_LINK_COUNT)
	    {
	      errnum = ERR_SYMLINK_LOOP;
	      return 0;
	    }

	  /* Find out how long our remaining name is. */
	  len = 0;
	  while (dirname[len] && !isspace ((uint8_t)dirname[len]))
	    len++;

	  /* Get the symlink size. */
	  filemax = (INODE->i_size);
	  if (filemax + len > sizeof (linkbuf) - 2)
	    {
	      errnum = ERR_FILELENGTH;
	      return 0;
	    }

	  if (len)
	    {
	      /* Copy the remaining name to the end of the symlink data.
	         Note that DIRNAME and LINKBUF may overlap! */
	      memmove (linkbuf + filemax, dirname, len);
	    }
	  linkbuf[filemax + len] = '\0';

	  /* Read the symlink data. */
	  if (! ext2_is_fast_symlink (ffi))
	    {
	      /* Read the necessary blocks, and reset the file pointer. */
	      len = ext2fs_read (ffi, linkbuf, filemax);
	      filepos = 0;
	      if (!len)
		return 0;
	    }
	  else
	    {
	      /* Copy the data directly from the inode. */
	      len = filemax;
	      memmove (linkbuf, (char *) INODE->i_block, len);
	    }

#ifdef E2DEBUG
	  printf ("symlink=%s\n", linkbuf);
#endif

	  dirname = linkbuf;
	  if (*dirname == '/')
	    {
	      /* It's an absolute link, so look it up in root. */
	      current_ino = EXT2_ROOT_INO;
	      updir_ino = current_ino;
	    }
	  else
	    {
	      /* Relative, so look it up in our parent directory. */
	      current_ino = updir_ino;
	    }

	  /* Try again using the new name. */
	  continue;
	}

      /* if end of filename, INODE points to the file's inode */
      if (!*dirname || isspace ((uint8_t)*dirname))
	{
	  if (!S_ISREG (INODE->i_mode))
	    {
	      errnum = ERR_BAD_FILETYPE;
	      return 0;
	    }

	  filemax = (INODE->i_size);
	  return 1;
	}

      /* else we have to traverse a directory */
      updir_ino = current_ino;

      /* skip over slashes */
      while (*dirname == '/')
	dirname++;

      /* if this isn't a directory of sufficient size to hold our file, abort */
      if (!(INODE->i_size) || !S_ISDIR (INODE->i_mode))
	{
	  errnum = ERR_BAD_FILETYPE;
	  return 0;
	}

      /* skip to next slash or end of filename (space) */
      for (rest = dirname; (ch = *rest) && !isspace ((uint8_t)ch) && ch != '/';
	   rest++);

      /* look through this directory and find the next filename component */
      /* invariant: rest points to slash after the next filename component */
      *rest = 0;
      loc = 0;

      do
	{

#ifdef E2DEBUG
	  printf ("dirname=%s, rest=%s, loc=%d\n", dirname, rest, loc);
#endif /* E2DEBUG */

	  /* if our location/byte offset into the directory exceeds the size,
	     give up */
	  if (loc >= INODE->i_size)
	    {
	      if (print_possibilities < 0)
		{
# if 0
		  putchar ('\n');
# endif
		}
	      else
		{
		  errnum = ERR_FILE_NOT_FOUND;
		  *rest = ch;
		}
	      return (print_possibilities < 0);
	    }

	  /* else, find the (logical) block component of our location */
	  blk = loc >> EXT2_BLOCK_SIZE_BITS (SUPERBLOCK);

	  /* we know which logical block of the directory entry we are looking
	     for, now we have to translate that to the physical (fs) block on
	     the disk */
	  map = ext2fs_block_map (ffi, blk);
#ifdef E2DEBUG
	  printf ("fs block=%d\n", map);
#endif /* E2DEBUG */
	  mapblock2 = -1;
	  if ((map < 0) || !ext2_rdfsb (ffi, map, DATABLOCK2))
	    {
	      errnum = ERR_FSYS_CORRUPT;
	      *rest = ch;
	      return 0;
	    }
	  off = loc & (EXT2_BLOCK_SIZE (SUPERBLOCK) - 1);
	  dp = (struct ext2_dir_entry *) (DATABLOCK2 + off);
	  /* advance loc prematurely to next on-disk directory entry  */
	  loc += dp->rec_len;

	  /* NOTE: ext2fs filenames are NOT null-terminated */

#ifdef E2DEBUG
	  printf ("directory entry ino=%d\n", dp->inode);
	  if (dp->inode)
	    printf ("entry=%s\n", dp->name);
#endif /* E2DEBUG */

	  if (dp->inode)
	    {
	      int saved_c = dp->name[dp->name_len];

	      dp->name[dp->name_len] = 0;
	      str_chk = substring (dirname, dp->name);

# ifndef STAGE1_5
	      if (print_possibilities && ch != '/'
		  && (!*dirname || str_chk <= 0))
		{
		  if (print_possibilities > 0)
		    print_possibilities = -print_possibilities;
		  print_a_completion (dp->name);
		}
# endif

	      dp->name[dp->name_len] = saved_c;
	    }

	}
      while (!dp->inode || (str_chk || (print_possibilities && ch != '/')));

      current_ino = dp->inode;
      *(dirname = rest) = ch;
    }
  /* never get here */
}

fsi_plugin_ops_t *
fsi_init_plugin(int version, fsi_plugin_t *fp, const char **name)
{
	static fsig_plugin_ops_t ops = {
		FSIMAGE_PLUGIN_VERSION,
		.fpo_mount = ext2fs_mount,
		.fpo_dir = ext2fs_dir,
		.fpo_read = ext2fs_read
	};

	*name = "ext2fs";
	return (fsig_init(fp, &ops));
}
