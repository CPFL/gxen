/*
 * Access vector cache interface for object managers.
 *
 * Author : Stephen Smalley, <sds@epoch.ncsc.mil>
 */
 
/* Ported to Xen 3.0, George Coker, <gscoker@alpha.ncsc.mil> */

#ifndef _FLASK_AVC_H_
#define _FLASK_AVC_H_

#include <xen/errno.h>
#include <xen/lib.h>
#include <xen/spinlock.h>
#include <asm/percpu.h>
#include "flask.h"
#include "av_permissions.h"
#include "security.h"

#ifdef FLASK_DEVELOP
extern int flask_enforcing;
#else
#define flask_enforcing 1
#endif

/*
 * An entry in the AVC.
 */
struct avc_entry;

struct task_struct;
struct vfsmount;
struct dentry;
struct inode;
struct sock;
struct sk_buff;

/* Auxiliary data to use in generating the audit record. */
struct avc_audit_data {
    char    type;
#define AVC_AUDIT_DATA_NONE  0
#define AVC_AUDIT_DATA_DEV   1
#define AVC_AUDIT_DATA_IRQ   2
#define AVC_AUDIT_DATA_RANGE 3
#define AVC_AUDIT_DATA_MEMORY 4
    struct domain *sdom;
    struct domain *tdom;
    union {
        unsigned long device;
        int irq;
        struct {
            unsigned long start;
            unsigned long end;
        } range;
        struct {
            unsigned long pte;
            unsigned long mfn;
        } memory;
    };
};

/* Initialize an AVC audit data structure. */
#define AVC_AUDIT_DATA_INIT(_d,_t) \
        { memset((_d), 0, sizeof(struct avc_audit_data)); \
         (_d)->type = AVC_AUDIT_DATA_##_t; }

/*
 * AVC statistics
 */
struct avc_cache_stats
{
    unsigned int lookups;
    unsigned int hits;
    unsigned int misses;
    unsigned int allocations;
    unsigned int reclaims;
    unsigned int frees;
};

/*
 * AVC operations
 */

void avc_init(void);

void avc_audit(u32 ssid, u32 tsid, u16 tclass, u32 requested,
        struct av_decision *avd, int result, struct avc_audit_data *auditdata);

int avc_has_perm_noaudit(u32 ssid, u32 tsid, u16 tclass, u32 requested,
                                                     struct av_decision *avd);

int avc_has_perm(u32 ssid, u32 tsid, u16 tclass, u32 requested,
                                             struct avc_audit_data *auditdata);

#define AVC_CALLBACK_GRANT        1
#define AVC_CALLBACK_TRY_REVOKE        2
#define AVC_CALLBACK_REVOKE        4
#define AVC_CALLBACK_RESET        8
#define AVC_CALLBACK_AUDITALLOW_ENABLE    16
#define AVC_CALLBACK_AUDITALLOW_DISABLE    32
#define AVC_CALLBACK_AUDITDENY_ENABLE    64
#define AVC_CALLBACK_AUDITDENY_DISABLE    128

int avc_add_callback(int (*callback)(u32 event, u32 ssid, u32 tsid,
                        u16 tclass, u32 perms, u32 *out_retained), u32 events, 
                                    u32 ssid, u32 tsid, u16 tclass, u32 perms);

/* Exported to selinuxfs */
struct xen_flask_hash_stats;
int avc_get_hash_stats(struct xen_flask_hash_stats *arg);
extern unsigned int avc_cache_threshold;

#ifdef FLASK_AVC_STATS
DECLARE_PER_CPU(struct avc_cache_stats, avc_cache_stats);
#endif

#endif /* _FLASK_AVC_H_ */

