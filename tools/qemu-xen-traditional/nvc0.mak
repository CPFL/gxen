# NVC0 Makefile
#
# This file is included in Makefile.target

VPATH+=:$(SRC_PATH)/hw/nvc0

OBJS += nvc0.o
OBJS += nvc0_channel.o
OBJS += nvc0_ioport.o
OBJS += nvc0_mmio.o
OBJS += nvc0_mmio_bar0.o
OBJS += nvc0_mmio_bar1.o
OBJS += nvc0_mmio_bar3.o

# link pciaccess
LIBS+=-lpciaccess
