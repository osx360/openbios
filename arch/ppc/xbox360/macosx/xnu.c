/*
 *	<xnu.c>
 *
 *   Copyright (C) 2025-2026 John Davis
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *
 */

#include "config.h"
#include "libopenbios/bindings.h"
#include "macosx.h"
#include "macho-loader.h"
#include "nlist.h"

#define PPC_OPCODE_SIZE             4
#define PPC_BRANCH_MASK             0x3FFFFFC

#define PROCESSOR_VERSION_970       0x00390000
#define PROCESSOR_VERSION_XENON     0x00710000

#define CPU_SUBTYPE_POWERPC_750     9
#define CPU_SUBTYPE_POWERPC_970     100

// CPU features from XNU exception.h
#define pfFloat     0x80000000
#define pfAltivec   0x40000000
#define pfSMPcap    0x10000000
#define pfCanSleep  0x08000000
#define pfCanNap    0x04000000
#define pfCanDoze   0x02000000
#define pfSlowNap   0x00400000
#define pfNoMuMMCK  0x00200000
#define pfNoL2PFNap 0x00100000
#define pfSCOMFixUp 0x00080000
#define pfL2        0x00008000
#define	pf128Byte   0x00000080
#define	pf64Bit     0x00000010

// CPU capabilities from XNU cpu_capabilities.h
#define	kHasAltivec                 0x00000001  // Has Altivec
#define	k64Bit                      0x00000002  // CPU is 64-bit
#define	kCache128                   0x00000010  // 128 byte cacheline size
#define	kDataStreamsRecommended     0x00000080  // dst, dstt, dstst, dss, and dssall instructions available and recommended
#define	kDataStreamsAvailable       0x00000100  // dst, dstt, dstst, dss, and dssall instructions available (may or may not be rec'd)
#define	kDcbtStreamsRecommended     0x00000200  // Enhanced dcbt instruction available and recommended
#define	kDcbtStreamsAvailable       0x00000400  // Enhanced dcbt instruction available (but may or may not be recommended)

#define	kHasGraphicsOps             0x08000000	// Has fres, frsqrte, and fsel instructions
#define	kHasStfiwx                  0x10000000	// Has stfiwx instruction
#define	kHasFsqrt                   0x20000000	// Has fsqrt and fsqrts instructions

//
// Patches XNU to prevent double character prints on panic in 10.4.
//
#define XNU_DISABLE_CONSDEBUG_PUTC      1

static unsigned long vectors_addr = 0;

static int
find_stored_bootx_vectors(unsigned long end_addr)
{
    const uint32_t find_vector[] = {
        0x7DB243A6, // mtsprg 2, r13
        0x7D7343A6, // mtsprg 3, r11
    };

    //
    // Search downwards for vector signatures.
    //
    end_addr -= 0x4000;
    while (end_addr > 0x4000) {
        const char *data_ptr = (const char*)(end_addr);
        if ((memcmp(&data_ptr[0x100], find_vector, sizeof (find_vector)) == 0) &&
            (memcmp(&data_ptr[0x200], find_vector, sizeof (find_vector)) == 0) &&
            (memcmp(&data_ptr[0x300], find_vector, sizeof (find_vector)) == 0) &&
            (memcmp(&data_ptr[0x380], find_vector, sizeof (find_vector)) == 0) &&
            (memcmp(&data_ptr[0x400], find_vector, sizeof (find_vector)) == 0) &&
            (memcmp(&data_ptr[0x480], find_vector, sizeof (find_vector)) == 0) &&
            (memcmp(&data_ptr[0x500], find_vector, sizeof (find_vector)) == 0) &&
            (memcmp(&data_ptr[0x600], find_vector, sizeof (find_vector)) == 0) &&
            (memcmp(&data_ptr[0x700], find_vector, sizeof (find_vector)) == 0)) {
            vectors_addr = end_addr;
            printk("find_stored_bootx_vectors: Found stored XNU vectors at 0x%lX\n", vectors_addr);
            return 1;
        }

        end_addr--;
    }

    printk("find_stored_bootx_vectors: Failed to locate stored XNU vectors\n");
    return 0;
}

//
// Find a pattern.
//
static int
find_patch_pattern(const char *data, size_t *data_offset, size_t data_length,
    const void *find, const void *find_mask, size_t find_length)
{
    size_t  curr_offset;
    size_t  last_offset;
    size_t  i;

    const char *find_buf = (const char*) find;
    const char *find_mask_buf = (const char*) find_mask;

    curr_offset = *data_offset;
    last_offset = data_length - find_length;
    if (find_mask_buf) {
        while (curr_offset <= last_offset) {
            for (i = 0; i < find_length; i++) {
                if ((data[curr_offset + i] & find_mask_buf[i]) != find_buf[i])
                    break;
            }

            if (i == find_length) {
                *data_offset = curr_offset;
                return 1;
            }

            curr_offset++;
        }
    } else {
        while (curr_offset <= last_offset) {
            for (i = 0; i < find_length; i++) {
                if (data[curr_offset + i] != find_buf[i])
                    break;
            }

            if (i == find_length) {
                *data_offset = curr_offset;
                return 1;
            }

            curr_offset++;
        }
    }

    return 0;
}

