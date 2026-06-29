/*
 *	<macho.c>
 *
 *   Copyright (C) 2025-2026 John Davis
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *
 */

#include "config.h"
#include "macho-loader.h"
#include "macosx.h"
#include "nlist.h"

//
// Gets the highest virtual address in the Mach-O binary.
//
unsigned long
macho_get_top(void *macho)
{
    struct mach_header      *header;
    char                    *command_ptr;
    struct load_command     *load_cmd;
    struct segment_command  *seg_cmd;
    unsigned long           top;

    header = (struct mach_header*)macho;
    if (header->magic != MH_MAGIC) {
        printk("macho_get_top: Not a macho at %p, wrong magic 0x%lx\n", macho, header->magic);
        return 0;
    }

    // Iterate through commands and get highest virtual address.
    top = 0;
    command_ptr = (char*)(header + 1);
    for (int i = 0; i < header->ncmds; i++) {
        load_cmd = (struct load_command*)command_ptr;

        if (load_cmd->cmd == LC_SEGMENT) {
            seg_cmd = (struct segment_command*)load_cmd;
            if ((seg_cmd->vmaddr + seg_cmd->vmsize) > top)
                top = seg_cmd->vmaddr + seg_cmd->vmsize;
        }

        command_ptr += load_cmd->cmdsize;
    }

    return top;
}

//
// Resolves a symbol from a Mach-O binary.
//
unsigned long
macho_resolve_symbol(macho_sym_context_t *context, const char *sym_name)
{
    const char *symStr;
    for (unsigned long i = 0; i < context->symbol_count; i++) {
        //
        // Get the symbol string for the symbol.
        //
        symStr = (const char*)(context->string_table + context->symbol_table[i].n_un.n_strx);
        if (strcmp(sym_name, symStr) == 0) {
            printk("macho_resolve_symbol: found symbol '%s' at 0x%lX\n", sym_name, context->symbol_table[i].n_value);
            return context->symbol_table[i].n_value;
        }
    }

    printk("macho_resolve_symbol: failed to locate symbol '%s'\n", sym_name);
    return 0;
}
