/*
 *   Creation Date: <1999/11/07 19:02:11 samuel>
 *   Time-stamp: <2004/01/07 19:42:36 samuel>
 *
 *	<ofmem.c>
 *
 *	OF Memory manager
 *
 *   Copyright (C) 1999-2004 Samuel Rydh (samuel@ibrium.se)
 *   Copyright (C) 2004 Stefan Reinauer
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *
 */

#include "config.h"
#include "libopenbios/bindings.h"
#include "libc/string.h"
#include "libopenbios/ofmem.h"
#include "kernel.h"
#include "mmutypes.h"
#include "asm/processor.h"
#include "xbox360.h"

#define BIT(n)		(1U << (31 - (n)))

/* called from assembly */
extern void dsi_exception(void);
extern void isi_exception(void);
extern void setup_mmu(void);

/****************************************************************
 * Memory usage (before of_quiesce is called)
 *
 *			Physical
 *
 *	0x00000000	Exception vectors
 *	0x00004000	Free space
 *  0x07DFFFFF  End of free space
 *  0x07E00000  Open Firmware (us)
 *  0x08000000  OF allocations top
 *  0x08000000  Hash table
 *	0x08100000	Free space
 *
 * Allocations grow downwards from 0x08000000
 *
 ****************************************************************/

#define HASH_BITS		15
#define HASH_SIZE		(2 << HASH_BITS)
#define SEGR_BASE		0x0400

#define FREE_BASE_1		0x00004000

#define OF_CODE_START	&_start
#define OF_CODE_END	    &_end
#define OF_CODE_SIZE    (((OF_CODE_END - OF_CODE_START) + 4095) & ~(0xFFF))
#define OF_MALLOC_BASE	&_end

#define HASH_BASE		0x08000000
#define FREE_BASE_2		((HASH_BASE + HASH_SIZE + 0xFFFFF) & ~0x000FFFFFUL)

static ofmem_t s_ofmem;

#define IO_BASE			0x80000000
#define OFMEM			(&s_ofmem)

static inline unsigned long
get_hash_base(void)
{
    return HASH_BASE;
}

static unsigned long
get_heap_top(void)
{
    return HASH_BASE;
}

static inline size_t
ALIGN_SIZE(size_t x, size_t a)
{
    return (x + a - 1) & ~(a - 1);
}

ofmem_t*
ofmem_arch_get_private(void)
{
    return OFMEM;
}

void*
ofmem_arch_get_malloc_base(void)
{
    return OF_MALLOC_BASE;
}

ucell
ofmem_arch_get_heap_top(void)
{
    return get_heap_top();
}

ucell
ofmem_arch_get_virt_top(void)
{
    return XBOX360_FB_BASE;
}

void
ofmem_arch_unmap_pages(ucell virt, ucell size)
{
    /* kill page mappings in provided range */
}

void
ofmem_arch_map_pages(phys_addr_t phys, ucell virt, ucell size, ucell mode)
{
    /* none yet */
}

ucell
ofmem_arch_get_iomem_base(void)
{
    return 0;
}

ucell
ofmem_arch_get_iomem_top(void)
{
    return 0;
}

retain_t *
ofmem_arch_get_retained(void)
{
    /* not implemented */
    return NULL;
}

int
ofmem_arch_get_physaddr_cellsize(void)
{
    return 1;
}

int
ofmem_arch_encode_physaddr(ucell *p, phys_addr_t value)
{
    int n = 0;
    p[n++] = value;
    return n;
}

/* Return size of a single MMU package translation property entry in cells */
int ofmem_arch_get_translation_entry_size(void) {
    return 3 + ofmem_arch_get_physaddr_cellsize();
}

/* Generate translation property entry for PPC.
 * According to the platform bindings for PPC
 * (http://www.openfirmware.org/1275/bindings/ppc/release/ppc-2_1.html#REF34579)
 * a translation property entry has the following layout:
 *
 *      virtual address
 *      length
 *      physical address
 *      mode
 */
