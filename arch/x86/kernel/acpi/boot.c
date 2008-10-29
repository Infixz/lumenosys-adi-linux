/*
 *  boot.c - Architecture-Specific Low-Level ACPI Boot Support
 *
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2001 Jun Nakajima <jun.nakajima@intel.com>
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

#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/acpi_pmtmr.h>
#include <linux/efi.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/irq.h>
#include <linux/bootmem.h>
#include <linux/ioport.h>

#include <asm/pgtable.h>
#include <asm/io_apic.h>
#include <asm/apic.h>
#include <asm/genapic.h>
#include <asm/io.h>
#include <asm/mpspec.h>
#include <asm/smp.h>

#ifdef CONFIG_X86_LOCAL_APIC
# include <mach_apic.h>
#endif

static int __initdata acpi_force = 0;

#ifdef	CONFIG_ACPI
int acpi_disabled = 0;
#else
int acpi_disabled = 1;
#endif
EXPORT_SYMBOL(acpi_disabled);

#ifdef	CONFIG_X86_64

#include <asm/proto.h>

#else				/* X86 */

#ifdef	CONFIG_X86_LOCAL_APIC
#include <mach_apic.h>
#include <mach_mpparse.h>
#endif				/* CONFIG_X86_LOCAL_APIC */

#endif				/* X86 */

#define BAD_MADT_ENTRY(entry, end) (					    \
		(!entry) || (unsigned long)entry + sizeof(*entry) > end ||  \
		((struct acpi_subtable_header *)entry)->length < sizeof(*entry))

#define PREFIX			"ACPI: "

int acpi_noirq;				/* skip ACPI IRQ initialization */
int acpi_pci_disabled;		/* skip ACPI PCI scan and IRQ initialization */
EXPORT_SYMBOL(acpi_pci_disabled);
int acpi_ht __initdata = 1;	/* enable HT */

int acpi_lapic;
int acpi_ioapic;
int acpi_strict;

u8 acpi_sci_flags __initdata;
int acpi_sci_override_gsi __initdata;
int acpi_skip_timer_override __initdata;
int acpi_use_timer_override __initdata;

#ifdef CONFIG_X86_LOCAL_APIC
static u64 acpi_lapic_addr __initdata = APIC_DEFAULT_PHYS_BASE;
#endif

#ifndef __HAVE_ARCH_CMPXCHG
#warning ACPI uses CMPXCHG, i486 and later hardware
#endif

/* --------------------------------------------------------------------------
                              Boot-time Configuration
   -------------------------------------------------------------------------- */

/*
 * The default interrupt routing model is PIC (8259).  This gets
 * overridden if IOAPICs are enumerated (below).
 */
enum acpi_irq_model_id acpi_irq_model = ACPI_IRQ_MODEL_PIC;


/*
 * Temporarily use the virtual area starting from FIX_IO_APIC_BASE_END,
 * to map the target physical address. The problem is that set_fixmap()
 * provides a single page, and it is possible that the page is not
 * sufficient.
 * By using this area, we can map up to MAX_IO_APICS pages temporarily,
 * i.e. until the next __va_range() call.
 *
 * Important Safety Note:  The fixed I/O APIC page numbers are *subtracted*
 * from the fixed base.  That's why we start at FIX_IO_APIC_BASE_END and
 * count idx down while incrementing the phys address.
 */
char *__init __acpi_map_table(unsigned long phys, unsigned long size)
{
	unsigned long base, offset, mapped_size;
	int idx;

	if (!phys || !size)
		return NULL;

	if (phys+size <= (max_low_pfn_mapped << PAGE_SHIFT))
		return __va(phys);

	offset = phys & (PAGE_SIZE - 1);
	mapped_size = PAGE_SIZE - offset;
	clear_fixmap(FIX_ACPI_END);
	set_fixmap(FIX_ACPI_END, phys);
	base = fix_to_virt(FIX_ACPI_END);

	/*
	 * Most cases can be covered by the below.
	 */
	idx = FIX_ACPI_END;
	while (mapped_size < size) {
		if (--idx < FIX_ACPI_BEGIN)
			return NULL;	/* cannot handle this */
		phys += PAGE_SIZE;
		clear_fixmap(idx);
		set_fixmap(idx, phys);
		mapped_size += PAGE_SIZE;
	}

	return ((unsigned char *)base + offset);
}

#ifdef CONFIG_PCI_MMCONFIG

static int acpi_mcfg_64bit_base_addr __initdata = FALSE;

/* The physical address of the MMCONFIG aperture.  Set from ACPI tables. */
struct acpi_mcfg_allocation *pci_mmcfg_config;
int pci_mmcfg_config_num;

static int __init acpi_mcfg_oem_check(struct acpi_table_mcfg *mcfg)
{
	if (!strcmp(mcfg->header.oem_id, "SGI"))
		acpi_mcfg_64bit_base_addr = TRUE;

	return 0;
}

int __init acpi_parse_mcfg(struct acpi_table_header *header)
{
	struct acpi_table_mcfg *mcfg;
	unsigned long i;
	int config_size;

	if (!header)
		return -EINVAL;

	mcfg = (struct acpi_table_mcfg *)header;

	/* how many config structures do we have */
	pci_mmcfg_config_num = 0;
	i = header->length - sizeof(struct acpi_table_mcfg);
	while (i >= sizeof(struct acpi_mcfg_allocation)) {
		++pci_mmcfg_config_num;
		i -= sizeof(struct acpi_mcfg_allocation);
	};
	if (pci_mmcfg_config_num == 0) {
		printk(KERN_ERR PREFIX "MMCONFIG has no entries\n");
		return -ENODEV;
	}

	config_size = pci_mmcfg_config_num * sizeof(*pci_mmcfg_config);
	pci_mmcfg_config = kmalloc(config_size, GFP_KERNEL);
	if (!pci_mmcfg_config) {
		printk(KERN_WARNING PREFIX
		       "No memory for MCFG config tables\n");
		return -ENOMEM;
	}

	memcpy(pci_mmcfg_config, &mcfg[1], config_size);

	acpi_mcfg_oem_check(mcfg);

	for (i = 0; i < pci_mmcfg_config_num; ++i) {
		if ((pci_mmcfg_config[i].address > 0xFFFFFFFF) &&
		    !acpi_mcfg_64bit_base_addr) {
			printk(KERN_ERR PREFIX
			       "MMCONFIG not in low 4GB of memory\n");
			kfree(pci_mmcfg_config);
			pci_mmcfg_config_num = 0;
			return -ENODEV;
		}
	}

	return 0;
}
#endif				/* CONFIG_PCI_MMCONFIG */

#ifdef CONFIG_X86_LOCAL_APIC
static int __init acpi_parse_madt(struct acpi_table_header *table)
{
	struct acpi_table_madt *madt = NULL;

	if (!cpu_has_apic)
		return -EINVAL;

	madt = (struct acpi_table_madt *)table;
	if (!madt) {
		printk(KERN_WARNING PREFIX "Unable to map MADT\n");
		return -ENODEV;
	}

	if (madt->address) {
		acpi_lapic_addr = (u64) madt->address;

		printk(KERN_DEBUG PREFIX "Local APIC address 0x%08x\n",
		       madt->address);
	}

	acpi_madt_oem_check(madt->header.oem_id, madt->header.oem_table_id);

	return 0;
}

