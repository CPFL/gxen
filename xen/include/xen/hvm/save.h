/*
 * save.h: HVM support routines for save/restore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 */

#ifndef __XEN_HVM_SAVE_H__
#define __XEN_HVM_SAVE_H__

#include <xen/types.h>
#include <public/xen.h>
#include <public/hvm/save.h>

/* Marshalling and unmarshalling uses a buffer with size and cursor. */
typedef struct hvm_domain_context {
    uint32_t cur;
    uint32_t size;
    uint8_t *data;
} hvm_domain_context_t;

/* Marshalling an entry: check space and fill in the header */
int _hvm_init_entry(struct hvm_domain_context *h,
                    uint16_t tc, uint16_t inst, uint32_t len);

/* Marshalling: copy the contents in a type-safe way */
void _hvm_write_entry(struct hvm_domain_context *h,
                      void *src, uint32_t src_len);

/* Marshalling: init and copy; evaluates to zero on success */
#define hvm_save_entry(_x, _inst, _h, _src) ({                  \
    int r;                                                      \
    r = _hvm_init_entry((_h), HVM_SAVE_CODE(_x),                \
                        (_inst), HVM_SAVE_LENGTH(_x));          \
    if ( r == 0 )                                               \
        _hvm_write_entry((_h), (_src), HVM_SAVE_LENGTH(_x));    \
    r; })

/* Unmarshalling: test an entry's size and typecode and record the instance */
int _hvm_check_entry(struct hvm_domain_context *h, 
                     uint16_t type, uint32_t len, bool_t strict_length);

/* Unmarshalling: copy the contents in a type-safe way */
void _hvm_read_entry(struct hvm_domain_context *h,
                     void *dest, uint32_t dest_len);

/*
 * Unmarshalling: check, then copy. Evaluates to zero on success. This load
 * function requires the save entry to be the same size as the dest structure.
 */
#define _hvm_load_entry(_x, _h, _dst, _strict) ({                       \
    int r;                                                              \
    if ( (r = _hvm_check_entry((_h), HVM_SAVE_CODE(_x),                 \
               HVM_SAVE_LENGTH(_x), (_strict))) == 0 )                  \
        _hvm_read_entry((_h), (_dst), HVM_SAVE_LENGTH(_x));             \
    else if (HVM_SAVE_HAS_COMPAT(_x)                                    \
             && (r = _hvm_check_entry((_h), HVM_SAVE_CODE(_x),          \
                       HVM_SAVE_LENGTH_COMPAT(_x), (_strict))) == 0 ) { \
        _hvm_read_entry((_h), (_dst), HVM_SAVE_LENGTH_COMPAT(_x));      \
        r=HVM_SAVE_FIX_COMPAT(_x, (_dst));                              \
    }                                                                   \
    r; })

#define hvm_load_entry(_x, _h, _dst)            \
    _hvm_load_entry(_x, _h, _dst, 1)
#define hvm_load_entry_zeroextend(_x, _h, _dst) \
    _hvm_load_entry(_x, _h, _dst, 0)

/* Unmarshalling: what is the instance ID of the next entry? */
static inline uint16_t hvm_load_instance(struct hvm_domain_context *h)
{
    struct hvm_save_descriptor *d 
        = (struct hvm_save_descriptor *)&h->data[h->cur];
    return d->instance;
}

/* Handler types for different types of save-file entry. 
 * The save handler may save multiple instances of a type into the buffer;
 * the load handler will be called once for each instance found when
 * restoring.  Both return non-zero on error. */
typedef int (*hvm_save_handler) (struct domain *d, 
                                 hvm_domain_context_t *h);
typedef int (*hvm_load_handler) (struct domain *d,
                                 hvm_domain_context_t *h);

/* Init-time function to declare a pair of handlers for a type,
 * and the maximum buffer space needed to save this type of state */
void hvm_register_savevm(uint16_t typecode,
                         const char *name, 
                         hvm_save_handler save_state,
                         hvm_load_handler load_state,
                         size_t size, int kind);

/* The space needed for saving can be per-domain or per-vcpu: */
#define HVMSR_PER_DOM  0
#define HVMSR_PER_VCPU 1

/* Syntactic sugar around that function: specify the max number of
 * saves, and this calculates the size of buffer needed */
#define HVM_REGISTER_SAVE_RESTORE(_x, _save, _load, _num, _k)             \
static int __hvm_register_##_x##_save_and_restore(void)                   \
{                                                                         \
    hvm_register_savevm(HVM_SAVE_CODE(_x),                                \
                        #_x,                                              \
                        &_save,                                           \
                        &_load,                                           \
                        (_num) * (HVM_SAVE_LENGTH(_x)                     \
                                  + sizeof (struct hvm_save_descriptor)), \
                        _k);                                              \
    return 0;                                                             \
}                                                                         \
__initcall(__hvm_register_##_x##_save_and_restore);


/* Entry points for saving and restoring HVM domain state */
size_t hvm_save_size(struct domain *d);
int hvm_save(struct domain *d, hvm_domain_context_t *h);
int hvm_save_one(struct domain *d,  uint16_t typecode, uint16_t instance, 
                 XEN_GUEST_HANDLE_64(uint8) handle);
int hvm_load(struct domain *d, hvm_domain_context_t *h);

/* Arch-specific definitions. */
struct hvm_save_header;
void arch_hvm_save(struct domain *d, struct hvm_save_header *hdr);
int arch_hvm_load(struct domain *d, struct hvm_save_header *hdr);

#endif /* __XEN_HVM_SAVE_H__ */
