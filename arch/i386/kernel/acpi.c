/*
 *  acpi.c - Architecture-Specific Low-Level ACPI Support
 *
 *  Copyright (C) 2001 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2001 Jun Nakajima <jun.nakajima@intel.com>
 *  Copyright (C) 2001 Patrick Mochel <mochel@osdl.org>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/bootmem.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/acpi.h>
#include <asm/mpspec.h>
#include <asm/io.h>
#include <asm/apic.h>
#include <asm/apicdef.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/io_apic.h>
#include <asm/tlbflush.h>


#define PREFIX			"ACPI: "


/* --------------------------------------------------------------------------
                              Boot-time Configuration
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI_BOOT

/*
 * Use reserved fixmap pages for physical-to-virtual mappings of ACPI tables.
 * Note that the same range is used for each table, so tables that need to
 * persist should be memcpy'd.
 */
char *
__acpi_map_table (
	unsigned long	phys_addr,
	unsigned long	size)
{
	unsigned long	base = 0;
	unsigned long	mapped_phys = phys_addr;
	unsigned long	offset = phys_addr & (PAGE_SIZE - 1);
	unsigned long	mapped_size = PAGE_SIZE - offset;
	unsigned long	avail_size = mapped_size + (PAGE_SIZE * FIX_ACPI_PAGES);
	int		idx = FIX_ACPI_BEGIN;

	if (!phys_addr || !size)
		return NULL;

	base = fix_to_virt(FIX_ACPI_BEGIN);

	set_fixmap(idx, mapped_phys);

	if (size > avail_size)
		return NULL;

	/* If the table doesn't map completely into the fist page... */
	if (size > mapped_size) {
		do {
			/* Make sure we don't go past our range */
			if (idx++ == FIX_ACPI_END)
				return NULL;
			mapped_phys = mapped_phys + PAGE_SIZE;
			set_fixmap(idx, mapped_phys);
			mapped_size = mapped_size + PAGE_SIZE;
		} while (mapped_size < size);
	}

	return ((unsigned char *) base + offset);
}


#ifdef CONFIG_X86_LOCAL_APIC

static int total_cpus __initdata = 0;

/* From mpparse.c */
extern void __init MP_processor_info(struct mpc_config_processor *);

int __init
acpi_parse_lapic (
	acpi_table_entry_header *header)
{
	struct acpi_table_lapic	*cpu = NULL;
	struct mpc_config_processor proc_entry;

	cpu = (struct acpi_table_lapic*) header;
	if (!cpu)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	if (!cpu->flags.enabled) {
		printk(KERN_INFO "Processor #%d disabled\n", cpu->id);
		return 0;
	}

	if (cpu->id >= MAX_APICS) {
		printk(KERN_WARNING "Processor #%d invalid (max %d)\n",
			cpu->id, MAX_APICS);
		return -ENODEV;
	}

	/*
	 * Fill in the info we want to save.  Not concerned about
	 * the processor ID.  Processor features aren't present in
	 * the table.
	 */
	proc_entry.mpc_type = MP_PROCESSOR;
	proc_entry.mpc_apicid = cpu->id;
	proc_entry.mpc_cpuflag = CPU_ENABLED;
	if (cpu->id == boot_cpu_physical_apicid) {
		/* TBD: Circular reference trying to establish BSP */
		proc_entry.mpc_cpuflag |= CPU_BOOTPROCESSOR;
	}
	proc_entry.mpc_cpufeature = (boot_cpu_data.x86 << 8)
		| (boot_cpu_data.x86_model << 4) | boot_cpu_data.x86_mask;
	proc_entry.mpc_featureflag = boot_cpu_data.x86_capability[0];
	proc_entry.mpc_reserved[0] = 0;
	proc_entry.mpc_reserved[1] = 0;
	proc_entry.mpc_apicver = 0x10;	/* integrated APIC */

	MP_processor_info(&proc_entry);

	total_cpus++;

	return 0;
}


int __init
acpi_parse_lapic_addr_ovr (
	acpi_table_entry_header *header)
{
	struct acpi_table_lapic_addr_ovr *lapic_addr_ovr = NULL;

	lapic_addr_ovr = (struct acpi_table_lapic_addr_ovr*) header;
	if (!lapic_addr_ovr)
		return -EINVAL;

	/* TBD: Support local APIC address override entries */

	return 0;
}


int __init
acpi_parse_lapic_nmi (
	acpi_table_entry_header *header)
{
	struct acpi_table_lapic_nmi *lacpi_nmi = NULL;

	lacpi_nmi = (struct acpi_table_lapic_nmi*) header;
	if (!lacpi_nmi)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/* TBD: Support local APIC NMI entries */

	return 0;
}

#endif /*CONFIG_X86_LOCAL_APIC*/


#ifdef CONFIG_X86_IO_APIC

int __init
acpi_parse_ioapic (
	acpi_table_entry_header *header)
{
	struct acpi_table_ioapic *ioapic = NULL;
	
	ioapic = (struct acpi_table_ioapic*) header;
	if (!ioapic)
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/* TBD: Support ioapic entries */

	return 0;
}

#endif /*CONFIG_X86_IO_APIC*/