static void __cpuinit acpi_register_lapic(int id, u8 enabled)
{
	unsigned int ver = 0;

	if (!enabled) {
		++disabled_cpus;
		return;
	}

	if (boot_cpu_physical_apicid != -1U)
		ver = apic_version[boot_cpu_physical_apicid];

	generic_processor_info(id, ver);
}

static int __init
acpi_parse_lapic(struct acpi_subtable_header * header, const unsigned long end)
{
	struct acpi_madt_local_apic *processor = NULL;

	processor = (struct acpi_madt_local_apic *)header;

	if (BAD_MADT_ENTRY(processor, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/*
	 * We need to register disabled CPU as well to permit
	 * counting disabled CPUs. This allows us to size
	 * cpus_possible_map more accurately, to permit
	 * to not preallocating memory for all NR_CPUS
	 * when we use CPU hotplug.
	 */
	acpi_register_lapic(processor->id,	/* APIC ID */
			    processor->lapic_flags & ACPI_MADT_ENABLED);

	return 0;
}

static int __init
acpi_parse_sapic(struct acpi_subtable_header *header, const unsigned long end)
{
	struct acpi_madt_local_sapic *processor = NULL;

	processor = (struct acpi_madt_local_sapic *)header;

	if (BAD_MADT_ENTRY(processor, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	acpi_register_lapic((processor->id << 8) | processor->eid,/* APIC ID */
			    processor->lapic_flags & ACPI_MADT_ENABLED);

	return 0;
}

static int __init
acpi_parse_lapic_addr_ovr(struct acpi_subtable_header * header,
			  const unsigned long end)
{
	struct acpi_madt_local_apic_override *lapic_addr_ovr = NULL;

	lapic_addr_ovr = (struct acpi_madt_local_apic_override *)header;

	if (BAD_MADT_ENTRY(lapic_addr_ovr, end))
		return -EINVAL;

	acpi_lapic_addr = lapic_addr_ovr->address;

	return 0;
}

static int __init
acpi_parse_lapic_nmi(struct acpi_subtable_header * header, const unsigned long end)
{
	struct acpi_madt_local_apic_nmi *lapic_nmi = NULL;

	lapic_nmi = (struct acpi_madt_local_apic_nmi *)header;

	if (BAD_MADT_ENTRY(lapic_nmi, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	if (lapic_nmi->lint != 1)
		printk(KERN_WARNING PREFIX "NMI not connected to LINT 1!\n");

	return 0;
}

#endif				/*CONFIG_X86_LOCAL_APIC */

#ifdef CONFIG_X86_IO_APIC

static int __init
acpi_parse_ioapic(struct acpi_subtable_header * header, const unsigned long end)
{
	struct acpi_madt_io_apic *ioapic = NULL;

	ioapic = (struct acpi_madt_io_apic *)header;

	if (BAD_MADT_ENTRY(ioapic, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	mp_register_ioapic(ioapic->id,
			   ioapic->address, ioapic->global_irq_base);

	return 0;
}

/*
 * Parse Interrupt Source Override for the ACPI SCI
 */
static void __init acpi_sci_ioapic_setup(u32 gsi, u16 polarity, u16 trigger)
{
	if (trigger == 0)	/* compatible SCI trigger is level */
		trigger = 3;

	if (polarity == 0)	/* compatible SCI polarity is low */
		polarity = 3;

	/* Command-line over-ride via acpi_sci= */
	if (acpi_sci_flags & ACPI_MADT_TRIGGER_MASK)
		trigger = (acpi_sci_flags & ACPI_MADT_TRIGGER_MASK) >> 2;

	if (acpi_sci_flags & ACPI_MADT_POLARITY_MASK)
		polarity = acpi_sci_flags & ACPI_MADT_POLARITY_MASK;

	/*
	 * mp_config_acpi_legacy_irqs() already setup IRQs < 16
	 * If GSI is < 16, this will update its flags,
	 * else it will create a new mp_irqs[] entry.
	 */
	mp_override_legacy_irq(gsi, polarity, trigger, gsi);

	/*
	 * stash over-ride to indicate we've been here
	 * and for later update of acpi_gbl_FADT
	 */
	acpi_sci_override_gsi = gsi;
	return;
}

static int __init
acpi_parse_int_src_ovr(struct acpi_subtable_header * header,
		       const unsigned long end)
{
	struct acpi_madt_interrupt_override *intsrc = NULL;

	intsrc = (struct acpi_madt_interrupt_override *)header;

	if (BAD_MADT_ENTRY(intsrc, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	if (intsrc->source_irq == acpi_gbl_FADT.sci_interrupt) {
		acpi_sci_ioapic_setup(intsrc->global_irq,
				      intsrc->inti_flags & ACPI_MADT_POLARITY_MASK,
				      (intsrc->inti_flags & ACPI_MADT_TRIGGER_MASK) >> 2);
		return 0;
	}

	if (acpi_skip_timer_override &&
	    intsrc->source_irq == 0 && intsrc->global_irq == 2) {
		printk(PREFIX "BIOS IRQ0 pin2 override ignored.\n");
		return 0;
	}

	mp_override_legacy_irq(intsrc->source_irq,
				intsrc->inti_flags & ACPI_MADT_POLARITY_MASK,
				(intsrc->inti_flags & ACPI_MADT_TRIGGER_MASK) >> 2,
				intsrc->global_irq);

	return 0;
}

static int __init
acpi_parse_nmi_src(struct acpi_subtable_header * header, const unsigned long end)
{
	struct acpi_madt_nmi_source *nmi_src = NULL;

	nmi_src = (struct acpi_madt_nmi_source *)header;

	if (BAD_MADT_ENTRY(nmi_src, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	/* TBD: Support nimsrc entries? */

	return 0;
}

#endif				/* CONFIG_X86_IO_APIC */

/*
 * acpi_pic_sci_set_trigger()
 *
 * use ELCR to set PIC-mode trigger type for SCI
 *
 * If a PIC-mode SCI is not recognized or gives spurious IRQ7's
 * it may require Edge Trigger -- use "acpi_sci=edge"
 *
 * Port 0x4d0-4d1 are ECLR1 and ECLR2, the Edge/Level Control Registers
 * for the 8259 PIC.  bit[n] = 1 means irq[n] is Level, otherwise Edge.
 * ECLR1 is IRQs 0-7 (IRQ 0, 1, 2 must be 0)
 * ECLR2 is IRQs 8-15 (IRQ 8, 13 must be 0)
 */

void __init acpi_pic_sci_set_trigger(unsigned int irq, u16 trigger)
{
	unsigned int mask = 1 << irq;
	unsigned int old, new;

	/* Real old ELCR mask */
	old = inb(0x4d0) | (inb(0x4d1) << 8);

	/*
	 * If we use ACPI to set PCI IRQs, then we should clear ELCR
	 * since we will set it correctly as we enable the PCI irq
	 * routing.
	 */
	new = acpi_noirq ? old : 0;

	/*
	 * Update SCI information in the ELCR, it isn't in the PCI
	 * routing tables..
	 */
	switch (trigger) {
	case 1:		/* Edge - clear */
		new &= ~mask;
		break;
	case 3:		/* Level - set */
		new |= mask;
		break;
	}

	if (old == new)
		return;

	printk(PREFIX "setting ELCR to %04x (from %04x)\n", new, old);
	outb(new, 0x4d0);
	outb(new >> 8, 0x4d1);
}

int acpi_gsi_to_irq(u32 gsi, unsigned int *irq)
{
	*irq = gsi;
	return 0;
}

/*
 * success: return IRQ number (>=0)
 * failure: return < 0
 */
int acpi_register_gsi(u32 gsi, int triggering, int polarity)
{
	unsigned int irq;
	unsigned int plat_gsi = gsi;

#ifdef CONFIG_PCI
	/*
	 * Make sure all (legacy) PCI IRQs are set as level-triggered.
	 */
	if (acpi_irq_model == ACPI_IRQ_MODEL_PIC) {
		if (triggering == ACPI_LEVEL_SENSITIVE)
			eisa_set_level_irq(gsi);
	}
#endif

#ifdef CONFIG_X86_IO_APIC
	if (acpi_irq_model == ACPI_IRQ_MODEL_IOAPIC) {
		plat_gsi = mp_register_gsi(gsi, triggering, polarity);
	}
#endif
	acpi_gsi_to_irq(plat_gsi, &irq);
	return irq;
}

/*
 *  ACPI based hotplug support for CPU
 */
#ifdef CONFIG_ACPI_HOTPLUG_CPU

static int __cpuinit _acpi_map_lsapic(acpi_handle handle, int *pcpu)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	struct acpi_madt_local_apic *lapic;
	cpumask_t tmp_map, new_map;
	u8 physid;
	int cpu;

	if (ACPI_FAILURE(acpi_evaluate_object(handle, "_MAT", NULL, &buffer)))
		return -EINVAL;

	if (!buffer.length || !buffer.pointer)
		return -EINVAL;

	obj = buffer.pointer;
	if (obj->type != ACPI_TYPE_BUFFER ||
	    obj->buffer.length < sizeof(*lapic)) {
		kfree(buffer.pointer);
		return -EINVAL;
	}

	lapic = (struct acpi_madt_local_apic *)obj->buffer.pointer;

	if (lapic->header.type != ACPI_MADT_TYPE_LOCAL_APIC ||
	    !(lapic->lapic_flags & ACPI_MADT_ENABLED)) {
		kfree(buffer.pointer);
		return -EINVAL;
	}

	physid = lapic->id;

	kfree(buffer.pointer);
	buffer.length = ACPI_ALLOCATE_BUFFER;
	buffer.pointer = NULL;

	tmp_map = cpu_present_map;
	acpi_register_lapic(physid, lapic->lapic_flags & ACPI_MADT_ENABLED);

	/*
	 * If mp_register_lapic successfully generates a new logical cpu
	 * number, then the following will get us exactly what was mapped
	 */
	cpus_andnot(new_map, cpu_present_map, tmp_map);
	if (cpus_empty(new_map)) {
		printk ("Unable to map lapic to logical cpu number\n");
		return -EINVAL;
	}

	cpu = first_cpu(new_map);

	*pcpu = cpu;
	return 0;
}

/* wrapper to silence section mismatch warning */
int __ref acpi_map_lsapic(acpi_handle handle, int *pcpu)
{
	return _acpi_map_lsapic(handle, pcpu);
}
EXPORT_SYMBOL(acpi_map_lsapic);

int acpi_unmap_lsapic(int cpu)
{
	per_cpu(x86_cpu_to_apicid, cpu) = -1;
	cpu_clear(cpu, cpu_present_map);
	num_processors--;

	return (0);
}

EXPORT_SYMBOL(acpi_unmap_lsapic);
#endif				/* CONFIG_ACPI_HOTPLUG_CPU */

int acpi_register_ioapic(acpi_handle handle, u64 phys_addr, u32 gsi_base)
{
	/* TBD */
	return -EINVAL;
}

EXPORT_SYMBOL(acpi_register_ioapic);

int acpi_unregister_ioapic(acpi_handle handle, u32 gsi_base)
{
	/* TBD */
	return -EINVAL;
}

EXPORT_SYMBOL(acpi_unregister_ioapic);

static int __init acpi_parse_sbf(struct acpi_table_header *table)
{
	struct acpi_table_boot *sb;

	sb = (struct acpi_table_boot *)table;
	if (!sb) {
		printk(KERN_WARNING PREFIX "Unable to map SBF\n");
		return -ENODEV;
	}

	sbf_port = sb->cmos_index;	/* Save CMOS port */

	return 0;
}

#ifdef CONFIG_HPET_TIMER
#include <asm/hpet.h>

static struct __initdata resource *hpet_res;

static int __init acpi_parse_hpet(struct acpi_table_header *table)
{
	struct acpi_table_hpet *hpet_tbl;

	hpet_tbl = (struct acpi_table_hpet *)table;
	if (!hpet_tbl) {
		printk(KERN_WARNING PREFIX "Unable to map HPET\n");
		return -ENODEV;
	}

	if (hpet_tbl->address.space_id != ACPI_SPACE_MEM) {
		printk(KERN_WARNING PREFIX "HPET timers must be located in "
		       "memory.\n");
		return -1;
	}

	hpet_address = hpet_tbl->address.address;

	/*
	 * Some broken BIOSes advertise HPET at 0x0. We really do not
	 * want to allocate a resource there.
	 */
	if (!hpet_address) {
		printk(KERN_WARNING PREFIX
		       "HPET id: %#x base: %#lx is invalid\n",
		       hpet_tbl->id, hpet_address);
		return 0;
	}
#ifdef CONFIG_X86_64
	/*
	 * Some even more broken BIOSes advertise HPET at
	 * 0xfed0000000000000 instead of 0xfed00000. Fix it up and add
	 * some noise:
	 */
	if (hpet_address == 0xfed0000000000000UL) {
		if (!hpet_force_user) {
			printk(KERN_WARNING PREFIX "HPET id: %#x "
			       "base: 0xfed0000000000000 is bogus\n "
			       "try hpet=force on the kernel command line to "
			       "fix it up to 0xfed00000.\n", hpet_tbl->id);
			hpet_address = 0;
			return 0;
		}
		printk(KERN_WARNING PREFIX
		       "HPET id: %#x base: 0xfed0000000000000 fixed up "
		       "to 0xfed00000.\n", hpet_tbl->id);
		hpet_address >>= 32;
	}
#endif
	printk(KERN_INFO PREFIX "HPET id: %#x base: %#lx\n",
	       hpet_tbl->id, hpet_address);

	/*
	 * Allocate and initialize the HPET firmware resource for adding into
	 * the resource tree during the lateinit timeframe.
	 */
#define HPET_RESOURCE_NAME_SIZE 9
	hpet_res = alloc_bootmem(sizeof(*hpet_res) + HPET_RESOURCE_NAME_SIZE);

	hpet_res->name = (void *)&hpet_res[1];
	hpet_res->flags = IORESOURCE_MEM;
	snprintf((char *)hpet_res->name, HPET_RESOURCE_NAME_SIZE, "HPET %u",
		 hpet_tbl->sequence);

	hpet_res->start = hpet_address;
	hpet_res->end = hpet_address + (1 * 1024) - 1;

	return 0;
}

/*
 * hpet_insert_resource inserts the HPET resources used into the resource
 * tree.
 */
static __init int hpet_insert_resource(void)
{
	if (!hpet_res)
		return 1;

	return insert_resource(&iomem_resource, hpet_res);
}

late_initcall(hpet_insert_resource);

#else
#define	acpi_parse_hpet	NULL
#endif

static int __init acpi_parse_fadt(struct acpi_table_header *table)
{

#ifdef CONFIG_X86_PM_TIMER
	/* detect the location of the ACPI PM Timer */
	if (acpi_gbl_FADT.header.revision >= FADT2_REVISION_ID) {
		/* FADT rev. 2 */
		if (acpi_gbl_FADT.xpm_timer_block.space_id !=
		    ACPI_ADR_SPACE_SYSTEM_IO)
			return 0;

		pmtmr_ioport = acpi_gbl_FADT.xpm_timer_block.address;
		/*
		 * "X" fields are optional extensions to the original V1.0
		 * fields, so we must selectively expand V1.0 fields if the
		 * corresponding X field is zero.
	 	 */
		if (!pmtmr_ioport)
			pmtmr_ioport = acpi_gbl_FADT.pm_timer_block;
	} else {
		/* FADT rev. 1 */
		pmtmr_ioport = acpi_gbl_FADT.pm_timer_block;
	}
	if (pmtmr_ioport)
		printk(KERN_INFO PREFIX "PM-Timer IO Port: %#x\n",
		       pmtmr_ioport);
#endif
	return 0;
}

#ifdef	CONFIG_X86_LOCAL_APIC
/*
 * Parse LAPIC entries in MADT
 * returns 0 on success, < 0 on error
 */

static void __init acpi_register_lapic_address(unsigned long address)
{
	mp_lapic_addr = address;

	set_fixmap_nocache(FIX_APIC_BASE, address);
	if (boot_cpu_physical_apicid == -1U) {
		boot_cpu_physical_apicid  = read_apic_id();
		apic_version[boot_cpu_physical_apicid] =
			 GET_APIC_VERSION(apic_read(APIC_LVR));
	}
}

static int __init early_acpi_parse_madt_lapic_addr_ovr(void)
{
	int count;

	if (!cpu_has_apic)
		return -ENODEV;

	/*
	 * Note that the LAPIC address is obtained from the MADT (32-bit value)
	 * and (optionally) overriden by a LAPIC_ADDR_OVR entry (64-bit value).
	 */

	count =
	    acpi_table_parse_madt(ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE,
				  acpi_parse_lapic_addr_ovr, 0);
	if (count < 0) {
		printk(KERN_ERR PREFIX
		       "Error parsing LAPIC address override entry\n");
		return count;
	}

	acpi_register_lapic_address(acpi_lapic_addr);

	return count;
}

static int __init acpi_parse_madt_lapic_entries(void)
{
	int count;

	if (!cpu_has_apic)
		return -ENODEV;

	/*
	 * Note that the LAPIC address is obtained from the MADT (32-bit value)
	 * and (optionally) overriden by a LAPIC_ADDR_OVR entry (64-bit value).
	 */

	count =
	    acpi_table_parse_madt(ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE,
				  acpi_parse_lapic_addr_ovr, 0);
	if (count < 0) {
		printk(KERN_ERR PREFIX
		       "Error parsing LAPIC address override entry\n");
		return count;
	}

	acpi_register_lapic_address(acpi_lapic_addr);

	count = acpi_table_parse_madt(ACPI_MADT_TYPE_LOCAL_SAPIC,
				      acpi_parse_sapic, MAX_APICS);

	if (!count)
		count = acpi_table_parse_madt(ACPI_MADT_TYPE_LOCAL_APIC,
					      acpi_parse_lapic, MAX_APICS);
	if (!count) {
		printk(KERN_ERR PREFIX "No LAPIC entries present\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return -ENODEV;
	} else if (count < 0) {
		printk(KERN_ERR PREFIX "Error parsing LAPIC entry\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return count;
	}

	count =
	    acpi_table_parse_madt(ACPI_MADT_TYPE_LOCAL_APIC_NMI, acpi_parse_lapic_nmi, 0);
	if (count < 0) {
		printk(KERN_ERR PREFIX "Error parsing LAPIC NMI entry\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return count;
	}
	return 0;
}
#endif				/* CONFIG_X86_LOCAL_APIC */

#ifdef	CONFIG_X86_IO_APIC
#define MP_ISA_BUS		0

#ifdef CONFIG_X86_ES7000
extern int es7000_plat;
#endif

static struct {
	int apic_id;
	int gsi_base;
	int gsi_end;
	DECLARE_BITMAP(pin_programmed, MP_MAX_IOAPIC_PIN + 1);
} mp_ioapic_routing[MAX_IO_APICS];

static int mp_find_ioapic(int gsi)
{
	int i = 0;

	/* Find the IOAPIC that manages this GSI. */
	for (i = 0; i < nr_ioapics; i++) {
		if ((gsi >= mp_ioapic_routing[i].gsi_base)
		    && (gsi <= mp_ioapic_routing[i].gsi_end))
			return i;
	}

	printk(KERN_ERR "ERROR: Unable to locate IOAPIC for GSI %d\n", gsi);
	return -1;
}

static u8 __init uniq_ioapic_id(u8 id)
{
#ifdef CONFIG_X86_32
	if ((boot_cpu_data.x86_vendor == X86_VENDOR_INTEL) &&
	    !APIC_XAPIC(apic_version[boot_cpu_physical_apicid]))
		return io_apic_get_unique_id(nr_ioapics, id);
	else
		return id;
#else
	int i;
	DECLARE_BITMAP(used, 256);
	bitmap_zero(used, 256);
	for (i = 0; i < nr_ioapics; i++) {
		struct mp_config_ioapic *ia = &mp_ioapics[i];
		__set_bit(ia->mp_apicid, used);
	}
	if (!test_bit(id, used))
		return id;
	return find_first_zero_bit(used, 256);
#endif
}

static int bad_ioapic(unsigned long address)
{
	if (nr_ioapics >= MAX_IO_APICS) {
		printk(KERN_ERR "ERROR: Max # of I/O APICs (%d) exceeded "
		       "(found %d)\n", MAX_IO_APICS, nr_ioapics);
		panic("Recompile kernel with bigger MAX_IO_APICS!\n");
	}
	if (!address) {
		printk(KERN_ERR "WARNING: Bogus (zero) I/O APIC address"
		       " found in table, skipping!\n");
		return 1;
	}
	return 0;
}

void __init mp_register_ioapic(int id, u32 address, u32 gsi_base)
{
	int idx = 0;

	if (bad_ioapic(address))
		return;

	idx = nr_ioapics;

	mp_ioapics[idx].mp_type = MP_IOAPIC;
	mp_ioapics[idx].mp_flags = MPC_APIC_USABLE;
	mp_ioapics[idx].mp_apicaddr = address;

	set_fixmap_nocache(FIX_IO_APIC_BASE_0 + idx, address);
	mp_ioapics[idx].mp_apicid = uniq_ioapic_id(id);
#ifdef CONFIG_X86_32
	mp_ioapics[idx].mp_apicver = io_apic_get_version(idx);
#else
	mp_ioapics[idx].mp_apicver = 0;
#endif
	/*
	 * Build basic GSI lookup table to facilitate gsi->io_apic lookups
	 * and to prevent reprogramming of IOAPIC pins (PCI GSIs).
	 */
	mp_ioapic_routing[idx].apic_id = mp_ioapics[idx].mp_apicid;
	mp_ioapic_routing[idx].gsi_base = gsi_base;
	mp_ioapic_routing[idx].gsi_end = gsi_base +
	    io_apic_get_redir_entries(idx);

	printk(KERN_INFO "IOAPIC[%d]: apic_id %d, version %d, address 0x%lx, "
	       "GSI %d-%d\n", idx, mp_ioapics[idx].mp_apicid,
	       mp_ioapics[idx].mp_apicver, mp_ioapics[idx].mp_apicaddr,
	       mp_ioapic_routing[idx].gsi_base, mp_ioapic_routing[idx].gsi_end);

	nr_ioapics++;
}

static void assign_to_mp_irq(struct mp_config_intsrc *m,
				    struct mp_config_intsrc *mp_irq)
{
	memcpy(mp_irq, m, sizeof(struct mp_config_intsrc));
}

static int mp_irq_cmp(struct mp_config_intsrc *mp_irq,
				struct mp_config_intsrc *m)
{
	return memcmp(mp_irq, m, sizeof(struct mp_config_intsrc));
}

static void save_mp_irq(struct mp_config_intsrc *m)
{
	int i;

	for (i = 0; i < mp_irq_entries; i++) {
		if (!mp_irq_cmp(&mp_irqs[i], m))
			return;
	}

	assign_to_mp_irq(m, &mp_irqs[mp_irq_entries]);
	if (++mp_irq_entries == MAX_IRQ_SOURCES)
		panic("Max # of irq sources exceeded!!\n");
}

void __init mp_override_legacy_irq(u8 bus_irq, u8 polarity, u8 trigger, u32 gsi)
{
	int ioapic;
	int pin;
	struct mp_config_intsrc mp_irq;

	/*
	 * Convert 'gsi' to 'ioapic.pin'.
	 */
	ioapic = mp_find_ioapic(gsi);
	if (ioapic < 0)
		return;
	pin = gsi - mp_ioapic_routing[ioapic].gsi_base;

	/*
	 * TBD: This check is for faulty timer entries, where the override
	 *      erroneously sets the trigger to level, resulting in a HUGE
	 *      increase of timer interrupts!
	 */
	if ((bus_irq == 0) && (trigger == 3))
		trigger = 1;

	mp_irq.mp_type = MP_INTSRC;
	mp_irq.mp_irqtype = mp_INT;
	mp_irq.mp_irqflag = (trigger << 2) | polarity;
	mp_irq.mp_srcbus = MP_ISA_BUS;
	mp_irq.mp_srcbusirq = bus_irq;	/* IRQ */
	mp_irq.mp_dstapic = mp_ioapics[ioapic].mp_apicid; /* APIC ID */
	mp_irq.mp_dstirq = pin;	/* INTIN# */

	save_mp_irq(&mp_irq);
}

void __init mp_config_acpi_legacy_irqs(void)
{
	int i;
	int ioapic;
	unsigned int dstapic;
	struct mp_config_intsrc mp_irq;

#if defined (CONFIG_MCA) || defined (CONFIG_EISA)
	/*
	 * Fabricate the legacy ISA bus (bus #31).
	 */
	mp_bus_id_to_type[MP_ISA_BUS] = MP_BUS_ISA;
#endif
	set_bit(MP_ISA_BUS, mp_bus_not_pci);
	pr_debug("Bus #%d is ISA\n", MP_ISA_BUS);

#ifdef CONFIG_X86_ES7000
	/*
	 * Older generations of ES7000 have no legacy identity mappings
	 */
	if (es7000_plat == 1)
		return;
#endif

	/*
	 * Locate the IOAPIC that manages the ISA IRQs (0-15).
	 */
	ioapic = mp_find_ioapic(0);
	if (ioapic < 0)
		return;
	dstapic = mp_ioapics[ioapic].mp_apicid;

	/*
	 * Use the default configuration for the IRQs 0-15.  Unless
	 * overridden by (MADT) interrupt source override entries.
	 */
	for (i = 0; i < 16; i++) {
		int idx;

		for (idx = 0; idx < mp_irq_entries; idx++) {
			struct mp_config_intsrc *irq = mp_irqs + idx;

			/* Do we already have a mapping for this ISA IRQ? */
			if (irq->mp_srcbus == MP_ISA_BUS
			    && irq->mp_srcbusirq == i)
				break;

			/* Do we already have a mapping for this IOAPIC pin */
			if (irq->mp_dstapic == dstapic &&
			    irq->mp_dstirq == i)
				break;
		}

		if (idx != mp_irq_entries) {
			printk(KERN_DEBUG "ACPI: IRQ%d used by override.\n", i);
			continue;	/* IRQ already used */
		}

		mp_irq.mp_type = MP_INTSRC;
		mp_irq.mp_irqflag = 0;	/* Conforming */
		mp_irq.mp_srcbus = MP_ISA_BUS;
		mp_irq.mp_dstapic = dstapic;
		mp_irq.mp_irqtype = mp_INT;
		mp_irq.mp_srcbusirq = i; /* Identity mapped */
		mp_irq.mp_dstirq = i;

		save_mp_irq(&mp_irq);
	}
}

int mp_register_gsi(u32 gsi, int triggering, int polarity)
{
	int ioapic;
	int ioapic_pin;
#ifdef CONFIG_X86_32
#define MAX_GSI_NUM	4096
#define IRQ_COMPRESSION_START	64

	static int pci_irq = IRQ_COMPRESSION_START;
	/*
	 * Mapping between Global System Interrupts, which
	 * represent all possible interrupts, and IRQs
	 * assigned to actual devices.
	 */
	static int gsi_to_irq[MAX_GSI_NUM];
#else

	if (acpi_irq_model != ACPI_IRQ_MODEL_IOAPIC)
		return gsi;
#endif

	/* Don't set up the ACPI SCI because it's already set up */
	if (acpi_gbl_FADT.sci_interrupt == gsi)
		return gsi;

	ioapic = mp_find_ioapic(gsi);
	if (ioapic < 0) {
		printk(KERN_WARNING "No IOAPIC for GSI %u\n", gsi);
		return gsi;
	}

	ioapic_pin = gsi - mp_ioapic_routing[ioapic].gsi_base;

#ifdef CONFIG_X86_32
	if (ioapic_renumber_irq)
		gsi = ioapic_renumber_irq(ioapic, gsi);
#endif

	/*
	 * Avoid pin reprogramming.  PRTs typically include entries
	 * with redundant pin->gsi mappings (but unique PCI devices);
	 * we only program the IOAPIC on the first.
	 */
	if (ioapic_pin > MP_MAX_IOAPIC_PIN) {
		printk(KERN_ERR "Invalid reference to IOAPIC pin "
		       "%d-%d\n", mp_ioapic_routing[ioapic].apic_id,
		       ioapic_pin);
		return gsi;
	}
	if (test_bit(ioapic_pin, mp_ioapic_routing[ioapic].pin_programmed)) {
		pr_debug("Pin %d-%d already programmed\n",
			 mp_ioapic_routing[ioapic].apic_id, ioapic_pin);
#ifdef CONFIG_X86_32
		return (gsi < IRQ_COMPRESSION_START ? gsi : gsi_to_irq[gsi]);
#else
		return gsi;
#endif
	}

	set_bit(ioapic_pin, mp_ioapic_routing[ioapic].pin_programmed);
#ifdef CONFIG_X86_32
	/*
	 * For GSI >= 64, use IRQ compression
	 */
	if ((gsi >= IRQ_COMPRESSION_START)
	    && (triggering == ACPI_LEVEL_SENSITIVE)) {
		/*
		 * For PCI devices assign IRQs in order, avoiding gaps
		 * due to unused I/O APIC pins.
		 */
		int irq = gsi;
		if (gsi < MAX_GSI_NUM) {
			/*
			 * Retain the VIA chipset work-around (gsi > 15), but
			 * avoid a problem where the 8254 timer (IRQ0) is setup
			 * via an override (so it's not on pin 0 of the ioapic),
			 * and at the same time, the pin 0 interrupt is a PCI
			 * type.  The gsi > 15 test could cause these two pins
			 * to be shared as IRQ0, and they are not shareable.
			 * So test for this condition, and if necessary, avoid
			 * the pin collision.
			 */
			gsi = pci_irq++;
			/*
			 * Don't assign IRQ used by ACPI SCI
			 */
			if (gsi == acpi_gbl_FADT.sci_interrupt)
				gsi = pci_irq++;
			gsi_to_irq[irq] = gsi;
		} else {
			printk(KERN_ERR "GSI %u is too high\n", gsi);
			return gsi;
		}
	}
#endif
	io_apic_set_pci_routing(ioapic, ioapic_pin, gsi,
				triggering == ACPI_EDGE_SENSITIVE ? 0 : 1,
				polarity == ACPI_ACTIVE_HIGH ? 0 : 1);
	return gsi;
}

int mp_config_acpi_gsi(unsigned char number, unsigned int devfn, u8 pin,
			u32 gsi, int triggering, int polarity)
{
#ifdef CONFIG_X86_MPPARSE
	struct mp_config_intsrc mp_irq;
	int ioapic;

	if (!acpi_ioapic)
		return 0;

	/* print the entry should happen on mptable identically */
	mp_irq.mp_type = MP_INTSRC;
	mp_irq.mp_irqtype = mp_INT;
	mp_irq.mp_irqflag = (triggering == ACPI_EDGE_SENSITIVE ? 4 : 0x0c) |
				(polarity == ACPI_ACTIVE_HIGH ? 1 : 3);
	mp_irq.mp_srcbus = number;
	mp_irq.mp_srcbusirq = (((devfn >> 3) & 0x1f) << 2) | ((pin - 1) & 3);
	ioapic = mp_find_ioapic(gsi);
	mp_irq.mp_dstapic = mp_ioapic_routing[ioapic].apic_id;
	mp_irq.mp_dstirq = gsi - mp_ioapic_routing[ioapic].gsi_base;

	save_mp_irq(&mp_irq);
#endif
	return 0;
}

/*
 * Parse IOAPIC related entries in MADT
 * returns 0 on success, < 0 on error
 */
static int __init acpi_parse_madt_ioapic_entries(void)
{
	int count;

	/*
	 * ACPI interpreter is required to complete interrupt setup,
	 * so if it is off, don't enumerate the io-apics with ACPI.
	 * If MPS is present, it will handle them,
	 * otherwise the system will stay in PIC mode
	 */
	if (acpi_disabled || acpi_noirq) {
		return -ENODEV;
	}

	if (!cpu_has_apic)
		return -ENODEV;

	/*
	 * if "noapic" boot option, don't look for IO-APICs
	 */
	if (skip_ioapic_setup) {
		printk(KERN_INFO PREFIX "Skipping IOAPIC probe "
		       "due to 'noapic' option.\n");
		return -ENODEV;
	}

	count =
	    acpi_table_parse_madt(ACPI_MADT_TYPE_IO_APIC, acpi_parse_ioapic,
				  MAX_IO_APICS);
	if (!count) {
		printk(KERN_ERR PREFIX "No IOAPIC entries present\n");
		return -ENODEV;
	} else if (count < 0) {
		printk(KERN_ERR PREFIX "Error parsing IOAPIC entry\n");
		return count;
	}

	count =
	    acpi_table_parse_madt(ACPI_MADT_TYPE_INTERRUPT_OVERRIDE, acpi_parse_int_src_ovr,
				  nr_irqs);
	if (count < 0) {
		printk(KERN_ERR PREFIX
		       "Error parsing interrupt source overrides entry\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return count;
	}

	/*
	 * If BIOS did not supply an INT_SRC_OVR for the SCI
	 * pretend we got one so we can set the SCI flags.
	 */
	if (!acpi_sci_override_gsi)
		acpi_sci_ioapic_setup(acpi_gbl_FADT.sci_interrupt, 0, 0);

	/* Fill in identity legacy mapings where no override */
	mp_config_acpi_legacy_irqs();

	count =
	    acpi_table_parse_madt(ACPI_MADT_TYPE_NMI_SOURCE, acpi_parse_nmi_src,
				  nr_irqs);
	if (count < 0) {
		printk(KERN_ERR PREFIX "Error parsing NMI SRC entry\n");
		/* TBD: Cleanup to allow fallback to MPS */
		return count;
	}

	return 0;
}
#else
static inline int acpi_parse_madt_ioapic_entries(void)
{
	return -1;
}
#endif	/* !CONFIG_X86_IO_APIC */

static void __init early_acpi_process_madt(void)
{
#ifdef CONFIG_X86_LOCAL_APIC
	int error;

	if (!acpi_table_parse(ACPI_SIG_MADT, acpi_parse_madt)) {

		/*
		 * Parse MADT LAPIC entries
		 */
		error = early_acpi_parse_madt_lapic_addr_ovr();
		if (!error) {
			acpi_lapic = 1;
			smp_found_config = 1;
		}
		if (error == -EINVAL) {
			/*
			 * Dell Precision Workstation 410, 610 come here.
			 */
			printk(KERN_ERR PREFIX
			       "Invalid BIOS MADT, disabling ACPI\n");
			disable_acpi();
		}
	}
#endif
}

static void __init acpi_process_madt(void)
{
#ifdef CONFIG_X86_LOCAL_APIC
	int error;

	if (!acpi_table_parse(ACPI_SIG_MADT, acpi_parse_madt)) {

		/*
		 * Parse MADT LAPIC entries
		 */
		error = acpi_parse_madt_lapic_entries();
		if (!error) {
			acpi_lapic = 1;

#ifdef CONFIG_X86_GENERICARCH
			generic_bigsmp_probe();
#endif
			/*
			 * Parse MADT IO-APIC entries
			 */
			error = acpi_parse_madt_ioapic_entries();
			if (!error) {
				acpi_irq_model = ACPI_IRQ_MODEL_IOAPIC;
				acpi_irq_balance_set(NULL);
				acpi_ioapic = 1;

				smp_found_config = 1;
#ifdef CONFIG_X86_32
				setup_apic_routing();
#endif
			}
		}
		if (error == -EINVAL) {
			/*
			 * Dell Precision Workstation 410, 610 come here.
			 */
			printk(KERN_ERR PREFIX
			       "Invalid BIOS MADT, disabling ACPI\n");
			disable_acpi();
		}
	}
#endif
	return;
}

static int __init disable_acpi_irq(const struct dmi_system_id *d)
{
	if (!acpi_force) {
		printk(KERN_NOTICE "%s detected: force use of acpi=noirq\n",
		       d->ident);
		acpi_noirq_set();
	}
	return 0;
}

static int __init disable_acpi_pci(const struct dmi_system_id *d)
{
	if (!acpi_force) {
		printk(KERN_NOTICE "%s detected: force use of pci=noacpi\n",
		       d->ident);
		acpi_disable_pci();
	}
	return 0;
}

static int __init dmi_disable_acpi(const struct dmi_system_id *d)
{
	if (!acpi_force) {
		printk(KERN_NOTICE "%s detected: acpi off\n", d->ident);
		disable_acpi();
	} else {
		printk(KERN_NOTICE
		       "Warning: DMI blacklist says broken, but acpi forced\n");
	}
	return 0;
}

/*
 * Limit ACPI to CPU enumeration for HT
 */
static int __init force_acpi_ht(const struct dmi_system_id *d)
{
	if (!acpi_force) {
		printk(KERN_NOTICE "%s detected: force use of acpi=ht\n",
		       d->ident);
		disable_acpi();
		acpi_ht = 1;
	} else {
		printk(KERN_NOTICE
		       "Warning: acpi=force overrules DMI blacklist: acpi=ht\n");
	}
	return 0;
}

/*
 * Force ignoring BIOS IRQ0 pin2 override
 */
static int __init dmi_ignore_irq0_timer_override(const struct dmi_system_id *d)
{
	/*
	 * The ati_ixp4x0_rev() early PCI quirk should have set
	 * the acpi_skip_timer_override flag already:
	 */
	if (!acpi_skip_timer_override) {
		WARN(1, KERN_ERR "ati_ixp4x0 quirk not complete.\n");
		pr_notice("%s detected: Ignoring BIOS IRQ0 pin2 override\n",
			d->ident);
		acpi_skip_timer_override = 1;
	}
	return 0;
}

/*
 * If your system is blacklisted here, but you find that acpi=force
 * works for you, please contact acpi-devel@sourceforge.net
 */
static struct dmi_system_id __initdata acpi_dmi_table[] = {
	/*
	 * Boxes that need ACPI disabled
	 */
	{
	 .callback = dmi_disable_acpi,
	 .ident = "IBM Thinkpad",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "IBM"),
		     DMI_MATCH(DMI_BOARD_NAME, "2629H1G"),
		     },
	 },

	/*
	 * Boxes that need acpi=ht
	 */
	{
	 .callback = force_acpi_ht,
	 .ident = "FSC Primergy T850",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "PRIMERGY T850"),
		     },
	 },
	{
	 .callback = force_acpi_ht,
	 .ident = "HP VISUALIZE NT Workstation",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "Hewlett-Packard"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "HP VISUALIZE NT Workstation"),
		     },
	 },
	{
	 .callback = force_acpi_ht,
	 .ident = "Compaq Workstation W8000",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Compaq"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "Workstation W8000"),
		     },
	 },
	{
	 .callback = force_acpi_ht,
	 .ident = "ASUS P4B266",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
		     DMI_MATCH(DMI_BOARD_NAME, "P4B266"),
		     },
	 },
	{
	 .callback = force_acpi_ht,
	 .ident = "ASUS P2B-DS",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
		     DMI_MATCH(DMI_BOARD_NAME, "P2B-DS"),
		     },
	 },
	{
	 .callback = force_acpi_ht,
	 .ident = "ASUS CUR-DLS",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
		     DMI_MATCH(DMI_BOARD_NAME, "CUR-DLS"),
		     },
	 },
	{
	 .callback = force_acpi_ht,
	 .ident = "ABIT i440BX-W83977",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "ABIT <http://www.abit.com>"),
		     DMI_MATCH(DMI_BOARD_NAME, "i440BX-W83977 (BP6)"),
		     },
	 },
	{
	 .callback = force_acpi_ht,
	 .ident = "IBM Bladecenter",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "IBM"),
		     DMI_MATCH(DMI_BOARD_NAME, "IBM eServer BladeCenter HS20"),
		     },
	 },
	{
	 .callback = force_acpi_ht,
	 .ident = "IBM eServer xSeries 360",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "IBM"),
		     DMI_MATCH(DMI_BOARD_NAME, "eServer xSeries 360"),
		     },
	 },
	{
	 .callback = force_acpi_ht,
	 .ident = "IBM eserver xSeries 330",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "IBM"),
		     DMI_MATCH(DMI_BOARD_NAME, "eserver xSeries 330"),
		     },
	 },
	{
	 .callback = force_acpi_ht,
	 .ident = "IBM eserver xSeries 440",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "IBM"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "eserver xSeries 440"),
		     },
	 },

	/*
	 * Boxes that need ACPI PCI IRQ routing disabled
	 */
	{
	 .callback = disable_acpi_irq,
	 .ident = "ASUS A7V",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC"),
		     DMI_MATCH(DMI_BOARD_NAME, "<A7V>"),
		     /* newer BIOS, Revision 1011, does work */
		     DMI_MATCH(DMI_BIOS_VERSION,
			       "ASUS A7V ACPI BIOS Revision 1007"),
		     },
	 },
	{
		/*
		 * Latest BIOS for IBM 600E (1.16) has bad pcinum
		 * for LPC bridge, which is needed for the PCI
		 * interrupt links to work. DSDT fix is in bug 5966.
		 * 2645, 2646 model numbers are shared with 600/600E/600X
		 */
	 .callback = disable_acpi_irq,
	 .ident = "IBM Thinkpad 600 Series 2645",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "IBM"),
		     DMI_MATCH(DMI_BOARD_NAME, "2645"),
		     },
	 },
	{
	 .callback = disable_acpi_irq,
	 .ident = "IBM Thinkpad 600 Series 2646",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "IBM"),
		     DMI_MATCH(DMI_BOARD_NAME, "2646"),
		     },
	 },
	/*
	 * Boxes that need ACPI PCI IRQ routing and PCI scan disabled
	 */
	{			/* _BBN 0 bug */
	 .callback = disable_acpi_pci,
	 .ident = "ASUS PR-DLS",
	 .matches = {
		     DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
		     DMI_MATCH(DMI_BOARD_NAME, "PR-DLS"),
		     DMI_MATCH(DMI_BIOS_VERSION,
			       "ASUS PR-DLS ACPI BIOS Revision 1010"),
		     DMI_MATCH(DMI_BIOS_DATE, "03/21/2003")
		     },
	 },
	{
	 .callback = disable_acpi_pci,
	 .ident = "Acer TravelMate 36x Laptop",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 360"),
		     },
	 },
	{}
};

