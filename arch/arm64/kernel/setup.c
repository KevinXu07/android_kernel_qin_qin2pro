/*
 * Based on arch/arm/kernel/setup.c
 *
 * Copyright (C) 1995-2001 Russell King
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/acpi.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/utsname.h>
#include <linux/initrd.h>
#include <linux/console.h>
#include <linux/cache.h>
#include <linux/bootmem.h>
#include <linux/screen_info.h>
#include <linux/init.h>
#include <linux/kexec.h>
#include <linux/root_dev.h>
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/memblock.h>
#include <linux/of_fdt.h>
#include <linux/libfdt.h>
#include <linux/efi.h>
#include <linux/psci.h>
#include <linux/sched/task.h>
#include <linux/mm.h>

#include <asm/acpi.h>
#include <asm/fixmap.h>
#include <asm/cpu.h>
#include <asm/cputype.h>
#include <asm/elf.h>
#include <asm/cpufeature.h>
#include <asm/cpu_ops.h>
#include <asm/kasan.h>
#include <asm/numa.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/smp_plat.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/traps.h>
#include <asm/memblock.h>
#include <asm/efi.h>
#include <asm/xen/hypervisor.h>
#include <asm/early_ioremap.h>
#include <linux/io.h>
#include <linux/string.h>
#include <asm/mmu_context.h>

phys_addr_t __fdt_pointer __initdata;


/* Qin2 Pro v1: use the source 4.14 board DTB and keep bootloader identity. */
extern const unsigned char qin2pro_embedded_dtb[];
static unsigned char qin2pro_fdt_copy[SZ_256K] __aligned(8);
static bool qin2pro_embedded_selected __initdata;
static char qin2pro_bootargs[2048] __initdata;

#define QIN2PRO_FB_PHYS		0x9d654000UL
#define QIN2PRO_FB_PITCH	0x4000
#define QIN2PRO_FB_FILL		0x2000
#define QIN2PRO_NUM_HUNS	0x58000UL
#define QIN2PRO_NUM_TENS	0x80000UL
#define QIN2PRO_NUM_ONES	0xA8000UL
#define QIN2PRO_NUM_STRIDE	0x4000
#define QIN2PRO_NUM_THICK	0x2000

static void __init qin2pro_fb_fill(unsigned long off, unsigned int len,
				   unsigned char v, int use_early)
{
	void *p;

	if (use_early)
		p = early_memremap(QIN2PRO_FB_PHYS + off, len);
	else
		p = memremap(QIN2PRO_FB_PHYS + off, len, MEMREMAP_WB);
	if (!p)
		return;
	memset(p, v, len);
	__flush_dcache_area(p, len);
	if (use_early)
		early_memunmap(p, len);
	else
		memunmap(p);
}

/* Tally readout at fixed fb offsets, always on-screen:
 * tens cluster = n/10 stripes starting at +0x80000,
 * ones cluster = n%10 stripes starting at +0xA8000.
 * e.g. 3 stripes up + 4 stripes down = mark 34. */
static void __init qin2pro_fb_num(unsigned int n, int use_early)
{
	unsigned int i;
	static bool cleared;

	if (!cleared) {
		cleared = true;
		qin2pro_fb_fill(0, 30 * QIN2PRO_FB_PITCH, 0, use_early);
		qin2pro_fb_fill(QIN2PRO_NUM_HUNS - 0x4000, QIN2PRO_NUM_ONES + 9 * QIN2PRO_NUM_STRIDE + QIN2PRO_NUM_THICK - QIN2PRO_NUM_HUNS + 0x4000, 0, use_early);
	}

	for (i = 0; i < 9; i++) {
		qin2pro_fb_fill(QIN2PRO_NUM_TENS + i * QIN2PRO_NUM_STRIDE,
				QIN2PRO_NUM_THICK, 0, use_early);
		qin2pro_fb_fill(QIN2PRO_NUM_ONES + i * QIN2PRO_NUM_STRIDE,
				QIN2PRO_NUM_THICK, 0, use_early);
	}
	for (i = 0; i < n / 10 && i < 9; i++)
		qin2pro_fb_fill(QIN2PRO_NUM_TENS + i * QIN2PRO_NUM_STRIDE,
				QIN2PRO_NUM_THICK, 0xf0, use_early);
	for (i = 0; i < n % 10; i++)
		qin2pro_fb_fill(QIN2PRO_NUM_ONES + i * QIN2PRO_NUM_STRIDE,
				QIN2PRO_NUM_THICK, 0xf0, use_early);
}