int __init
acpi_parse_madt (
	unsigned long		phys_addr,
	unsigned long		size)
{
	struct acpi_table_madt	*madt = NULL;

	if (!phys_addr || !size)
		return -EINVAL;

	madt = (struct acpi_table_madt *) __acpi_map_table(phys_addr, size);
	if (!madt) {
		printk(KERN_WARNING PREFIX "Unable to map MADT\n");
		return -ENODEV;
	}

#ifdef CONFIG_X86_LOCAL_APIC
	if (madt->lapic_address)
		mp_lapic_addr = madt->lapic_address;
	else
		mp_lapic_addr = APIC_DEFAULT_PHYS_BASE;
#endif /*CONFIG_X86_LOCAL_APIC*/

	printk(KERN_INFO PREFIX "Local APIC address 0x%08x\n",
		madt->lapic_address);

	return 0;
}


static unsigned long __init
acpi_scan_rsdp (
	unsigned long		start,
	unsigned long		length)
{
	unsigned long		offset = 0;
	unsigned long		sig_len = sizeof("RSD PTR ") - 1;

	/*
	 * Scan all 16-byte boundaries of the physical memory region for the
	 * RSDP signature.
	 */
	for (offset = 0; offset < length; offset += 16) {
		if (0 != strncmp((char *) (start + offset), "RSD PTR ", sig_len))
			continue;
		return (start + offset);
	}

	return 0;
}


int __init
acpi_find_rsdp (
	unsigned long		*rsdp_phys)
{
	if (!rsdp_phys)
		return -EINVAL;

	/*
	 * Scan memory looking for the RSDP signature. First search EBDA (low
	 * memory) paragraphs and then search upper memory (E0000-FFFFF).
	 */
	(*rsdp_phys) = acpi_scan_rsdp (0, 0x400);
	if (!(*rsdp_phys))
		(*rsdp_phys) = acpi_scan_rsdp (0xE0000, 0xFFFFF);

	if (!(*rsdp_phys))
		return -ENODEV;

	return 0;
}


#endif /*CONFIG_ACPI_BOOT*/


/* --------------------------------------------------------------------------
                            PCI Interrupt Routing Support
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI_PCI
int __init
acpi_get_interrupt_model (
	int		*type)
{
	if (!type)
		return -EINVAL;

#ifdef CONFIG_X86_IO_APIC
	if (io_apic_assign_pci_irqs)
		*type = ACPI_PCI_ROUTING_IOAPIC;
	else
#endif
		*type = ACPI_PCI_ROUTING_PIC;

	return 0;
}
#endif


/* --------------------------------------------------------------------------
                              Low-Level Sleep Support
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI_SLEEP

#define DEBUG

#ifdef DEBUG
#include <linux/serial.h>
#endif

/* address in low memory of the wakeup routine. */
unsigned long acpi_wakeup_address = 0;

/* new page directory that we will be using */
static pmd_t *pmd;

/* saved page directory */
static pmd_t saved_pmd;

/* page which we'll use for the new page directory */
static pte_t *ptep;

extern unsigned long FASTCALL(acpi_copy_wakeup_routine(unsigned long));

/*
 * acpi_create_identity_pmd
 *
 * Create a new, identity mapped pmd.
 *
 * Do this by creating new page directory, and marking all the pages as R/W
 * Then set it as the new Page Middle Directory.
 * And, of course, flush the TLB so it takes effect.
 *
 * We save the address of the old one, for later restoration.
 */
static void acpi_create_identity_pmd (void)
{
	pgd_t *pgd;
	int i;

	ptep = (pte_t*)__get_free_page(GFP_KERNEL);

	/* fill page with low mapping */
	for (i = 0; i < PTRS_PER_PTE; i++)
		set_pte(ptep + i, mk_pte_phys(i << PAGE_SHIFT, PAGE_SHARED));

	pgd = pgd_offset(current->active_mm, 0);
	pmd = pmd_alloc(current->mm,pgd, 0);

	/* save the old pmd */
	saved_pmd = *pmd;

	/* set the new one */
	set_pmd(pmd, __pmd(_PAGE_TABLE + __pa(ptep)));

	/* flush the TLB */
	local_flush_tlb();
}

/*
 * acpi_restore_pmd
 *
 * Restore the old pmd saved by acpi_create_identity_pmd and
 * free the page that said function alloc'd
 */
static void acpi_restore_pmd (void)
{
	set_pmd(pmd, saved_pmd);
	local_flush_tlb();
	free_page((unsigned long)ptep);
}

/**
 * acpi_save_state_mem - save kernel state
 *
 * Create an identity mapped page table and copy the wakeup routine to
 * low memory.
 */
int acpi_save_state_mem (void)
{
	acpi_create_identity_pmd();
	acpi_copy_wakeup_routine(acpi_wakeup_address);

	return 0;
}

/**
 * acpi_save_state_disk - save kernel state to disk
 *
 */
int acpi_save_state_disk (void)
{
	return 1;
}

/*
 * acpi_restore_state
 */
void acpi_restore_state_mem (void)
{
	acpi_restore_pmd();
}

/**
 * acpi_reserve_bootmem - do _very_ early ACPI initialisation
 *
 * We allocate a page in low memory for the wakeup
 * routine for when we come back from a sleep state. The
 * runtime allocator allows specification of <16M pages, but not
 * <1M pages.
 */
void __init acpi_reserve_bootmem(void)
{
	acpi_wakeup_address = (unsigned long)alloc_bootmem_low(PAGE_SIZE);
	printk(KERN_DEBUG "ACPI: have wakeup address 0x%8.8lx\n", acpi_wakeup_address);
}

#endif /*CONFIG_ACPI_SLEEP*/