/* second table for DMI checks that should run after early-quirks */
static struct dmi_system_id __initdata acpi_dmi_table_late[] = {
	/*
	 * HP laptops which use a DSDT reporting as HP/SB400/10000,
	 * which includes some code which overrides all temperature
	 * trip points to 16C if the INTIN2 input of the I/O APIC
	 * is enabled.  This input is incorrectly designated the
	 * ISA IRQ 0 via an interrupt source override even though
	 * it is wired to the output of the master 8259A and INTIN0
	 * is not connected at all.  Force ignoring BIOS IRQ0 pin2
	 * override in that cases.
	 */
	{
	 .callback = dmi_ignore_irq0_timer_override,
	 .ident = "HP nx6115 laptop",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "HP Compaq nx6115"),
		     },
	 },
	{
	 .callback = dmi_ignore_irq0_timer_override,
	 .ident = "HP NX6125 laptop",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "HP Compaq nx6125"),
		     },
	 },
	{
	 .callback = dmi_ignore_irq0_timer_override,
	 .ident = "HP NX6325 laptop",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "HP Compaq nx6325"),
		     },
	 },
	{
	 .callback = dmi_ignore_irq0_timer_override,
	 .ident = "HP 6715b laptop",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "HP Compaq 6715b"),
		     },
	 },
	{}
};

