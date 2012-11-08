/*
 * battery_mgmt.h
 *
 * Copyright (c) 2008  Kamala Narasimhan
 * Copyright (c) 2008  Citrix Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _BATTERY_MGMT_H
#define _BATTERY_MGMT_H

#ifdef CONFIG_STUBDOM
#define CONFIG_NO_BATTERY_MGMT
#endif
#ifdef _BSD /* There's no ioperm(), outb(), inb() */
#define CONFIG_NO_BATTERY_MGMT
#endif

enum POWER_MGMT_MODE { PM_MODE_NONE = 0, PM_MODE_PT, PM_MODE_NON_PT };
enum BATTERY_INFO_TYPE { BATT_NONE, BIF, BST };
typedef struct battery_state_info {
    enum BATTERY_INFO_TYPE type;
    uint8_t port_b2_val;
    uint8_t port_86_val;
    uint8_t port_66_val;
    char *battery_data;
    uint8_t current_index;
} battery_state_info;

void battery_mgmt_init(PCIDevice *device);

#endif
