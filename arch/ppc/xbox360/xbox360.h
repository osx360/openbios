/*
 *	<xbox360.h>
 *
 *   Copyright (C) 2026 John Davis
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   version 2
 *
 */

#ifndef _H_XBOX360
#define _H_XBOX360

#define XBOX360_RAM_SIZE            0x20000000
#define XBOX360_FB_SIZE             0x1000000 // Direct framebuffer + space for bounce buffer used in XNU (8MB x2, good for 1920x1080x32)
#define XBOX360_FB_BASE             (XBOX360_RAM_SIZE - XBOX360_FB_SIZE)
#define XBOX360_RAM_SIZE_ACTUAL     XBOX360_FB_BASE

#define XBOX360_GPU_BASE            0xEC800000

#define XBOX360_IC_PHYS             0x0000020000050000ULL
#define XBOX360_IC_VIRT             0xF0000000

#define XBOX360_TIMEBASE_FREQ       50000000
#define XBOX360_CPU_FREQ            3200000000
#define XBOX360_CPU_NAME            "PowerPC,Xenon"
#define XBOX360_RAM_TYPE            "GDDR3"

#include "kernel.h"

#endif
