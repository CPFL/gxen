/******************************************************************************
 * edd.h
 * 
 * Copyright (C) 2002, 2003, 2004 Dell Inc.
 * by Matt Domsch <Matt_Domsch@dell.com>
 *
 * structures and definitions for the int 13h, ax={41,48}h
 * BIOS Enhanced Disk Drive Services
 * This is based on the T13 group document D1572 Revision 0 (August 14 2002)
 * available at http://www.t13.org/docs2002/d1572r0.pdf.  It is
 * very similar to D1484 Revision 3 http://www.t13.org/docs2002/d1484r3.pdf
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2.0 as published by
 * the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __XEN_EDD_H__
#define __XEN_EDD_H__

#ifndef __ASSEMBLY__

struct edd_info {
    /* Int13, Fn48: Check Extensions Present. */
    u8 device;                   /* %dl: device */
    u8 version;                  /* %ah: major version */
    u16 interface_support;       /* %cx: interface support bitmap */
    /* Int13, Fn08: Legacy Get Device Parameters. */
    u16 legacy_max_cylinder;     /* %cl[7:6]:%ch: maximum cylinder number */
    u8 legacy_max_head;          /* %dh: maximum head number */
    u8 legacy_sectors_per_track; /* %cl[5:0]: maximum sector number */
    /* Int13, Fn41: Get Device Parameters (as filled into %ds:%esi). */
    struct edd_device_params {
        u16 length;
        u16 info_flags;
        u32 num_default_cylinders;
        u32 num_default_heads;
        u32 sectors_per_track;
        u64 number_of_sectors;
        u16 bytes_per_sector;
        u32 dpte_ptr;            /* 0xFFFFFFFF for our purposes */
        u16 key;                 /* = 0xBEDD */
        u8 device_path_info_length;
        u8 reserved2;
        u16 reserved3;
        u8 host_bus_type[4];
        u8 interface_type[8];
        union {
            struct {
                u16 base_address;
                u16 reserved1;
                u32 reserved2;
            } __attribute__ ((packed)) isa;
            struct {
                u8 bus;
                u8 slot;
                u8 function;
                u8 channel;
                u32 reserved;
            } __attribute__ ((packed)) pci;
            /* pcix is same as pci */
            struct {
                u64 reserved;
            } __attribute__ ((packed)) ibnd;
            struct {
                u64 reserved;
            } __attribute__ ((packed)) xprs;
            struct {
                u64 reserved;
            } __attribute__ ((packed)) htpt;
            struct {
                u64 reserved;
            } __attribute__ ((packed)) unknown;
        } interface_path;
        union {
            struct {
                u8 device;
                u8 reserved1;
                u16 reserved2;
                u32 reserved3;
                u64 reserved4;
            } __attribute__ ((packed)) ata;
            struct {
                u8 device;
                u8 lun;
                u8 reserved1;
                u8 reserved2;
                u32 reserved3;
                u64 reserved4;
            } __attribute__ ((packed)) atapi;
            struct {
                u16 id;
                u64 lun;
                u16 reserved1;
                u32 reserved2;
            } __attribute__ ((packed)) scsi;
            struct {
                u64 serial_number;
                u64 reserved;
            } __attribute__ ((packed)) usb;
            struct {
                u64 eui;
                u64 reserved;
            } __attribute__ ((packed)) i1394;
            struct {
                u64 wwid;
                u64 lun;
            } __attribute__ ((packed)) fibre;
            struct {
                u64 identity_tag;
                u64 reserved;
            } __attribute__ ((packed)) i2o;
            struct {
                u32 array_number;
                u32 reserved1;
                u64 reserved2;
            } __attribute__ ((packed)) raid;
            struct {
                u8 device;
                u8 reserved1;
                u16 reserved2;
                u32 reserved3;
                u64 reserved4;
            } __attribute__ ((packed)) sata;
            struct {
                u64 reserved1;
                u64 reserved2;
            } __attribute__ ((packed)) unknown;
        } device_path;
        u8 reserved4;
        u8 checksum;
    } __attribute__ ((packed)) edd_device_params;
} __attribute__ ((packed));

struct mbr_signature {
    u8 device;
    u8 pad[3];
    u32 signature;
} __attribute__ ((packed));

/* These all reside in the boot trampoline. Access via bootsym(). */
extern struct mbr_signature boot_mbr_signature[];
extern u8 boot_mbr_signature_nr;
extern struct edd_info boot_edd_info[];
extern u8 boot_edd_info_nr;

#endif /* __ASSEMBLY__ */

/* Maximum number of EDD information structures at boot_edd_info. */
#define EDD_INFO_MAX            6

/* Maximum number of MBR signatures at boot_mbr_signature. */
#define EDD_MBR_SIG_MAX         16

/* Size of components of EDD information structure. */
#define EDDEXTSIZE              8
#define EDDPARMSIZE             74

#endif /* __XEN_EDD_H__ */
