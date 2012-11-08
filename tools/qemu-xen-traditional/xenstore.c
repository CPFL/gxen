/*
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2006 Christian Limpach
 * Copyright (C) 2006 XenSource Ltd.
 *
 */

#include "qemu-common.h"
#include "qemu-char.h"

#include "block_int.h"
#include <unistd.h>
#include <assert.h>

#include "exec-all.h"
#include "sysemu.h"

#include "hw.h"
#include "pci.h"
#include "qemu-timer.h"
#include "qemu-xen.h"

struct xs_handle *xsh = NULL;
static char *media_filename[MAX_DRIVES+1];
static QEMUTimer *insert_timer = NULL;
static char *xenbus_param_paths[MAX_DRIVES+1];

int xenstore_find_device(BlockDriverState *bs)
{
    int i;

    for (i = 0; i < MAX_DRIVES + 1; i++) {
        if (drives_table[i].bdrv == bs)
            return i;
    }
    return -1;
}

void xenstore_do_eject(BlockDriverState *bs)
{
    int i;

    i = xenstore_find_device(bs);
    if (i == -1) {
        fprintf(stderr, "couldn't find disk to eject.\n");
        return;
    }
    if (xenbus_param_paths[i])
        xs_write(xsh, XBT_NULL, xenbus_param_paths[i], "eject", strlen("eject"));
}

#define UWAIT_MAX (30*1000000) /* thirty seconds */
#define UWAIT     (100000)     /* 1/10th second  */

struct xenstore_watch_cb_t
{
    char                *path;
    xenstore_callback   cb;
    void                *opaque;
};

static struct xenstore_watch_cb_t *xenstore_watch_callbacks = NULL;

int xenstore_watch_new_callback(const char          *path,
                                xenstore_callback   fptr,
                                void                *opaque)
{
    int         i = 0, ret = 0;

    ret = xs_watch(xsh, path, path);
    if (ret == 0)
        return 0;

    if (!xenstore_watch_callbacks)
    {
        xenstore_watch_callbacks = malloc(sizeof (struct xenstore_watch_cb_t));
        xenstore_watch_callbacks[0].path = NULL;
    }

    while (xenstore_watch_callbacks[i].path)
    {
	if (!strcmp(xenstore_watch_callbacks[i].path, path))
	{
	    xenstore_watch_callbacks[i].cb = fptr;
	    xenstore_watch_callbacks[i].opaque = opaque;
	    return ret;
	}
        i++;
    }

    xenstore_watch_callbacks = realloc(xenstore_watch_callbacks,
                                       (i + 2) * sizeof (struct xenstore_watch_cb_t));
    xenstore_watch_callbacks[i].path = strdup(path);
    xenstore_watch_callbacks[i].cb = fptr;
    xenstore_watch_callbacks[i].opaque = opaque;
    xenstore_watch_callbacks[i + 1].path = NULL;
    return ret;
}


static int pasprintf(char **buf, const char *fmt, ...)
{
    va_list ap;
    int ret = 0;

    if (*buf)
        free(*buf);
    va_start(ap, fmt);
    if (vasprintf(buf, fmt, ap) == -1) {
        buf = NULL;
        ret = -1;
    }
    va_end(ap);
    return ret;
}

static void insert_media(void *opaque)
{
    int i;
    BlockDriverState *bs;

    for (i = 0; i < MAX_DRIVES + 1; i++) {
        bs = drives_table[i].bdrv;
        if (media_filename[i] && bs && bs->filename[0] == '\0') {
            BlockDriver *format;
            if ( strstart(media_filename[i], "/dev/cd", NULL) 
              || strstart(media_filename[i], "/dev/scd", NULL)) 
                format = &bdrv_host_device;
            else 
                format = &bdrv_raw;

            bdrv_open2(bs, media_filename[i], 0, format);
#ifdef CONFIG_STUBDOM
            {
                char *buf, *backend, *params_path, *params;
                unsigned int len;
                asprintf(&buf, "%s/backend", media_filename[i]);
                backend = xs_read(xsh, XBT_NULL, buf, &len);
                asprintf(&params_path, "%s/params", backend);
                params = xs_read(xsh, XBT_NULL, params_path, &len);
                pstrcpy(bs->filename, sizeof(bs->filename), params);
                free(buf);
                free(backend);
                free(params_path);
                free(params);
            }
#else
            pstrcpy(bs->filename, sizeof(bs->filename), media_filename[i]);
#endif
            free(media_filename[i]);
            media_filename[i] = NULL;
        }
    }
}

void xenstore_check_new_media_present(int timeout)
{

    if (insert_timer == NULL)
        insert_timer = qemu_new_timer(rt_clock, insert_media, NULL);
    qemu_mod_timer(insert_timer, qemu_get_clock(rt_clock) + timeout);
}

static void waitForDevice(char *fn)
{ 
    struct stat sbuf;
    int status;
    int uwait = UWAIT_MAX;

    do {
        status = stat(fn, &sbuf);
        if (!status) break;
        usleep(UWAIT);
        uwait -= UWAIT;
    } while (uwait > 0);

    return;
}

static int any_hdN;

static int parse_drive_name(const char *dev, DriveInfo *out) {
    /* alway sleaves out->bdrv unchanged */
    /* on success, returns 0 and fills in out->type, ->bus, ->unit */
    /* if drive name not understood, returns -1 and *out may be garbage */
    int ch, max, per_bus;

    /* Change xvdN to look like hdN */
    if (!any_hdN && !strncmp(dev, "xvd", 3) && strlen(dev) == 4) {
        ch = dev[3];
        fprintf(logfile, "Using %s for guest's hd%c\n", dev, ch);
        out->type = IF_IDE;
    } else if (!strncmp(dev, "hd", 2) && strlen(dev) == 3) {
        ch = dev[2];
        out->type = IF_IDE;
    } else if (!strncmp(dev, "sd", 2) && strlen(dev) == 3) {
        ch = dev[2];
        out->type = IF_SCSI;
    } else {
        fprintf(stderr, "qemu: ignoring not-understood drive `%s'\n", dev);
        return -1;
    }

    if (out->type == IF_SCSI) {
        max = MAX_SCSI_DEVS;
        per_bus = max;
    } else {
        max = 4;
        per_bus = 2;
    }

    ch = ch - 'a';
    if (ch >= max) {
        fprintf(stderr, "qemu: drive `%s' out of range\n", dev);
        return -1;
    }

    out->bus = ch / per_bus;
    out->unit = ch % per_bus;
    
    return 0;
}       

static int drive_name_to_index(const char *name) {
    DriveInfo tmp;
    int ret;

    ret = parse_drive_name(name, &tmp);  if (ret) return -1;
    ret = drive_get_index(tmp.type, tmp.bus, tmp.unit);
    return ret;
}

