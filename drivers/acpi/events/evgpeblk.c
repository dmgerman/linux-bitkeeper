/******************************************************************************
 *
 * Module Name: evgpeblk - GPE block creation and initialization.
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2003, R. Byron Moore
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

#include <acpi/acpi.h>
#include <acpi/acevents.h>
#include <acpi/acnamesp.h>

#define _COMPONENT          ACPI_EVENTS
	 ACPI_MODULE_NAME    ("evgpe")


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_save_method_info
 *
 * PARAMETERS:  Callback from walk_namespace
 *
 * RETURN:      None
 *
 * DESCRIPTION: Called from acpi_walk_namespace. Expects each object to be a
 *              control method under the _GPE portion of the namespace.
 *              Extract the name and GPE type from the object, saving this
 *              information for quick lookup during GPE dispatch
 *
 *              The name of each GPE control method is of the form:
 *                  "_Lnn" or "_Enn"
 *              Where:
 *                  L      - means that the GPE is level triggered
 *                  E      - means that the GPE is edge triggered
 *                  nn     - is the GPE number [in HEX]
 *
 ******************************************************************************/

static acpi_status
acpi_ev_save_method_info (
	acpi_handle                     obj_handle,
	u32                             level,
	void                            *obj_desc,
	void                            **return_value)
{
	struct acpi_gpe_block_info      *gpe_block = (void *) obj_desc;
	struct acpi_gpe_event_info      *gpe_event_info;
	u32                             gpe_number;
	char                            name[ACPI_NAME_SIZE + 1];
	u8                              type;
	acpi_status                     status;


	ACPI_FUNCTION_NAME ("ev_save_method_info");


	/* Extract the name from the object and convert to a string */

	ACPI_MOVE_UNALIGNED32_TO_32 (name,
			 &((struct acpi_namespace_node *) obj_handle)->name.integer);
	name[ACPI_NAME_SIZE] = 0;

	/*
	 * Edge/Level determination is based on the 2nd character of the method name
	 */
	switch (name[1]) {
	case 'L':
		type = ACPI_EVENT_LEVEL_TRIGGERED;
		break;

	case 'E':
		type = ACPI_EVENT_EDGE_TRIGGERED;
		break;

	default:
		/* Unknown method type, just ignore it! */

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Unknown GPE method type: %s (name not of form _Lnn or _Enn)\n",
			name));
		return (AE_OK);
	}

	/* Convert the last two characters of the name to the GPE Number */

	gpe_number = ACPI_STRTOUL (&name[2], NULL, 16);
	if (gpe_number == ACPI_UINT32_MAX) {
		/* Conversion failed; invalid method, just ignore it */

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Could not extract GPE number from name: %s (name is not of form _Lnn or _Enn)\n",
			name));
		return (AE_OK);
	}

	/* Ensure that we have a valid GPE number for this GPE block */

	if ((gpe_number < gpe_block->block_base_number) ||
		(gpe_number >= (gpe_block->register_count * 8))) {
		/* Not valid, all we can do here is ignore it */

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"GPE number associated with method %s is not valid\n", name));
		return (AE_OK);
	}

	/*
	 * Now we can add this information to the gpe_event_info block
	 * for use during dispatch of this GPE.
	 */
	gpe_event_info = &gpe_block->event_info[gpe_number - gpe_block->block_base_number];

	gpe_event_info->type     = type;
	gpe_event_info->method_node = (struct acpi_namespace_node *) obj_handle;

	/*
	 * Enable the GPE (SCIs should be disabled at this point)
	 */
	status = acpi_hw_enable_gpe (gpe_event_info);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Registered GPE method %s as GPE number %2.2X\n",
		name, gpe_number));
	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_install_gpe_block
 *
 * PARAMETERS:  gpe_block   - New GPE block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install new GPE block with mutex support
 *
 ******************************************************************************/

acpi_status
acpi_ev_install_gpe_block (
	struct acpi_gpe_block_info      *gpe_block)
{
	struct acpi_gpe_block_info      *next_gpe_block;
	acpi_status                     status;