//
// Patch a pattern starting at the specified symbol.
//
static int
patch_pattern(macho_sym_context_t *sym_context, const char *symbol, size_t search_length,
    const void *find, const void *find_mask, const void *repl, const void *repl_mask, size_t patch_length)
{
    unsigned long   symbol_addr;
    char            *base;
    size_t          base_offset;

    const char *repl_buf = (const char*) repl;
    const char *repl_mask_buf = (const char*) repl_mask;

    // Get location at symbol.
    symbol_addr = macho_resolve_symbol(sym_context, symbol);
    if (symbol_addr == 0)
        return 0;

    // Vectors aren't in the final spot yet, they are copied by BootX just before kernel handoff.
    if (symbol_addr < 0x4000)
        symbol_addr += vectors_addr;
    base = (char*)symbol_addr;

    // Look for pattern after symbol.
    base_offset = 0;
    if (!find_patch_pattern(base, &base_offset, search_length, find, find_mask, patch_length)) {
        printk("xnu_patch_pattern: Failed to find patch pattern for %s patch (%u bytes)\n", symbol, patch_length);
        return 0;
    }


    // Perform the patch.
    if (repl_mask_buf) {
        for (size_t i = 0; i < patch_length; i++) {
            base[base_offset + i] = (base[base_offset + i] & ~repl_mask_buf[i]) | (repl_buf[i] & repl_mask_buf[i]);
        }
    } else {
        memcpy(&base[base_offset], repl, patch_length);
    }

    return 1;
}

//
// Patch function to return 0.
//
static int
patch_disable_function(macho_sym_context_t *sym_context, const char *symbol)
{
    const uint32_t find[] = {
        0x00000000,
        0x00000000
    };
    const uint32_t repl[] = {
        0x38600000,
        0x4E800020
    };

    return patch_pattern(sym_context, symbol, 0x100, find, find, repl, NULL, sizeof(find));
}

//
// Patch the CPU type checking to enable Xenon matching.
//
static int
patch_cpu_check(macho_sym_context_t *sym_context, uint32_t xnu_version)
{
    static const uint32_t find[] = {
        0xFFFF0000,                 // PVR mask
        PROCESSOR_VERSION_970,      // PVR version and revision
        pfFloat | pfAltivec | pfSMPcap | pfCanSleep | pfCanNap | pf128Byte | pf64Bit | pfL2 | pfSCOMFixUp,  // Features
        kHasAltivec | k64Bit | kCache128 | kDataStreamsAvailable | kDcbtStreamsRecommended | kDcbtStreamsAvailable | kHasGraphicsOps | kHasStfiwx | kHasFsqrt,  // Capabilities
        0x00000000,                 // Patch features
        0x00000000,                 // Init function
        CPU_SUBTYPE_POWERPC_970
    };
    static const uint32_t repl[] = {
        0xFFFF0000,                 // PVR mask
        PROCESSOR_VERSION_XENON,    // PVR version and revision
        pfFloat | pfSMPcap | pf128Byte | pf64Bit | pfL2 | pfSCOMFixUp,  // Features
        k64Bit | kCache128,         // Capabilities
        0x00000000,                 // Patch features
        0x00000000,                 // Init function
        CPU_SUBTYPE_POWERPC_970
    };
    static const uint32_t mask[] = {
        0xFFFFFFFF,     // PVR mask
        0xFFFFFFFF,     // PVR version and revision
        0xFFFFFFFF,     // Features
        0xFFFFFFFF,     // Capabilities
        0x00000000,     // Patch features
        0x00000000,     // Init function
        0xFFFFFFFF
    };

    static const uint32_t find_tiger[] = {
        0xFFFF0000,                 // PVR mask
        PROCESSOR_VERSION_970,      // PVR version and revision
        pfFloat | pfAltivec | pfSMPcap | pfCanSleep | pfCanNap | pf128Byte | pf64Bit | pfL2 | pfSCOMFixUp,  // Features
        kHasAltivec | k64Bit | kCache128 | kDataStreamsAvailable | kDcbtStreamsRecommended | kDcbtStreamsAvailable | kHasGraphicsOps | kHasStfiwx | kHasFsqrt,  // Capabilities
        0x00000000,                 // Power management features
        0x00000000,                 // Patch features
        0x00000000,                 // Init function
        CPU_SUBTYPE_POWERPC_970
    };
    static const uint32_t repl_tiger[] = {
        0xFFFF0000,                 // PVR mask
        PROCESSOR_VERSION_XENON,    // PVR version and revision
        pfFloat | pfSMPcap | pf128Byte | pf64Bit | pfL2 | pfSCOMFixUp,  // Features
        k64Bit | kCache128,         // Capabilities
        0x00000000,                 // Power management features
        0x00000000,                 // Patch features
        0x00000000,                 // Init function
        CPU_SUBTYPE_POWERPC_970
    };
    static const uint32_t mask_tiger[] = {
        0xFFFFFFFF,     // PVR mask
        0xFFFFFFFF,     // PVR version and revision
        0xFFFFFFFF,     // Features
        0xFFFFFFFF,     // Capabilities
        0xFFFFFFFF,     // Power management features
        0x00000000,     // Patch features
        0x00000000,     // Init function
        0xFFFFFFFF
    };

    if (xnu_match_darwin_version(xnu_version, XNU_VERSION_TIGER_MIN, XNU_VERSION_LEOPARD_MAX)) {
        if (!patch_pattern(sym_context, "__start", 0x1000, find_tiger, mask_tiger, repl_tiger, mask_tiger, sizeof(find_tiger))) {
            printk("xnu_patch_cpu_check: Failed to patch CPU info\n");
            return 0;
        }
    } else {
        if (!patch_pattern(sym_context, "__start", 0x1000, find, mask, repl, mask, sizeof(find))) {
            printk("xnu_patch_cpu_check: Failed to patch CPU info\n");
            return 0;
        }
    }

    printk("xnu_patch_cpu_check: Patched CPU info\n");
    return 1;
}

