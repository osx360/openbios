/*
 *	<xnu_version.c>
 *
 *   Copyright (C) 2025-2026 John Davis
 *
 *   Portions taken from OpenCorePkg under the BSD-3 license.
 *   Portions Copyright (C) 2019, vit9696. All rights reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *
 */

#include "config.h"
#include "macosx.h"

#define DARWIN_VERSION_STR      "Darwin Kernel Version "

//
// Parse the Darwin version from a string.
//
static uint32_t
parse_darwin_version(const char *version_str)
{
    uint32_t  version;
    uint32_t  version_part;

    if ((*version_str == '\0') || (*version_str < '0') || (*version_str > '9')) {
        return 0;
    }

    version = 0;
    for (int i = 0; i < 3; i++) {
        version     *= 100;
        version_part = 0;

        for (int j = 0; j < 2; j++) {
            //
            // Handle single digit parts, i.e. parse 1.2.3 as 010203.
            //
            if ((*version_str != '.') && (*version_str != '\0')) {
                version_part *= 10;
            }

            if ((*version_str >= '0') && (*version_str <= '9')) {
                version_part += *version_str++ - '0';
            } else if ((*version_str != '.') && (*version_str != '\0')) {
                return 0;
            }
        }

        version += version_part;
        if (*version_str == '.') {
            version_str++;
        }
    }

    return version;
}

int
xnu_match_darwin_version(uint32_t kernel_ver, uint32_t min_ver, uint32_t max_ver)
{
    //
    // Check against min <= curr <= max.
    // curr=0 -> curr=inf, max=0  -> max=inf
    //

    // Replace max inf with max known version.
    if (max_ver == 0)
        max_ver = kernel_ver;

    // Handle curr=inf <= max=inf(?) case.
    if (kernel_ver == 0)
        return kernel_ver == 0;

    // Handle curr=num > max=num case.
    if (kernel_ver > max_ver)
        return 0;

    // Handle min=num > curr=num case.
    if (kernel_ver < min_ver)
        return 0;

    return 1;
}

//
// Search the kernel for the Darwin version.
//
uint32_t
xnu_read_darwin_version(macho_sym_context_t *sym_context)
{
    char*           base;
    uint32_t        offset;
    uint32_t        index;
    char            darwin_version[32];
    uint32_t        darwin_version_int;
    unsigned long   kern_sym_version;

    kern_sym_version = macho_resolve_symbol(sym_context, "_version");
    if (kern_sym_version == 0)
        return 0;

    //
    // Look for version string.
    //
    base = (char*)kern_sym_version;
    offset = 0;
    if (strncmp(base, DARWIN_VERSION_STR, strlen(DARWIN_VERSION_STR)) != 0) {
        printk("xnu_read_darwin_version: malformed version string\n");
        return 0;
    }

    //
    // Get the actual version.
    //
    offset += strlen(DARWIN_VERSION_STR);
    for (index = 0; index < sizeof (darwin_version) - 1; index++, offset++) {
        if ((offset >= 0x1000) || (base[offset] == ':')) {
            break;
        }

        darwin_version[index] = base[offset];
    }
    darwin_version[index] = '\0';

    darwin_version_int = parse_darwin_version(darwin_version);
    printk("xnu_read_darwin_version: XNU kernel version: %s (%u)\n", darwin_version, darwin_version_int);

    return darwin_version_int;
}
