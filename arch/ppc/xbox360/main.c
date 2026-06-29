/*
 *   Creation Date: <2002/10/02 22:24:24 samuel>
 *   Time-stamp: <2004/03/27 01:57:55 samuel>
 *
 *	<main.c>
 *
 *
 *
 *   Copyright (C) 2002, 2003, 2004 Samuel Rydh (samuel@ibrium.se)
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *
 */

#include "config.h"
#include "libopenbios/bindings.h"
#include "libopenbios/elf_load.h"
#include "arch/common/nvram.h"
#include "packages/nvram.h"
#include "libc/diskio.h"
#include "libc/vsprintf.h"
#include "kernel.h"
#include "drivers/drivers.h"
#include "libopenbios/ofmem.h"
#include "libopenbios/initprogram.h"
#include "context.h"
#define NO_QEMU_PROTOS

//#define DEBUG_QEMU

#ifdef DEBUG_QEMU
#define SUBSYS_DPRINTF(subsys, fmt, args...) \
    do { printk("%s - %s: " fmt, subsys, __func__ , ##args); } while (0)
#else
#define SUBSYS_DPRINTF(subsys, fmt, args...) \
    do { } while (0)
#endif
#define CHRP_DPRINTF(fmt, args...) SUBSYS_DPRINTF("CHRP", fmt, ##args)
#define ELF_DPRINTF(fmt, args...) SUBSYS_DPRINTF("ELF", fmt, ##args)
#define NEWWORLD_DPRINTF(fmt, args...) SUBSYS_DPRINTF("NEWWORLD", fmt, ##args)

/************************************************************************/
/*	entry								*/
/************************************************************************/

void
boot( void )
{
    fword("update-chosen");
}