//
// Patch reported L2 cache size to 1MB. Cosmetic patch.
//
static void
patch_l2_size(macho_sym_context_t *sym_context, uint32_t xnu_version)
{
    const char find_tiger[] = {
        0x3E, 0x80, 0x00, 0x08,     // lis r20, 0x8
        0x38, 0x00, 0x00, 0x00      // lis r0, 0
    };
    const char repl_tiger[] = {
        0x3E, 0x80, 0x00, 0x10,     // lis r20, 0x10
        0x38, 0x00, 0x00, 0x00      // lis r0, 0
    };

    const char *find;
    const char *repl;
    size_t length;

    if (xnu_match_darwin_version(xnu_version, XNU_VERSION_TIGER_MIN, XNU_VERSION_LEOPARD_MAX)) {
        find = find_tiger;
        repl = repl_tiger;
        length = sizeof (find_tiger);
    } else {
        return;
    }

    if (!patch_pattern(sym_context, "__start", 0x1000, find, NULL, repl, NULL, length)) {
        printk("xnu_patch_l2_size: Failed to patch L2 size\n");
        return;
    }

    printk("xnu_patch_l2_size: Patched L2 size\n");
}

//
// Patch mapalc1 to remove use of dcbz instructions.
// On the G5, these clear 32 bytes of memory. On Xenon they clear 128 bytes (cacheline size), clobbering surrounding memory.
//
static int
patch_mapalc1_dcbz(macho_sym_context_t *sym_context)
{
    unsigned long   symbol_addr;
    char            *base_mapalc1;
    char            *base_hw_hash_init;
    size_t          offset_mapalc1;
    size_t          offset_hw_hash_init;

    int32_t         disp_mapalc1;
    int32_t         disp_hw_hash_init;

    //
    // Original dcbz instructions in mapalc1
    //
    const uint32_t find_mapalc1[] = {
        0x38E60020, // addi r7, r6, 0x20
        0x7C0667EC, // dcbz r6, r12
        0x7C0767EC  // dcbz r7, r12
    };
    uint32_t repl_mapalc1[] = {
        0x48000000, // b ... (replaced at runtime)
        0x60000000, // nop
        0x60000000  // nop
    };

    //
    // 32-bit codepath in hw_hash_init unused on 64-bit processors
    //
    const uint32_t find_hw_hash_init[] = {
        0x816B0004  // lwz r11, 4(r11)
    };
    uint32_t repl_hw_hash_init[] = {
        0x7CC66214, // add r6, r6, r12
        0x38E00000, // li  r7, 0x0
        0xF8E60000, // std r7, 0(r6)
        0xF8E60008, // std r7, 8(r6)
        0xF8E60010, // std r7, 16(r6)
        0xF8E60018, // std r7, 24(r6)
        0xF8E60020, // std r7, 32(r6)
        0xF8E60028, // std r7, 40(r6)
        0xF8E60030, // std r7, 48(r6)
        0xF8E60038, // std r7, 56(r6)
        0x48000000  // b ... (replaced at runtime)
    };

    //
    // Get location in mapalc1 to patch jump into.
    //
    symbol_addr = macho_resolve_symbol(sym_context, "_mapalc1");
    if (symbol_addr == 0)
        return 0;
    base_mapalc1 = (char*)symbol_addr;

    offset_mapalc1 = 0;
    if (!find_patch_pattern(base_mapalc1, &offset_mapalc1, 0x200, find_mapalc1, NULL, sizeof(find_mapalc1))) {
        printk("patch_mapalc1_dcbz: Failed to locate patch area in mapalc1\n");
        return 0;
    }

    //
    // Get location in hw_hash_init to steal space from.
    // 32-bit code is never called, can use it as a jump point.
    //
    symbol_addr = macho_resolve_symbol(sym_context, "_hw_hash_init");
    if (symbol_addr == 0)
        return 0;
    base_hw_hash_init = (char*)symbol_addr;

    offset_hw_hash_init = 0;
    if (!find_patch_pattern(base_hw_hash_init, &offset_hw_hash_init, 0x200, find_hw_hash_init, NULL, sizeof(find_hw_hash_init))) {
        printk("patch_mapalc1_dcbz: Failed to locate patch area in hw_hash_init\n");
        return 0;
    }
    printk("patch_mapalc1_dcbz: mapalc1 offset 0x%X, hw_hash_init offset 0x%X\n", offset_mapalc1, offset_hw_hash_init);

    //
    // Insert jump from mapalc1 to patch area within hw_hash_init. TODO: assumes hw_hash_init is always after mapalc1.
    //
    disp_mapalc1 = (int32_t)((size_t)base_hw_hash_init + offset_hw_hash_init) - (int32_t)((size_t)base_mapalc1 + offset_mapalc1);
    repl_mapalc1[0] |= ((uint32_t)disp_mapalc1) & PPC_BRANCH_MASK;
    memcpy(&base_mapalc1[offset_mapalc1], repl_mapalc1, sizeof(repl_mapalc1));
    printk("patch_mapalc1_dcbz: branch to hw_hash_init disp %d (0x%X)\n", disp_mapalc1, repl_mapalc1[0]);

    //
    // Insert dcbz replacement logic + jump back to original code location. TODO: assumes hw_hash_init is always after mapalc1.
    //
    disp_hw_hash_init = (int32_t)((size_t)base_mapalc1 + offset_mapalc1 + PPC_OPCODE_SIZE)
        - ((int32_t)((size_t)base_hw_hash_init + offset_hw_hash_init) + sizeof(repl_hw_hash_init) - PPC_OPCODE_SIZE);
    repl_hw_hash_init[10] |= ((uint32_t)disp_hw_hash_init) & PPC_BRANCH_MASK;
    memcpy(&base_hw_hash_init[offset_hw_hash_init], repl_hw_hash_init, sizeof(repl_hw_hash_init));
    printk("patch_mapalc1_dcbz: branch to mapalc1 disp %d (0x%X)\n", disp_hw_hash_init, repl_hw_hash_init[10]);

    return 1;
}