	status = acpi_ut_acquire_mutex (ACPI_MTX_EVENTS);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Install the new block at the end of the global list */

	if (acpi_gbl_gpe_block_list_head) {
		next_gpe_block = acpi_gbl_gpe_block_list_head;
		while (next_gpe_block->next) {
			next_gpe_block = next_gpe_block->next;
		}

		next_gpe_block->next = gpe_block;
		gpe_block->previous = next_gpe_block;
	}
	else {
		acpi_gbl_gpe_block_list_head = gpe_block;
	}

	status = acpi_ut_release_mutex (ACPI_MTX_EVENTS);
	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_create_gpe_info_blocks
 *
 * PARAMETERS:  gpe_block   - New GPE block
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create the register_info and event_info blocks for this GPE block
 *
 ******************************************************************************/

acpi_status
acpi_ev_create_gpe_info_blocks (
	struct acpi_gpe_block_info      *gpe_block)
{
	struct acpi_gpe_register_info   *gpe_register_info = NULL;
	struct acpi_gpe_event_info      *gpe_event_info = NULL;
	struct acpi_gpe_event_info      *this_event;
	struct acpi_gpe_register_info   *this_register;
	acpi_native_uint                i;
	acpi_native_uint                j;
	acpi_status                     status;


	ACPI_FUNCTION_TRACE ("ev_create_gpe_info_blocks");


	/* Allocate the GPE register information block */

	gpe_register_info = ACPI_MEM_CALLOCATE (
			  (acpi_size) gpe_block->register_count *
			  sizeof (struct acpi_gpe_register_info));
	if (!gpe_register_info) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Could not allocate the gpe_register_info table\n"));
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/*
	 * Allocate the GPE event_info block. There are eight distinct GPEs
	 * per register.  Initialization to zeros is sufficient.
	 */
	gpe_event_info = ACPI_MEM_CALLOCATE (
			   ((acpi_size) gpe_block->register_count * ACPI_GPE_REGISTER_WIDTH) *
			   sizeof (struct acpi_gpe_event_info));
	if (!gpe_event_info) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Could not allocate the gpe_event_info table\n"));
		status = AE_NO_MEMORY;
		goto error_exit;
	}

	/*
	 * Initialize the GPE Register and Event structures.  A goal of these
	 * tables is to hide the fact that there are two separate GPE register sets
	 * in a given gpe hardware block, the status registers occupy the first half,
	 * and the enable registers occupy the second half.  Another goal is to hide
	 * the fact that there may be multiple GPE hardware blocks.
	 */
	this_register = gpe_register_info;
	this_event   = gpe_event_info;

	for (i = 0; i < gpe_block->register_count; i++) {
		/* Init the register_info for this GPE register (8 GPEs) */

		this_register->base_gpe_number = (u8) (gpe_block->block_base_number +
				   (i * ACPI_GPE_REGISTER_WIDTH));

		ACPI_STORE_ADDRESS (this_register->status_address.address,
				 (gpe_block->block_address.address
				 + i));

		ACPI_STORE_ADDRESS (this_register->enable_address.address,
				 (gpe_block->block_address.address
				 + i
				 + gpe_block->register_count));

		this_register->status_address.address_space_id = gpe_block->block_address.address_space_id;
		this_register->enable_address.address_space_id = gpe_block->block_address.address_space_id;
		this_register->status_address.register_bit_width = ACPI_GPE_REGISTER_WIDTH;
		this_register->enable_address.register_bit_width = ACPI_GPE_REGISTER_WIDTH;
		this_register->status_address.register_bit_offset = ACPI_GPE_REGISTER_WIDTH;
		this_register->enable_address.register_bit_offset = ACPI_GPE_REGISTER_WIDTH;

		/* Init the event_info for each GPE within this register */

		for (j = 0; j < ACPI_GPE_REGISTER_WIDTH; j++) {
			this_event->bit_mask = acpi_gbl_decode_to8bit[j];
			this_event->register_info = this_register;
			this_event++;
		}

		/*
		 * Clear the status/enable registers.  Note that status registers
		 * are cleared by writing a '1', while enable registers are cleared
		 * by writing a '0'.
		 */
		status = acpi_hw_low_level_write (ACPI_GPE_REGISTER_WIDTH, 0x00,
				 &this_register->enable_address, 0);
		if (ACPI_FAILURE (status)) {
			goto error_exit;
		}

		status = acpi_hw_low_level_write (ACPI_GPE_REGISTER_WIDTH, 0xFF,
				 &this_register->status_address, 0);
		if (ACPI_FAILURE (status)) {
			goto error_exit;
		}

		this_register++;
	}

