/*
 * QEMU System Emulator block driver
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef BLOCK_INT_H
#define BLOCK_INT_H

#include "block.h"
#include "qemu-option.h"
#include "qemu-queue.h"
#include "qemu-coroutine.h"
#include "qemu-timer.h"
#include "qapi-types.h"

#define BLOCK_FLAG_ENCRYPT	1
#define BLOCK_FLAG_COMPAT6	4

#define BLOCK_OPT_SIZE          "size"
#define BLOCK_OPT_ENCRYPT       "encryption"
#define BLOCK_OPT_COMPAT6       "compat6"
#define BLOCK_OPT_BACKING_FILE  "backing_file"
#define BLOCK_OPT_BACKING_FMT   "backing_fmt"
#define BLOCK_OPT_CLUSTER_SIZE  "cluster_size"
#define BLOCK_OPT_TABLE_SIZE    "table_size"
#define BLOCK_OPT_PREALLOC      "preallocation"
#define BLOCK_OPT_SUBFMT        "subformat"

typedef struct AIOPool {
    void (*cancel)(BlockDriverAIOCB *acb);
    int aiocb_size;
    BlockDriverAIOCB *free_aiocb;
} AIOPool;

struct BlockDriver {
    const char *format_name;
    int instance_size;
    int (*bdrv_probe)(const uint8_t *buf, int buf_size, const char *filename);
    int (*bdrv_probe_device)(const char *filename);
    int (*bdrv_open)(BlockDriverState *bs, int flags);
    int (*bdrv_file_open)(BlockDriverState *bs, const char *filename, int flags);
    int (*bdrv_read)(BlockDriverState *bs, int64_t sector_num,
                     uint8_t *buf, int nb_sectors);
    int (*bdrv_write)(BlockDriverState *bs, int64_t sector_num,
                      const uint8_t *buf, int nb_sectors);
    void (*bdrv_close)(BlockDriverState *bs);
    int (*bdrv_create)(const char *filename, QEMUOptionParameter *options);
    int (*bdrv_is_allocated)(BlockDriverState *bs, int64_t sector_num,
                             int nb_sectors, int *pnum);
    int (*bdrv_set_key)(BlockDriverState *bs, const char *key);
    int (*bdrv_make_empty)(BlockDriverState *bs);
    /* aio */
    BlockDriverAIOCB *(*bdrv_aio_readv)(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque);
    BlockDriverAIOCB *(*bdrv_aio_writev)(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque);
    BlockDriverAIOCB *(*bdrv_aio_flush)(BlockDriverState *bs,
        BlockDriverCompletionFunc *cb, void *opaque);
    BlockDriverAIOCB *(*bdrv_aio_discard)(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque);

    int coroutine_fn (*bdrv_co_readv)(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, QEMUIOVector *qiov);
    int coroutine_fn (*bdrv_co_writev)(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors, QEMUIOVector *qiov);
    int coroutine_fn (*bdrv_co_discard)(BlockDriverState *bs,
        int64_t sector_num, int nb_sectors);

    /*
     * Invalidate any cached meta-data.
     */
    void (*bdrv_invalidate_cache)(BlockDriverState *bs);

    /*
     * Flushes all data that was already written to the OS all the way down to
     * the disk (for example raw-posix calls fsync()).
     */
    int coroutine_fn (*bdrv_co_flush_to_disk)(BlockDriverState *bs);

    /*
     * Flushes all internal caches to the OS. The data may still sit in a
     * writeback cache of the host OS, but it will survive a crash of the qemu
     * process.
     */
    int coroutine_fn (*bdrv_co_flush_to_os)(BlockDriverState *bs);

    int (*bdrv_aio_multiwrite)(BlockDriverState *bs, BlockRequest *reqs,
        int num_reqs);
    int (*bdrv_merge_requests)(BlockDriverState *bs, BlockRequest* a,
        BlockRequest *b);


    const char *protocol_name;
    int (*bdrv_truncate)(BlockDriverState *bs, int64_t offset);
    int64_t (*bdrv_getlength)(BlockDriverState *bs);
    int64_t (*bdrv_get_allocated_file_size)(BlockDriverState *bs);
    int (*bdrv_write_compressed)(BlockDriverState *bs, int64_t sector_num,
                                 const uint8_t *buf, int nb_sectors);

    int (*bdrv_snapshot_create)(BlockDriverState *bs,
                                QEMUSnapshotInfo *sn_info);
    int (*bdrv_snapshot_goto)(BlockDriverState *bs,
                              const char *snapshot_id);
    int (*bdrv_snapshot_delete)(BlockDriverState *bs, const char *snapshot_id);
    int (*bdrv_snapshot_list)(BlockDriverState *bs,
                              QEMUSnapshotInfo **psn_info);
    int (*bdrv_snapshot_load_tmp)(BlockDriverState *bs,
                                  const char *snapshot_name);
    int (*bdrv_get_info)(BlockDriverState *bs, BlockDriverInfo *bdi);

    int (*bdrv_save_vmstate)(BlockDriverState *bs, const uint8_t *buf,
                             int64_t pos, int size);
    int (*bdrv_load_vmstate)(BlockDriverState *bs, uint8_t *buf,
                             int64_t pos, int size);

    int (*bdrv_change_backing_file)(BlockDriverState *bs,
        const char *backing_file, const char *backing_fmt);

    /* removable device specific */
    int (*bdrv_is_inserted)(BlockDriverState *bs);
    int (*bdrv_media_changed)(BlockDriverState *bs);
    void (*bdrv_eject)(BlockDriverState *bs, int eject_flag);
    void (*bdrv_lock_medium)(BlockDriverState *bs, bool locked);

    /* to control generic scsi devices */
    int (*bdrv_ioctl)(BlockDriverState *bs, unsigned long int req, void *buf);
    BlockDriverAIOCB *(*bdrv_aio_ioctl)(BlockDriverState *bs,
        unsigned long int req, void *buf,
        BlockDriverCompletionFunc *cb, void *opaque);

    /* List of options for creating images, terminated by name == NULL */
    QEMUOptionParameter *create_options;


    /*
     * Returns 0 for completed check, -errno for internal errors.
     * The check results are stored in result.
     */
    int (*bdrv_check)(BlockDriverState* bs, BdrvCheckResult *result);

    void (*bdrv_debug_event)(BlockDriverState *bs, BlkDebugEvent event);

    /*
     * Returns 1 if newly created images are guaranteed to contain only
     * zeros, 0 otherwise.
     */
    int (*bdrv_has_zero_init)(BlockDriverState *bs);

    QLIST_ENTRY(BlockDriver) list;
};

