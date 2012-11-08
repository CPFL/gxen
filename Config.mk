# -*- mode: Makefile; -*-

ifeq ($(filter /%,$(XEN_ROOT)),)
$(error XEN_ROOT must be absolute)
endif

# fallback for older make
realpath = $(wildcard $(foreach file,$(1),$(shell cd -P $(dir $(file)) && echo "$$PWD/$(notdir $(file))")))

-include $(XEN_ROOT)/.config

# A debug build of Xen and tools?
debug ?= n
debug_symbols ?= $(debug)

XEN_COMPILE_ARCH    ?= $(shell uname -m | sed -e s/i.86/x86_32/ \
                         -e s/i86pc/x86_32/ -e s/amd64/x86_64/ -e s/arm.*/arm/)
XEN_TARGET_ARCH     ?= $(XEN_COMPILE_ARCH)
XEN_OS              ?= $(shell uname -s)

CONFIG_$(XEN_OS) := y

SHELL     ?= /bin/sh

# Tools to run on system hosting the build
HOSTCC      = gcc
HOSTCFLAGS  = -Wall -Werror -Wstrict-prototypes -O2 -fomit-frame-pointer
HOSTCFLAGS += -fno-strict-aliasing

DISTDIR     ?= $(XEN_ROOT)/dist
DESTDIR     ?= /

# Allow phony attribute to be listed as dependency rather than fake target
.PHONY: .phony

# Use Clang/LLVM instead of GCC?
clang ?= n
ifeq ($(clang),n)
gcc := y
else
gcc := n
endif


include $(XEN_ROOT)/config/$(XEN_OS).mk
include $(XEN_ROOT)/config/$(XEN_TARGET_ARCH).mk

SHAREDIR    ?= $(PREFIX)/share
DOCDIR      ?= $(SHAREDIR)/doc/xen
MANDIR      ?= $(SHAREDIR)/man
BASH_COMPLETION_DIR ?= $(CONFIG_DIR)/bash_completion.d

# arguments: variable, common path part, path to test, if yes, if no
define setvar_dir
  ifndef $(1)
    ifneq (,$(wildcard $(2)$(3)))
      $(1) ?= $(2)$(4)
    else
      $(1) ?= $(2)$(5)
    endif
  endif
endef

# See distro_mapping.txt for other options
$(eval $(call setvar_dir,CONFIG_LEAF_DIR,,/etc/sysconfig,sysconfig,default))
$(eval $(call setvar_dir,SUBSYS_DIR,/var/run,/subsys,/subsys,))
$(eval $(call setvar_dir,INITD_DIR,/etc,/rc.d/init.d,/rc.d/init.d,/init.d))

ifneq ($(EXTRA_PREFIX),)
EXTRA_INCLUDES += $(EXTRA_PREFIX)/include
EXTRA_LIB += $(EXTRA_PREFIX)/lib
endif

PYTHON      ?= python
PYTHON_PREFIX_ARG ?= --prefix="$(PREFIX)"
# The above requires that PREFIX contains *no spaces*. This variable is here
# to permit the user to set PYTHON_PREFIX_ARG to '' to workaround this bug:
#  https://bugs.launchpad.net/ubuntu/+bug/362570

# cc-option: Check if compiler supports first option, else fall back to second.
#
# This is complicated by the fact that unrecognised -Wno-* options:
#   (a) are ignored unless the compilation emits a warning; and
#   (b) even then produce a warning rather than an error
# To handle this we do a test compile, passing the option-under-test, on a code
# fragment that will always produce a warning (integer assigned to pointer).
# We then grep for the option-under-test in the compiler's output, the presence
# of which would indicate an "unrecognized command-line option" warning/error.
#
# Usage: cflags-y += $(call cc-option,$(CC),-march=winchip-c6,-march=i586)
cc-option = $(shell if test -z "`echo 'void*p=1;' | \
              $(1) $(2) -S -o /dev/null -xc - 2>&1 | grep -- $(2) -`"; \
              then echo "$(2)"; else echo "$(3)"; fi ;)

# cc-option-add: Add an option to compilation flags, but only if supported.
# Usage: $(call cc-option-add CFLAGS,CC,-march=winchip-c6)
cc-option-add = $(eval $(call cc-option-add-closure,$(1),$(2),$(3)))
define cc-option-add-closure
    ifneq ($$(call cc-option,$$($(2)),$(3),n),n)
        $(1) += $(3)
    endif
endef

cc-options-add = $(foreach o,$(3),$(call cc-option-add,$(1),$(2),$(o)))

# cc-ver: Check compiler is at least specified version. Return boolean 'y'/'n'.
# Usage: ifeq ($(call cc-ver,$(CC),0x030400),y)
cc-ver = $(shell if [ $$((`$(1) -dumpversion | awk -F. \
           '{ printf "0x%02x%02x%02x", $$1, $$2, $$3}'`)) -ge $$(($(2))) ]; \
           then echo y; else echo n; fi ;)

# cc-ver-check: Check compiler is at least specified version, else fail.
# Usage: $(call cc-ver-check,CC,0x030400,"Require at least gcc-3.4")
cc-ver-check = $(eval $(call cc-ver-check-closure,$(1),$(2),$(3)))
define cc-ver-check-closure
    ifeq ($$(call cc-ver,$$($(1)),$(2)),n)
        override $(1) = echo "*** FATAL BUILD ERROR: "$(3) >&2; exit 1;
        cc-option := n
    endif
