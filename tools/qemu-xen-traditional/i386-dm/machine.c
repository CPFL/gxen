#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/pc.h"
#include "hw/isa.h"

#include "exec-all.h"

void cpu_save(QEMUFile *f, void *opaque) { }
int cpu_load(QEMUFile *f, void *opaque, int version_id) { return 0; }

void register_machines(void)
{
    qemu_register_machine(&xenfv_machine);
    qemu_register_machine(&xenpv_machine);
}

void *vmmouse_init(void *m) { return NULL; }
