ifdef CONFIG_AUDIO
CPPFLAGS += -DHAS_AUDIO
endif
QEMU_PROG=qemu-dm

include $(QEMU_ROOT)/xen-hooks.mak