static void xenstore_get_backend_path(char **backend, const char *devtype,
				      const char *frontend_dompath,
				      int frontend_domid,
				      const char *inst_danger) {
    /* On entry: *backend will be passed to free()
     * On succcess: *backend will be from malloc
     * On failure: *backend==0
     */
    char *bpath=0;
    char *frontend_path=0;
    char *backend_dompath=0;
    char *expected_backend=0;
    char *frontend_backend_path=0;
    char *backend_frontend_path=0;
    char *frontend_doublecheck=0;
    int len;
    const char *frontend_idpath_slash;

    /* clear out return value for if we error out */
    free(*backend);
    *backend = 0;

    if (strchr(inst_danger,'/')) {
        fprintf(logfile, "xenstore_get_backend_path inst_danger has slash"
                " which is forbidden (devtype %s)\n", devtype);
	goto out;
    }

    if (pasprintf(&frontend_path, "%s/device/%s/%s",
                  frontend_dompath, devtype, inst_danger)
        == -1) goto out;

    if (pasprintf(&frontend_backend_path, "%s/backend",
                  frontend_path)
        == -1) goto out;

    bpath = xs_read(xsh, XBT_NULL, frontend_backend_path, &len);

    /* now we must check that the backend is intended for use
     * by this frontend, since the frontend's /backend xenstore node
     * is writeable by the untrustworthy guest. */

    backend_dompath = xs_get_domain_path(xsh, domid_backend);
    if (!backend_dompath) goto out;
    
    const char *expected_devtypes[4];
    const char **expected_devtype = expected_devtypes;

    *expected_devtype++ = devtype;
    if (!strcmp(devtype, "vbd")) {
	*expected_devtype++ = "tap";
	*expected_devtype++ = "qdisk";
    }
    *expected_devtype = 0;
    assert(expected_devtype <
           expected_devtypes + ARRAY_SIZE(expected_devtypes));

    for (expected_devtype = expected_devtypes;
         *expected_devtype;
         expected_devtype++) {
    
        if (pasprintf(&expected_backend, "%s/backend/%s/%lu/%s",
                      backend_dompath, *expected_devtype,
                      frontend_domid, inst_danger)
            == -1) goto out;

        if (!strcmp(bpath, expected_backend))
            goto found;
    }

    fprintf(stderr, "frontend `%s' devtype `%s' expected backend `%s'"
            " got `%s', ignoring\n",
            frontend_path, devtype, expected_backend, bpath);
    errno = EINVAL;
    goto out;

 found:

    if (pasprintf(&backend_frontend_path, "%s/frontend", bpath)
        == -1) goto out;

    frontend_doublecheck = xs_read(xsh, XBT_NULL, backend_frontend_path, &len);

    if (strcmp(frontend_doublecheck, frontend_path)) {
        fprintf(stderr, "frontend `%s' backend `%s' points to other frontend"
                " `%s', ignoring\n", frontend_path, bpath, frontend_doublecheck);
        errno = EINVAL;
        goto out;
    }

    /* steal bpath */
    *backend = bpath;
    bpath = 0;

 out:
    free(bpath);
    free(frontend_path);
    free(backend_dompath);
    free(expected_backend);
    free(frontend_backend_path);
    free(backend_frontend_path);
    free(frontend_doublecheck);
}

static const char *xenstore_get_guest_uuid(void)
{
    static char *already_computed = NULL;

    char *domain_path = NULL, *vm_path = NULL, *vm_value = NULL, *p = NULL;
    unsigned int len;

    if (already_computed)
        return already_computed;

    if (xsh == NULL)
        return NULL;

    domain_path = xs_get_domain_path(xsh, domid);
    if (domain_path == NULL) {
        fprintf(logfile, "xs_get_domain_path() error. domid %d.\n", domid);
        goto out;
    }

    if (pasprintf(&vm_path, "%s/vm", domain_path) == -1) {
        fprintf(logfile, "xenstore_get_guest_uuid(): out of memory.\n");
        goto out;
    }
    vm_value = xs_read(xsh, XBT_NULL, vm_path, &len);
    if (vm_value == NULL) {
        fprintf(logfile, "xs_read(): uuid get error. %s.\n", vm_path);
        goto out;
    }

    if (strtok(vm_value, "/") == NULL) {
        fprintf(logfile, "failed to parse guest uuid\n");
        goto out;
    }
    p = strtok(NULL, "/");
    if (p == NULL) {
        fprintf(logfile, "failed to parse guest uuid\n");
        goto out;
    }

    if (pasprintf(&already_computed, "%s", p) == -1) {
        fprintf(logfile, "xenstore_get_guest_uuid(): out of memory.\n");
        goto out;
    }

    fprintf(logfile, "Guest uuid = %s\n", already_computed);

 out:
    free(domain_path);
    free(vm_path);
    free(vm_value);

    return already_computed;
}

uint32_t xenstore_read_target(void)
{
    char *domain_path = NULL, *target_path = NULL, *target_value = NULL, *p = NULL;
    unsigned int len;
    uint32_t target_domid = 0;

    if (xsh == NULL)
        return 0;

    domain_path = xs_get_domain_path(xsh, domid);
    if (domain_path == NULL) {
        fprintf(logfile, "xs_get_domain_path() error. domid %d.\n", domid);
        goto out;
    }

    if (pasprintf(&target_path, "%s/target", domain_path) == -1) {
        fprintf(logfile, "xenstore_get_guest_uuid(): out of memory.\n");
        goto out;
    }
    target_value = xs_read(xsh, XBT_NULL, target_path, &len);
    if (target_value == NULL) {
        fprintf(logfile, "xs_read(): target get error. %s.\n", target_path);
        goto out;
    }

    fprintf(logfile, "target = %s\n", target_value);
    target_domid = strtoul(target_value, NULL, 10);

 out:
    free(domain_path);
    free(target_path);
    free(target_value);

    return target_domid;
}

#define PT_PCI_MSITRANSLATE_DEFAULT 0
#define PT_PCI_POWER_MANAGEMENT_DEFAULT 0
int direct_pci_msitranslate;
int direct_pci_power_mgmt;
void xenstore_init(void)
{
    xenstore_get_guest_uuid();

    xsh = xs_daemon_open();
    if (xsh == NULL) {
        fprintf(logfile, "Could not contact xenstore for domain config\n");
        return;
    }
}

