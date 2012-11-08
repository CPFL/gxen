
/*******************************************************************************
 *
 * Module Name: hwregs - Read/write access functions for the various ACPI
 *                       control and status registers.
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2006, R. Byron Moore
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#include <asm/io.h>
#include <xen/config.h>
#include <xen/init.h>
#include <xen/types.h>
#include <xen/errno.h>
#include <acpi/acpi.h>

#define _COMPONENT          ACPI_HARDWARE
ACPI_MODULE_NAME("hwregs")

/*******************************************************************************
 *
 * FUNCTION:    acpi_hw_get_register_bit_mask
 *
 * PARAMETERS:  register_id         - Index of ACPI Register to access
 *
 * RETURN:      The bitmask to be used when accessing the register
 *
 * DESCRIPTION: Map register_id into a register bitmask.
 *
 ******************************************************************************/
static struct acpi_bit_register_info *
acpi_hw_get_bit_register_info(u32 register_id)
{
	ACPI_FUNCTION_ENTRY();

	if (register_id > ACPI_BITREG_MAX) {
		ACPI_DEBUG_PRINT((AE_INFO, "Invalid BitRegister ID: %X",
			    register_id));
		return (NULL);
	}

	return (&acpi_gbl_bit_register_info[register_id]);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_get_register
 *
 * PARAMETERS:  register_id     - ID of ACPI bit_register to access
 *              return_value    - Value that was read from the register
 *
 * RETURN:      Status and the value read from specified Register. Value
 *              returned is normalized to bit0 (is shifted all the way right)
 *
 * DESCRIPTION: ACPI bit_register read function.
 *
 ******************************************************************************/

acpi_status acpi_get_register(u32 register_id, u32 * return_value)
{
	u32 register_value = 0;
	struct acpi_bit_register_info *bit_reg_info;
	acpi_status status;

	ACPI_FUNCTION_TRACE(acpi_get_register);

	/* Get the info structure corresponding to the requested ACPI Register */

	bit_reg_info = acpi_hw_get_bit_register_info(register_id);
	if (!bit_reg_info) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Read from the register */

	status = acpi_hw_register_read(bit_reg_info->parent_register,
				       &register_value);

	if (ACPI_SUCCESS(status)) {

		/* Normalize the value that was read */

		register_value =
		    ((register_value & bit_reg_info->access_bit_mask)
		     >> bit_reg_info->bit_position);

		*return_value = register_value;

		ACPI_DEBUG_PRINT((ACPI_DB_IO, "Read value %8.8X register %X\n",
				  register_value,
				  bit_reg_info->parent_register));
	}

	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_set_register
 *
 * PARAMETERS:  register_id     - ID of ACPI bit_register to access
 *              Value           - (only used on write) value to write to the
 *                                Register, NOT pre-normalized to the bit pos
 *
 * RETURN:      Status
 *
 * DESCRIPTION: ACPI Bit Register write function.
 *
 ******************************************************************************/
acpi_status acpi_set_register(u32 register_id, u32 value)
{
	u32 register_value = 0;
	struct acpi_bit_register_info *bit_reg_info;
	acpi_status status;

	ACPI_FUNCTION_TRACE_U32(acpi_set_register, register_id);

	/* Get the info structure corresponding to the requested ACPI Register */

	bit_reg_info = acpi_hw_get_bit_register_info(register_id);
	if (!bit_reg_info) {
		ACPI_DEBUG_PRINT((AE_INFO, "Bad ACPI HW RegisterId: %X",
			    register_id));
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* Always do a register read first so we can insert the new bits  */

	status = acpi_hw_register_read(bit_reg_info->parent_register,
				       &register_value);
	if (ACPI_FAILURE(status)) {
		goto unlock_and_exit;
	}

	/*
	 * Decode the Register ID
	 * Register ID = [Register block ID] | [bit ID]
	 *
	 * Check bit ID to fine locate Register offset.
	 * Check Mask to determine Register offset, and then read-write.
	 */
	switch (bit_reg_info->parent_register) {
	case ACPI_REGISTER_PM1_STATUS:

		/*
		 * Status Registers are different from the rest. Clear by
		 * writing 1, and writing 0 has no effect. So, the only relevant
		 * information is the single bit we're interested in, all others should
		 * be written as 0 so they will be left unchanged.
		 */
		value = ACPI_REGISTER_PREPARE_BITS(value,
						   bit_reg_info->bit_position,
						   bit_reg_info->
						   access_bit_mask);
		if (value) {
			status = acpi_hw_register_write(ACPI_REGISTER_PM1_STATUS,
							(u16) value);
			register_value = 0;
		}
		break;

	case ACPI_REGISTER_PM1_ENABLE:

		ACPI_REGISTER_INSERT_VALUE(register_value,
					   bit_reg_info->bit_position,
					   bit_reg_info->access_bit_mask,
					   value);

		status = acpi_hw_register_write(ACPI_REGISTER_PM1_ENABLE,
						(u16) register_value);
		break;

	case ACPI_REGISTER_PM1_CONTROL:

		/*
		 * Write the PM1 Control register.
		 * Note that at this level, the fact that there are actually TWO
		 * registers (A and B - and B may not exist) is abstracted.
		 */
		ACPI_DEBUG_PRINT((ACPI_DB_IO, "PM1 control: Read %X\n",
				  register_value));

		ACPI_REGISTER_INSERT_VALUE(register_value,
					   bit_reg_info->bit_position,
					   bit_reg_info->access_bit_mask,
					   value);

		status = acpi_hw_register_write(ACPI_REGISTER_PM1_CONTROL,
						(u16) register_value);
		break;

	case ACPI_REGISTER_PM2_CONTROL:

#if 0 /* Redundant read in original Linux code. */
		status = acpi_hw_register_read(ACPI_REGISTER_PM2_CONTROL,
					       &register_value);
		if (ACPI_FAILURE(status)) {
			goto unlock_and_exit;
		}
#endif

		ACPI_DEBUG_PRINT((ACPI_DB_IO,
				  "PM2 control: Read %X from %8.8X%8.8X\n",
				  register_value,
				  ACPI_FORMAT_UINT64(acpi_gbl_FADT.
						     xpm2_control_block.
						     address)));

		ACPI_REGISTER_INSERT_VALUE(register_value,
					   bit_reg_info->bit_position,
					   bit_reg_info->access_bit_mask,
					   value);

		ACPI_DEBUG_PRINT((ACPI_DB_IO,
				  "About to write %4.4X to %8.8X%8.8X\n",
				  register_value,
				  ACPI_FORMAT_UINT64(acpi_gbl_FADT.
						     xpm2_control_block.
						     address)));

		status = acpi_hw_register_write(ACPI_REGISTER_PM2_CONTROL,
						(u8) (register_value));
		break;

	default:
		break;
	}

      unlock_and_exit:

	/* Normalize the value that was read */

	ACPI_DEBUG_EXEC(register_value =
			((register_value & bit_reg_info->access_bit_mask) >>
			 bit_reg_info->bit_position));

	ACPI_DEBUG_PRINT((ACPI_DB_IO,
			  "Set bits: %8.8X actual %8.8X register %X\n", value,
			  register_value, bit_reg_info->parent_register));
	return_ACPI_STATUS(status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_hw_register_read
 *
 * PARAMETERS:  register_id         - ACPI Register ID
 *              return_value        - Where the register value is returned
 *
 * RETURN:      Status and the value read.
 *
 * DESCRIPTION: Read from the specified ACPI register
 *
 ******************************************************************************/
acpi_status
acpi_hw_register_read(u32 register_id, u32 * return_value)
{
	u32 value1 = 0;
	u32 value2 = 0;
	acpi_status status;

	ACPI_FUNCTION_TRACE(hw_register_read);

	switch (register_id) {
	case ACPI_REGISTER_PM1_STATUS:	/* 16-bit access */

		status =
		    acpi_hw_low_level_read(16, &value1,
					   &acpi_gbl_FADT.xpm1a_event_block);
		if (ACPI_FAILURE(status)) {
			goto exit;
		}

		/* PM1B is optional */

		status =
		    acpi_hw_low_level_read(16, &value2,
					   &acpi_gbl_FADT.xpm1b_event_block);
		value1 |= value2;
		break;

	case ACPI_REGISTER_PM1_ENABLE:	/* 16-bit access */

		status =
		    acpi_hw_low_level_read(16, &value1, &acpi_gbl_xpm1a_enable);
		if (ACPI_FAILURE(status)) {
			goto exit;
		}

		/* PM1B is optional */

		status =
		    acpi_hw_low_level_read(16, &value2, &acpi_gbl_xpm1b_enable);
		value1 |= value2;
		break;

	case ACPI_REGISTER_PM1_CONTROL:	/* 16-bit access */

		status =
		    acpi_hw_low_level_read(16, &value1,
					   &acpi_gbl_FADT.xpm1a_control_block);
		if (ACPI_FAILURE(status)) {
			goto exit;
		}

		status =
		    acpi_hw_low_level_read(16, &value2,
					   &acpi_gbl_FADT.xpm1b_control_block);
		value1 |= value2;
		break;

	case ACPI_REGISTER_PM2_CONTROL:	/* 8-bit access */

		status =
		    acpi_hw_low_level_read(8, &value1,
					   &acpi_gbl_FADT.xpm2_control_block);
		break;

	case ACPI_REGISTER_PM_TIMER:	/* 32-bit access */

		status =
		    acpi_hw_low_level_read(32, &value1,
					   &acpi_gbl_FADT.xpm_timer_block);
		break;

	case ACPI_REGISTER_SMI_COMMAND_BLOCK:	/* 8-bit access */

		status =
		    acpi_os_read_port(acpi_gbl_FADT.smi_command, &value1, 8);
		break;

	default:
		ACPI_DEBUG_PRINT((AE_INFO, "Unknown Register ID: %X", register_id));
		status = AE_BAD_PARAMETER;
		break;
	}

      exit:

	if (ACPI_SUCCESS(status)) {
		*return_value = value1;
	}

	return_ACPI_STATUS(status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_hw_register_write
 *
 * PARAMETERS:  register_id         - ACPI Register ID
 *              Value               - The value to write
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Write to the specified ACPI register
 *
 * NOTE: In accordance with the ACPI specification, this function automatically
 * preserves the value of the following bits, meaning that these bits cannot be
 * changed via this interface:
 *
 * PM1_CONTROL[0] = SCI_EN
 * PM1_CONTROL[9]
 * PM1_STATUS[11]
 *
 * ACPI References:
 * 1) Hardware Ignored Bits: When software writes to a register with ignored
 *      bit fields, it preserves the ignored bit fields
 * 2) SCI_EN: OSPM always preserves this bit position
 *
 ******************************************************************************/

acpi_status acpi_hw_register_write(u32 register_id, u32 value)
{
	acpi_status status;
	u32 read_value;

	ACPI_FUNCTION_TRACE(hw_register_write);

	switch (register_id) {
	case ACPI_REGISTER_PM1_STATUS:	/* 16-bit access */

		/* Perform a read first to preserve certain bits (per ACPI spec) */

		status = acpi_hw_register_read(ACPI_REGISTER_PM1_STATUS,
					       &read_value);
		if (ACPI_FAILURE(status)) {
			goto exit;
		}

		/* Insert the bits to be preserved */

		ACPI_INSERT_BITS(value, ACPI_PM1_STATUS_PRESERVED_BITS,
				 read_value);

		/* Now we can write the data */

		status =
		    acpi_hw_low_level_write(16, value,
					    &acpi_gbl_FADT.xpm1a_event_block);
		if (ACPI_FAILURE(status)) {
			goto exit;
		}

		/* PM1B is optional */

		status =
		    acpi_hw_low_level_write(16, value,
					    &acpi_gbl_FADT.xpm1b_event_block);
		break;

	case ACPI_REGISTER_PM1_ENABLE:	/* 16-bit access */

		status =
		    acpi_hw_low_level_write(16, value, &acpi_gbl_xpm1a_enable);
		if (ACPI_FAILURE(status)) {
			goto exit;
		}

		/* PM1B is optional */

		status =
		    acpi_hw_low_level_write(16, value, &acpi_gbl_xpm1b_enable);
		break;

	case ACPI_REGISTER_PM1_CONTROL:	/* 16-bit access */

		/*
		 * Perform a read first to preserve certain bits (per ACPI spec)
		 */
		status = acpi_hw_register_read(ACPI_REGISTER_PM1_CONTROL,
					       &read_value);
		if (ACPI_FAILURE(status)) {
			goto exit;
		}

		/* Insert the bits to be preserved */

		ACPI_INSERT_BITS(value, ACPI_PM1_CONTROL_PRESERVED_BITS,
				 read_value);

		/* Now we can write the data */

		status =
		    acpi_hw_low_level_write(16, value,
					    &acpi_gbl_FADT.xpm1a_control_block);
		if (ACPI_FAILURE(status)) {
			goto exit;
		}

		status =
		    acpi_hw_low_level_write(16, value,
					    &acpi_gbl_FADT.xpm1b_control_block);
		break;

	case ACPI_REGISTER_PM1A_CONTROL:	/* 16-bit access */

		status =
		    acpi_hw_low_level_write(16, value,
					    &acpi_gbl_FADT.xpm1a_control_block);
		break;

	case ACPI_REGISTER_PM1B_CONTROL:	/* 16-bit access */

		status =
		    acpi_hw_low_level_write(16, value,
					    &acpi_gbl_FADT.xpm1b_control_block);
		break;

	case ACPI_REGISTER_PM2_CONTROL:	/* 8-bit access */

		status =
		    acpi_hw_low_level_write(8, value,
					    &acpi_gbl_FADT.xpm2_control_block);
		break;

	case ACPI_REGISTER_PM_TIMER:	/* 32-bit access */

		status =
		    acpi_hw_low_level_write(32, value,
					    &acpi_gbl_FADT.xpm_timer_block);
		break;

	case ACPI_REGISTER_SMI_COMMAND_BLOCK:	/* 8-bit access */

		/* SMI_CMD is currently always in IO space */

		status =
		    acpi_os_write_port(acpi_gbl_FADT.smi_command, value, 8);
		break;

	default:
		status = AE_BAD_PARAMETER;
		break;
	}

      exit:
	return_ACPI_STATUS(status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_hw_low_level_read
 *
 * PARAMETERS:  Width               - 8, 16, or 32
 *              Value               - Where the value is returned
 *              Reg                 - GAS register structure
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read from either memory or IO space.
 *
 ******************************************************************************/

acpi_status
acpi_hw_low_level_read(u32 width, u32 * value, struct acpi_generic_address *reg)
{
	u64 address;
	acpi_status status;

	ACPI_FUNCTION_NAME(hw_low_level_read);

	/*
	 * Must have a valid pointer to a GAS structure, and
	 * a non-zero address within. However, don't return an error
	 * because the PM1A/B code must not fail if B isn't present.
	 */
	if (!reg) {
		return (AE_OK);
	}

	/* Get a local copy of the address. Handles possible alignment issues */

	ACPI_MOVE_64_TO_64(&address, &reg->address);
	if (!address) {
		return (AE_OK);
	}
	*value = 0;

	/*
	 * Two address spaces supported: Memory or IO.
	 * PCI_Config is not supported here because the GAS struct is insufficient
	 */
	switch (reg->space_id) {
	case ACPI_ADR_SPACE_SYSTEM_MEMORY:

		status = acpi_os_read_memory((acpi_physical_address) address,
					     value, width);
		break;

	case ACPI_ADR_SPACE_SYSTEM_IO:

		status = acpi_os_read_port((acpi_io_address) address,
					   value, width);
		break;

	default:

		return (AE_BAD_PARAMETER);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_IO,
			  "Read:  %8.8X width %2d from %8.8X%8.8X (%s)\n",
			  *value, width,
			  ACPI_FORMAT_UINT64(address),
			  acpi_ut_get_region_name(reg->address_space_id)));

	return (status);
}

/******************************************************************************
 *
 * FUNCTION:    acpi_hw_low_level_write
 *
 * PARAMETERS:  Width               - 8, 16, or 32
 *              Value               - To be written
 *              Reg                 - GAS register structure
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Write to either memory or IO space.
 *
 ******************************************************************************/

acpi_status
acpi_hw_low_level_write(u32 width, u32 value, struct acpi_generic_address * reg)
{
	u64 address;
	acpi_status status;

	ACPI_FUNCTION_NAME(hw_low_level_write);

	/*
	 * Must have a valid pointer to a GAS structure, and
	 * a non-zero address within. However, don't return an error
	 * because the PM1A/B code must not fail if B isn't present.
	 */
	if (!reg) {
		return (AE_OK);
	}

	/* Get a local copy of the address. Handles possible alignment issues */

	ACPI_MOVE_64_TO_64(&address, &reg->address);
	if (!address) {
		return (AE_OK);
	}

	/*
	 * Two address spaces supported: Memory or IO.
	 * PCI_Config is not supported here because the GAS struct is insufficient
	 */
	switch (reg->space_id) {
	case ACPI_ADR_SPACE_SYSTEM_MEMORY:

		status = acpi_os_write_memory((acpi_physical_address) address,
					      value, width);
		break;

	case ACPI_ADR_SPACE_SYSTEM_IO:

		status = acpi_os_write_port((acpi_io_address) address,
					    value, width);
		break;

	default:
		return (AE_BAD_PARAMETER);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_IO,
			  "Wrote: %8.8X width %2d   to %8.8X%8.8X (%s)\n",
			  value, width,
			  ACPI_FORMAT_UINT64(address),
			  acpi_ut_get_region_name(reg->address_space_id)));

	return (status);
}

