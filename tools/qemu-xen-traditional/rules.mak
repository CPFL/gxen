
%.o: %.c
	@mkdir -p $(dir $(TARGET_DIR)$@)
	$(call quiet-command,$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<,"  CC    $(TARGET_DIR)$@")

%.o: %.S
	@mkdir -p $(dir $(TARGET_DIR)$@)
	$(call quiet-command,$(CC) $(CPPFLAGS) -c -o $@ $<,"  AS    $(TARGET_DIR)$@")

%.o: %.m
	@mkdir -p $(dir $(TARGET_DIR)$@)
	$(call quiet-command,$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<,"  OBJC  $(TARGET_DIR)$@")

%.o: %.cc
	@mkdir -p $(dir $(TARGET_DIR)$@)
	$(call quiet-command,$(CXX) $(CPPFLAGS) $(CFLAGS) $(CXXFLAGS) -std=c++0x -c -o $@ $<,"  CXX   $(TARGET_DIR)$@")

LINK = $(call quiet-command,$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS),"  LINK  $(TARGET_DIR)$@")

%$(EXESUF): %.o
	$(LINK)

%.a:
	@mkdir -p $(dir $(TARGET_DIR)$@)
	$(call quiet-command,rm -f $@ && $(AR) rcs $@ $^,"  AR    $(TARGET_DIR)$@")

quiet-command = $(if $(V),$1,$(if $(2),@echo $2 && $1, @$1))