void xenstore_parse_domain_config(int hvm_domid)
{
    char **e_danger = NULL;
    char *buf = NULL;
    char *fpath = NULL, *bpath = NULL,
        *dev = NULL, *params = NULL, *drv = NULL;
    int i, ret;
    unsigned int len, num, hd_index, pci_devid = 0;
    BlockDriverState *bs;
    BlockDriver *format;

    /* Read-only handling for image files */
    char *mode = NULL;
    int flags;
    int is_readonly;

    /* paths controlled by untrustworthy guest, and values read from them */
    char *danger_path;
    char *danger_buf = NULL;
    char *danger_type = NULL;

    for(i = 0; i < MAX_DRIVES + 1; i++)
        media_filename[i] = NULL;

    danger_path = xs_get_domain_path(xsh, hvm_domid);
    if (danger_path == NULL) {
        fprintf(logfile, "xs_get_domain_path() error\n");
        goto out;
    }

    if (pasprintf(&danger_buf, "%s/device/vbd", danger_path) == -1)
        goto out;

    e_danger = xs_directory(xsh, XBT_NULL, danger_buf, &num);
    if (e_danger == NULL)
        num = 0;

    for (i = 0; i < num; i++) {
        /* read the backend path */
        xenstore_get_backend_path(&bpath, "vbd", danger_path, hvm_domid,
				  e_danger[i]);
        if (bpath == NULL)
            continue;    
        /* read the name of the device */
        if (pasprintf(&buf, "%s/dev", bpath) == -1)
            continue;
        free(dev);
        dev = xs_read(xsh, XBT_NULL, buf, &len);
        if (dev == NULL)
            continue;
        if (!strncmp(dev, "hd", 2)) {
            any_hdN = 1;
            break;
        }
    }
        
    for (i = 0; i < num; i++) {
	format = NULL; /* don't know what the format is yet */
        /* read the backend path */
        xenstore_get_backend_path(&bpath, "vbd", danger_path, hvm_domid, e_danger[i]);
        if (bpath == NULL)
            continue;
        /* read the name of the device */
        if (pasprintf(&buf, "%s/dev", bpath) == -1)
            continue;
        free(dev);
        dev = xs_read(xsh, XBT_NULL, buf, &len);
        if (dev == NULL)
            continue;
	if (nb_drives >= MAX_DRIVES) {
	    fprintf(stderr, "qemu: too many drives, skipping `%s'\n", dev);
	    continue;
	}
	ret = parse_drive_name(dev, &drives_table[nb_drives]);
	if (ret)
	    continue;
        /* read the type of the device */
        if (pasprintf(&danger_buf, "%s/device/vbd/%s/device-type",
                      danger_path, e_danger[i]) == -1)
            continue;
        free(danger_type);
        danger_type = xs_read(xsh, XBT_NULL, danger_buf, &len);
        if (pasprintf(&buf, "%s/params", bpath) == -1)
            continue;
        free(params);
        params = xs_read(xsh, XBT_NULL, buf, &len);
        if (params == NULL)
            continue;
        /* read the name of the device */
        if (pasprintf(&buf, "%s/type", bpath) == -1)
            continue;
        free(drv);
        drv = xs_read(xsh, XBT_NULL, buf, &len);
        if (drv == NULL)
            continue;
        /* Obtain blktap sub-type prefix */
        if ((!strcmp(drv, "tap") || !strcmp(drv, "qdisk")) && params[0]) {
            char *offset = strchr(params, ':'); 
            if (!offset)
                continue ;
	    free(drv);
	    drv = malloc(offset - params + 1);
	    memcpy(drv, params, offset - params);
	    drv[offset - params] = '\0';
	    if (!strcmp(drv, "aio"))
		/* qemu does aio anyway if it can */
		format = &bdrv_raw;
            memmove(params, offset+1, strlen(offset+1)+1 );
            fprintf(logfile, "Strip off blktap sub-type prefix to %s (drv '%s')\n", params, drv); 
        }
        /* Prefix with /dev/ if needed */
        if (!strcmp(drv, "phy") && params[0] != '/') {
            char *newparams = malloc(5 + strlen(params) + 1);
            sprintf(newparams, "/dev/%s", params);
            free(params);
            params = newparams;
	    format = &bdrv_raw;
        }

#if 0
	/* Phantom VBDs are disabled because the use of paths
	 * from guest-controlled areas in xenstore is unsafe.
	 * Hopefully if they are really needed for something
	 * someone will shout and then we will find out what for.
	 */
        /* 
         * check if device has a phantom vbd; the phantom is hooked
         * to the frontend device (for ease of cleanup), so lookup 
         * the frontend device, and see if there is a phantom_vbd
         * if there is, we will use resolution as the filename
         */
        if (pasprintf(&danger_buf, "%s/device/vbd/%s/phantom_vbd", path, e_danger[i]) == -1)
            continue;
        free(danger_fpath);
        danger_fpath = xs_read(xsh, XBT_NULL, danger_buf, &len);
        if (danger_fpath) {
            if (pasprintf(&danger_buf, "%s/dev", danger_fpath) == -1)
                continue;
            free(params);
	    params_danger = xs_read(xsh, XBT_NULL, danger_buf , &len);
            DANGER DANGER params is supposedly trustworthy but here
	                  we read it from untrusted part of xenstore
            if (params) {
                /* 
                 * wait for device, on timeout silently fail because we will 
                 * fail to open below
                 */
                waitForDevice(params);
            }
        }
#endif

        bs = bdrv_new(dev);
        /* check if it is a cdrom */
        if (danger_type && !strcmp(danger_type, "cdrom")) {
            bdrv_set_type_hint(bs, BDRV_TYPE_CDROM);
            if (pasprintf(&buf, "%s/params", bpath) != -1) {
                char *buf2, *frontend;
                xs_watch(xsh, buf, dev);
                asprintf(&buf2, "%s/frontend", bpath);
                frontend = xs_read(xsh, XBT_NULL, buf2, &len);
                asprintf(&xenbus_param_paths[nb_drives], "%s/eject", frontend);
                free(frontend);
                free(buf2);
            }
        }

        /* open device now if media present */
#ifdef CONFIG_STUBDOM
        if (pasprintf(&danger_buf, "%s/device/vbd/%s", danger_path, e_danger[i]) == -1)
            continue;
	if (bdrv_open2(bs, danger_buf, BDRV_O_CACHE_WB /* snapshot and write-back */, &bdrv_raw) == 0) {
	    pstrcpy(bs->filename, sizeof(bs->filename), params);
	}
#else
        if (params[0]) {
	    if (!format) {
		if (!drv) {
		    fprintf(stderr, "qemu: type (image format) not specified for vbd '%s' or image '%s'\n", buf, params);
		    continue;
		}
		if (!strcmp(drv,"qcow")) {
		    /* autoguess qcow vs qcow2 */
		} else if (!strcmp(drv,"file")) {
		    format = &bdrv_raw;
		} else if (!strcmp(drv,"phy")) {
                    if (strstart(params, "/dev/cd", NULL) 
                     || strstart(params, "/dev/scd", NULL)) 
                        format = &bdrv_host_device;
                    else
                        format = &bdrv_raw;
		} else {
		    format = bdrv_find_format(drv);
		    if (!format) {
			fprintf(stderr, "qemu: type (image format) '%s' unknown for vbd '%s' or image '%s'\n", drv, buf, params);
			continue;
		    }
		}
	    }
            pstrcpy(bs->filename, sizeof(bs->filename), params);

            flags = BDRV_O_CACHE_WB; /* snapshot and write-back */
            is_readonly = 0;
            if (pasprintf(&buf, "%s/mode", bpath) == -1)
                continue;
            free(mode);
            mode = xs_read(xsh, XBT_NULL, buf, &len);
            if (mode == NULL)
                continue;
            if (strchr(mode, 'r') && !strchr(mode, 'w'))
                is_readonly = 1;

            if (!is_readonly)
                flags |= BDRV_O_ACCESS & O_RDWR;

            fprintf(stderr, "Using file %s in read-%s mode\n", bs->filename, is_readonly ? "only" : "write");

            if (bdrv_open2(bs, params, flags, format) < 0)
                fprintf(stderr, "qemu: could not open vbd '%s' or hard disk image '%s' (drv '%s' format '%s')\n", buf, params, drv ? drv : "?", format ? format->format_name : "0");
        }

#endif

	drives_table[nb_drives].bdrv = bs;
	drives_table[nb_drives].used = 1;
#ifdef CONFIG_STUBDOM
    media_filename[nb_drives] = strdup(danger_buf);
#else
    media_filename[nb_drives] = strdup(bs->filename);
#endif
	nb_drives++;

    }

#ifdef CONFIG_STUBDOM
    if (pasprintf(&danger_buf, "%s/device/vkbd", danger_path) == -1)
        goto out;

    free(e_danger);
    e_danger = xs_directory(xsh, XBT_NULL, danger_buf, &num);

    if (e_danger) {
        for (i = 0; i < num; i++) {
            if (pasprintf(&danger_buf, "%s/device/vkbd/%s", danger_path, e_danger[i]) == -1)
                continue;
            xenfb_connect_vkbd(danger_buf);
        }
    }

    if (pasprintf(&danger_buf, "%s/device/vfb", danger_path) == -1)
        goto out;

    free(e_danger);
    e_danger = xs_directory(xsh, XBT_NULL, danger_buf, &num);

    if (e_danger) {
        for (i = 0; i < num; i++) {
            if (pasprintf(&danger_buf, "%s/device/vfb/%s", danger_path, e_danger[i]) == -1)
                continue;
            xenfb_connect_vfb(danger_buf);
        }
    }
#endif


    /* Set a watch for log-dirty commands from the migration tools */
    if (pasprintf(&buf, "/local/domain/0/device-model/%u/logdirty/cmd",
                  domid) != -1) {
        xs_watch(xsh, buf, "logdirty");
        fprintf(logfile, "Watching %s\n", buf);
    }

    /* Set a watch for suspend requests from the migration tools */
    if (pasprintf(&buf, 
                  "/local/domain/0/device-model/%u/command", domid) != -1) {
        xs_watch(xsh, buf, "dm-command");
        fprintf(logfile, "Watching %s\n", buf);
    }

    /* Set a watch for vcpu-set */
    if (pasprintf(&buf, "/local/domain/%u/cpu", domid) != -1) {
        xs_watch(xsh, buf, "vcpu-set");
        fprintf(logfile, "Watching %s\n", buf);
    }

    /* no need for ifdef CONFIG_STUBDOM, since in the qemu case
     * hvm_domid is always equal to domid */
    hvm_domid = domid;

    /* get the pci pass-through parameters */
    if (pasprintf(&buf, "/local/domain/0/backend/pci/%u/%u/msitranslate",
                  hvm_domid, pci_devid) != -1)
    {
        free(params);
        params = xs_read(xsh, XBT_NULL, buf, &len);
        if (params)
            direct_pci_msitranslate = atoi(params);
        else
            direct_pci_msitranslate = PT_PCI_MSITRANSLATE_DEFAULT;
    }

    if (pasprintf(&buf, "/local/domain/0/backend/pci/%u/%u/power_mgmt",
                  hvm_domid, pci_devid) != -1)
    {
        free(params);
        params = xs_read(xsh, XBT_NULL, buf, &len);
        if (params)
            direct_pci_power_mgmt = atoi(params);
        else
            direct_pci_power_mgmt = PT_PCI_POWER_MANAGEMENT_DEFAULT;
    }

 out:
    free(danger_type);
    free(mode);
    free(params);
    free(dev);
    free(bpath);
    free(buf);
    free(danger_buf);
    free(danger_path);
    free(e_danger);
    free(drv);
    return;
}

