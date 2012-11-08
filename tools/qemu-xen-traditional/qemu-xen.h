#ifndef QEMU_XEN_H
#define QEMU_XEN_H

/* vl.c */
extern int restore;
extern int vga_ram_size;

#if defined(__i386__) || defined(__x86_64__)
#define phys_ram_addr(x) (qemu_map_cache(x, 0))
#elif defined(__ia64__)
#define phys_ram_addr(x) (((x) < ram_size) ? (phys_ram_base + (x)) : NULL)
#endif

/* xen_machine_fv.c */

#if (defined(__i386__) || defined(__x86_64__)) && !defined(QEMU_TOOL)
#define MAPCACHE

#if defined(__i386__) 
#define MAX_MCACHE_SIZE    0x40000000 /* 1GB max for x86 */
#define MCACHE_BUCKET_SHIFT 16
#elif defined(__x86_64__)
#define MAX_MCACHE_SIZE    0x1000000000 /* 64GB max for x86_64 */
#define MCACHE_BUCKET_SHIFT 20
#endif

#define MCACHE_BUCKET_SIZE (1UL << MCACHE_BUCKET_SHIFT)
#endif

uint8_t *qemu_map_cache(target_phys_addr_t phys_addr, uint8_t lock);
void     qemu_invalidate_entry(uint8_t *buffer);
void     qemu_invalidate_map_cache(void);

#define mapcache_lock()   ((void)0)
#define mapcache_unlock() ((void)0)

/* helper2.c */
extern long time_offset;
void timeoffset_get(void);

/* xen_platform.c */
#ifndef QEMU_TOOL
void xen_vga_populate_vram(uint64_t vram_addr, uint32_t size);
void xen_vga_vram_map(uint64_t vram_addr, uint32_t size);
void set_vram_mapping(void *opaque, unsigned long begin, unsigned long end);
void unset_vram_mapping(void *opaque);
#endif

void pci_unplug_netifs(void);
void destroy_hvm_domain(void);
void unregister_iomem(target_phys_addr_t start);

#ifdef __ia64__
static inline void xc_domain_shutdown_hook(xc_interface *xc_handle, uint32_t domid)
{
        xc_ia64_save_to_nvram(xc_handle, domid);
}
void handle_buffered_pio(void);
#else
#define xc_domain_shutdown_hook(xc_handle, domid)       do {} while (0)
#define handle_buffered_pio()                           do {} while (0)
#endif

/* xenstore.c */
void xenstore_init(void);
uint32_t xenstore_read_target(void);
void xenstore_parse_domain_config(int domid);
int xenstore_parse_disable_pf_config(void);
int xenstore_fd(void);
void xenstore_process_event(void *opaque);
void xenstore_record_dm(const char *subpath, const char *state);
void xenstore_record_dm_state(const char *state);
void xenstore_check_new_media_present(int timeout);
void xenstore_read_vncpasswd(int domid, char *pwbuf, size_t pwbuflen);
void xenstore_write_vslots(char *vslots);

int xenstore_domain_has_devtype_danger(struct xs_handle *handle,
                                const char *devtype);
char **xenstore_domain_get_devices_danger(struct xs_handle *handle,
                                   const char *devtype, unsigned int *num);
char *xenstore_read_hotplug_status(struct xs_handle *handle,
                                   const char *devtype,
				   const char *inst_danger);
char *xenstore_backend_read_variable(struct xs_handle *,
                                     const char *devtype,
				     const char *inst_danger,
                                     const char *var);
int xenstore_subscribe_to_hotplug_status(struct xs_handle *handle,
                                         const char *devtype,
                                         const char *inst,
                                         const char *token);
int xenstore_unsubscribe_from_hotplug_status(struct xs_handle *handle,
                                             const char *devtype,
                                             const char *inst,
                                             const char *token);

typedef void (*xenstore_callback) (const char *path, void *opaque);
int xenstore_watch_new_callback(const char *path, xenstore_callback fptr, void *opaque);

char *xenstore_dom_read(int domid, const char *key, unsigned int *len);
int xenstore_dom_write(int domid, const char *key, const char *value);
void xenstore_dom_watch(int domid, const char *key, xenstore_callback ftp, void *opaque);
void xenstore_dom_chmod(int domid, const char *key, const char *perms);

char *xenstore_read(const char *path);
int xenstore_write(const char *path, const char *val);

 /* `danger' means that this parameter, variable or function refers to
  * an area of xenstore which is writeable by the guest and thus must
  * not be trusted by qemu code.  For variables containing xenstore
  * paths, `danger' can mean that both the path refers to a
  * guest-writeable area (so that data read from there is dangerous
  * too) and that path's value was read from somewhere controlled by
  * the guest (so writing to this path is not safe either).
  */
 /* When we're stubdom we don't mind doing as our domain tells us to,
  * at least when it comes to running our own frontends
  */

int xenstore_vm_write(int domid, const char *key, const char *val);
char *xenstore_vm_read(int domid, const char *key, unsigned int *len);
char *xenstore_device_model_read(int domid, const char *key, unsigned int *len);
char *xenstore_read_battery_data(int battery_status);
int xenstore_refresh_battery_status(void);
int xenstore_pv_driver_build_blacklisted(uint16_t product_number,
                                         uint32_t build_nr);
void xenstore_do_eject(BlockDriverState *bs);
int xenstore_find_device(BlockDriverState *bs);

/* xenfbfront.c */
int xenfb_pv_display_init(DisplayState *ds);
void xenfb_pv_display_vram(void *vram_start);
int xenfb_connect_vkbd(const char *path);
int xenfb_connect_vfb(const char *path);

int has_tpm_device_danger(void);

static void vga_dirty_log_start(void *s) { }
static void vga_dirty_log_stop(void *s) { }

#endif /*QEMU_XEN_H*/