/*
 * acpi_boot_table_init() and acpi_boot_init()
 *  called from setup_arch(), always.
 *	1. checksums all tables
 *	2. enumerates lapics
 *	3. enumerates io-apics
 *
 * acpi_table_init() is separate to allow reading SRAT without
 * other side effects.
 *
 * side effects of acpi_boot_init:
 *	acpi_lapic = 1 if LAPIC found
 *	acpi_ioapic = 1 if IOAPIC found
 *	if (acpi_lapic && acpi_ioapic) smp_found_config = 1;
 *	if acpi_blacklisted() acpi_disabled = 1;
 *	acpi_irq_model=...
 *	...
 *
 * return value: (currently ignored)
 *	0: success
 *	!0: failure
 */

int __init acpi_boot_table_init(void)
{
	int error;

	dmi_check_system(acpi_dmi_table);

	/*
	 * If acpi_disabled, bail out
	 * One exception: acpi=ht continues far enough to enumerate LAPICs
	 */
	if (acpi_disabled && !acpi_ht)
		return 1;

	/*
	 * Initialize the ACPI boot-time table parser.
	 */
	error = acpi_table_init();
	if (error) {
		disable_acpi();
		return error;
	}

	acpi_table_parse(ACPI_SIG_BOOT, acpi_parse_sbf);

	/*
	 * blacklist may disable ACPI entirely
	 */
	error = acpi_blacklisted();
	if (error) {
		if (acpi_force) {
			printk(KERN_WARNING PREFIX "acpi=force override\n");
		} else {
			printk(KERN_WARNING PREFIX "Disabling ACPI support\n");
			disable_acpi();
			return error;
		}
	}

	return 0;
}

