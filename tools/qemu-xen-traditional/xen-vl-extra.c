/*
 * We #include this from vl.c.
 *
 * This is a bit yucky, but it means that the line numbers and other
 * textual differences in vl.c remain small.
 */
/* There is no need for multiple-inclusion protection since
 * there is only one place where this file is included. */

#include "sysemu.h"

/* Max number of PCI emulation */
#define MAX_PCI_EMULATION 32

int restore;
#ifdef CONFIG_OPENGL
int opengl_enabled = 1;
#else
int opengl_enabled = 0;
#endif
static const char *direct_pci;
static int nb_pci_emulation = 0;
static char pci_emulation_config_text[MAX_PCI_EMULATION][256];
PCI_EMULATION_INFO *PciEmulationInfoHead = NULL;

int vncunused;


/* We use simpler state save/load functions for Xen */

void do_savevm(const char *name)
{
    QEMUFile *f;
    int saved_vm_running, ret;

    f = qemu_fopen(name, "wb");
    
    /* ??? Should this occur after vm_stop?  */
    qemu_aio_flush();

    saved_vm_running = vm_running;
    vm_stop(0);

    if (!f) {
        fprintf(logfile, "Failed to open savevm file '%s'\n", name);
        goto the_end;
    }
    
    ret = qemu_savevm_state(f);
    qemu_fclose(f);

    if (ret < 0)
        fprintf(logfile, "Error %d while writing VM to savevm file '%s'\n",
                ret, name);

 the_end:
    if (saved_vm_running)
        vm_start();

    return;
}
void do_loadvm(const char *name)
{
    QEMUFile *f;
    int saved_vm_running, ret;

    /* Flush all IO requests so they don't interfere with the new state.  */
    qemu_aio_flush();

    saved_vm_running = vm_running;
    vm_stop(0);

    /* restore the VM state */
    f = qemu_fopen(name, "rb");
    if (!f) {
        fprintf(logfile, "Could not open VM state file\n");
        goto the_end;
    }

    ret = qemu_loadvm_state(f);
    qemu_fclose(f);
    if (ret < 0) {
        fprintf(logfile, "Error %d while loading savevm file '%s'\n",
                ret, name);
        goto the_end; 
    }

#if 0 
    /* del tmp file */
    if (unlink(name) == -1)
        fprintf(stderr, "delete tmp qemu state file failed.\n");
#endif


 the_end:
    if (saved_vm_running)
        vm_start();
}

struct qemu_alarm_timer;

#ifdef CONFIG_PASSTHROUGH
void do_pci_del(char *devname)
{
    int devfn;
    char *devname_cpy;

    devname_cpy = strdup(devname);
    if (!devname_cpy)
        return;

    devfn = bdf_to_devfn(devname);

    if (devfn < 0)
        fprintf(logfile, "Device \"%s\" is not used by a hotplug device.\n",
                devname_cpy);
    else
        acpi_php_del(devfn);

    free(devname_cpy);
}

void do_pci_add(char *devname)
{
    int devfn;

    devfn = insert_to_pci_devfn(devname);

    acpi_php_add(devfn);
}

int pci_emulation_add(char *config_text)
{
    PCI_EMULATION_INFO *new;
    if ((new = qemu_mallocz(sizeof(PCI_EMULATION_INFO))) == NULL) {
        return -1;
    }
    parse_pci_emulation_info(config_text, new);
    new->next = PciEmulationInfoHead;
    PciEmulationInfoHead = new;
    return 0;
}
#endif
