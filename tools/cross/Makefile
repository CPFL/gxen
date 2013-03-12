CPPFLAGS= -g -W -Wall -Wextra -Wno-unused-parameter
LDFLAGS=
TARGET=cross
LIBS= -lpciaccess -lrt -lboost_system -lboost_thread -lpthread
SRC= $(wildcard *.c) $(wildcard *.cc) $(wildcard *.asm)
OBJS= $(addprefix $(TARGET_DIR), $(patsubst %.asm,%.o,$(patsubst %.cc,%.o,$(patsubst %.c,%.o,$(SRC)))))
TARGET_DIR=build/

.SUFFIXES: .c .o
.SUFFIXES: .cc .o
.SUFFIXES: .asm .o
.PHONY: all clean depend

all: $(TARGET_DIR) $(TARGET_DIR)$(TARGET)

$(TARGET_DIR)$(TARGET): $(OBJS)
	$(LINK)

$(TARGET_DIR):
	@mkdir -p $@

clean:
	@rm -rf $(TARGET_DIR)

$(TARGET_DIR)%.o: %.c
	$(call quiet-command,$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $(TARGET_DIR)$@ $<,"  CC    $@")

$(TARGET_DIR)%.o: %.S
	$(call quiet-command,$(CC) $(CPPFLAGS) -c -o $@ $<,"  AS    $@")

$(TARGET_DIR)%.o: %.m
	$(call quiet-command,$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<,"  OBJC  $@")

$(TARGET_DIR)%.o: %.cc
	$(call quiet-command,$(CXX) $(CPPFLAGS) $(CFLAGS) $(CXXFLAGS) -c -o $@ $<,"  CXX   $@")

LINK = $(call quiet-command,$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS),"  LINK  $@")

%$(EXESUF): %.o
	$(LINK)

$(TARGET_DIR)%.a:
	$(call quiet-command,rm -f $@ && $(AR) rcs $@ $^,"  AR    $@")

quiet-command = $(if $(V),$1,$(if $(2),@echo $2 && $1, @$1))
