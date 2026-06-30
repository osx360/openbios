/*
 *   Creation Date: <2004/08/28 18:38:22 greg>
 *   Time-stamp: <2004/08/28 18:38:22 greg>
 *
 *	<init.c>
 *
 *	Initialization for Xbox 360
 *
 *   Copyright (C) 2004 Greg Watson
 *   Copyright (C) 2005 Stefan Reinauer
 *   Copyright (C) 2026 John Davis
 *
 *   based on mol/init.c:
 *
 *   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004 Samuel & David Rydh
 *      (samuel@ibrium.se, dary@lindesign.se)
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *
 */

#include "config.h"
#include "libopenbios/openbios.h"
#include "libopenbios/bindings.h"
#include "libopenbios/console.h"
#include "drivers/pci.h"
#include "arch/common/nvram.h"
#include "drivers/drivers.h"
#include "xbox360/xbox360.h"
#include "libopenbios/ofmem.h"
#include "openbios-version.h"
#include "libc/byteorder.h"
#include "libc/vsprintf.h"
#define NO_QEMU_PROTOS
#include "arch/ppc/processor.h"

#include "../../drivers/timer.h" // TODO


extern void unexpected_excep(int vector);

static inline uint64_t
in_be64(volatile uint64_t *addr)
{
	uint32_t hi, lo;

	__asm__ __volatile__("ld %1,0(%2); eieio; srdi %0,%1,32":"=r"(hi),"=r"(lo):"b"(addr));
	return ((uint64_t)hi << 32) | lo;
}

static inline void
out_be64(volatile uint64_t *addr, uint64_t val)
{
    uint32_t hi = (uint32_t)(val >> 32);
    uint32_t lo = (uint32_t)(val);
    uint32_t tmp;

    __asm__ __volatile__(
        "rldicl %0, %2, 0, 32\n\t"   // Clear any garbage in the upper 32 bits of 'lo', store in 'tmp'
        "rldimi %0, %1, 32, 0\n\t"   // Shift 'hi' left 32 bits and insert it into the upper half of 'tmp'
        "std %0, 0(%3)\n\t"          // Store the combined 64-bit register to memory
        "eieio\n\t"
        : "=&r"(tmp)                 // Early-clobber output for our combined 64-bit value
        : "r"(hi), "r"(lo), "b"(addr) // 'b' constraint ensures a valid base register for the offset
        : "memory"
    );
}

void
unexpected_excep(int vector)
{
    unsigned long nip;
    asm volatile("mfsrr0 %0" : "=r" (nip) : );
    printk("openbios panic: Unexpected exception %x at %lx\n", vector, nip);
    for (;;) {
    }
}

extern void __divide_error(void);

void
__divide_error(void)
{
    return;
}

unsigned long isa_io_base;

extern struct _console_ops xbox360_console_ops;

int is_apple(void)
{
    return 1;
}

int is_oldworld(void)
{
    return 0;
}

int is_newworld(void)
{
    return 1;
}

static const pci_arch_t xbox360_arch = {
    .name = "XENON",
    .vendor_id = PCI_VENDOR_ID_MICROSOFT,
    .device_id = PCI_DEVICE_ID_MICROSOFT_XENON_HOST_BRIDGE,
    .cfg_addr = 0xD0000000,
    .cfg_data = 0xD0000000,
    .cfg_base = 0xD0000000,
    .cfg_len = 0x1000000,
    .host_pci_base = 0x0,
    .pci_mem_base = 0xEA000000,
    .mem_len = 0x10000000,
    .io_base = 0,
    .io_len = 0,
    .host_ranges = {
        { .type = MEMORY_SPACE_32, .parentaddr = 0, .childaddr = 0x80000000, .len = 0x80000000 },
        { .type = 0, .parentaddr = 0, .childaddr = 0, .len = 0 }
    },
    .irqs = { 15, 15, 15, 15 }
};

typedef union {
    unsigned char   data[16];
    unsigned long   data32[4];
} xbox360_smc_msg;

