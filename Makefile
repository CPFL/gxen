#
# Grand Unified Makefile for Xen.
#

# Default target must appear before any include lines
.PHONY: all
all: dist

export XEN_ROOT=$(CURDIR)
include Config.mk

SUBARCH := $(subst x86_32,i386,$(XEN_TARGET_ARCH))
export XEN_TARGET_ARCH SUBARCH XEN_SYSTYPE
include buildconfigs/Rules.mk

# build and install everything into the standard system directories
.PHONY: install
install: install-xen install-kernels install-tools install-stubdom install-docs

.PHONY: build
build: kernels
	$(MAKE) -C xen build
	$(MAKE) -C tools build
	$(MAKE) -C stubdom build
ifeq (x86_64,$(XEN_TARGET_ARCH))
	XEN_TARGET_ARCH=x86_32 $(MAKE) -C stubdom pv-grub
endif
	$(MAKE) -C docs build

# The test target is for unit tests that can run without an installation.  Of
# course, many tests require a machine running Xen itself, and these are
# handled elsewhere.
.PHONY: test
test:
	$(MAKE) -C tools/python test

# build and install everything into local dist directory
.PHONY: dist
dist: DESTDIR=$(DISTDIR)/install
dist: dist-xen dist-kernels dist-tools dist-stubdom dist-docs dist-misc

dist-misc:
	$(INSTALL_DIR) $(DISTDIR)/
	$(INSTALL_DATA) ./COPYING $(DISTDIR)
	$(INSTALL_DATA) ./README $(DISTDIR)
	$(INSTALL_PROG) ./install.sh $(DISTDIR)
dist-%: DESTDIR=$(DISTDIR)/install
dist-%: install-%
	@: # do nothing

# Legacy dist targets
.PHONY: xen tools stubdom kernels docs
xen: dist-xen
tools: dist-tools
kernels: dist-kernels
stubdom: dist-stubdom
docs: dist-docs

.PHONY: prep-kernels
prep-kernels:
	for i in $(XKERNELS) ; do $(MAKE) $$i-prep || exit 1; done

.PHONY: install-xen
install-xen:
	$(MAKE) -C xen install

.PHONY: install-tools
install-tools:
	$(MAKE) -C tools install

ifeq ($(CONFIG_IOEMU),y)
install-tools: tools/qemu-xen-traditional-dir tools/qemu-xen-dir
endif

.PHONY: install-kernels
install-kernels:
	for i in $(XKERNELS) ; do $(MAKE) $$i-install || exit 1; done

.PHONY: install-stubdom
install-stubdom: tools/qemu-xen-traditional-dir install-tools
	$(MAKE) -C stubdom install
ifeq (x86_64,$(XEN_TARGET_ARCH))
	XEN_TARGET_ARCH=x86_32 $(MAKE) -C stubdom install-grub
endif

tools/qemu-xen-traditional-dir:
	$(MAKE) -C tools qemu-xen-traditional-dir-find

.PHONY: tools/qemu-xen-traditional-dir-force-update
tools/qemu-xen-traditional-dir-force-update:
	$(MAKE) -C tools qemu-xen-traditional-dir-force-update

tools/qemu-xen-dir:
	$(MAKE) -C tools qemu-xen-dir-find

.PHONY: tools/qemu-xen-dir-force-update
tools/qemu-xen-dir-force-update:
	$(MAKE) -C tools qemu-xen-dir-force-update

.PHONY: tools/firmware/seabios-dir-force-update
tools/firmware/seabios-dir-force-update:
	$(MAKE) -C tools/firmware seabios-dir-force-update

.PHONY: install-docs
install-docs:
	$(MAKE) -C docs install || true

.PHONY: dev-docs
dev-docs:
	$(MAKE) -C docs dev-docs

# Build all the various kernels and modules
.PHONY: kbuild
kbuild: kernels

# Delete the kernel build trees entirely
.PHONY: kdelete
kdelete:
	for i in $(XKERNELS) ; do $(MAKE) $$i-delete ; done

# Clean the kernel build trees
.PHONY: kclean
kclean:
	for i in $(XKERNELS) ; do $(MAKE) $$i-clean ; done

# build xen, the tools, and a domain 0 plus unprivileged linux-xen images,
# and place them in the install directory. 'make install' should then
# copy them to the normal system directories
.PHONY: world
world: 
	$(MAKE) clean
	$(MAKE) kdelete
	$(MAKE) dist

# Package a build in a .deb file
.PHONY: deb
deb: dist
	fakeroot sh ./tools/misc/mkdeb $(XEN_ROOT) $$($(MAKE) -C xen xenversion | grep -v :)

# clean doesn't do a kclean
.PHONY: clean
clean::
	$(MAKE) -C xen clean
	$(MAKE) -C tools clean
	$(MAKE) -C stubdom crossclean
ifeq (x86_64,$(XEN_TARGET_ARCH))
	XEN_TARGET_ARCH=x86_32 $(MAKE) -C stubdom crossclean
endif
	$(MAKE) -C docs clean

# clean, but blow away kernel build tree plus tarballs
.PHONY: distclean
distclean:
	$(MAKE) -C xen distclean
	$(MAKE) -C tools distclean
	$(MAKE) -C stubdom distclean
ifeq (x86_64,$(XEN_TARGET_ARCH))
	XEN_TARGET_ARCH=x86_32 $(MAKE) -C stubdom distclean