/* early_memremap works until early_ioremap_reset() */
void __init qin2pro_fb_mark(unsigned int band, unsigned char value)
{
	if (band < 30)
		qin2pro_fb_fill(band * QIN2PRO_FB_PITCH, QIN2PRO_FB_FILL,
				value, 1);
	qin2pro_fb_num(band, 1);
}

/* post-paging_init: linear map covers all DRAM */
static void __init qin2pro_fb_mark2(unsigned int band, unsigned char value)
{
	if (band < 30)
		qin2pro_fb_fill(band * QIN2PRO_FB_PITCH, QIN2PRO_FB_FILL,
				value, 0);
	qin2pro_fb_num(band, 0);
}
/* 3-digit tally for initcall index / late-boot phase codes */
void __init qin2pro_fb_mark3(unsigned int n)
{
	unsigned int i;

	for (i = 0; i < 9; i++) {
		qin2pro_fb_fill(QIN2PRO_NUM_HUNS + i * QIN2PRO_NUM_STRIDE,
				QIN2PRO_NUM_THICK, 0, 0);
		qin2pro_fb_fill(QIN2PRO_NUM_TENS + i * QIN2PRO_NUM_STRIDE,
				QIN2PRO_NUM_THICK, 0, 0);
		qin2pro_fb_fill(QIN2PRO_NUM_ONES + i * QIN2PRO_NUM_STRIDE,
				QIN2PRO_NUM_THICK, 0, 0);
	}
	for (i = 0; i < n / 100 && i < 9; i++)
		qin2pro_fb_fill(QIN2PRO_NUM_HUNS + i * QIN2PRO_NUM_STRIDE,
				QIN2PRO_NUM_THICK, 0xf0, 0);
	for (i = 0; i < (n / 10) % 10 && i < 9; i++)
		qin2pro_fb_fill(QIN2PRO_NUM_TENS + i * QIN2PRO_NUM_STRIDE,
				QIN2PRO_NUM_THICK, 0xf0, 0);
	for (i = 0; i < n % 10; i++)
		qin2pro_fb_fill(QIN2PRO_NUM_ONES + i * QIN2PRO_NUM_STRIDE,
				QIN2PRO_NUM_THICK, 0xf0, 0);
}


static bool __init qin2pro_runtime_prefix(const char *token, int len)
{
	static const char * const prefixes[] = {
		"androidboot.mode=", "androidboot.serialno=",
		"androidboot.verifiedbootstate=", "androidboot.flash.locked=",
		"androidboot.slot_suffix=", "androidboot.bootdevice=",
		"sysdump_magic=", "modem=", "ltemode=", "rfboard.id=",
		"rfhw.id=", "crystal=", "32k.less=",
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(prefixes); i++) {
		int prefix_len = strlen(prefixes[i]);

		if (len >= prefix_len && !memcmp(token, prefixes[i], prefix_len))
			return true;
	}
	return false;
}

static bool __init qin2pro_has_prefix(const char *args, const char *prefix)
{
	return strstr(args, prefix) != NULL;
}

