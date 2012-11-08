#ifndef QEMU_HW_SCSI_H
#define QEMU_HW_SCSI_H

#include "qdev.h"
#include "block.h"
#include "sysemu.h"

#define MAX_SCSI_DEVS	255

#define SCSI_CMD_BUF_SIZE     16

typedef struct SCSIBus SCSIBus;
typedef struct SCSIBusInfo SCSIBusInfo;
typedef struct SCSICommand SCSICommand;
typedef struct SCSIDevice SCSIDevice;
typedef struct SCSIDeviceInfo SCSIDeviceInfo;
typedef struct SCSIRequest SCSIRequest;
typedef struct SCSIReqOps SCSIReqOps;

enum SCSIXferMode {
    SCSI_XFER_NONE,      /*  TEST_UNIT_READY, ...            */
    SCSI_XFER_FROM_DEV,  /*  READ, INQUIRY, MODE_SENSE, ...  */
    SCSI_XFER_TO_DEV,    /*  WRITE, MODE_SELECT, ...         */
};

typedef struct SCSISense {
    uint8_t key;
    uint8_t asc;
    uint8_t ascq;
} SCSISense;

#define SCSI_SENSE_BUF_SIZE 96

struct SCSICommand {
    uint8_t buf[SCSI_CMD_BUF_SIZE];
    int len;
    size_t xfer;
    uint64_t lba;
    enum SCSIXferMode mode;
};

struct SCSIRequest {
    SCSIBus           *bus;
    SCSIDevice        *dev;
    const SCSIReqOps  *ops;
    uint32_t          refcount;
    uint32_t          tag;
    uint32_t          lun;
    uint32_t          status;
    SCSICommand       cmd;
    BlockDriverAIOCB  *aiocb;
    uint8_t sense[SCSI_SENSE_BUF_SIZE];
    uint32_t sense_len;
    bool enqueued;
    bool io_canceled;
    bool retry;
    void *hba_private;
    QTAILQ_ENTRY(SCSIRequest) next;
};

struct SCSIDevice
{
    DeviceState qdev;
    VMChangeStateEntry *vmsentry;
    QEMUBH *bh;
    uint32_t id;
    BlockConf conf;
    SCSIDeviceInfo *info;
    SCSISense unit_attention;
    bool sense_is_ua;
    uint8_t sense[SCSI_SENSE_BUF_SIZE];
    uint32_t sense_len;
    QTAILQ_HEAD(, SCSIRequest) requests;
    uint32_t channel;
    uint32_t lun;
    int blocksize;
    int type;
    uint64_t max_lba;
};

/* cdrom.c */
int cdrom_read_toc(int nb_sectors, uint8_t *buf, int msf, int start_track);
int cdrom_read_toc_raw(int nb_sectors, uint8_t *buf, int msf, int session_num);

/* scsi-bus.c */
struct SCSIReqOps {
    size_t size;
    void (*free_req)(SCSIRequest *req);
    int32_t (*send_command)(SCSIRequest *req, uint8_t *buf);
    void (*read_data)(SCSIRequest *req);
    void (*write_data)(SCSIRequest *req);
    void (*cancel_io)(SCSIRequest *req);
    uint8_t *(*get_buf)(SCSIRequest *req);
};

typedef int (*scsi_qdev_initfn)(SCSIDevice *dev);
struct SCSIDeviceInfo {
    DeviceInfo qdev;
    scsi_qdev_initfn init;
    void (*destroy)(SCSIDevice *s);
    SCSIRequest *(*alloc_req)(SCSIDevice *s, uint32_t tag, uint32_t lun,
                              uint8_t *buf, void *hba_private);
    void (*unit_attention_reported)(SCSIDevice *s);
};

struct SCSIBusInfo {
    int tcq;
    int max_channel, max_target, max_lun;
    void (*transfer_data)(SCSIRequest *req, uint32_t arg);
    void (*complete)(SCSIRequest *req, uint32_t arg);
    void (*cancel)(SCSIRequest *req);
};

struct SCSIBus {
    BusState qbus;
    int busnr;

    SCSISense unit_attention;
    const SCSIBusInfo *info;
};

void scsi_bus_new(SCSIBus *bus, DeviceState *host, const SCSIBusInfo *info);
void scsi_qdev_register(SCSIDeviceInfo *info);

static inline SCSIBus *scsi_bus_from_device(SCSIDevice *d)
{
    return DO_UPCAST(SCSIBus, qbus, d->qdev.parent_bus);
}