//
// Patch mapalc2 to remove use of dcbz instructions.
// On the G5, these clear 32 bytes of memory. On Xenon they clear 128 bytes (cacheline size), clobbering surrounding memory.
//
static int
patch_mapalc2_dcbz(macho_sym_context_t *sym_context)
{
    unsigned long   symbol_addr;
    char            *base_mapalc2;
    char            *base_hw_setup_trans;
    size_t          offset_mapalc2;
    size_t          offset_hw_setup_trans;

    int32_t         disp_mapalc2;
    int32_t         disp_hw_setup_trans;

    //
    // Original dcbz instructions in mapalc2
    //
    const uint32_t find_mapalc2[] = {
        0x38E60020, // addi r7, r6, 0x20
        0x39060040, // addi r8, r6, 0x40
        0x39260060, // addi r9, r6, 0x60
        0x7C0667EC, // dcbz r6, r12
        0x7C0767EC, // dcbz r7, r12
        0x7C0867EC, // dcbz r8, r12
        0x7C0967EC  // dcbz r9, r12
    };
    uint32_t repl_mapalc2[] = {
        0x48000000, // b ... (replaced at runtime)
        0x60000000, // nop
        0x60000000, // nop
        0x60000000, // nop
        0x60000000, // nop
        0x60000000, // nop
        0x60000000  // nop
    };

    //
    // 32-bit codepath in hw_setup_trans unused on 64-bit processors
    //
    const uint32_t find_hw_setup_trans[] = {
        0x39200000  // li r9, 0
    };
    uint32_t repl_hw_setup_trans[] = {
        0x7CC66214, // add r6, r6, r12
        0x38E00000, // li  r7, 0
        0xF8E60000, // std r7, 0(r6)
        0xF8E60008, // std r7, 8(r6)
        0xF8E60010, // std r7, 16(r6)
        0xF8E60018, // std r7, 24(r6)
        0xF8E60020, // std r7, 32(r6)
        0xF8E60028, // std r7, 40(r6)
        0xF8E60030, // std r7, 48(r6)
        0xF8E60038, // std r7, 56(r6)
        0xF8E60040, // std r7, 64(r6)
        0xF8E60048, // std r7, 72(r6)
        0xF8E60050, // std r7, 80(r6)
        0xF8E60058, // std r7, 88(r6)
        0xF8E60060, // std r7, 96(r6)
        0xF8E60068, // std r7, 104(r6)
        0xF8E60070, // std r7, 112(r6)
        0xF8E60078, // std r7, 120(r6)
        0x48000000  // b ... (replaced at runtime)
    };

    //
    // Get location in mapalc2 to patch jump into.
    //
    symbol_addr = macho_resolve_symbol(sym_context, "_mapalc2");
    if (symbol_addr == 0)
        return 0;
    base_mapalc2 = (char*)symbol_addr;

    offset_mapalc2 = 0;
    if (!find_patch_pattern(base_mapalc2, &offset_mapalc2, 0x200, find_mapalc2, NULL, sizeof(find_mapalc2))) {
        printk("patch_mapalc2_dcbz: Failed to locate patch area in mapalc2\n");
        return 0;
    }

    //
    // Get location in hw_setup_trans to steal space from.
    // 32-bit code is never called, can use it as a jump point.
    //
    symbol_addr = macho_resolve_symbol(sym_context, "_hw_setup_trans");
    if (symbol_addr == 0)
        return 0;
    base_hw_setup_trans = (char*)symbol_addr;

    offset_hw_setup_trans = 0;
    if (!find_patch_pattern(base_hw_setup_trans, &offset_hw_setup_trans, 0x200, find_hw_setup_trans, NULL, sizeof(find_hw_setup_trans))) {
        printk("patch_mapalc2_dcbz: Failed to locate patch area in hw_setup_trans\n");
        return 0;
    }
    printk("patch_mapalc2_dcbz: mapalc2 offset 0x%X, hw_setup_trans offset 0x%X\n", offset_mapalc2, offset_hw_setup_trans);

    //
    // Insert jump from mapalc1 to patch area within hw_setup_trans. TODO: assumes hw_setup_trans is always after mapalc2.
    //
    disp_mapalc2 = (int32_t)((size_t)base_hw_setup_trans + offset_hw_setup_trans) - (int32_t)((size_t)base_mapalc2 + offset_mapalc2);
    repl_mapalc2[0] |= ((uint32_t)disp_mapalc2) & PPC_BRANCH_MASK;
    memcpy(&base_mapalc2[offset_mapalc2], repl_mapalc2, sizeof(repl_mapalc2));
    printk("patch_mapalc2_dcbz: branch to hw_setup_trans disp %d (0x%X)\n", disp_mapalc2, repl_mapalc2[0]);

    //
    // Insert dcbz replacement logic + jump back to original code location. TODO: assumes hw_setup_trans is always after mapalc2.
    //
    disp_hw_setup_trans = (int32_t)((size_t)base_mapalc2 + offset_mapalc2 + PPC_OPCODE_SIZE)
        - ((int32_t)((size_t)base_hw_setup_trans + offset_hw_setup_trans) + sizeof(repl_hw_setup_trans) - PPC_OPCODE_SIZE);
    repl_hw_setup_trans[18] |= ((uint32_t)disp_hw_setup_trans) & PPC_BRANCH_MASK;
    memcpy(&base_hw_setup_trans[offset_hw_setup_trans], repl_hw_setup_trans, sizeof(repl_hw_setup_trans));
    printk("patch_mapalc2_dcbz: branch to mapalc2 disp %d (0x%X)\n", disp_hw_setup_trans, repl_hw_setup_trans[18]);

    return 1;
}