static int __init qin2pro_merge_runtime_args(const void *source, int source_len,
						const void *target, int target_len)
{
	const char *src = source;
	int out_len;

	if (!target || target_len < 1 || target_len > sizeof(qin2pro_bootargs))
		return -EINVAL;
	memcpy(qin2pro_bootargs, target, target_len - 1);
	out_len = target_len - 1;
	qin2pro_bootargs[out_len] = '\0';

	/* Keep only boot-mode/identity values from the bootloader.  In particular,
	 * never reintroduce its root=/dev/ram0 or stock lcd_name. */
	while (source && source_len > 1 && *src) {
		const char *start = src;
		const char *end = src;
		int len;

		while (*end && *end != ' ' && *end != '\t')
			end++;
		len = end - start;
		if (len > 0 && qin2pro_runtime_prefix(start, len) &&
		    out_len + len + 1 < sizeof(qin2pro_bootargs)) {
			char key[64];
			int key_len = 0;

			while (key_len < len && key_len < sizeof(key) - 1 &&
			       start[key_len] != '=')
				key[key_len] = start[key_len], key_len++;
			if (key_len < sizeof(key) - 1) {
				key[key_len++] = '=';
				key[key_len] = '\0';
			}
			if (!qin2pro_has_prefix(qin2pro_bootargs, key)) {
				qin2pro_bootargs[out_len++] = ' ';
				memcpy(qin2pro_bootargs + out_len, start, len);
				out_len += len;
				qin2pro_bootargs[out_len] = '\0';
			}
		}
		while (*end == ' ' || *end == '\t')
			end++;
		if (source_len <= end - (const char *)source)
			break;
		source_len -= end - src;
		src = end;
	}
	return out_len + 1;
}

static phys_addr_t __init qin2pro_prepare_fdt(phys_addr_t boot_fdt_phys)
{
	const void *boot_fdt;
	const void *source_bootargs;
	const void *value;
	int source_chosen, target_chosen, source_len, target_len;

	boot_fdt = fixmap_remap_fdt(boot_fdt_phys);
	if (!boot_fdt || fdt_check_header(boot_fdt))
		return boot_fdt_phys;
	if (fdt_open_into(qin2pro_embedded_dtb, qin2pro_fdt_copy,
			  sizeof(qin2pro_fdt_copy)))
		return boot_fdt_phys;
	source_chosen = fdt_path_offset(boot_fdt, "/chosen");
	target_chosen = fdt_path_offset(qin2pro_fdt_copy, "/chosen");
	if (target_chosen < 0)
		return boot_fdt_phys;

	target_len = 0;
	value = fdt_getprop(qin2pro_fdt_copy, target_chosen,
				"bootargs", &target_len);
	source_bootargs = source_chosen >= 0 ?
		fdt_getprop(boot_fdt, source_chosen, "bootargs", &source_len) : NULL;
	if (!value || qin2pro_merge_runtime_args(source_bootargs, source_len,
						 value, target_len) < 0)
		return boot_fdt_phys;
	if (fdt_setprop(qin2pro_fdt_copy, target_chosen, "bootargs",
				qin2pro_bootargs, strlen(qin2pro_bootargs) + 1))
		return boot_fdt_phys;

	/* initrd is supplied by the bootloader for recovery.  Preserve it when
	 * available, while allowing normal system boots with no initrd property. */
	if (source_chosen >= 0) {
		static const char * const props[] = {
			"linux,initrd-start", "linux,initrd-end", "stdout-path"
		};
		int i, len;

		for (i = 0; i < ARRAY_SIZE(props); i++) {
			value = fdt_getprop(boot_fdt, source_chosen, props[i], &len);
			if (value && fdt_setprop(qin2pro_fdt_copy, target_chosen,
						 props[i], value, len))
				return boot_fdt_phys;
		}
	}
	qin2pro_embedded_selected = true;
	qin2pro_fb_mark(5, 0xbb);
	return __pa_symbol(qin2pro_fdt_copy);
}

/*
 * Standard memory resources
 */