void ofmem_arch_create_translation_entry(ucell *transentry, translation_t *t) {
    int i = 0;

    transentry[i++] = t->virt;
    transentry[i++] = t->size;
    i += ofmem_arch_encode_physaddr(&transentry[i], t->phys);
    transentry[i++] = t->mode;
}

/* Return the size of a memory available entry given the phandle in cells */
int ofmem_arch_get_available_entry_size(phandle_t ph) {
    if (ph == s_phandle_memory) {
        return 1 + ofmem_arch_get_physaddr_cellsize();
    } else {
        return 1 + 1;
    }
}

/* Generate memory available property entry for PPC */
void ofmem_arch_create_available_entry(phandle_t ph, ucell *availentry, phys_addr_t start, ucell size) {
    int i = 0;

    if (ph == s_phandle_memory) {
        i += ofmem_arch_encode_physaddr(availentry, start);
    } else {
    availentry[i++] = start;
    }

    availentry[i] = size;
}

/************************************************************************/
/*	OF private allocations						*/
/************************************************************************/

/* Private functions for mapping between physical/virtual addresses */
phys_addr_t
va2pa(unsigned long va)
{
    return (phys_addr_t)va;
}

unsigned long
pa2va(phys_addr_t pa)
{
    return (unsigned long)pa;
}

void *
malloc(size_t size)
{
    return ofmem_malloc(size);
}

void
free(void *ptr)
{
    ofmem_free(ptr);
}

void *
realloc(void *ptr, size_t size)
{
    return ofmem_realloc(ptr, size);
}

#define	SEGR_USER		BIT(2)
#define SEGR_BASE		0x0400

#define SLB_VSID_SHIFT	12
#define SLB_ESID_VALID	(1ULL << 27)
#define SLB_ESID_SHIFT	28


/************************************************************************/
/*	misc								*/
/************************************************************************/

ucell
ofmem_arch_default_translation_mode(phys_addr_t phys)
{
    /* XXX: Guard bit not set as it should! */
    if (phys < XBOX360_FB_BASE)
        return 0x02;	/*0xa*/	/* wim GxPp */
    return 0x6a;		/* WIm GxPp, I/O */
}

ucell ofmem_arch_io_translation_mode(phys_addr_t phys)
{
    return 0x6a;		/* WIm GxPp, I/O */
}

/************************************************************************/
/*	page fault handler						*/
/************************************************************************/

static phys_addr_t
ea_to_phys(unsigned long ea, ucell *mode)
{
    phys_addr_t phys;

    /* hardcode our translation needs */
    if( ea >= (unsigned long)OF_CODE_START && ea < (unsigned long)OF_CODE_END ) {
        *mode = 0x02;
        return ea;
    }

    phys = ofmem_translate(ea, mode);
    if(phys == -1) {
        phys = ea;
        *mode = ofmem_arch_default_translation_mode(phys);

        /* print_virt_range(); */
        /* print_phys_range(); */
        /* print_trans(); */
    }
    return phys;
}

/* Return the next slot to evict, in the range of [0..7] */
static int
next_evicted_slot(void)
{
    static int next_grab_slot;
    int *next_grab_slot_va;
    int r;

    next_grab_slot_va = &next_grab_slot;
    r = *next_grab_slot_va;
    *next_grab_slot_va = (r + 1) % 8;

    return r;
}

