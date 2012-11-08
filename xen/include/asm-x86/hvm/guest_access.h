#ifndef __ASM_X86_HVM_GUEST_ACCESS_H__
#define __ASM_X86_HVM_GUEST_ACCESS_H__

unsigned long copy_to_user_hvm(void *to, const void *from, unsigned len);
unsigned long clear_user_hvm(void *to, unsigned int len);
unsigned long copy_from_user_hvm(void *to, const void *from, unsigned len);

#endif /* __ASM_X86_HVM_GUEST_ACCESS_H__ */
