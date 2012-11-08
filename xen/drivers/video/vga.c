/******************************************************************************
 * vga.c
 * 
 * VGA support routines.
 */

#include <xen/config.h>
#include <xen/init.h>
#include <xen/lib.h>
#include <xen/mm.h>
#include <xen/vga.h>
#include <asm/io.h>

/* Filled in by arch boot code. */
struct xen_vga_console_info vga_console_info;

static int vgacon_keep;
static unsigned int xpos, ypos;
static unsigned char *video;

static void vga_text_puts(const char *s);
static void vga_noop_puts(const char *s) {}
void (*vga_puts)(const char *) = vga_noop_puts;

/*
 * 'vga=<mode-specifier>[,keep]' where <mode-specifier> is one of:
 * 
 *   'vga=ask':
 *      display a vga menu of available modes
 * 
 *   'vga=current':
 *      use the current vga mode without modification
 * 
 *   'vga=text-80x<rows>':
 *      text mode, where <rows> is one of {25,28,30,34,43,50,60}
 * 
 *   'vga=gfx-<width>x<height>x<depth>':
 *      graphics mode, e.g., vga=gfx-1024x768x16
 * 
 *   'vga=mode-<mode>:
 *      specifies a mode as specified in 'vga=ask' menu
 *      (NB. menu modes are displayed in hex, so mode numbers here must
 *           be prefixed with '0x' (e.g., 'vga=mode-0x0318'))
 * 
 * The option 'keep' causes Xen to continue to print to the VGA console even 
 * after domain 0 starts to boot. The default behaviour is to relinquish
 * control of the console to domain 0.
 */
static char __initdata opt_vga[30] = "";
string_param("vga", opt_vga);

/* VGA text-mode definitions. */
static unsigned int columns, lines;
#define ATTRIBUTE   7

#ifdef CONFIG_X86_64
void vesa_early_init(void);
void vesa_endboot(bool_t keep);
#else
#define vesa_early_init() ((void)0)
#define vesa_endboot(x)   ((void)0)
#endif

void __init vga_init(void)
{
    char *p;

    /* Look for 'keep' in comma-separated options. */
    for ( p = opt_vga; p != NULL; p = strchr(p, ',') )
    {
        if ( *p == ',' )
            p++;
        if ( strncmp(p, "keep", 4) == 0 )
            vgacon_keep = 1;
    }

    switch ( vga_console_info.video_type )
    {
    case XEN_VGATYPE_TEXT_MODE_3:
        if ( page_is_ram_type(paddr_to_pfn(0xB8000), RAM_TYPE_CONVENTIONAL) ||
             ((video = ioremap(0xB8000, 0x8000)) == NULL) )
            return;
        outw(0x200a, 0x3d4); /* disable cursor */
        columns = vga_console_info.u.text_mode_3.columns;
        lines   = vga_console_info.u.text_mode_3.rows;
        memset(video, 0, columns * lines * 2);
        vga_puts = vga_text_puts;
        break;
    case XEN_VGATYPE_VESA_LFB:
    case XEN_VGATYPE_EFI_LFB:
        vesa_early_init();
        break;
    default:
        memset(&vga_console_info, 0, sizeof(vga_console_info));
        break;
    }
}

void __init vga_endboot(void)
{
    if ( vga_puts == vga_noop_puts )
        return;

    printk("Xen is %s VGA console.\n",
           vgacon_keep ? "keeping" : "relinquishing");

    if ( !vgacon_keep )
        vga_puts = vga_noop_puts;

    switch ( vga_console_info.video_type )
    {
    case XEN_VGATYPE_TEXT_MODE_3:
        if ( !vgacon_keep )
            memset(video, 0, columns * lines * 2);
        break;
    case XEN_VGATYPE_VESA_LFB:
    case XEN_VGATYPE_EFI_LFB:
        vesa_endboot(vgacon_keep);
        break;
    default:
        BUG();
    }
}

static void vga_text_puts(const char *s)
{
    char c;

    while ( (c = *s++) != '\0' )
    {
        if ( (c == '\n') || (xpos >= columns) )
        {
            if ( ++ypos >= lines )
            {
                ypos = lines - 1;
                memmove(video, video + 2 * columns, ypos * 2 * columns);
                memset(video + ypos * 2 * columns, 0, 2 * xpos);
            }
            xpos = 0;
        }

        if ( c != '\n' )
        {
            video[(xpos + ypos * columns) * 2]     = c;
            video[(xpos + ypos * columns) * 2 + 1] = ATTRIBUTE;
            xpos++;
        }
    }
}

int __init fill_console_start_info(struct dom0_vga_console_info *ci)
{
    memcpy(ci, &vga_console_info, sizeof(*ci));
    return 1;
}