static void
xbox360_smc_write(xbox360_smc_msg *msg)
{
    while ((in_le32((volatile unsigned int*)(XBOX360_SMC_BASE + 0x84)) & 0x4) == 0);

    out_le32((volatile unsigned int*)(XBOX360_SMC_BASE + 0x84), 0x4);
    for (int i = 0; i < 4; i++) {
        out_be32((volatile unsigned int*)(XBOX360_SMC_BASE + 0x80), msg->data32[i]);
    }
    out_le32((volatile unsigned int*)(XBOX360_SMC_BASE + 0x84), 0x0);
}

static void
xbox360_reset_all(void)
{
    xbox360_smc_msg msg;
    bzero (&msg, sizeof (msg));

    msg.data[0] = 0x82;
    msg.data[1] = 0x04;
    msg.data[2] = 0x31;

    xbox360_smc_write(&msg);
}

static void
xbox360_poweroff(void)
{
    xbox360_smc_msg msg;
    bzero (&msg, sizeof (msg));

    msg.data[0] = 0x82;
    msg.data[1] = 0x01;

    xbox360_smc_write(&msg);
}

void
entry(void)
{
    isa_io_base = 0x80000000;

    arch = &xbox360_arch;

    // Switch framebuffer to new location
    out_be32((unsigned int*)(XBOX360_GPU_BASE + 0x6144), 1);
    out_be32((unsigned int*)(XBOX360_GPU_BASE + 0x6110), XBOX360_FB_BASE);
    out_be32((unsigned int*)(XBOX360_GPU_BASE + 0x6144), 0);

    out_be32((unsigned int*)(XBOX360_GPU_BASE + 0x65cc), 1);
    out_be32((unsigned int*)(XBOX360_GPU_BASE + 0x2840), XBOX360_FB_BASE);
    out_be32((unsigned int*)(XBOX360_GPU_BASE + 0x65cc), 0);

#ifdef CONFIG_DEBUG_CONSOLE
    init_console(xbox360_console_ops);
#endif

    ofmem_init();
    initialize_forth();
    /* won't return */

    printk("of_startup returned!\n");
    for (;;) {
    }
}

/* -- phys.lo ... phys.hi */
static void
push_physaddr(phys_addr_t value)
{
    PUSH(value);
}

/* From drivers/timer.c */
extern unsigned long timer_freq;

static void
memory_xenon_init(void)
{
    push_str("/memory");
    fword("find-device");

    push_physaddr(0);
    fword("encode-phys");

    PUSH(XBOX360_RAM_SIZE);
    fword("encode-int");
    fword("encode+");
    push_str("reg");
    fword("property");

    push_str(XBOX360_RAM_TYPE);
    fword("encode-string");
    push_str("dimm-types");
    fword("property");
}

static void
cpu_xenon_init(void)
{
    push_str("/cpus");
    fword("find-device");

    fword("new-device");

    push_str(XBOX360_CPU_NAME);
    fword("device-name");

    push_str("cpu");
    fword("device-type");

    PUSH(mfpvr());
    fword("encode-int");
    push_str("cpu-version");
    fword("property");

    PUSH(0x8000);
    fword("encode-int");
    push_str("d-cache-size");
    fword("property");

    PUSH(0x8000);
    fword("encode-int");
    push_str("i-cache-size");
    fword("property");

    PUSH(0x80);
    fword("encode-int");
    push_str("d-cache-sets");
    fword("property");

    PUSH(0x80);
    fword("encode-int");
    push_str("i-cache-sets");
    fword("property");

    PUSH(0x20);
    fword("encode-int");
    push_str("d-cache-block-size");
    fword("property");

    PUSH(0x20);
    fword("encode-int");
    push_str("i-cache-block-size");
    fword("property");

    PUSH(0x40);
    fword("encode-int");
    push_str("tlb-sets");
    fword("property");

    PUSH(0x80);
    fword("encode-int");
    push_str("tlb-size");
    fword("property");

    timer_freq = XBOX360_TIMEBASE_FREQ;
    PUSH(timer_freq);
    fword("encode-int");
    push_str("timebase-frequency");
    fword("property");

    PUSH(XBOX360_CPU_FREQ);
    fword("encode-int");
    push_str("clock-frequency");
    fword("property");

    PUSH(XBOX360_TIMEBASE_FREQ * 4);
    fword("encode-int");
    push_str("bus-frequency");
    fword("property");

    push_str("running");
    fword("encode-string");
    push_str("state");
    fword("property");

    PUSH(0x20);
    fword("encode-int");
    push_str("reservation-granule-size");
    fword("property");

    PUSH(0);
    fword("encode-int");
    push_str("reg");
    fword("property");

    fword("finish-device");
}