int xenstore_parse_disable_pf_config ()
{
    char *params = NULL, *buf = NULL;
    int disable_pf = 0;
    unsigned int len;

    if (pasprintf(&buf, "/local/domain/0/device-model/%u/disable_pf",domid) == -1)
        goto out;

    params = xs_read(xsh, XBT_NULL, buf, &len);
    if (params == NULL)
        goto out;

    disable_pf = atoi(params);

 out:
    free(buf);
    free(params);
    return disable_pf;
}

int xenstore_fd(void)
{
    if (xsh)
        return xs_fileno(xsh);
    return -1;
}

static void xenstore_process_logdirty_event(void)
{
    char *act;
    char *ret_path = NULL;
    char *cmd_path = NULL;
    unsigned int len;

    /* Remember the paths for the command and response entries */
    if (pasprintf(&ret_path,
                "/local/domain/0/device-model/%u/logdirty/ret",
                domid) == -1) {
        fprintf(logfile, "Log-dirty: out of memory\n");
        exit(1);
    }
    if (pasprintf(&cmd_path,
                "/local/domain/0/device-model/%u/logdirty/cmd",
                domid) == -1) {
        fprintf(logfile, "Log-dirty: out of memory\n");
        exit(1);
    }

    
    /* Read the required active buffer from the store */
    act = xs_read(xsh, XBT_NULL, cmd_path, &len);
    if (!act) {
        fprintf(logfile, "Log-dirty: no command yet.\n");
        goto out;
    }
    fprintf(logfile, "Log-dirty command %s\n", act);

    if (!strcmp(act, "enable")) {
        xen_logdirty_enable = 1;
    } else if (!strcmp(act, "disable")) {
        xen_logdirty_enable = 0;
    } else {
        fprintf(logfile, "Log-dirty: bad log-dirty command: %s\n", act);
        exit(1);
    }

    /* Ack that we've service the command */
    xs_write(xsh, XBT_NULL, ret_path, act, len);

    free(act);
out:
    free(ret_path);
    free(cmd_path);
}


