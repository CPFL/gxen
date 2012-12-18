# NVC0 Makefile
#
# This file is included in Makefile.target
OBJS += nvc0.o
OBJS += nvc0_channel.o
OBJS += nvc0_ioport.o
OBJS += nvc0_mmio.o

# link pciaccess
LIBS+=-lpciaccess