static void
hash_page(unsigned long ea, uint64_t phys, ucell mode)
{
    uint64_t vsid_mask, page_mask, pgidx, hash;
    uint64_t htab_mask, mask, avpn;
    uint64_t pgaddr;
    int i, found;
    uint64_t vsid, vsid_sh, sdr, sdr_sh, sdr_mask;
    mPTE_64_t *pp;

    vsid = (ea >> 28) + SEGR_BASE;
    vsid_sh = 7;
    vsid_mask = 0x00003FFFFFFFFF80ULL;
    sdr = mfsdr1();
    sdr_sh = 18;
    sdr_mask = 0x3FF80;
    page_mask = 0x0FFFFFFF; // XXX correct?
    pgidx = (ea & page_mask) >> PAGE_SHIFT;
    avpn = (vsid << 12) | ((pgidx >> 4) & 0x0F80);

    hash = ((vsid ^ pgidx) << vsid_sh) & vsid_mask;
    htab_mask = 0x0FFFFFFF >> (28 - (sdr & 0x1F));
    mask = (htab_mask << sdr_sh) | sdr_mask;
    pgaddr = sdr | (hash & mask);
    pp = (mPTE_64_t *)(unsigned long)pgaddr;

    /* replace old translation */
    for (found = 0, i = 0; !found && i < 8; i++)
        if (pp[i].avpn == avpn)
            found = 1;


    /* otherwise use a free slot */
    if (!found) {
        for (i = 0; !found && i < 8; i++)
            if (!pp[i].v)
                found = 1;
    }

    /* out of slots, just evict one */
    if (!found)
        i = next_evicted_slot() + 1;
    i--;

    {
    mPTE_64_t p = {
        // .avpn_low = avpn,
        .avpn = avpn >> 7,
        .h = 0,
        .v = 1,

        .rpn = (phys & ~0xFFFULL) >> 12,
        .r = mode & (1 << 8) ? 1 : 0,
        .c = mode & (1 << 7) ? 1 : 0,
        .w = mode & (1 << 6) ? 1 : 0,
        .i = mode & (1 << 5) ? 1 : 0,
        .m = mode & (1 << 4) ? 1 : 0,
        .g = mode & (1 << 3) ? 1 : 0,
        .n = mode & (1 << 2) ? 1 : 0,
        .pp = mode & 3,
    };
    pp[i] = p;
    }

    asm volatile("tlbie %0" :: "r"(ea));
}

void
dsi_exception(void)
{
    unsigned long dar, dsisr;
    ucell mode;
    phys_addr_t phys;

    asm volatile("mfdar %0" : "=r" (dar) : );
    asm volatile("mfdsisr %0" : "=r" (dsisr) : );

    phys = ea_to_phys(dar, &mode);
    hash_page(dar, phys, mode);
}

void
isi_exception(void)
{
    unsigned long nip, srr1;
    ucell mode;
    phys_addr_t phys;

    asm volatile("mfsrr0 %0" : "=r" (nip) : );
    asm volatile("mfsrr1 %0" : "=r" (srr1) : );

    phys = ea_to_phys(nip, &mode);
    hash_page(nip, phys, mode);
}

/************************************************************************/
/*	init / cleanup							*/
/************************************************************************/

void
setup_mmu(void)
{
	// Configure SDR1 for the hash tables
    memset((void *)HASH_BASE, 0, HASH_SIZE);
	mtsdr1(HASH_BASE | MAX(HASH_BITS - 18, 0));

	// Reconfigure SLBs to cover 4GB of address space (16x 256MB segments).
    slbia();
	for (unsigned long i = 0; i < 16; i++) {
        unsigned long rs = (SEGR_BASE + i) << SLB_VSID_SHIFT;
        unsigned long rb = (i << SLB_ESID_SHIFT) | SLB_ESID_VALID | i;
        slbmte(rs, rb);
    }

	// Enable MMU.
	mtmsr(mfmsr() | MSR_IR | MSR_DR);

    // Map the interrupt controller for later use.
    hash_page(XBOX360_IC_VIRT, XBOX360_IC_PHYS, 0x6a);
}

void
ofmem_init(void)
{
	ofmem_t *ofmem = OFMEM;
	/* In case we can't rely on memory being zero initialized */
	memset(ofmem, 0, sizeof(*ofmem));

    ofmem->ramsize = XBOX360_RAM_SIZE_ACTUAL;

	ofmem_claim_phys(0, FREE_BASE_1, 0);
	ofmem_claim_virt(0, FREE_BASE_1, 0);
    ofmem_claim_phys((phys_addr_t)OF_CODE_START, FREE_BASE_2 - (ucell)OF_CODE_START, 0 );
	ofmem_claim_virt((phys_addr_t)OF_CODE_START, FREE_BASE_2 - (ucell)OF_CODE_START, 0 );
}