struct BlockDriverState {
    int64_t total_sectors; /* if we are reading a disk image, give its
                              size in sectors */
    int read_only; /* if true, the media is read only */
    int keep_read_only; /* if true, the media was requested to stay read only */
    int open_flags; /* flags used to open the file, re-used for re-open */
    int encrypted; /* if true, the media is encrypted */
    int valid_key; /* if true, a valid encryption key has been set */
    int sg;        /* if true, the device is a /dev/sg* */

    BlockDriver *drv; /* NULL means no media */
    void *opaque;

    void *dev;                  /* attached device model, if any */
    /* TODO change to DeviceState when all users are qdevified */
    const BlockDevOps *dev_ops;
    void *dev_opaque;

    char filename[1024];
    char backing_file[1024]; /* if non zero, the image is a diff of
                                this file image */
    char backing_format[16]; /* if non-zero and backing_file exists */
    int is_temporary;

    BlockDriverState *backing_hd;
    BlockDriverState *file;

    /* async read/write emulation */

    void *sync_aiocb;

    /* I/O stats (display with "info blockstats"). */
    uint64_t nr_bytes[BDRV_MAX_IOTYPE];
    uint64_t nr_ops[BDRV_MAX_IOTYPE];
    uint64_t total_time_ns[BDRV_MAX_IOTYPE];
    uint64_t wr_highest_sector;

    /* Whether the disk can expand beyond total_sectors */
    int growable;

    /* the memory alignment required for the buffers handled by this driver */
    int buffer_alignment;

    /* do we need to tell the quest if we have a volatile write cache? */
    int enable_write_cache;

    /* NOTE: the following infos are only hints for real hardware
       drivers. They are not used by the block driver */
    int cyls, heads, secs, translation;
    BlockErrorAction on_read_error, on_write_error;
    bool iostatus_enabled;
    BlockDeviceIoStatus iostatus;
    char device_name[32];
    unsigned long *dirty_bitmap;
    int64_t dirty_count;
    int in_use; /* users other than guest access, eg. block migration */
    QTAILQ_ENTRY(BlockDriverState) list;
    void *private;
};

struct BlockDriverAIOCB {
    AIOPool *pool;
    BlockDriverState *bs;
    BlockDriverCompletionFunc *cb;
    void *opaque;
    BlockDriverAIOCB *next;
};

void get_tmp_filename(char *filename, int size);

void *qemu_aio_get(AIOPool *pool, BlockDriverState *bs,
                   BlockDriverCompletionFunc *cb, void *opaque);
void qemu_aio_release(void *p);

#ifdef _WIN32
int is_windows_drive(const char *filename);
#endif

#endif /* BLOCK_INT_H */