endef

define buildmakevars2shellvars
    export PREFIX="$(PREFIX)";                                            \
    export XEN_SCRIPT_DIR="$(XEN_SCRIPT_DIR)";                            \
    export XEN_ROOT="$(XEN_ROOT)"
endef

#
# Compare $(1) and $(2) and replace $(2) with $(1) if they differ
#
# Typically $(1) is a newly generated file and $(2) is the target file
# being regenerated. This prevents changing the timestamp of $(2) only
# due to being auto regenereated with the same contents.
define move-if-changed
	if ! cmp -s $(1) $(2); then mv -f $(1) $(2); else rm -f $(1); fi
endef

buildmakevars2file = $(eval $(call buildmakevars2file-closure,$(1)))
define buildmakevars2file-closure
    .PHONY: genpath
    genpath:
	rm -f $(1).tmp;                                                     \
	$(foreach var,                                                      \
	          SBINDIR BINDIR LIBEXEC LIBDIR SHAREDIR PRIVATE_BINDIR     \
	          XENFIRMWAREDIR XEN_CONFIG_DIR XEN_SCRIPT_DIR XEN_LOCK_DIR \
	          XEN_RUN_DIR XEN_PAGING_DIR,                               \
	          echo "$(var)=\"$($(var))\"" >>$(1).tmp;)        \
	$(call move-if-changed,$(1).tmp,$(1))
endef

ifeq ($(debug_symbols),y)
CFLAGS += -g
endif

CFLAGS += -fno-strict-aliasing

CFLAGS += -std=gnu99

CFLAGS += -Wall -Wstrict-prototypes

# Clang complains about macros that expand to 'if ( ( foo == bar ) ) ...'
# and is over-zealous with the printf format lint
# and is a bit too fierce about unused return values
CFLAGS-$(clang) += -Wno-parentheses -Wno-format -Wno-unused-value

$(call cc-option-add,HOSTCFLAGS,HOSTCC,-Wdeclaration-after-statement)
$(call cc-option-add,CFLAGS,CC,-Wdeclaration-after-statement)
$(call cc-option-add,CFLAGS,CC,-Wno-unused-but-set-variable)

LDFLAGS += $(foreach i, $(EXTRA_LIB), -L$(i)) 
CFLAGS += $(foreach i, $(EXTRA_INCLUDES), -I$(i))
LDFLAGS += $(foreach i, $(PREPEND_LIB), -L$(i))
CFLAGS += $(foreach i, $(PREPEND_INCLUDES), -I$(i))
APPEND_LDFLAGS += $(foreach i, $(APPEND_LIB), -L$(i))
APPEND_CFLAGS += $(foreach i, $(APPEND_INCLUDES), -I$(i))

EMBEDDED_EXTRA_CFLAGS := -nopie -fno-stack-protector -fno-stack-protector-all
EMBEDDED_EXTRA_CFLAGS += -fno-exceptions

# Enable XSM security module (by default, Flask).
XSM_ENABLE ?= n
FLASK_ENABLE ?= $(XSM_ENABLE)

XEN_EXTFILES_URL=http://xenbits.xen.org/xen-extfiles
# All the files at that location were downloaded from elsewhere on
# the internet.  The original download URL is preserved as a comment
# near the place in the Xen Makefiles where the file is used.

ifeq ($(GIT_HTTP),y)
QEMU_REMOTE=http://xenbits.xen.org/git-http/qemu-xen-4.2-testing.git
else
QEMU_REMOTE=git://xenbits.xen.org/qemu-xen-4.2-testing.git
endif

QEMU_UPSTREAM_URL ?= $(XEN_ROOT)/tools/qemu-xen

ifeq ($(GIT_HTTP),y)
OVMF_UPSTREAM_URL ?= http://xenbits.xen.org/git-http/ovmf.git
QEMU_UPSTREAM_URL ?= http://xenbits.xen.org/git-http/qemu-upstream-4.2-testing.git
SEABIOS_UPSTREAM_URL ?= http://xenbits.xen.org/git-http/seabios.git
else
OVMF_UPSTREAM_URL ?= git://xenbits.xen.org/ovmf.git
QEMU_UPSTREAM_URL ?= git://xenbits.xen.org/qemu-upstream-4.2-testing.git
SEABIOS_UPSTREAM_URL ?= git://xenbits.xen.org/seabios.git
endif
OVMF_UPSTREAM_REVISION ?= b0855f925c6e2e0b21fbb03fab4b5fb5b6876871
QEMU_UPSTREAM_REVISION ?= qemu-xen-4.2.0
SEABIOS_UPSTREAM_TAG ?= rel-1.6.3.2
# Sun Mar 11 09:27:07 2012 -0400
# Update version to 1.6.3.2

ETHERBOOT_NICS ?= rtl8139 8086100e

# Specify which qemu-dm to use. This may be `ioemu' to use the old
# Mercurial in-tree version, or a local directory, or a git URL.
# CONFIG_QEMU ?= `pwd`/$(XEN_ROOT)/../qemu-xen.git
# CONFIG_QEMU ?= $(QEMU_REMOTE)
CONFIG_QEMU   ?= $(XEN_ROOT)/tools/qemu-xen-traditional

QEMU_TAG ?= xen-4.2.0

# Short answer -- do not enable this unless you know what you are
# doing and are prepared for some pain.

CONFIG_TESTS       ?= y
