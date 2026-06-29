/*
 *	<macosx.c>
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
#include "libc/vsprintf.h"
#include "boot_args.h"
#include "device_tree.h"
#include "macho-loader.h"
#include "macosx.h"
#include "mkext.h"

#define MKEXT_FILENAME      "Xbox360.mkext"

static phandle_t
obp_devopen(const char *path)
{
    phandle_t ph;

    push_str(path);
    fword("open-dev");
    ph = POP();

    return ph;
}

static int
obp_devread(phandle_t ph, char *buf, int nbytes)
{
    int ret;

    PUSH((int)buf);
    PUSH(nbytes);
    push_str("read");
    PUSH(ph);
    fword("$call-method");
    ret = POP();

    return ret;
}

static int
obp_devseek(phandle_t ph, int hi, int lo)
{
    int ret;

    PUSH(lo);
    PUSH(hi);
    push_str("seek");
    PUSH(ph);
    fword("$call-method");
    ret = POP();

    return ret;
}

boot_args_ptr
macosx_get_boot_args(void)
{
    phandle_t   memory_map;
    uint32_t*   prop;
    int         proplen;

    //
    // Read the boot args location from the memory-map node.
    //
    memory_map = find_dev("/chosen/memory-map");
    if (!memory_map) {
        return 0;
    }
    prop = (uint32_t*)get_property(memory_map, "BootArgs", &proplen);
    if (!prop || (prop[1] != sizeof (boot_args))) {
        return 0;
    }

    return (boot_args_ptr)(prop[0]);
}

int
macosx_patch(void)
{
    //
    // BootX generally lays out things in memory in the following order:
    //
    // Kernel
    // Drivers
    // Boot arguments
    // Flattened devicetree
    //
    boot_args_ptr   xnu_boot_args;
    DTEntry         dt_entry;
    void            *dt_mkext;
    mkext_header    mkext;
    int             mkext_valid;
    unsigned long   prop[2];
    char            mkext_name[32];
    char            mkext_path[32];
    void            *new_dt;
    unsigned long   new_dt_length;
    phandle_t       ph;
    int             ret;

    //
    // Perform XNU patches.
    //
    xnu_patch();

    //
    // Get the boot arguments and devicetree.
    //
    xnu_boot_args = macosx_get_boot_args();
    if (!xnu_boot_args) {
        printk("Failed to get boot args!\n");
        return 0;
    }

    printk("BootArgs: %p, top of kernel: 0x%lx\n", xnu_boot_args, xnu_boot_args->topOfKernelData);
    printk("Devicetree: %p, length: 0x%lx\n", xnu_boot_args->deviceTreeP, xnu_boot_args->deviceTreeLength);
    DTInit(xnu_boot_args->deviceTreeP, xnu_boot_args->deviceTreeLength);

    //
    // Add a new DriversPackage property to the memory map.
    // This will be located where the devicetree currently is.
    //
    if (DTLookupEntry(0, "/chosen/memory-map", &dt_entry) != kSuccess)
        return 0;

    //
    // Read the MKEXT header.
    // Search partitions until the MKEXT is found.
    //
    mkext_valid = 0;
    for (uint32_t i = 0; i < 10; i++) {
        snprintf(mkext_path, sizeof (mkext_path), "ud:%u,\\%s", i, MKEXT_FILENAME);
        printk("Checking for mkext at %s\n", mkext_path);
        ph = obp_devopen(mkext_path);
        if (ph == 0)
            continue;

        printk("Trying mkext at %s\n", mkext_path);

        ret = obp_devseek(ph, 0, 0);
        if (ret != 0) {
            printk("Failed to seek mkext\n");
            continue;
        }

        ret = obp_devread(ph, (char*)&mkext, sizeof (mkext));
        if (ret != sizeof (mkext)) {
            printk("Failed to read mkext header\n");
            continue;
        }

        if ((mkext.magic != MKEXT_MAGIC) || (mkext.signature != MKEXT_SIGN)) {
            printk("Invalid mkext magic/signature\n");
            continue;
        }

        mkext_valid = 1;
        break;
    }

    if (!mkext_valid) {
        printk("Failed to open mkext\n");
        return 0;
    }

    dt_mkext = xnu_boot_args->deviceTreeP;
    prop[0] = (unsigned long)dt_mkext;
    prop[1] = mkext.length;
    sprintf(mkext_name, "DriversPackage-%lx", prop[0]);
    printk("MKEXT (%s) will be at %p, length: %x\n", mkext_name, dt_mkext, mkext.length);

    if (DTAddProperty(dt_entry, mkext_name, prop, sizeof (prop), &new_dt, &new_dt_length) != kSuccess) {
        return 0;
    }

    //
    // Read the mkext.
    //
    ret = obp_devseek(ph, 0, 0);
    if (ret !=  0) {
        printk("failed to seek mkext\n");
        return 0;
    }
    ret = obp_devread(ph, dt_mkext, mkext.length);
    if (ret != mkext.length) {
        printk("failed to read all mkext\n");
        return 0;
    }

    // TODO: do adler check.

    //
    // Copy the new devicetree after the mkext.
    //
    xnu_boot_args->deviceTreeP      = ((char*)xnu_boot_args->deviceTreeP) + ((prop[1] + 0xFFF) & ~(0xFFF));
    xnu_boot_args->deviceTreeLength = new_dt_length;
    memcpy(xnu_boot_args->deviceTreeP, new_dt, new_dt_length);
    free(new_dt);

    //
    // Fix the DeviceTree property.
    //
    DTInit(xnu_boot_args->deviceTreeP, xnu_boot_args->deviceTreeLength);
    if (DTLookupEntry(0, "/chosen/memory-map", &dt_entry) != kSuccess) {
        return 0;
    }
    prop[0] = (uint32_t)xnu_boot_args->deviceTreeP;
    prop[1] = new_dt_length;
    DTSetProperty(dt_entry, "DeviceTree", prop);

    //
    // Adjust top of kernel address.
    //
    xnu_boot_args->topOfKernelData = (unsigned long)xnu_boot_args->deviceTreeP + ((new_dt_length + 0xFFF) & ~(0xFFF));

    printk("New top of kernel: 0x%lx\n", xnu_boot_args->topOfKernelData);
    printk("New devicetree: %p, length: 0x%lx\n", xnu_boot_args->deviceTreeP, xnu_boot_args->deviceTreeLength);

    //
    // Fix DRAM. XNU must not use the top 16MB, and it doesn't seem to like having the remainder all in one bank.
    //
    xnu_boot_args->PhysicalDRAM[0].base = 0x00000000; // 0MB start
    xnu_boot_args->PhysicalDRAM[0].size = 0x02000000; // 32MB size TODO: This reduces freezes, but they still may occur.
    xnu_boot_args->PhysicalDRAM[1].base = 0x10000000; // 256MB start
    xnu_boot_args->PhysicalDRAM[1].size = 0x0F000000; // 240MB size

    for (int i = 0; i < kMaxDRAMBanks; i++) {
        if (xnu_boot_args->PhysicalDRAM[i].size == 0)
            continue;
        printk("DRAM[%d] start: 0x%lx, length 0x%lx\n", i, xnu_boot_args->PhysicalDRAM[i].base, xnu_boot_args->PhysicalDRAM[i].size);
    }

    printk("XNU ready to boot\n");
    return 1;
}
