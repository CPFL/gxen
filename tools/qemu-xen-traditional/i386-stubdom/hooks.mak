include $(QEMU_ROOT)/xen-hooks.mak

OBJS += block-vbd.o

QEMU_STUBDOM= qemu.a

PROGS=$(QEMU_STUBDOM) libqemu.a
TOOLS=

$(QEMU_STUBDOM): $(OBJS)
	$(AR) rcs $@ $^
