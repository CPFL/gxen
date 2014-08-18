CPPFLAGS+= -I$(XEN_ROOT)/tools/libxc
CPPFLAGS+= -I$(XEN_ROOT)/tools/xenstore
CPPFLAGS+= -I$(XEN_ROOT)/tools/include

SSE2 := $(call cc-option,-msse2,)
ifeq ($(SSE2),-msse2)
CFLAGS += -DUSE_SSE2=1 -msse2
endif

override QEMU_PROG=qemu-dm

CFLAGS += -Wno-unused -Wno-declaration-after-statement

ifeq (,$(shell $(CC) -Wno-pointer-sign -E - </dev/null >/dev/null || echo x))
CFLAGS += -Wno-pointer-sign
endif 

CFLAGS += $(CMDLINE_CFLAGS)

LIBS += -L$(XEN_ROOT)/tools/libxc -lxenctrl -lxenguest
LIBS += -L$(XEN_ROOT)/tools/xenstore -lxenstore

LDFLAGS := $(CFLAGS) $(LDFLAGS)

OBJS += piix4acpi.o
OBJS += xenstore.o
OBJS += xen_platform.o
OBJS += xen_machine_fv.o
OBJS += xen_machine_pv.o
OBJS += xen_backend.o
OBJS += xenfb.o
OBJS += xen_console.o
ifndef CONFIG_STUBDOM
OBJS += xen_disk.o
endif
OBJS += xen_machine_fv.o
OBJS += exec-dm.o
OBJS += pci_emulation.o
OBJS += helper2.o
OBJS += battery_mgmt.o

ifdef CONFIG_STUBDOM
CPPFLAGS += $(TARGET_CPPFLAGS) -DNEED_CPU_H \
	-I$(TARGET_DIRS) -I$(QEMU_ROOT)/i386-dm -I$(QEMU_ROOT)/hw -I$(QEMU_ROOT)/fpu
CONFIG_SDL=
CONFIG_AUDIO=
OBJS += xenfbfront.o
else
ifndef CONFIG_NetBSD
CPPFLAGS+= -I$(XEN_ROOT)/tools/blktap/lib
LIBS += -L$(XEN_ROOT)/tools/blktap/lib -lblktap
OBJS += xen_blktap.o
OBJS += tpm_tis.o
endif
endif

ifdef CONFIG_STUBDOM
CONFIG_PASSTHROUGH=1
else
  ifeq (,$(wildcard /usr/include/pci))
$(warning === pciutils-dev package not found - missing /usr/include/pci)
$(warning === PCI passthrough capability has been disabled)
  else
CONFIG_PASSTHROUGH=1
  endif
endif

ifdef CONFIG_PASSTHROUGH
OBJS+= pass-through.o pt-msi.o pt-graphics.o
LIBS += -lpci
CFLAGS += -DCONFIG_PASSTHROUGH 
$(info === PCI passthrough capability has been enabled ===)
endif

BAD_OBJS += gdbstub.o acpi.o apic.o
BAD_OBJS += vmmouse.o vmport.o tcg* helper.o vmware_vga.o virtio-balloon.o

OBJS := $(filter-out $(BAD_OBJS), $(OBJS))

EXESUF=-xen

datadir := $(subst qemu,xen/qemu,$(datadir))
docdir :=  $(subst qemu,xen/qemu,$(docdir))
mandir :=  $(subst share/man,share/xen/man,$(mandir))

configdir := $(XEN_SCRIPT_DIR)