/* Accept state change commands from the control tools */
static void xenstore_process_dm_command_event(void)
{
    char *path = NULL, *command = NULL, *par = NULL;
    unsigned int len;

    if (pasprintf(&path, 
                  "/local/domain/0/device-model/%u/command", domid) == -1) {
        fprintf(logfile, "out of memory reading dm command\n");
        goto out;
    }
    command = xs_read(xsh, XBT_NULL, path, &len);
    if (!command)
        goto out;
    
    if (!xs_rm(xsh, XBT_NULL, path))
        fprintf(logfile, "xs_rm failed: path=%s\n", path);

    if (!strncmp(command, "save", len)) {
        fprintf(logfile, "dm-command: pause and save state\n");
        xen_pause_requested = 1;
    } else if (!strncmp(command, "continue", len)) {
        fprintf(logfile, "dm-command: continue after state save\n");
        xen_pause_requested = 0;
    } else if (!strncmp(command, "usb-add", len)) {
        fprintf(logfile, "dm-command: usb-add a usb device\n");
        if (pasprintf(&path,
                "/local/domain/0/device-model/%u/parameter", domid) == -1) {
            fprintf(logfile, "out of memory reading dm command parameter\n");
            goto out;
        }
        par = xs_read(xsh, XBT_NULL, path, &len);
        fprintf(logfile, "dm-command: usb-add a usb device: %s \n", par);
        if (!par)
            goto out;
        do_usb_add(par);
        xenstore_record_dm_state("usb-added");
        fprintf(logfile, "dm-command: finish usb-add a usb device:%s\n",par);
    } else if (!strncmp(command, "usb-del", len)) {
        fprintf(logfile, "dm-command: usb-del a usb device\n");
        if (pasprintf(&path,
                "/local/domain/0/device-model/%u/parameter", domid) == -1) {
            fprintf(logfile, "out of memory reading dm command parameter\n");
            goto out;
        }
        par = xs_read(xsh, XBT_NULL, path, &len);
        fprintf(logfile, "dm-command: usb-del a usb device: %s \n", par);
        if (!par)
            goto out;
        do_usb_del(par);
        xenstore_record_dm_state("usb-deleted");
        fprintf(logfile, "dm-command: finish usb-del a usb device:%s\n",par);
#ifdef CONFIG_PASSTHROUGH
    } else if (!strncmp(command, "pci-rem", len)) {
        fprintf(logfile, "dm-command: hot remove pass-through pci dev \n");

        if (pasprintf(&path, 
                      "/local/domain/0/device-model/%u/parameter", domid) == -1) {
            fprintf(logfile, "out of memory reading dm command parameter\n");
            goto out;
        }
        par = xs_read(xsh, XBT_NULL, path, &len);
        if (!par)
            goto out;

        do_pci_del(par);
        free(par);
    } else if (!strncmp(command, "pci-ins", len)) {
        fprintf(logfile, "dm-command: hot insert pass-through pci dev \n");

        if (pasprintf(&path, 
                      "/local/domain/0/device-model/%u/parameter", domid) == -1) {
            fprintf(logfile, "out of memory reading dm command parameter\n");
            goto out;
        }
        par = xs_read(xsh, XBT_NULL, path, &len);
        if (!par)
            goto out;

        do_pci_add(par);
        free(par);
#endif
    } else {
        fprintf(logfile, "dm-command: unknown command\"%*s\"\n", len, command);
    }

 out:
    free(path);
    free(command);
}

void xenstore_record_dm(const char *subpath, const char *state)
{
    char *path = NULL;

    if (pasprintf(&path, 
                  "/local/domain/0/device-model/%u/%s", domid, subpath) == -1) {
        fprintf(logfile, "out of memory recording dm \n");
        goto out;
    }
    if (!xs_write(xsh, XBT_NULL, path, state, strlen(state)))
        fprintf(logfile, "error recording dm \n");

 out:
    free(path);
}

int
xenstore_pv_driver_build_blacklisted(uint16_t product_nr,
                                     uint32_t build_nr)
{
    char *buf = NULL;
    char *tmp;
    const char *product;

    switch (product_nr) {
    /*
     * In qemu-xen-unstable, this is the master registry of product
     * numbers.  If you need a new product number allocating, please
     * post to xen-devel@lists.xensource.com.  You should NOT use
     * an existing product number without allocating one.
     *
     * If you maintain a seaparate versioning and distribution path
     * for PV drivers you should have a separate product number so
     * that your drivers can be separated from others'.
     *
     * During development, you may use the product ID 0xffff to
     * indicate a driver which is yet to be released.
     */
    case 1: product = "xensource-windows";  break; /* Citrix */
    case 2: product = "gplpv-windows";      break; /* James Harper */
    case 0xffff: product = "experimental";  break;
    default:
        /* Don't know what product this is -> we can't blacklist
         * it. */
        return 0;
    }
    if (asprintf(&buf, "/mh/driver-blacklist/%s/%d", product, build_nr) < 0)
        return 0;
    tmp = xs_read(xsh, XBT_NULL, buf, NULL);
    free(tmp);
    free(buf);
    if (tmp == NULL)
        return 0;
    else
        return 1;
}

void xenstore_record_dm_state(const char *state)
{
    xenstore_record_dm("state", state);
}

static void xenstore_process_vcpu_set_event(char **vec)
{
    char *act = NULL;
    char *vcpustr, *node = vec[XS_WATCH_PATH];
    unsigned int vcpu, len;

    vcpustr = strstr(node, "cpu/");
    if (!vcpustr) {
        fprintf(stderr, "vcpu-set: watch node error.\n");
        return;
    }
    sscanf(vcpustr, "cpu/%u", &vcpu);

    act = xs_read(xsh, XBT_NULL, node, &len);
    if (!act) {
        fprintf(stderr, "vcpu-set: no command yet.\n");
        return;
    }

    if (!strncmp(act, "online", len))
        qemu_cpu_add_remove(vcpu, 1);
    else if (!strncmp(act, "offline", len))
        qemu_cpu_add_remove(vcpu, 0);
    else
        fprintf(stderr, "vcpu-set: command error.\n");

    free(act);
    return;
}