//
// Patch switch_in to force hypervisor bit on in MSR for rfid.
// XNU does not track hypervisor bit, must be on for proper operation.
//
static int
patch_switch_in_msr(macho_sym_context_t *sym_context)
{
    const uint32_t find[] = {
        0x7C832378, // mr r3, r4
        0x7CBA03A6, // mtsrr0 r5
        0x7CDB03A6, // mtsrr1 r6
        0x40E20008, // bne+ +8
        0x4C000064, // rfi
        0x4C000024  // rfid
    };
    const uint32_t repl[] = {
        0x38600001, // li r3, 0x1
        0x7866E0CE, // rldimi r6, r3, 0x3C, 0x3 (set hypervisor bit in MSR)
        0x7C832378, // mr r3, r4
        0x7CBA03A6, // mtsrr0 r5
        0x7CDB03A6, // mtsrr1 r6
        0x4C000024  // rfid
    };

    if (!patch_pattern(sym_context, "_switch_in", 0x200, find, NULL, repl, NULL, sizeof (find))) {
        printk("patch_switch_in_msr: Failed to patch context switch MSR pattern\n");
        return 0;
    }

    printk("patch_switch_in_msr: Patched context switch MSR pattern\n");
    return 1;
}

//
// Patch EmulExit to force hypervisor bit on in MSR for rfid when jumping to exception handler.
// XNU does not track hypervisor bit, must be on for proper operation.
//
static int
patch_exception_msr(macho_sym_context_t *sym_context)
{
    const uint32_t find[] = {
        0x806D01B8, // lwz r3, 0x1B8(r13)
        0x7E9A03A6, // mtsrr0 r20
        0x7EBB03A6, // mtsrr1 r21
        0x41FB0008, // bso+ cr6, +8
        0x4C000064, // rfi
        0x4C000024  // rfid
    };
    const uint32_t repl[] = {
        0x38600001, // li r3, 0x1
        0x7875E0CE, // rldimi r21, r3, 0x3C, 0x3 (set hypervisor bit in MSR)
        0x806D01B8, // lwz r3, 0x1B8(r13)
        0x7E9A03A6, // mtsrr0 r20
        0x7EBB03A6, // mtsrr1 r21
        0x4C000024  // rfid
    };

    if (!patch_pattern(sym_context, "_EmulExit", 0x1000, find, NULL, repl, NULL, sizeof (find))) {
        printk("patch_emulexit_msr: Failed to patch exception MSR pattern\n");
        return 0;
    }

    printk("patch_emulexit_msr: Patched exception MSR pattern\n");
    return 1;
}

