QEMU_ROOT ?= .
XEN_ROOT ?= $(QEMU_ROOT)/../xen-unstable.hg
include $(XEN_ROOT)/tools/Rules.mk

ifdef CONFIG_STUBDOM
export TARGET_DIRS=i386-stubdom
else
export TARGET_DIRS=i386-dm
endif

SUBDIR_RULES=subdir-$(TARGET_DIRS)
subdir-$(TARGET_DIRS): libqemu_common.a

include $(QEMU_ROOT)/xen-hooks.mak
