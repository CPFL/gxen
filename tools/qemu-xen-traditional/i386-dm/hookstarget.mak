IOEMU_OS=$(shell uname -s)

install-hook:
	$(INSTALL_DIR) "$(DESTDIR)/$(bindir)"
	$(INSTALL_DIR) "$(DESTDIR)/$(configdir)"
	$(INSTALL_PROG) $(QEMU_ROOT)/i386-dm/qemu-ifup-$(IOEMU_OS) "$(DESTDIR)/$(configdir)/qemu-ifup"