//
// Patch EmulExit to force hypervisor bit on in MSR for rfid when returning from interrupt handler.
// XNU does not track hypervisor bit, must be on for proper operation.
//
static int
patch_interrupt_msr(macho_sym_context_t *sym_context, uint32_t xnu_version)
{
    unsigned long   symbol_addr;
    char            *base;
    size_t          offset_32;
    size_t          offset_64;
    int32_t         disp_to32;
    int32_t         disp_to64;

    const uint32_t find_32[] = {
        0x7FF243A6, // mtsprg 2, r31
        0x7FF342A6, // mfsprg r31, 3
        0x4C000064, // rfi
        0x00000000
    };
    uint32_t repl_32_tiger[] = {
        0x38C00001, // li r6, 1
        0x78DAE0CE, // rldimi r26, r6, 0x3C, 0x3 (set hypervisor bit in MSR)
        0x7F5B03A6, // mtsrr1 r26
        0x48000000  // b ... (replaced at runtime)
    };
    uint32_t repl_32_leopard[] = {
        0x38C00001, // li r6, 1
        0x78C8E0CE, // rldimi r8, r6, 0x3C, 0x3 (set hypervisor bit in MSR)
        0x7D1B03A6, // mtsrr1 r8
        0x48000000  // b ... (replaced at runtime)
    };
    uint32_t *repl_32;
    size_t repl_32_length;

    const uint32_t find_64_tiger[] = {
        0x7F3A03A6, // mtsrr0 r25
        0xE8000000, // ld ...
        0x7F5B03A6, // mtsrr1 r26
        0xE8000000, // ld ...
    };
    const uint32_t find_64_leopard[] = {
        0x7F3A03A6, // mtsrr0 r25
        0xE8000000, // ld ...
        0x7D1B03A6, // mtsrr1 r8
        0xE8000000, // ld ...
    };

    const uint32_t *find_64;
    size_t find_64_length;

    uint32_t repl_64 = 0x48000000; // b ... (replaced at runtime)
    const uint32_t mask_64[] = {
        0xFFFFFFFF,
        0xFF000000,
        0xFFFFFFFF,
        0xFF000000,
    };

    if (xnu_match_darwin_version(xnu_version, XNU_VERSION_TIGER_MIN, XNU_VERSION_TIGER_MAX)) {
        find_64 = find_64_tiger;
        find_64_length = sizeof (find_64_tiger);
        repl_32 = repl_32_tiger;
        repl_32_length = sizeof (repl_32_tiger);
    } else if (xnu_match_darwin_version(xnu_version, XNU_VERSION_LEOPARD_MIN, XNU_VERSION_LEOPARD_MAX)) {
        find_64 = find_64_leopard;
        find_64_length = sizeof (find_64_leopard);
        repl_32 = repl_32_leopard;
        repl_32_length = sizeof (repl_32_leopard);
    }

    //
    // Both locations in EmulExit.
    //
    symbol_addr = macho_resolve_symbol(sym_context, "_EmulExit");
    if (symbol_addr == 0)
        return 0;

    // Vectors aren't in the final spot yet, they are copied by BootX just before kernel handoff.
    if (symbol_addr < 0x4000)
        symbol_addr += vectors_addr;
    base = (char*)symbol_addr;

    //
    // Find locations in 32-bit and 64-bit interrupt epilogues.
    //
    offset_32 = 0;
    if (!find_patch_pattern(base, &offset_32, 0x1000, find_32, NULL, sizeof(find_32))) {
        printk("patch_interrupt_msr: Failed to locate 32-bit patch area\n");
        return 0;
    }

    offset_64 = offset_32;
    if (!find_patch_pattern(base, &offset_64, 0x1000, find_64, mask_64, find_64_length)) {
        printk("patch_interrupt_msr: Failed to locate 64-bit patch area\n");
        return 0;
    }
    printk("patch_interrupt_msr: 32-bit offset 0x%X, 64-bit offset 0x%X\n", offset_32, offset_64);

    //
    // Insert jump from 64-bit epilogue to patch area within 32-bit epilogue into 64-bit epilogue.
    //
    disp_to32 = (int32_t)((size_t)base + offset_32) - (int32_t)((size_t)base + offset_64 + 8);
    repl_64 |= ((uint32_t)disp_to32) & PPC_BRANCH_MASK;
    memcpy(&base[offset_64 + 8], &repl_64, sizeof(repl_64)); // Must sync with above
    printk("patch_interrupt_msr: branch to 32-bit disp %d (0x%X)\n", disp_to32, repl_64);

    //
    // Insert jump from 32-bit epilogue back to next instruction in 64-bit epilogue into 32-bit epilogue patch area.
    //
    disp_to64 = ((int32_t)((size_t)base + offset_64) - (int32_t)((size_t)base + offset_32));
    repl_32[3] |= ((uint32_t)disp_to64) & PPC_BRANCH_MASK; // Must sync with above
    memcpy(&base[offset_32], repl_32, repl_32_length); // Must sync with above
    printk("patch_interrupt_msr: branch to 64-bit disp %d (0x%X)\n", disp_to64, repl_32[3]);

    printk("patch_interrupt_msr: Patched interrupt MSR pattern\n");
    return 1;
}

