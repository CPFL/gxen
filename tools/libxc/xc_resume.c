/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "xc_private.h"
#include "xg_private.h"
#include "xg_save_restore.h"

#if defined(__i386__) || defined(__x86_64__)

#include <xen/foreign/x86_32.h>
#include <xen/foreign/x86_64.h>
#include <xen/hvm/params.h>

static int pv_guest_width(xc_interface *xch, uint32_t domid)
{
    DECLARE_DOMCTL;
    domctl.domain = domid;
    domctl.cmd = XEN_DOMCTL_get_address_size;
    if ( xc_domctl(xch, &domctl) != 0 )
    {
        PERROR("Could not get guest address size");
        return -1;
    }
    return domctl.u.address_size.size / 8;
}

static int modify_returncode(xc_interface *xch, uint32_t domid)
{
    vcpu_guest_context_any_t ctxt;
    xc_dominfo_t info;
    xen_capabilities_info_t caps;
    struct domain_info_context _dinfo = {};
    struct domain_info_context *dinfo = &_dinfo;
    int rc;

    if ( xc_domain_getinfo(xch, domid, 1, &info) != 1 )
    {
        PERROR("Could not get domain info");
        return -1;
    }

    if ( info.hvm )
    {
        /* HVM guests without PV drivers have no return code to modify. */
        unsigned long irq = 0;
        xc_get_hvm_param(xch, domid, HVM_PARAM_CALLBACK_IRQ, &irq);
        if ( !irq )
            return 0;

        /* HVM guests have host address width. */
        if ( xc_version(xch, XENVER_capabilities, &caps) != 0 )
        {
            PERROR("Could not get Xen capabilities");
            return -1;
        }
        dinfo->guest_width = strstr(caps, "x86_64") ? 8 : 4;
    }
    else
    {
        /* Probe PV guest address width. */
        dinfo->guest_width = pv_guest_width(xch, domid);
        if ( dinfo->guest_width < 0 )
            return -1;
    }

    if ( (rc = xc_vcpu_getcontext(xch, domid, 0, &ctxt)) != 0 )
        return rc;

    SET_FIELD(&ctxt, user_regs.eax, 1);

    if ( (rc = xc_vcpu_setcontext(xch, domid, 0, &ctxt)) != 0 )
        return rc;

    return 0;
}

#else

static int modify_returncode(xc_interface *xch, uint32_t domid)
{
    return 0;

}

#endif

static int xc_domain_resume_cooperative(xc_interface *xch, uint32_t domid)
{
    DECLARE_DOMCTL;
    int rc;

    /*
     * Set hypercall return code to indicate that suspend is cancelled
     * (rather than resuming in a new domain context).
     */
    if ( (rc = modify_returncode(xch, domid)) != 0 )
        return rc;

    domctl.cmd = XEN_DOMCTL_resumedomain;
    domctl.domain = domid;
    return do_domctl(xch, &domctl);
}