static struct resource mem_res[] = {
	{
		.name = "Kernel code",
		.start = 0,
		.end = 0,
		.flags = IORESOURCE_SYSTEM_RAM
	},
	{
		.name = "Kernel data",
		.start = 0,
		.end = 0,
		.flags = IORESOURCE_SYSTEM_RAM
	}
};

#define kernel_code mem_res[0]
#define kernel_data mem_res[1]

/*
 * The recorded values of x0 .. x3 upon kernel entry.
 */
u64 __cacheline_aligned boot_args[4];

void __init smp_setup_processor_id(void)
{
	u64 mpidr = read_cpuid_mpidr() & MPIDR_HWID_BITMASK;
	cpu_logical_map(0) = mpidr;

	/*
	 * clear __my_cpu_offset on boot CPU to avoid hang caused by
	 * using percpu variable early, for example, lockdep will
	 * access percpu variable inside lock_release
	 */
	set_my_cpu_offset(0);
	pr_info("Booting Linux on physical CPU 0x%lx\n", (unsigned long)mpidr);
}

bool arch_match_cpu_phys_id(int cpu, u64 phys_id)
{
	return phys_id == cpu_logical_map(cpu);
}

struct mpidr_hash mpidr_hash;
/**
 * smp_build_mpidr_hash - Pre-compute shifts required at each affinity
 *			  level in order to build a linear index from an
 *			  MPIDR value. Resulting algorithm is a collision
 *			  free hash carried out through shifting and ORing
 */
static void __init smp_build_mpidr_hash(void)
{
	u32 i, affinity, fs[4], bits[4], ls;
	u64 mask = 0;
	/*
	 * Pre-scan the list of MPIDRS and filter out bits that do
	 * not contribute to affinity levels, ie they never toggle.
	 */
	for_each_possible_cpu(i)
		mask |= (cpu_logical_map(i) ^ cpu_logical_map(0));
	pr_debug("mask of set bits %#llx\n", mask);
	/*
	 * Find and stash the last and first bit set at all affinity levels to
	 * check how many bits are required to represent them.
	 */
	for (i = 0; i < 4; i++) {
		affinity = MPIDR_AFFINITY_LEVEL(mask, i);
		/*
		 * Find the MSB bit and LSB bits position
		 * to determine how many bits are required
		 * to express the affinity level.
		 */
		ls = fls(affinity);
		fs[i] = affinity ? ffs(affinity) - 1 : 0;
		bits[i] = ls - fs[i];
	}
	/*
	 * An index can be created from the MPIDR_EL1 by isolating the
	 * significant bits at each affinity level and by shifting
	 * them in order to compress the 32 bits values space to a
	 * compressed set of values. This is equivalent to hashing
	 * the MPIDR_EL1 through shifting and ORing. It is a collision free
	 * hash though not minimal since some levels might contain a number
	 * of CPUs that is not an exact power of 2 and their bit
	 * representation might contain holes, eg MPIDR_EL1[7:0] = {0x2, 0x80}.
	 */
	mpidr_hash.shift_aff[0] = MPIDR_LEVEL_SHIFT(0) + fs[0];
	mpidr_hash.shift_aff[1] = MPIDR_LEVEL_SHIFT(1) + fs[1] - bits[0];
	mpidr_hash.shift_aff[2] = MPIDR_LEVEL_SHIFT(2) + fs[2] -
						(bits[1] + bits[0]);
	mpidr_hash.shift_aff[3] = MPIDR_LEVEL_SHIFT(3) +
				  fs[3] - (bits[2] + bits[1] + bits[0]);
	mpidr_hash.mask = mask;
	mpidr_hash.bits = bits[3] + bits[2] + bits[1] + bits[0];
	pr_debug("MPIDR hash: aff0[%u] aff1[%u] aff2[%u] aff3[%u] mask[%#llx] bits[%u]\n",
		mpidr_hash.shift_aff[0],
		mpidr_hash.shift_aff[1],
		mpidr_hash.shift_aff[2],
		mpidr_hash.shift_aff[3],
		mpidr_hash.mask,
		mpidr_hash.bits);
	/*
	 * 4x is an arbitrary value used to warn on a hash table much bigger
	 * than expected on most systems.
	 */
	if (mpidr_hash_size() > 4 * num_possible_cpus())
		pr_warn("Large number of MPIDR hash buckets detected\n");
}