void xenstore_process_event(void *opaque)
{
    char **vec, *offset, *bpath = NULL, *buf = NULL, *drv = NULL, *image = NULL;
    unsigned int len, num, hd_index, i;

    vec = xs_read_watch(xsh, &num);
    if (!vec)
        return;

    /* process dm-command events before everything else */
    if (!strcmp(vec[XS_WATCH_TOKEN], "dm-command")) {
        xenstore_process_dm_command_event();
        goto out;
    }

    if (!strcmp(vec[XS_WATCH_TOKEN], "logdirty")) {
        xenstore_process_logdirty_event();
        goto out;
    }

    if (!strcmp(vec[XS_WATCH_TOKEN], "vcpu-set")) {
        xenstore_process_vcpu_set_event(vec);
        goto out;
    }

    /* if we are paused don't process anything else */
    if (xen_pause_requested)
        goto out;

    for (i = 0; xenstore_watch_callbacks &&  xenstore_watch_callbacks[i].path; i++)
	if (xenstore_watch_callbacks[i].cb &&
	    !strcmp(vec[XS_WATCH_TOKEN], xenstore_watch_callbacks[i].path))
            xenstore_watch_callbacks[i].cb(vec[XS_WATCH_TOKEN],
                                           xenstore_watch_callbacks[i].opaque);

    hd_index = drive_name_to_index(vec[XS_WATCH_TOKEN]);
    if (hd_index == -1) {
	fprintf(stderr,"medium change watch on `%s' -"
		" unknown device, ignored\n", vec[XS_WATCH_TOKEN]);
	goto out;
    }

    image = xs_read(xsh, XBT_NULL, vec[XS_WATCH_PATH], &len);

    fprintf(stderr,"medium change watch on `%s' (index: %d): %s\n",
	    vec[XS_WATCH_TOKEN], hd_index, image ? image : "<none>");

#ifndef CONFIG_STUBDOM
    if (image != NULL) {
        /* Strip off blktap sub-type prefix */
        bpath = strdup(vec[XS_WATCH_PATH]); 
        if (bpath == NULL)
            goto out;
        if ((offset = strrchr(bpath, '/')) != NULL) 
            *offset = '\0';
        if (pasprintf(&buf, "%s/type", bpath) == -1) 
            goto out;
        drv = xs_read(xsh, XBT_NULL, buf, &len);
	if (drv && (!strcmp(drv, "tap") || !strcmp(drv, "qdisk")) &&
		((offset = strchr(image, ':')) != NULL))
            memmove(image, offset+1, strlen(offset+1)+1);

        if (!strcmp(image, drives_table[hd_index].bdrv->filename))
            goto out;  /* identical */
    }
#else
    {
        char path[strlen(vec[XS_WATCH_PATH]) - 6 + 8];
        char *state;
        path[0] = '\0';
        strncat(path, vec[XS_WATCH_PATH], strlen(vec[XS_WATCH_PATH]) - 6);
        strcat(path, "state");
        state = xs_read(xsh, XBT_NULL, path, &len);
        if (image && image[0] && state && atoi(state) <= 4) {
            if (!strcmp(image, drives_table[hd_index].bdrv->filename))
                goto out;  /* identical */
            path[0] = '\0';
            strncat(path, vec[XS_WATCH_PATH], strlen(vec[XS_WATCH_PATH]) - 6);
            strcat(path, "frontend");
            free(image);
            image = NULL;
            image = xs_read(xsh, XBT_NULL, path, &len);
        } else {
            free(image);
            image = NULL;
        }
        free(state);
    }
#endif

    drives_table[hd_index].bdrv->filename[0] = '\0';
    bdrv_close(drives_table[hd_index].bdrv);
    if (media_filename[hd_index]) {
        free(media_filename[hd_index]);
        media_filename[hd_index] = NULL;
    }

    if (image && image[0]) {
        media_filename[hd_index] = strdup(image);
        xenstore_check_new_media_present(5000);
    }

 out:
    free(drv);
    free(buf);
    free(bpath);
    free(image);
    free(vec);
}

static void xenstore_write_domain_console_item
    (const char *item, const char *val)
{
    char *dompath;
    char *path = NULL;

    if (xsh == NULL)
        return;

    dompath = xs_get_domain_path(xsh, domid);
    if (dompath == NULL) goto out_err;

    if (pasprintf(&path, "%s/console/%s", dompath, item) == -1) goto out_err;

    if (xs_write(xsh, XBT_NULL, path, val, strlen(val)) == 0)
        goto out_err;

 out:
    free(path);
    return;

 out_err:
    fprintf(logfile, "write console item %s (%s) failed\n", item, path);
    goto out;
}

void xenstore_write_vncinfo(int port,
                            const struct sockaddr *addr,
                            socklen_t addrlen,
                            const char *password)
{
    char *portstr = NULL;
    const char *addrstr;

    if (pasprintf(&portstr, "%d", port) != -1) {
        xenstore_write_domain_console_item("vnc-port", portstr);
        free(portstr);
    }

    assert(addr->sa_family == AF_INET); 
    addrstr = inet_ntoa(((const struct sockaddr_in*)addr)->sin_addr);
    if (!addrstr) {
        fprintf(logfile, "inet_ntop on vnc-addr failed\n");
    } else {
        xenstore_write_domain_console_item("vnc-listen", addrstr);
    }

    if (password)
        xenstore_write_domain_console_item("vnc-pass", password);
}

void xenstore_write_vslots(char *vslots)
{
    char *path = NULL;
    int pci_devid = 0;

    if (pasprintf(&path, 
                  "/local/domain/0/backend/pci/%u/%u/vslots", domid, pci_devid) == -1) {
        fprintf(logfile, "out of memory when updating vslots.\n");
        goto out;
    }
    if (!xs_write(xsh, XBT_NULL, path, vslots, strlen(vslots)))
        fprintf(logfile, "error updating vslots \n");

 out:
    free(path);
}

void xenstore_read_vncpasswd(int domid, char *pwbuf, size_t pwbuflen)
{
    char *buf = NULL, *path, *uuid = NULL, *passwd = NULL;
    unsigned int i, len;

    pwbuf[0] = '\0';

    if (xsh == NULL)
        return;

    path = xs_get_domain_path(xsh, domid);
    if (path == NULL) {
        fprintf(logfile, "xs_get_domain_path() error. domid %d.\n", domid);
        return;
    }

    pasprintf(&buf, "%s/vm", path);
    free(path);
    uuid = xs_read(xsh, XBT_NULL, buf, &len);
    if (uuid == NULL) {
        fprintf(logfile, "xs_read(): uuid get error. %s.\n", buf);
        free(buf);
        return;
    }

    pasprintf(&buf, "%s/vncpasswd", uuid);
    free(uuid);
    passwd = xs_read(xsh, XBT_NULL, buf, &len);
    if (passwd == NULL) {
        fprintf(logfile, "xs_read(): vncpasswd get error. %s.\n", buf);
        free(buf);
        return;
    }

    if (len >= pwbuflen)
    {
        fprintf(logfile, "xenstore_read_vncpasswd(): truncated password to avoid buffer overflow\n");
        len = pwbuflen - 1;
    }

    for (i=0; i<len; i++)
        pwbuf[i] = passwd[i];
    pwbuf[len] = '\0';
    passwd[0] = '\0';
    if (xs_write(xsh, XBT_NULL, buf, passwd, 1) == 0)
        fprintf(logfile, "xs_write() vncpasswd failed.\n");

    free(passwd);
    free(buf);
}