SCSIDevice *scsi_bus_legacy_add_drive(SCSIBus *bus, BlockDriverState *bdrv,
                                      int unit, bool removable, int bootindex);
int scsi_bus_legacy_handle_cmdline(SCSIBus *bus);

/*
 * Predefined sense codes
 */

/* No sense data available */
extern const struct SCSISense sense_code_NO_SENSE;
/* LUN not ready, Manual intervention required */
extern const struct SCSISense sense_code_LUN_NOT_READY;
/* LUN not ready, Medium not present */
extern const struct SCSISense sense_code_NO_MEDIUM;
/* LUN not ready, medium removal prevented */
extern const struct SCSISense sense_code_NOT_READY_REMOVAL_PREVENTED;
/* Hardware error, internal target failure */
extern const struct SCSISense sense_code_TARGET_FAILURE;
/* Illegal request, invalid command operation code */
extern const struct SCSISense sense_code_INVALID_OPCODE;
/* Illegal request, LBA out of range */
extern const struct SCSISense sense_code_LBA_OUT_OF_RANGE;
/* Illegal request, Invalid field in CDB */
extern const struct SCSISense sense_code_INVALID_FIELD;
/* Illegal request, LUN not supported */
extern const struct SCSISense sense_code_LUN_NOT_SUPPORTED;
/* Illegal request, Saving parameters not supported */
extern const struct SCSISense sense_code_SAVING_PARAMS_NOT_SUPPORTED;
/* Illegal request, Incompatible format */
extern const struct SCSISense sense_code_INCOMPATIBLE_FORMAT;
/* Illegal request, medium removal prevented */
extern const struct SCSISense sense_code_ILLEGAL_REQ_REMOVAL_PREVENTED;
/* Command aborted, I/O process terminated */
extern const struct SCSISense sense_code_IO_ERROR;
/* Command aborted, I_T Nexus loss occurred */
extern const struct SCSISense sense_code_I_T_NEXUS_LOSS;
/* Command aborted, Logical Unit failure */
extern const struct SCSISense sense_code_LUN_FAILURE;
/* LUN not ready, Medium not present */
extern const struct SCSISense sense_code_UNIT_ATTENTION_NO_MEDIUM;
/* Unit attention, Power on, reset or bus device reset occurred */
extern const struct SCSISense sense_code_RESET;
/* Unit attention, Medium may have changed*/
extern const struct SCSISense sense_code_MEDIUM_CHANGED;
/* Unit attention, Reported LUNs data has changed */
extern const struct SCSISense sense_code_REPORTED_LUNS_CHANGED;
/* Unit attention, Device internal reset */
extern const struct SCSISense sense_code_DEVICE_INTERNAL_RESET;

#define SENSE_CODE(x) sense_code_ ## x

int scsi_sense_valid(SCSISense sense);
int scsi_build_sense(uint8_t *in_buf, int in_len,
                     uint8_t *buf, int len, bool fixed);

SCSIRequest *scsi_req_alloc(const SCSIReqOps *reqops, SCSIDevice *d,
                            uint32_t tag, uint32_t lun, void *hba_private);
SCSIRequest *scsi_req_new(SCSIDevice *d, uint32_t tag, uint32_t lun,
                          uint8_t *buf, void *hba_private);
int32_t scsi_req_enqueue(SCSIRequest *req);
void scsi_req_free(SCSIRequest *req);
SCSIRequest *scsi_req_ref(SCSIRequest *req);
void scsi_req_unref(SCSIRequest *req);

void scsi_req_build_sense(SCSIRequest *req, SCSISense sense);
void scsi_req_print(SCSIRequest *req);
void scsi_req_continue(SCSIRequest *req);
void scsi_req_data(SCSIRequest *req, int len);
void scsi_req_complete(SCSIRequest *req, int status);
uint8_t *scsi_req_get_buf(SCSIRequest *req);
int scsi_req_get_sense(SCSIRequest *req, uint8_t *buf, int len);
void scsi_req_abort(SCSIRequest *req, int status);
void scsi_req_cancel(SCSIRequest *req);
void scsi_req_retry(SCSIRequest *req);
void scsi_device_purge_requests(SCSIDevice *sdev, SCSISense sense);
int scsi_device_get_sense(SCSIDevice *dev, uint8_t *buf, int len, bool fixed);
SCSIDevice *scsi_device_find(SCSIBus *bus, int channel, int target, int lun);

/* scsi-generic.c. */
extern const SCSIReqOps scsi_generic_req_ops;

#endif