//
// Patch ultra-fast trap handler to force hypervisor bit on in MSR for rfid.
// XNU does not track hypervisor bit, must be on for proper operation.
//
static int
patch_uft_msr_tiger(macho_sym_context_t *sym_context)
{
    unsigned long   symbol_addr;
    char            *base;
    size_t          offset_loadmsr;
    size_t          offset_32;
    int32_t         disp_to32;
    int32_t         disp_toloadmsr;

    const uint32_t find_loadmsr[] = {
        0x7D7042A6, // mfsprg r11, 0
        0x7C7B03A6, // mtsrr1 r3
    };
    uint32_t repl_loadmsr[] = {
        0x7D7042A6, // mfsprg r11, 0
        0x48000000  // b ... (replaced at runtime)
    };

    const uint32_t find_32[] = {
        0x81000000, // lwz ...
        0x7DB242A6, // mfsprg r13, 2
        0x7D7243A6, // mtsprg 2, r11
        0x7D7342A6, // mfsprg r11, 3
        0x4C000064  // rfi
    };
    uint32_t repl_32[] = {
        0x78630002, // rotldi r3, r3, 0x20
        0x64631000, // oris r3, r3, 0x1000
        0x78630002, // rotldi r3, r3, 0x20
        0x7C7B03A6, // mtsrr1 r3
        0x48000000  // b ... (replaced at runtime)
    };
    const uint32_t mask_32[] = {
        0xFF000000, // lwz ...
        0xFFFFFFFF, // mfsprg r13, 2
        0xFFFFFFFF, // mtsprg 2, r11
        0xFFFFFFFF, // mfsprg r11, 3
        0xFFFFFFFF  // rfi
    };


    //
    // Both locations after uft_uaw_nop_if_32bit.
    //
    symbol_addr = macho_resolve_symbol(sym_context, "_uft_uaw_nop_if_32bit");
    if (symbol_addr == 0)
        return 0;

    // Vectors aren't in the final spot yet, they are copied by BootX just before kernel handoff.
    if (symbol_addr < 0x4000)
        symbol_addr += vectors_addr;
    base = (char*)symbol_addr;

    //
    // Find locations.
    //
    offset_loadmsr = 0;
    if (!find_patch_pattern(base, &offset_loadmsr, 0x1000, find_loadmsr, NULL, sizeof(find_loadmsr))) {
        printk("patch_uft_msr_tiger: Failed to locate load MSR patch area\n");
        return 0;
    }

    offset_32 = offset_loadmsr;
    if (!find_patch_pattern(base, &offset_32, 0x1000, find_32, mask_32, sizeof(find_32))) {
        printk("patch_uft_msr_tiger: Failed to locate 32-bit patch area\n");
        return 0;
    }
    printk("patch_uft_msr_tiger: load MSR offset 0x%X, 32-bit offset 0x%X\n", offset_loadmsr, offset_32);

    //
    // Insert jump from load MSR area to patch area within 32-bit epilogue into load MSR area.
    //
    disp_to32 = (int32_t)((size_t)base + offset_loadmsr) - (int32_t)((size_t)base + offset_32);
    repl_loadmsr[1] |= ((uint32_t)disp_to32) & PPC_BRANCH_MASK; // Must sync with above
    memcpy(&base[offset_loadmsr], repl_loadmsr, sizeof(repl_loadmsr));
    printk("patch_uft_msr_tiger: branch to 32-bit disp %d (0x%X)\n", disp_to32, repl_loadmsr[1]);

    //
    // Insert jump from 32-bit epilogue back to next instruction in 64-bit epilogue into 32-bit epilogue patch area.
    //
    disp_toloadmsr = ((int32_t)((size_t)base + offset_32) - (int32_t)((size_t)base + offset_loadmsr));
    repl_32[4] |= ((uint32_t)disp_toloadmsr) & PPC_BRANCH_MASK; // Must sync with above
    memcpy(&base[offset_32], repl_32, sizeof(repl_32));
    printk("patch_uft_msr_tiger: branch to load MSR disp %d (0x%X)\n", disp_toloadmsr, repl_32[4]);

    printk("patch_uft_msr_tiger: Patched UFT MSR pattern\n");
    return 1;
}