endif
	$(MAKE) -C docs distclean
	rm -rf dist patches/tmp
	for i in $(ALLKERNELS) ; do $(MAKE) $$i-delete ; done
	rm -rf patches/*/.makedep

# Linux name for GNU distclean
.PHONY: mrproper
mrproper: distclean

# Prepare for source tarball
.PHONY: src-tarball
src-tarball: distclean
	$(MAKE) -C xen .banner
	rm -rf xen/tools/figlet .[a-z]*
	$(MAKE) -C xen distclean

.PHONY: help
help:
	@echo 'Installation targets:'
	@echo '  install          - build and install everything'
	@echo '  install-xen      - build and install the Xen hypervisor'
	@echo '  install-tools    - build and install the control tools'
	@echo '  install-kernels  - build and install guest kernels'
	@echo '  install-stubdom  - build and install the stubdomain images'
	@echo '  install-docs     - build and install user documentation'
	@echo ''
	@echo 'Building targets:'
	@echo '  dist             - build and install everything into local dist directory'
	@echo '  world            - clean everything, delete guest kernel build'
	@echo '                     trees then make dist'
	@echo '  xen              - build and install Xen hypervisor'
	@echo '  tools            - build and install tools'
	@echo '  stubdom          - build and install the stubdomain images'
	@echo '  kernels          - build and install guest kernels'
	@echo '  kbuild           - synonym for make kernels'
	@echo '  docs             - build and install user documentation'
	@echo '  dev-docs         - build developer-only documentation'
	@echo ''
	@echo 'Cleaning targets:'
	@echo '  clean            - clean the Xen, tools and docs (but not guest kernel trees)'
	@echo '  distclean        - clean plus delete kernel build trees and'
	@echo '                     local downloaded files'
	@echo '  kdelete          - delete guest kernel build trees'
	@echo '  kclean           - clean guest kernel build trees'
	@echo ''
	@echo 'Miscellaneous targets:'
	@echo '  prep-kernels     - prepares kernel directories, does not build'
	@echo '  uninstall        - attempt to remove installed Xen tools'
	@echo '                     (use with extreme care!)'
	@echo
	@echo 'Trusted Boot (tboot) targets:'
	@echo '  build-tboot      - download and build the tboot module'
	@echo '  install-tboot    - download, build, and install the tboot module'
	@echo '  clean-tboot      - clean the tboot module if it exists'
	@echo
	@echo 'Environment:'
	@echo '  [ this documentation is sadly not complete ]'

# Use this target with extreme care!
.PHONY: uninstall
uninstall: D=$(DESTDIR)
uninstall:
	[ -d $(D)$(XEN_CONFIG_DIR) ] && mv -f $(D)$(XEN_CONFIG_DIR) $(D)$(XEN_CONFIG_DIR).old-`date +%s` || true
	$(MAKE) -C xen uninstall
	rm -rf $(D)$(CONFIG_DIR)/init.d/xendomains $(D)$(CONFIG_DIR)/init.d/xend
	rm -rf $(D)$(CONFIG_DIR)/init.d/xencommons $(D)$(CONFIG_DIR)/init.d/xen-watchdog
	rm -rf $(D)$(CONFIG_DIR)/hotplug/xen-backend.agent
	rm -f  $(D)$(CONFIG_DIR)/udev/rules.d/xen-backend.rules
	rm -f  $(D)$(CONFIG_DIR)/udev/rules.d/xend.rules
	rm -f  $(D)$(SYSCONFIG_DIR)/xendomains
	rm -f  $(D)$(SYSCONFIG_DIR)/xencommons
	rm -rf $(D)/var/run/xen* $(D)/var/lib/xen*
	make -C tools uninstall
	rm -rf $(D)/boot/tboot*

# Legacy targets for compatibility
.PHONY: linux26
linux26:
	$(MAKE) 'KERNELS=linux-2.6*' kernels

.PHONY: xenversion
xenversion:
	@$(MAKE) --no-print-directory -C xen xenversion

#
# tboot targets
#

TBOOT_TARFILE = tboot-20090330.tar.gz
#TBOOT_BASE_URL = http://downloads.sourceforge.net/tboot
TBOOT_BASE_URL = $(XEN_EXTFILES_URL)

.PHONY: build-tboot
build-tboot: download_tboot
	$(MAKE) -C tboot build

.PHONY: install-tboot
install-tboot: download_tboot
	$(MAKE) -C tboot install

.PHONY: dist-tboot
dist-tboot: download_tboot
	$(MAKE) DESTDIR=$(DISTDIR)/install -C tboot dist

.PHONY: clean-tboot
clean-tboot:
	[ ! -d tboot ] || $(MAKE) -C tboot clean

.PHONY: distclean-tboot
distclean-tboot:
	[ ! -d tboot ] || $(MAKE) -C tboot distclean

.PHONY: download_tboot
download_tboot: tboot/Makefile

tboot/Makefile: tboot/$(TBOOT_TARFILE)
	[ -e tboot/Makefile ] || tar -xzf tboot/$(TBOOT_TARFILE) -C tboot/ --strip-components 1

tboot/$(TBOOT_TARFILE):
	mkdir -p tboot
	wget -O tboot/$(TBOOT_TARFILE) $(TBOOT_BASE_URL)/$(TBOOT_TARFILE)