static void __init setup_machine_fdt(phys_addr_t dt_phys)
{
	void *dt_virt = qin2pro_embedded_selected ?
		qin2pro_fdt_copy : fixmap_remap_fdt(dt_phys);
	const char *name;

	if (!dt_virt || !early_init_dt_scan(dt_virt)) {
		pr_crit("\n"
			"Error: invalid device tree blob at physical address %pa (virtual address 0x%p)\n"
			"The dtb must be 8-byte aligned and must not exceed 2 MB in size\n"
			"\nPlease check your bootloader.",
			&dt_phys, dt_virt);

		while (true)
			cpu_relax();
	}

	name = of_flat_dt_get_machine_name();
	if (!name)
		return;

	pr_info("Machine model: %s\n", name);
	dump_stack_set_arch_desc("%s (DT)", name);
}

static void __init request_standard_resources(void)
{
	struct memblock_region *region;
	struct resource *res;

	kernel_code.start   = __pa_symbol(_text);
	kernel_code.end     = __pa_symbol(__init_begin - 1);
	kernel_data.start   = __pa_symbol(_sdata);
	kernel_data.end     = __pa_symbol(_end - 1);

	for_each_memblock(memory, region) {
		res = alloc_bootmem_low(sizeof(*res));
		if (memblock_is_nomap(region)) {
			res->name  = "reserved";
			res->flags = IORESOURCE_MEM;
		} else {
			res->name  = "System RAM";
			res->flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;
		}
		res->start = __pfn_to_phys(memblock_region_memory_base_pfn(region));
		res->end = __pfn_to_phys(memblock_region_memory_end_pfn(region)) - 1;

		request_resource(&iomem_resource, res);

		if (kernel_code.start >= res->start &&
		    kernel_code.end <= res->end)
			request_resource(res, &kernel_code);
		if (kernel_data.start >= res->start &&
		    kernel_data.end <= res->end)
			request_resource(res, &kernel_data);
#ifdef CONFIG_KEXEC_CORE
		/* Userspace will find "Crash kernel" region in /proc/iomem. */
		if (crashk_res.end && crashk_res.start >= res->start &&
		    crashk_res.end <= res->end)
			request_resource(res, &crashk_res);
#endif
	}
}

u64 __cpu_logical_map[NR_CPUS] = { [0 ... NR_CPUS-1] = INVALID_HWID };

