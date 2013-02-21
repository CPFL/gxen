
%.o: %.c
	$(call quiet-command,$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<,"  CC    $(TARGET_DIR)$@")

%.o: %.S
	$(call quiet-command,$(CC) $(CPPFLAGS) -c -o $@ $<,"  AS    $(TARGET_DIR)$@")

%.o: %.m
	$(call quiet-command,$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<,"  OBJC  $(TARGET_DIR)$@")

%.o: %.cc
	$(call quiet-command,$(CXX) $(CPPFLAGS) $(CFLAGS) $(CXXFLAGS) -c -o $@ $<,"  CXX   $(TARGET_DIR)$@")

LINK = $(call quiet-command,$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS),"  LINK  $(TARGET_DIR)$@")

%$(EXESUF): %.o
	$(LINK)

%.a:
	$(call quiet-command,rm -f $@ && $(AR) rcs $@ $^,"  AR    $(TARGET_DIR)$@")

quiet-command = $(if $(V),$1,$(if $(2),@echo $2 && $1, @$1))