/*
 * get all device instances of a certain type
 */
char **xenstore_domain_get_devices_danger(struct xs_handle *handle,
                                   const char *devtype, unsigned int *num)
{
    char *path;
    char *buf = NULL;
    char **e  = NULL;

    path = xs_get_domain_path(handle, domid);
    if (path == NULL)
        goto out;

    if (pasprintf(&buf, "%s/device/%s", path,devtype) == -1)
        goto out;

    e = xs_directory(handle, XBT_NULL, buf, num);

 out:
    free(path);
    free(buf);
    return e;
}

/*
 * Check whether a domain has devices of the given type
 */
int xenstore_domain_has_devtype_danger(struct xs_handle *handle,
                                    const char *devtype)
{
    int rc = 0;
    unsigned int num;
    char **e = xenstore_domain_get_devices_danger(handle, devtype, &num);
    if (e)
        rc = 1;
    free(e);
    return rc;
}

/*
 * Function that creates a path to a variable of an instance of a
 * certain device
 */
static char *get_device_variable_path(const char *devtype,
                                      const char *inst_danger,
                                      const char *var)
{
    char *buf = NULL;
    if (strchr(inst_danger,'/')) {
        fprintf(logfile, "get_device_variable_path inst_danger has slash"
                " which is forbidden (devtype %s)\n", devtype);
        return NULL;
    }

    if (pasprintf(&buf, "/local/domain/%d/backend/%s/%d/%s/%s",
                  domid_backend,
                  devtype,
                  domid,
                  inst_danger /* safe now */,
                  var) == -1) {
        free(buf);
        buf = NULL;
    }
    return buf;
}

char *xenstore_backend_read_variable(struct xs_handle *handle,
                                     const char *devtype,
                                     const char *inst_danger,
                                     const char *var)
{
    char *value = NULL;
    char *buf = NULL;
    unsigned int len;

    buf = get_device_variable_path(devtype, inst_danger, var);
    if (NULL == buf)
        goto out;

    value = xs_read(handle, XBT_NULL, buf, &len);

    free(buf);

 out:
    return value;
}

/*
  Read the hotplug status variable from the backend given the type
  of device and its instance.
*/
char *xenstore_read_hotplug_status(struct xs_handle *handle,
                                   const char *devtype,
                                   const char *inst_danger)
{
    return xenstore_backend_read_variable(handle, devtype, inst_danger,
                                          "hotplug-status");
}

/*
   Subscribe to the hotplug status of a device given the type of device and
   its instance.
   In case an error occurrs, a negative number is returned.
 */
int xenstore_subscribe_to_hotplug_status(struct xs_handle *handle,
                                         const char *devtype,
                                         const char *inst_danger,
                                         const char *token)
{
    int rc = 0;
    char *path = get_device_variable_path(devtype, inst_danger, "hotplug-status");

    if (path == NULL)
        return -1;

    if (0 == xs_watch(handle, path, token))
        rc = -2;

    free(path);

    return rc;
}

/*
 * Unsubscribe from a subscription to the status of a hotplug variable of
 * a device.
 */
int xenstore_unsubscribe_from_hotplug_status(struct xs_handle *handle,
                                             const char *devtype,
                                             const char *inst_danger,
                                             const char *token)
{
    int rc = 0;
    char *path;
    path = get_device_variable_path(devtype, inst_danger, "hotplug-status");
    if (path == NULL)
        return -1;

    if (0 == xs_unwatch(handle, path, token))
        rc = -2;

    free(path);

    return rc;
}

static char *xenstore_vm_key_path(int domid, const char *key) {
    const char *uuid;
    char *buf = NULL;
    
    if (xsh == NULL)
        return NULL;

    uuid = xenstore_get_guest_uuid();
    if (!uuid) return NULL;

    if (pasprintf(&buf, "/vm/%s/%s", uuid, key) == -1)
        return NULL;
    return buf;
}

char *xenstore_vm_read(int domid, const char *key, unsigned int *len)
{
    char *path = NULL, *value = NULL;

    path = xenstore_vm_key_path(domid, key);
    if (!path)
        return NULL;

    value = xs_read(xsh, XBT_NULL, path, len);
    if (value == NULL) {
        fprintf(logfile, "xs_read(%s): read error\n", path);
        goto out;
    }

 out:
    free(path);
    return value;
}

int xenstore_vm_write(int domid, const char *key, const char *value)
{
    char *path = NULL;
    int rc = -1;

    path = xenstore_vm_key_path(domid, key);
    if (!path)
        return 0;

    rc = xs_write(xsh, XBT_NULL, path, value, strlen(value));
    if (rc == 0) {
        fprintf(logfile, "xs_write(%s, %s): write error\n", path, key);
        goto out;
    }

 out:
    free(path);
    return rc;
}

char *xenstore_device_model_read(int domid, const char *key, unsigned int *len)
{
    char *path = NULL, *value = NULL;

    if (pasprintf(&path, "/local/domain/0/device-model/%d/%s", domid, key) == -1)
        return NULL;

    value = xs_read(xsh, XBT_NULL, path, len);
    if (value == NULL)
        fprintf(logfile, "xs_read(%s): read error\n", path);

    free(path);
    return value;
}

static char *xenstore_extended_power_mgmt_read(const char *key, unsigned int *len)
{
    char *path = NULL, *value = NULL;
    
    if (pasprintf(&path, "/pm/%s", key) == -1)
        return NULL;

    value = xs_read(xsh, XBT_NULL, path, len);
    if (value == NULL)
        fprintf(logfile, "xs_read(%s): read error\n", path);

    free(path);
    return value;
}

static int xenstore_extended_power_mgmt_write(const char *key, const char *value)
{
    int ret;
    char *path = NULL;
    
    if (pasprintf(&path, "/pm/%s", key) == -1)
        return -1;

    ret = xs_write(xsh, XBT_NULL, path, value, strlen(value));
    free(path);
    return ret;
}

static int
xenstore_extended_power_mgmt_event_trigger(const char *key, const char *value)
{
    int ret;
    char *path = NULL;
    
    if (pasprintf(&path, "events/%s", key) == -1)
        return -1;

    ret = xenstore_extended_power_mgmt_write(path, value);
    free(path);
    return ret;
}