static void arch_go(void);

static void
arch_go(void)
{
    /* Insert copyright property for MacOS 9 and below */
    if (find_dev("/rom/macos")) {
        fword("insert-copyright-property");
    }
}

/*
 *  filll        ( addr bytes quad -- )
 */

static void ffilll(void)
{
    const u32 longval = POP();
    u32 bytes = POP();
    u32 *laddr = (u32 *)cell2pointer(POP());
    u32 len;

    for (len = 0; len < bytes / sizeof(u32); len++) {
        *laddr++ = longval;
    }
}

/*
 * adler32        ( adler buf len -- checksum )
 *
 * Adapted from Mark Adler's original implementation (zlib license)
 *
 * Both OS 9 and BootX require this word for payload validation.
 */

#define DO1(buf,i)  {s1 += buf[i]; s2 += s1;}
#define DO2(buf,i)  DO1(buf,i); DO1(buf,i+1);
#define DO4(buf,i)  DO2(buf,i); DO2(buf,i+2);
#define DO8(buf,i)  DO4(buf,i); DO4(buf,i+4);
#define DO16(buf)   DO8(buf,0); DO8(buf,8);

static void adler32(void)
{
    uint32_t len = (uint32_t)POP();
    char *buf = (char *)POP();
    uint32_t adler = (uint32_t)POP();

    if (buf == NULL) {
        RET(-1);
    }

    uint32_t base = 65521;
    uint32_t nmax = 5552;

    uint32_t s1 = adler & 0xffff;
    uint32_t s2 = (adler >> 16) & 0xffff;

    uint32_t k;
    while (len > 0) {
        k = (len < nmax ? len : nmax);
        len -= k;

        while (k >= 16) {
            DO16(buf);
            buf += 16;
            k -= 16;
        }
        if (k != 0) {
            do {
                s1 += *buf++;
                s2 += s1;
            } while (--k);
        }

        s1 %= base;
        s2 %= base;
    }

    RET(s2 << 16 | s1);
}

/* ( size -- virt ) */
static void
dma_alloc(void)
{
    ucell size = POP();
    ucell addr;
    int ret;

    ret = ofmem_posix_memalign((void *)&addr, size, PAGE_SIZE);

    if (ret) {
        PUSH(0);
    } else {
        PUSH(addr);
    }
}

/* ( virt size cacheable? -- devaddr ) */
static void
dma_map_in(void)
{
    POP();
    POP();
    ucell va = POP();
    PUSH(va);
}

/* ( virt devaddr size -- ) */
static void
dma_sync(void)
{
    ucell size = POP();
    POP();
    ucell virt = POP();

    flush_dcache_range(cell2pointer(virt), cell2pointer(virt + size));
    flush_icache_range(cell2pointer(virt), cell2pointer(virt + size));
}

static void
ehci_disable(uint32_t addr)
{
    // Stop EHCI controller and wait for halt bit to be set.
    out_le32((volatile unsigned int*)(addr + 0x20), in_le32((volatile unsigned int*)(addr + 0x20)) & ~(0x1));
    while ((in_le32((volatile unsigned int*)(addr + 0x24)) & (1 << 12)) == 0) {
        mdelay(1);
    }

    // Reset the controller and wait for reset to complete.
    out_le32((volatile unsigned int*)(addr + 0x20), in_le32((volatile unsigned int*)(addr + 0x20)) | 0x2);
    while (in_le32((volatile unsigned int*)(addr + 0x20)) & 0x2) {
        mdelay(1);
    }
}