int __init early_acpi_boot_init(void)
{
	/*
	 * If acpi_disabled, bail out
	 * One exception: acpi=ht continues far enough to enumerate LAPICs
	 */
	if (acpi_disabled && !acpi_ht)
		return 1;

	/*
	 * Process the Multiple APIC Description Table (MADT), if present
	 */
	early_acpi_process_madt();

	return 0;
}

int __init acpi_boot_init(void)
{
	/* those are executed after early-quirks are executed */
	dmi_check_system(acpi_dmi_table_late);

	/*
	 * If acpi_disabled, bail out
	 * One exception: acpi=ht continues far enough to enumerate LAPICs
	 */
	if (acpi_disabled && !acpi_ht)
		return 1;

	acpi_table_parse(ACPI_SIG_BOOT, acpi_parse_sbf);

	/*
	 * set sci_int and PM timer address
	 */
	acpi_table_parse(ACPI_SIG_FADT, acpi_parse_fadt);

	/*
	 * Process the Multiple APIC Description Table (MADT), if present
	 */
	acpi_process_madt();

	acpi_table_parse(ACPI_SIG_HPET, acpi_parse_hpet);

	return 0;
}

static int __init parse_acpi(char *arg)
{
	if (!arg)
		return -EINVAL;

	/* "acpi=off" disables both ACPI table parsing and interpreter */
	if (strcmp(arg, "off") == 0) {
		disable_acpi();
	}
	/* acpi=force to over-ride black-list */
	else if (strcmp(arg, "force") == 0) {
		acpi_force = 1;
		acpi_ht = 1;
		acpi_disabled = 0;
	}
	/* acpi=strict disables out-of-spec workarounds */
	else if (strcmp(arg, "strict") == 0) {
		acpi_strict = 1;
	}
	/* Limit ACPI just to boot-time to enable HT */
	else if (strcmp(arg, "ht") == 0) {
		if (!acpi_force)
			disable_acpi();
		acpi_ht = 1;
	}
	/* "acpi=noirq" disables ACPI interrupt routing */
	else if (strcmp(arg, "noirq") == 0) {
		acpi_noirq_set();
	} else {
		/* Core will printk when we return error. */
		return -EINVAL;
	}
	return 0;
}
early_param("acpi", parse_acpi);