	gpe_block->register_info = gpe_register_info;
	gpe_block->event_info  = gpe_event_info;

	return_ACPI_STATUS (AE_OK);


error_exit:

	if (gpe_register_info) {
		ACPI_MEM_FREE (gpe_register_info);
	}
	if (gpe_event_info) {
		ACPI_MEM_FREE (gpe_event_info);
	}

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_create_gpe_block
 *
 * PARAMETERS:  TBD
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create and Install a block of GPE registers
 *
 ******************************************************************************/

acpi_status
acpi_ev_create_gpe_block (
	char                            *pathname,
	struct acpi_generic_address     *gpe_block_address,
	u32                             register_count,
	u8                              gpe_block_base_number,
	u32                             interrupt_level)
{
	struct acpi_gpe_block_info      *gpe_block;
	acpi_status                     status;
	acpi_handle                     obj_handle;


	ACPI_FUNCTION_TRACE ("ev_create_gpe_block");


	if (!register_count) {
		return_ACPI_STATUS (AE_OK);
	}

	/* Get a handle to the parent object for this GPE block */

	status = acpi_get_handle (NULL, pathname, &obj_handle);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Allocate a new GPE block */

	gpe_block = ACPI_MEM_CALLOCATE (sizeof (struct acpi_gpe_block_info));
	if (!gpe_block) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/* Initialize the new GPE block */

	gpe_block->register_count = register_count;
	gpe_block->block_base_number = gpe_block_base_number;

	ACPI_MEMCPY (&gpe_block->block_address, gpe_block_address, sizeof (struct acpi_generic_address));

	/* Create the register_info and event_info sub-structures */

	status = acpi_ev_create_gpe_info_blocks (gpe_block);
	if (ACPI_FAILURE (status)) {
		ACPI_MEM_FREE (gpe_block);
		return_ACPI_STATUS (status);
	}

	/* Install the new block in the global list(s) */
	/* TBD: Install block in the interrupt handler list */

	status = acpi_ev_install_gpe_block (gpe_block);
	if (ACPI_FAILURE (status)) {
		ACPI_MEM_FREE (gpe_block);
		return_ACPI_STATUS (status);
	}

	/* Dump info about this GPE block */

	ACPI_DEBUG_PRINT ((ACPI_DB_INIT, "GPE Block: %X registers at %8.8X%8.8X\n",
		gpe_block->register_count,
		ACPI_HIDWORD (gpe_block->block_address.address),
		ACPI_LODWORD (gpe_block->block_address.address)));

	ACPI_DEBUG_PRINT ((ACPI_DB_INIT, "GPE Block defined as GPE%d to GPE%d\n",
		gpe_block->block_base_number,
		(u32) (gpe_block->block_base_number +
				((gpe_block->register_count * ACPI_GPE_REGISTER_WIDTH) -1))));

	/* Find all GPE methods (_Lxx, _Exx) for this block */

	status = acpi_walk_namespace (ACPI_TYPE_METHOD, obj_handle,
			  ACPI_UINT32_MAX, acpi_ev_save_method_info,
			  gpe_block, NULL);

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_gpe_initialize
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize the GPE data structures
 *
 ******************************************************************************/

acpi_status
acpi_ev_gpe_initialize (void)
{
	u32                             register_count0 = 0;
	u32                             register_count1 = 0;
	u32                             gpe_number_max = 0;


	ACPI_FUNCTION_TRACE ("ev_gpe_initialize");


	/*
	 * Initialize the GPE Blocks defined in the FADT
	 *
	 * Why the GPE register block lengths are divided by 2:  From the ACPI Spec,
	 * section "General-Purpose Event Registers", we have:
	 *
	 * "Each register block contains two registers of equal length
	 *  GPEx_STS and GPEx_EN (where x is 0 or 1). The length of the
	 *  GPE0_STS and GPE0_EN registers is equal to half the GPE0_LEN
	 *  The length of the GPE1_STS and GPE1_EN registers is equal to
	 *  half the GPE1_LEN. If a generic register block is not supported
	 *  then its respective block pointer and block length values in the
	 *  FADT table contain zeros. The GPE0_LEN and GPE1_LEN do not need
	 *  to be the same size."
	 */

	/*
	 * Determine the maximum GPE number for this machine.
	 *
	 * Note: both GPE0 and GPE1 are optional, and either can exist without
	 * the other.
	 * If EITHER the register length OR the block address are zero, then that
	 * particular block is not supported.
	 */
	if (acpi_gbl_FADT->gpe0_blk_len &&
		acpi_gbl_FADT->xgpe0_blk.address) {
		/* GPE block 0 exists (has both length and address > 0) */

		register_count0 = (u16) (acpi_gbl_FADT->gpe0_blk_len / 2);

		gpe_number_max = (register_count0 * ACPI_GPE_REGISTER_WIDTH) - 1;

		acpi_ev_create_gpe_block ("\\_GPE", &acpi_gbl_FADT->xgpe0_blk,
			register_count0, 0, acpi_gbl_FADT->sci_int);
	}

	if (acpi_gbl_FADT->gpe1_blk_len &&
		acpi_gbl_FADT->xgpe1_blk.address) {
		/* GPE block 1 exists (has both length and address > 0) */

		register_count1 = (u16) (acpi_gbl_FADT->gpe1_blk_len / 2);

		/* Check for GPE0/GPE1 overlap (if both banks exist) */

		if ((register_count0) &&
			(gpe_number_max >= acpi_gbl_FADT->gpe1_base)) {
			ACPI_REPORT_ERROR ((
				"GPE0 block (GPE 0 to %d) overlaps the GPE1 block (GPE %d to %d) - Ignoring GPE1\n",
				gpe_number_max, acpi_gbl_FADT->gpe1_base,
				acpi_gbl_FADT->gpe1_base +
				((register_count1 * ACPI_GPE_REGISTER_WIDTH) - 1)));

			/* Ignore GPE1 block by setting the register count to zero */

			register_count1 = 0;
		}
		else {
			acpi_ev_create_gpe_block ("\\_GPE", &acpi_gbl_FADT->xgpe1_blk,
				register_count1, acpi_gbl_FADT->gpe1_base, acpi_gbl_FADT->sci_int);

			/*
			 * GPE0 and GPE1 do not have to be contiguous in the GPE number space,
			 * But, GPE0 always starts at zero.
			 */
			gpe_number_max = acpi_gbl_FADT->gpe1_base +
					   ((register_count1 * ACPI_GPE_REGISTER_WIDTH) - 1);
		}
	}

	/* Exit if there are no GPE registers */

	if ((register_count0 + register_count1) == 0) {
		/* GPEs are not required by ACPI, this is OK */

		ACPI_REPORT_INFO (("There are no GPE blocks defined in the FADT\n"));
		return_ACPI_STATUS (AE_OK);
	}

	/* Check for Max GPE number out-of-range */

	if (gpe_number_max > ACPI_GPE_MAX) {
		ACPI_REPORT_ERROR (("Maximum GPE number from FADT is too large: 0x%X\n",
			gpe_number_max));
		return_ACPI_STATUS (AE_BAD_VALUE);
	}

	return_ACPI_STATUS (AE_OK);
}

