# NVC0 Makefile
#
# This file is included in Makefile.target


VPATH+=:$(SRC_PATH)/hw/nvc0

OBJS += nvc0_main.o
OBJS += nvc0_ioport.o
OBJS += nvc0_mmio.o
OBJS += nvc0_mmio_bar0.o
OBJS += nvc0_mmio_bar1.o
OBJS += nvc0_mmio_bar3.o
OBJS += nvc0_mmio_bar4.o
OBJS += nvc0_mmio_rom.o
OBJS += nvc0_context.o

# link pciaccess and boost libraries
LIBS += -lpciaccess -lboost_thread -lboost_system -lboost_date_time

# for a3.h
CPPFLAGS += -I$(XEN_ROOT)/tools/a3