/* FIXME: Using pci= for an ACPI parameter is a travesty. */
static int __init parse_pci(char *arg)
{
	if (arg && strcmp(arg, "noacpi") == 0)
		acpi_disable_pci();
	return 0;
}
early_param("pci", parse_pci);

int __init acpi_mps_check(void)
{
#if defined(CONFIG_X86_LOCAL_APIC) && !defined(CONFIG_X86_MPPARSE)
/* mptable code is not built-in*/
	if (acpi_disabled || acpi_noirq) {
		printk(KERN_WARNING "MPS support code is not built-in.\n"
		       "Using acpi=off or acpi=noirq or pci=noacpi "
		       "may have problem\n");
		return 1;
	}
#endif
	return 0;
}

#ifdef CONFIG_X86_IO_APIC
static int __init parse_acpi_skip_timer_override(char *arg)
{
	acpi_skip_timer_override = 1;
	return 0;
}
early_param("acpi_skip_timer_override", parse_acpi_skip_timer_override);

static int __init parse_acpi_use_timer_override(char *arg)
{
	acpi_use_timer_override = 1;
	return 0;
}
early_param("acpi_use_timer_override", parse_acpi_use_timer_override);
#endif /* CONFIG_X86_IO_APIC */

static int __init setup_acpi_sci(char *s)
{
	if (!s)
		return -EINVAL;
	if (!strcmp(s, "edge"))
		acpi_sci_flags =  ACPI_MADT_TRIGGER_EDGE |
			(acpi_sci_flags & ~ACPI_MADT_TRIGGER_MASK);
	else if (!strcmp(s, "level"))
		acpi_sci_flags = ACPI_MADT_TRIGGER_LEVEL |
			(acpi_sci_flags & ~ACPI_MADT_TRIGGER_MASK);
	else if (!strcmp(s, "high"))
		acpi_sci_flags = ACPI_MADT_POLARITY_ACTIVE_HIGH |
			(acpi_sci_flags & ~ACPI_MADT_POLARITY_MASK);
	else if (!strcmp(s, "low"))
		acpi_sci_flags = ACPI_MADT_POLARITY_ACTIVE_LOW |
			(acpi_sci_flags & ~ACPI_MADT_POLARITY_MASK);
	else
		return -EINVAL;
	return 0;
}
early_param("acpi_sci", setup_acpi_sci);

int __acpi_acquire_global_lock(unsigned int *lock)
{
	unsigned int old, new, val;
	do {
		old = *lock;
		new = (((old & ~0x3) + 2) + ((old >> 1) & 0x1));
		val = cmpxchg(lock, old, new);
	} while (unlikely (val != old));
	return (new < 3) ? -1 : 0;
}

int __acpi_release_global_lock(unsigned int *lock)
{
	unsigned int old, new, val;
	do {
		old = *lock;
		new = old & ~0x3;
		val = cmpxchg(lock, old, new);
	} while (unlikely (val != old));
	return old & 0x1;
}