//
// Patch Emulate64 to always handle emulation without needing to enable diag mode.
// Used for trapping/emulating dcbz instructions.
//
static int
patch_emulate_diag(macho_sym_context_t *sym_context)
{
    const uint32_t find[] = {
        0x83C00000, // lwz r30, ...
        0x57C006B5, // rlwinm, r0, r30, 0, 26, 26
        0x41E20000  // beq+ ...
    };
    const uint32_t find_mask[] = {
        0xFFFF0000,
        0xFFFFFFFF,
        0xFFFF0000
    };
    const uint32_t repl[] = {
        0x60000000, // nop
        0x60000000, // nop
        0x60000000  // nop
    };

    if (!patch_pattern(sym_context, "_Emulate64", 0x1000, find, find_mask, repl, NULL, sizeof(find))) {
        printk("xnu_patch_emulate_diag: Failed to patch diag check\n");
        return 0;
    }

    printk("xnu_patch_emulate_diag: Patched diag check\n");
    return 1;
}

//
// Insert vector at 0x60 for secondary cores/threads.
//
static void
patch_insert_vector60(void)
{
    uint32_t *base = (uint32_t*)(vectors_addr + 0x60);

    base[0] = 0x48000000;
    printk("patch_insert_vector60: Patched vector 0x60\n");
}

#if XNU_DISABLE_CONSDEBUG_PUTC
static int xnu_patch_consdebug_putc(macho_sym_context_t *symContext) {
    unsigned long   sym;
    char            *base;

    static const char debugFind[] = {
        0x2F, 0x83, 0x00, 0x00,
        0x40, 0x9E
    };
    static const char debugRepl[] = {
        0x2F, 0x83, 0x00, 0x00,
        0x48, 0x00
    };

    //
    // Get _consdebug_putc function location.
    //
    sym = macho_resolve_symbol(symContext, "_consdebug_putc");
    if (sym == 0) {
        return 0;
    }
    base = (char*)sym;

    //
    // Look for pattern.
    //
    for (int i = 0; i < 0x4000; i++, base++) {
        if (memcmp(base, debugFind, sizeof (debugFind)) == 0) {
            memcpy(base, debugRepl, sizeof (debugRepl));
            return 1;
        }
    }

    printk("xnu_patch_consdebug_putc: failed to locate patch pattern\n");
    return 0;
}
#endif

int
xnu_get_symtab(macho_sym_context_t *sym_context)
{
    phandle_t   memory_map;
    uint32_t*   prop;
    int         proplen;
    struct symtab_command *symTabCommand;

    //
    // Read the kernel symtab from the memory-map node.
    //
    memory_map = find_dev("/chosen/memory-map");
    if (!memory_map)
        return 0;
    prop = (uint32_t*)get_property(memory_map, "Kernel-__SYMTAB", &proplen);
    if (!prop)
        return 0;
    symTabCommand = (struct symtab_command*)prop[0];

    sym_context->symbol_table   = (struct nlist*) symTabCommand->symoff;
    sym_context->symbol_count   = symTabCommand->nsyms;
    sym_context->string_table   = (const char*) symTabCommand->stroff;
    sym_context->string_size    = symTabCommand->strsize;

    return 1;
}

int
xnu_patch(void)
{
    macho_sym_context_t kernel_syms;
    uint32_t            xnu_version;

    //
    // Get the kernel symbol table.
    //
    if (!xnu_get_symtab(&kernel_syms)) {
        printk("Failed to get kernel symbol table\n");
        return 0;
    }

    xnu_version = xnu_read_darwin_version(&kernel_syms);
    if (xnu_version == 0)
        return 0;

    //
    // Get the vectors.
    //
    if (!find_stored_bootx_vectors(0x04000000))
        return 0;

    if (!patch_disable_function(&kernel_syms, "_PE_find_scc"))
        return 0;

    if (!patch_cpu_check(&kernel_syms, xnu_version))
        return 0;
    patch_l2_size(&kernel_syms, xnu_version);

    if (!patch_emulate_diag(&kernel_syms))
        return 0;

    if (!patch_mapalc1_dcbz(&kernel_syms))
        return 0;
    if (!patch_mapalc2_dcbz(&kernel_syms))
        return 0;
    if (!patch_switch_in_msr(&kernel_syms))
        return 0;
    if (!patch_exception_msr(&kernel_syms))
        return 0;
    if (!patch_interrupt_msr(&kernel_syms, xnu_version))
        return 0;
    if (xnu_match_darwin_version(xnu_version, XNU_VERSION_TIGER_MIN, XNU_VERSION_LEOPARD_MAX)) {
        if (!patch_uft_msr_tiger(&kernel_syms))
            return 0;
    }

    patch_insert_vector60();


    //
    // Clean up for panics.
    //
#if XNU_DISABLE_CONSDEBUG_PUTC
    if (xnu_match_darwin_version(xnu_version, XNU_VERSION_TIGER_MIN, XNU_VERSION_TIGER_MAX)) {
        xnu_patch_consdebug_putc(&kernel_syms);
    }
#endif

    return 1;
}