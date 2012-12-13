# Quadro 6000 Makefile
#
# This file is included in Makefile.target
OBJS += quadro6000.o
OBJS += quadro6000_channel.o

# link pciaccess
LIBS+=-lpciaccess