/*
 * Xen power management daemon stores battery generic information
 * like model, make, design volt, capacity etc. under /pm/bif and 
 * battery status information like charging/discharging rate
 * under /pm/bst in xenstore.
 */
char *xenstore_read_battery_data(int battery_status)
{
    if ( battery_status == 1 )
        return xenstore_extended_power_mgmt_read("bst", NULL);
    else
        return xenstore_extended_power_mgmt_read("bif", NULL);
}

/*
 * We set /pm/events/refreshbatterystatus xenstore entry
 * to refresh battert status info stored under /pm/bst
 * Xen power management daemon watches for changes to this
 * entry and triggers a refresh.   
 */
int xenstore_refresh_battery_status(void)
{
    return xenstore_extended_power_mgmt_event_trigger("refreshbatterystatus", "1");
}

/*
 * Create a store entry for a device (e.g., monitor, serial/parallel lines).
 * The entry is <domain-path><storeString>/tty and the value is the name
 * of the pty associated with the device.
 */
static int store_dev_info(const char *devName, int domid,
                          CharDriverState *cState, const char *storeString)
{
#ifdef CONFIG_STUBDOM
    fprintf(logfile, "can't store dev %s name for domid %d in %s from a stub domain\n", devName, domid, storeString);
    return ENOSYS;
#else
    xc_interface *xc_handle;
    struct xs_handle *xs;
    char *path;
    char *newpath;
    char *pts;
    char namebuf[128];
    int ret;

    /*
     * Only continue if we're talking to a pty
     */
    if (!cState->chr_getname) return 0;
    ret = cState->chr_getname(cState, namebuf, sizeof(namebuf));
    if (ret < 0) {
        fprintf(logfile, "ptsname failed (for '%s'): %s\n",
                storeString, strerror(errno));
        return 0;
    }
    if (memcmp(namebuf, "pty ", 4)) return 0;
    pts = namebuf + 4;

    /* We now have everything we need to set the xenstore entry. */
    xs = xs_daemon_open();
    if (xs == NULL) {
        fprintf(logfile, "Could not contact XenStore\n");
        return -1;
    }

    xc_handle = xc_interface_open(0,0,0);
    if (xc_handle == NULL) {
        fprintf(logfile, "xc_interface_open() error\n");
        return -1;
    }

    path = xs_get_domain_path(xs, domid);
    if (path == NULL) {
        fprintf(logfile, "xs_get_domain_path() error\n");
        return -1;
    }
    newpath = realloc(path, (strlen(path) + strlen(storeString) +
                             strlen("/tty") + 1));
    if (newpath == NULL) {
        free(path); /* realloc errors leave old block */
        fprintf(logfile, "realloc error\n");
        return -1;
    }
    path = newpath;

    strcat(path, storeString);
    strcat(path, "/tty");
    if (!xs_write(xs, XBT_NULL, path, pts, strlen(pts))) {
        fprintf(logfile, "xs_write for '%s' fail", storeString);
        return -1;
    }

    free(path);
    xs_daemon_close(xs);
    xc_interface_close(xc_handle);

    return 0;
#endif
}

void xenstore_store_serial_port_info(int i, CharDriverState *chr,
				     const char *devname) {
    char buf[16];

    snprintf(buf, sizeof(buf), "/serial/%d", i);
    store_dev_info(devname, domid, chr, buf);
}

void xenstore_store_pv_console_info(int i, CharDriverState *chr,
				     const char *devname) {
    char buf[32];

    if (i == 0)
        store_dev_info(devname, domid, chr, "/console");
    else {
        snprintf(buf, sizeof(buf), "/device/console/%d", i);
        store_dev_info(devname, domid, chr, buf);
    }
}

char *xenstore_dom_read(int domid, const char *key, unsigned int *len)
{
    char *buf = NULL, *path = NULL, *value = NULL;

    if (xsh == NULL)
        goto out;

    path = xs_get_domain_path(xsh, domid);
    if (path == NULL) {
        fprintf(logfile, "xs_get_domain_path(%d): error\n", domid);
        goto out;
    }

    pasprintf(&buf, "%s/%s", path, key);
    value = xs_read(xsh, XBT_NULL, buf, len);
    if (value == NULL) {
        fprintf(logfile, "xs_read(%s): read error\n", buf);
        goto out;
    }

 out:
    free(path);
    free(buf);
    return value;
}

void xenstore_dom_watch(int domid, const char *key, xenstore_callback fptr, void *opaque)
{
    char *buf = NULL, *path = NULL;
    int rc = -1;

    if (xsh == NULL)
        goto out;

    path = xs_get_domain_path(xsh, domid);
    if (path == NULL) {
        fprintf(logfile, "xs_get_domain_path: error\n");
        goto out;
    }

    pasprintf(&buf, "%s/%s", path, key);
    xenstore_watch_new_callback(buf, fptr, opaque);

 out:
    free(path);
    free(buf);
}

#ifndef CONFIG_STUBDOM

void xenstore_dom_chmod(int domid, const char *key, const char *perms)
{
    char *buf = NULL, *path = NULL;
    int rc = -1;
	struct xs_permissions p;

    if (xsh == NULL)
        goto out;

    path = xs_get_domain_path(xsh, domid);
    if (path == NULL) {
        fprintf(logfile, "xs_get_domain_path: error\n");
        goto out;
    }

    pasprintf(&buf, "%s/%s", path, key);

	xs_strings_to_perms(&p, 1, perms);
	xs_set_permissions(xsh, XBT_NULL, buf, &p, 1);

 out:
    free(path);
    free(buf);
}

#endif /*CONFIG_STUBDOM*/

int xenstore_dom_write(int domid, const char *key, const char *value)
{
    char *buf = NULL, *path = NULL;
    int rc = -1;

    if (xsh == NULL)
        goto out;

    path = xs_get_domain_path(xsh, domid);
    if (path == NULL) {
        fprintf(logfile, "xs_get_domain_path: error\n");
        goto out;
    }

    pasprintf(&buf, "%s/%s", path, key);
    rc = xs_write(xsh, XBT_NULL, buf, value, strlen(value));
    if (rc == 0) {
        fprintf(logfile, "xs_write(%s, %s): write error\n", buf, key);
        goto out;
    }

 out:
    free(path);
    free(buf);
    return rc;
}

char *xenstore_read(const char *path)
{
    char *value = NULL;
    unsigned int len;

    if (xsh == NULL)
        return NULL;
    return xs_read(xsh, XBT_NULL, path, &len);
}

int xenstore_write(const char *path, const char *val)
{
    if (xsh == NULL)
        return 1;
    return xs_write(xsh, XBT_NULL, path, val, strlen(val));
}
