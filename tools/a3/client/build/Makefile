SRCDIR=../
VPATH+=:$(SRCDIR)


CPPFLAGS+= -g3 -W -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS
# CPPFLAGS+= -O3 -DNDEBUG
CXXFLAGS+= -std=c++0x
LDFLAGS=
TARGET=a3-client
LIBS= -lpciaccess -lrt -lboost_system -lboost_thread -lboost_date_time -lpthread -lxenlight -lxenctrl
# http://wiki.xen.org/wiki/Choice_of_Toolstacks

SRC=$(notdir $(wildcard $(SRCDIR)*.c) $(wildcard $(SRCDIR)*.cc) $(wildcard $(SRCDIR)*.asm))
OBJS=$(addsuffix .o, $(basename $(SRC)))
DEPENDS=$(addsuffix .d, $(basename $(SRC)))

.SUFFIXES: .c .o
.SUFFIXES: .cc .o
.SUFFIXES: .asm .o
.PHONY: all clean depend

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LINK)

clean:
	@rm -rf $(OBJS) $(DEPENDS) $(TARGET)

quiet-command = $(if $(V),$1,$(if $(2),@echo $2 && $1, @$1))

%.o: %.c
	$(call quiet-command,$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<,"  CC    $@")

%.o: %.S
	$(call quiet-command,$(CC) $(CPPFLAGS) -c -o $@ $<,"  AS    $@")

%.o: %.m
	$(call quiet-command,$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<,"  OBJC  $@")

%.o: %.cc
	$(call quiet-command,$(CXX) $(CPPFLAGS) $(CFLAGS) $(CXXFLAGS) -c -o $@ $<,"  CXX   $@")

LINK = $(call quiet-command,$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS),"  LINK  $@")

%: %.o
	$(LINK)

%.a:
	$(call quiet-command,rm -f $@ && $(AR) rcs $@ $^,"  AR    $@")

%.d: %.c
	@set -e; $(CC) -MM $(CPPFLAGS) $< \
	    | sed "s/\($*\)\.o[ :]*/\1.o $@ : /g" > $@; \
	    [ -s $@ ] || rm -f $@

%.d: %.cc
	@set -e; $(CXX) -MM $(CPPFLAGS) $< \
	    | sed "s/\($*\)\.o[ :]*/\1.o $@ : /g" > $@; \
	    [ -s $@ ] || rm -f $@

-include $(DEPENDS)
