
BAD_LIBOBJS += exec.o cpu-exec.o tcg%.o translate.o host-utils.o
BAD_LIBOBJS += translate-all.o op_helper.o
BAD_LIBOBJS += fpu/%.o helper.o disas.o

LIBOBJS := $(filter-out $(BAD_LIBOBJS), $(LIBOBJS))