void
arch_of_init(void)
{
    char buf[256];
    const char *stdin_path, *stdout_path, *boot_path;
    char *boot_device;
    ofmem_t *ofmem = ofmem_arch_get_private();
    ucell load_base;

    openbios_init();
    modules_init();
    setup_timers();

    bind_func("ppc-dma-alloc", dma_alloc);
    feval("['] ppc-dma-alloc to (dma-alloc)");
    bind_func("ppc-dma-map-in", dma_map_in);
    feval("['] ppc-dma-map-in to (dma-map-in)");
    bind_func("ppc-dma-sync", dma_sync);
    feval("['] ppc-dma-sync to (dma-sync)");

    printk("\n");
    printk("=============================================================\n");
    printk(PROGRAM_NAME " " OPENBIOS_VERSION_STR " [%s]\n",
           OPENBIOS_BUILD_DATE);

    printk("Memory: %dM\n", ofmem->ramsize / 1024 / 1024);

    //
    // Create the device tree info.
    //
    memory_xenon_init();
    cpu_xenon_init();
    printk("%s PVR 0x%X\n", XBOX360_CPU_NAME, mfpvr());

    snprintf(buf, sizeof(buf), "/cpus/%s", XBOX360_CPU_NAME);
    ofmem_register(find_dev("/memory"), find_dev(buf));
    node_methods_init(buf);

    stdin_path = "keyboard";
    stdout_path = "screen";

    /* Setup nvram variables */
    push_str("/options");
    fword("find-device");

    /* No bootorder present, use fw_cfg device if no custom
        boot-device specified */
    fword("boot-device");
    boot_device = pop_fstr_copy();

    if (boot_device && strcmp(boot_device, "disk") == 0) {
        boot_path = "hd";

        snprintf(buf, sizeof(buf),
                    "%s:,\\\\:tbxi "
                    "%s:,\\ppc\\bootinfo.txt "
                    "%s:,%%BOOT",
                    boot_path, boot_path, boot_path);

        push_str(buf);
        fword("encode-string");
        push_str("boot-device");
        fword("property");
    }

    free(boot_device);

    /* Set up other properties */

    push_str("/chosen");
    fword("find-device");

    push_str(stdin_path);
    fword("pathres-resolve-aliases");
    push_str("input-device");
    fword("$setenv");

    push_str(stdout_path);
    fword("pathres-resolve-aliases");
    push_str("output-device");
    fword("$setenv");

    fword("activate-tty-interface");

    // Disable EHCI controllers to use OHCI only.
    ehci_disable(0xEA003000);
    ehci_disable(0xEA005000);

    // Configure PCI.
    push_str("/");
    fword("find-device");
    feval("\" /\" open-dev to my-self");
    ob_pci_init();
    feval("0 to my-self");

    for (int i = 0; i < 0x10; i++) {
        out_le32((uint32_t*)(0xEA000000 + 0x10 + i * 4), 0);
    }

    // Ack all outstanding interrupts.
    while (in_be64((uint64_t*)(XBOX360_IC_VIRT + 0x50)) != 0x7C);
    out_be64((uint64_t*)(XBOX360_IC_VIRT + 0x68), 0);

    device_end();

    // Bind poweroff and reset words.
    bind_func("ppc32-power-off", xbox360_poweroff);
    feval("['] ppc32-power-off to power-off");
    bind_func("ppc32-reset-all", xbox360_reset_all);
    feval("['] ppc32-reset-all to reset-all");

    /* Implementation of filll word (required by BootX) */
    bind_func("filll", ffilll);

    /* Implementation of adler32 word (required by OS 9, BootX) */
    bind_func("(adler32)", adler32);

    bind_func("platform-boot", boot);
    bind_func("(arch-go)", arch_go);

    /* Allocate 8MB memory at load-base */
    fword("load-base");
    load_base = POP();
    ofmem_claim_phys(load_base, 0x800000, 0);
    ofmem_claim_virt(load_base, 0x800000, 0);
    ofmem_map(load_base, load_base, 0x800000, 0);
}