static int xc_domain_resume_any(xc_interface *xch, uint32_t domid)
{
    DECLARE_DOMCTL;
    xc_dominfo_t info;
    int i, rc = -1;
#if defined(__i386__) || defined(__x86_64__)
    struct domain_info_context _dinfo = { .p2m_size = 0 };
    struct domain_info_context *dinfo = &_dinfo;
    unsigned long mfn;
    vcpu_guest_context_any_t ctxt;
    start_info_t *start_info;
    shared_info_t *shinfo = NULL;
    xen_pfn_t *p2m_frame_list_list = NULL;
    xen_pfn_t *p2m_frame_list = NULL;
    xen_pfn_t *p2m = NULL;
#endif

    if ( xc_domain_getinfo(xch, domid, 1, &info) != 1 )
    {
        PERROR("Could not get domain info");
        return rc;
    }

    /*
     * (x86 only) Rewrite store_mfn and console_mfn back to MFN (from PFN).
     */
#if defined(__i386__) || defined(__x86_64__)
    if ( info.hvm )
    {
        ERROR("Cannot resume uncooperative HVM guests");
        return rc;
    }

    dinfo->guest_width = pv_guest_width(xch, domid);
    if ( dinfo->guest_width != sizeof(long) )
    {
        ERROR("Cannot resume uncooperative cross-address-size guests");
        return rc;
    }

    /* Map the shared info frame */
    shinfo = xc_map_foreign_range(xch, domid, PAGE_SIZE,
                                  PROT_READ, info.shared_info_frame);
    if ( shinfo == NULL )
    {
        ERROR("Couldn't map shared info");
        goto out;
    }

    dinfo->p2m_size = shinfo->arch.max_pfn;

    p2m_frame_list_list =
        xc_map_foreign_range(xch, domid, PAGE_SIZE, PROT_READ,
                             shinfo->arch.pfn_to_mfn_frame_list_list);
    if ( p2m_frame_list_list == NULL )
    {
        ERROR("Couldn't map p2m_frame_list_list");
        goto out;
    }

    p2m_frame_list = xc_map_foreign_pages(xch, domid, PROT_READ,
                                          p2m_frame_list_list,
                                          P2M_FLL_ENTRIES);
    if ( p2m_frame_list == NULL )
    {
        ERROR("Couldn't map p2m_frame_list");
        goto out;
    }

    /* Map all the frames of the pfn->mfn table. For migrate to succeed,
       the guest must not change which frames are used for this purpose.
       (its not clear why it would want to change them, and we'll be OK
       from a safety POV anyhow. */
    p2m = xc_map_foreign_pages(xch, domid, PROT_READ,
                               p2m_frame_list,
                               P2M_FL_ENTRIES);
    if ( p2m == NULL )
    {
        ERROR("Couldn't map p2m table");
        goto out;
    }

    if ( xc_vcpu_getcontext(xch, domid, 0, &ctxt) )
    {
        ERROR("Could not get vcpu context");
        goto out;
    }

    mfn = GET_FIELD(&ctxt, user_regs.edx);

    start_info = xc_map_foreign_range(xch, domid, PAGE_SIZE,
                                      PROT_READ | PROT_WRITE, mfn);
    if ( start_info == NULL )
    {
        ERROR("Couldn't map start_info");
        goto out;
    }

    start_info->store_mfn        = p2m[start_info->store_mfn];
    start_info->console.domU.mfn = p2m[start_info->console.domU.mfn];

    munmap(start_info, PAGE_SIZE);
#endif /* defined(__i386__) || defined(__x86_64__) */

    /* Reset all secondary CPU states. */
    for ( i = 1; i <= info.max_vcpu_id; i++ )
        xc_vcpu_setcontext(xch, domid, i, NULL);

    /* Ready to resume domain execution now. */
    domctl.cmd = XEN_DOMCTL_resumedomain;
    domctl.domain = domid;
    rc = do_domctl(xch, &domctl);

#if defined(__i386__) || defined(__x86_64__)
 out:
    if (p2m)
        munmap(p2m, P2M_FL_ENTRIES*PAGE_SIZE);
    if (p2m_frame_list)
        munmap(p2m_frame_list, P2M_FLL_ENTRIES*PAGE_SIZE);
    if (p2m_frame_list_list)
        munmap(p2m_frame_list_list, PAGE_SIZE);
    if (shinfo)
        munmap(shinfo, PAGE_SIZE);
#endif

    return rc;
}

/*
 * Resume execution of a domain after suspend shutdown.
 * This can happen in one of two ways:
 *  1. Resume with special return code.
 *  2. Reset guest environment so it believes it is resumed in a new
 *     domain context.
 * (2) should be used only for guests which cannot handle the special
 * new return code. (1) is always safe (but slower).
 */
int xc_domain_resume(xc_interface *xch, uint32_t domid, int fast)
{
    return (fast
            ? xc_domain_resume_cooperative(xch, domid)
            : xc_domain_resume_any(xch, domid));
}
