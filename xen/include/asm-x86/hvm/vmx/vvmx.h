
/*
 * vvmx.h: Support virtual VMX for nested virtualization.
 *
 * Copyright (c) 2010, Intel Corporation.
 * Author: Qing He <qing.he@intel.com>
 *         Eddie Dong <eddie.dong@intel.com>
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
 *
 */
#ifndef __ASM_X86_HVM_VVMX_H__
#define __ASM_X86_HVM_VVMX_H__

struct nestedvmx {
    paddr_t    vmxon_region_pa;
    void       *iobitmap[2];		/* map (va) of L1 guest I/O bitmap */
    /* deferred nested interrupt */
    struct {
        unsigned long intr_info;
        u32           error_code;
    } intr;
};

#define vcpu_2_nvmx(v)	(vcpu_nestedhvm(v).u.nvmx)

/*
 * Encode of VMX instructions base on Table 24-11 & 24-12 of SDM 3B
 */

enum vmx_regs_enc {
    VMX_REG_RAX,
    VMX_REG_RCX,
    VMX_REG_RDX,
    VMX_REG_RBX,
    VMX_REG_RSP,
    VMX_REG_RBP,
    VMX_REG_RSI,
    VMX_REG_RDI,
#ifdef CONFIG_X86_64
    VMX_REG_R8,
    VMX_REG_R9,
    VMX_REG_R10,
    VMX_REG_R11,
    VMX_REG_R12,
    VMX_REG_R13,
    VMX_REG_R14,
    VMX_REG_R15,
#endif
};

enum vmx_sregs_enc {
    VMX_SREG_ES,
    VMX_SREG_CS,
    VMX_SREG_SS,
    VMX_SREG_DS,
    VMX_SREG_FS,
    VMX_SREG_GS,
};

union vmx_inst_info {
    struct {
        unsigned int scaling           :2; /* bit 0-1 */
        unsigned int __rsvd0           :1; /* bit 2 */
        unsigned int reg1              :4; /* bit 3-6 */
        unsigned int addr_size         :3; /* bit 7-9 */
        unsigned int memreg            :1; /* bit 10 */
        unsigned int __rsvd1           :4; /* bit 11-14 */
        unsigned int segment           :3; /* bit 15-17 */
        unsigned int index_reg         :4; /* bit 18-21 */
        unsigned int index_reg_invalid :1; /* bit 22 */
        unsigned int base_reg          :4; /* bit 23-26 */
        unsigned int base_reg_invalid  :1; /* bit 27 */
        unsigned int reg2              :4; /* bit 28-31 */
    } fields;
    u32 word;
};

int nvmx_vcpu_initialise(struct vcpu *v);
void nvmx_vcpu_destroy(struct vcpu *v);
int nvmx_vcpu_reset(struct vcpu *v);
uint64_t nvmx_vcpu_guestcr3(struct vcpu *v);
uint64_t nvmx_vcpu_hostcr3(struct vcpu *v);
uint32_t nvmx_vcpu_asid(struct vcpu *v);
enum hvm_intblk nvmx_intr_blocked(struct vcpu *v);
int nvmx_intercepts_exception(struct vcpu *v, 
                              unsigned int trap, int error_code);
void nvmx_domain_relinquish_resources(struct domain *d);

int nvmx_handle_vmxon(struct cpu_user_regs *regs);
int nvmx_handle_vmxoff(struct cpu_user_regs *regs);
/*
 * Virtual VMCS layout
 *
 * Since physical VMCS layout is unknown, a custom layout is used
 * for virtual VMCS seen by guest. It occupies a 4k page, and the
 * field is offset by an 9-bit offset into u64[], The offset is as
 * follow, which means every <width, type> pair has a max of 32
 * fields available.
 *
 *             9       7      5               0
 *             --------------------------------
 *     offset: | width | type |     index     |
 *             --------------------------------
 *
 * Also, since the lower range <width=0, type={0,1}> has only one
 * field: VPID, it is moved to a higher offset (63), and leaves the
 * lower range to non-indexed field like VMCS revision.
 *
 */

#define VVMCS_REVISION 0x40000001u

struct vvmcs_header {
    u32 revision;
    u32 abort;
};

union vmcs_encoding {
    struct {
        u32 access_type : 1;
        u32 index : 9;
        u32 type : 2;
        u32 rsv1 : 1;
        u32 width : 2;
        u32 rsv2 : 17;
    };
    u32 word;
};

enum vvmcs_encoding_width {
    VVMCS_WIDTH_16 = 0,
    VVMCS_WIDTH_64,
    VVMCS_WIDTH_32,
    VVMCS_WIDTH_NATURAL,
};

enum vvmcs_encoding_type {
    VVMCS_TYPE_CONTROL = 0,
    VVMCS_TYPE_RO,
    VVMCS_TYPE_GSTATE,
    VVMCS_TYPE_HSTATE,
};

u64 __get_vvmcs(void *vvmcs, u32 vmcs_encoding);
void __set_vvmcs(void *vvmcs, u32 vmcs_encoding, u64 val);

void nvmx_destroy_vmcs(struct vcpu *v);
int nvmx_handle_vmptrld(struct cpu_user_regs *regs);
int nvmx_handle_vmptrst(struct cpu_user_regs *regs);
int nvmx_handle_vmclear(struct cpu_user_regs *regs);
int nvmx_handle_vmread(struct cpu_user_regs *regs);
int nvmx_handle_vmwrite(struct cpu_user_regs *regs);
int nvmx_handle_vmresume(struct cpu_user_regs *regs);
int nvmx_handle_vmlaunch(struct cpu_user_regs *regs);
int nvmx_msr_read_intercept(unsigned int msr,
                                u64 *msr_content);
int nvmx_msr_write_intercept(unsigned int msr,
                                 u64 msr_content);

void nvmx_update_exec_control(struct vcpu *v, u32 value);
void nvmx_update_secondary_exec_control(struct vcpu *v,
                                        unsigned long value);
void nvmx_update_exception_bitmap(struct vcpu *v, unsigned long value);
void nvmx_switch_guest(void);
void nvmx_idtv_handling(void);
u64 nvmx_get_tsc_offset(struct vcpu *v);
int nvmx_n2_vmexit_handler(struct cpu_user_regs *regs,
                          unsigned int exit_reason);

#endif /* __ASM_X86_HVM_VVMX_H__ */