void __init setup_arch(char **cmdline_p)
{
	pr_info("Boot CPU: AArch64 Processor [%08x]\n", read_cpuid_id());

	sprintf(init_utsname()->machine, UTS_MACHINE);
	init_mm.start_code = (unsigned long) _text;
	init_mm.end_code   = (unsigned long) _etext;
	init_mm.end_data   = (unsigned long) _edata;
	init_mm.brk	   = (unsigned long) _end;

	*cmdline_p = boot_command_line;

	early_fixmap_init();
	early_ioremap_init();
	qin2pro_fb_mark(4, 0x99);

	setup_machine_fdt(qin2pro_prepare_fdt(__fdt_pointer));
	qin2pro_fb_mark(qin2pro_embedded_selected ? 6 : 7,
			qin2pro_embedded_selected ? 0xcc : 0x33);

	/*
	 * Initialise the static keys early as they may be enabled by the
	 * cpufeature code and early parameters.
	 */
	jump_label_init();
	qin2pro_fb_mark(8, 0x24);
	parse_early_param();
	qin2pro_fb_mark(9, 0x36);

	/*
	 *  Unmask asynchronous aborts after bringing up possible earlycon.
	 * (Report possible System Errors once we can report this occurred)
	 */
	local_async_enable();
	qin2pro_fb_mark(10, 0x48);

	/*
	 * TTBR0 is only used for the identity mapping at this stage. Make it
	 * point to zero page to avoid speculatively fetching new entries.
	 */
	cpu_uninstall_idmap();
	qin2pro_fb_mark(11, 0x5a);

	xen_early_init();
	qin2pro_fb_mark(12, 0x6c);
	efi_init();
	qin2pro_fb_mark(13, 0x7e);
	arm64_memblock_init();

	paging_init();
	qin2pro_fb_mark(21, 0xa4);

	acpi_table_upgrade();

	/* Parse the ACPI tables for possible boot-time configuration */
	acpi_boot_table_init();

	if (acpi_disabled)
		unflatten_device_tree();
	qin2pro_fb_mark(22, 0xb5);

	bootmem_init();
	qin2pro_fb_mark(23, 0xc6);

	kasan_init();
	qin2pro_fb_mark(24, 0xd7);

	request_standard_resources();
	qin2pro_fb_mark(25, 0xe8);

	early_ioremap_reset();
	qin2pro_fb_mark2(26, 0xf0);

	if (acpi_disabled)
		psci_dt_init();
	else
		psci_acpi_init();

	cpu_read_bootcpu_ops();
	smp_init_cpus();
	qin2pro_fb_mark2(27, 0x0f);
	smp_build_mpidr_hash();

	/* Init percpu seeds for random tags after cpus are set up. */
	kasan_init_tags();

#ifdef CONFIG_ARM64_SW_TTBR0_PAN
	/*
	 * Make sure init_thread_info.ttbr0 always generates translation
	 * faults in case uaccess_enable() is inadvertently called by the init
	 * thread.
	 */
	init_task.thread_info.ttbr0 = __pa_symbol(empty_zero_page);
#endif

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif
	if (boot_args[1] || boot_args[2] || boot_args[3]) {
		pr_err("WARNING: x1-x3 nonzero in violation of boot protocol:\n"
			"\tx1: %016llx\n\tx2: %016llx\n\tx3: %016llx\n"
			"This indicates a broken bootloader or old kernel\n",
			boot_args[1], boot_args[2], boot_args[3]);
	}
	qin2pro_fb_mark2(28, 0x1f);

}

static int __init topology_init(void)
{
	int i;

	for_each_online_node(i)
		register_one_node(i);

	for_each_possible_cpu(i) {
		struct cpu *cpu = &per_cpu(cpu_data.cpu, i);
		cpu->hotpluggable = 1;
		register_cpu(cpu, i);
	}

	return 0;
}
subsys_initcall(topology_init);

/*
 * Dump out kernel offset information on panic.
 */
static int dump_kernel_offset(struct notifier_block *self, unsigned long v,
			      void *p)
{
	const unsigned long offset = kaslr_offset();

	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE) && offset > 0) {
		pr_emerg("Kernel Offset: 0x%lx from 0x%lx\n",
			 offset, KIMAGE_VADDR);
	} else {
		pr_emerg("Kernel Offset: disabled\n");
	}
	return 0;
}

static struct notifier_block kernel_offset_notifier = {
	.notifier_call = dump_kernel_offset
};

static int __init register_kernel_offset_dumper(void)
{
	atomic_notifier_chain_register(&panic_notifier_list,
				       &kernel_offset_notifier);
	return 0;
}
__initcall(register_kernel_offset_dumper);

static int __init qin2pro_fb_late(void)
{
	void *fb = memremap(QIN2PRO_FB_PHYS + 29 * QIN2PRO_FB_PITCH,
			    QIN2PRO_FB_FILL, MEMREMAP_WB);

	if (fb) {
		memset(fb, 0x2f, QIN2PRO_FB_FILL);
		__flush_dcache_area(fb, QIN2PRO_FB_FILL);
		memunmap(fb);
	}
	return 0;
}
device_initcall(qin2pro_fb_late);
