/*
 *   Creation Date: <2001/06/16 21:30:18 samuel>
 *   Time-stamp: <2003/04/04 16:32:06 samuel>
 *
 *	<init.S>
 *
 *	Asm glue for ELF images
 *
 *   Copyright (C) 2001, 2002, 2003 Samuel Rydh (samuel@ibrium.se)
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *
 */

#include "autoconf.h"
#include "asm/asmdefs.h"
#include "asm/processor.h"

/************************************************************************/
/*	Macros								*/
/************************************************************************/

#define ILLEGAL_VECTOR( v )	\
    .org __vectors + v; vector__##v: \
    EXCEPTION_PREAMBLE; \
    mfmsr   r3 ; \
	clrldi  r3, r3, 1; /* Disable SF */ \
	ori     r3, r3, (MSR_IR | MSR_DR); /* Enable IR and DR */ \
	mtmsrd  r3; \
    isync; \
    li r3, v; \
    LOAD_REG_FUNC(r4, unexpected_excep); \
    mtctr r4; \
    bctr

#define VECTOR( v, dummystr )	.org __vectors + v ; vector__##v

#define ULONG_SIZE 4
#define STACKFRAME_MINSIZE 16

.macro EXCEPTION_PREAMBLE
    mtsprg1 r1 /* scratch */
    mfsprg0 r1 /* exception stack in sprg0 */
    addi    r1, r1, -80 /* push exception frame */

    stw	r0,0(r1) ;	/* save r0 */
    mfsprg1	r0 ;
    stw	r0,4(r1) ;	/* save r1 */
    stw	r2,8(r1) ;	/* save r2 */
    stw	r3,12(r1) ;	/* save r3 */
    stw	r4,16(r1) ;
    stw	r5,20(r1) ;
    stw	r6,24(r1) ;
    stw	r7,28(r1) ;
    stw	r8,32(r1) ;
    stw	r9,36(r1) ;
    stw	r10,40(r1) ;
    stw	r11,44(r1) ;
    stw	r12,48(r1) ;

    mflr	r0 ;
    stw	r0,52(r1) ;
    mfcr	r0 ;
    stw	r0,56(r1) ;
    mfctr	r0 ;
    stw	r0,60(r1) ;
    mfxer	r0 ;
    stw	r0,64(r1) ;

    /* 76(r1) unused */
    addi	r1,r1,-16 ;	/* call conventions uses 0(r1) and 4(r1)... */
.endm

/************************************************************************/
/*	stack space							*/
/************************************************************************/

	.section .bss
	.balign 32

    .space  32*1024		// 32 K client stack
__client_stack:
	.space	128

	.space	64*1024		// 64 K stack
__stack:
    .space  64

	.space  32*1024		// 32 K exception stack
__exception_stack:
    .space	128

/************************************************************************/
/*	vectors								*/
/************************************************************************/

    .section .text.vectors, "ax"
GLOBL(__vectors):
	b	_entry	// Xell entry point

    nop			// NULL-jmp trap
1:	nop			//
    b	1b

VECTOR( 0x60, "SLV" ):
slave_start:
	b	slave_start

ILLEGAL_VECTOR( 0x100 )
ILLEGAL_VECTOR( 0x200 )

VECTOR( 0x300, "DSI" ):
    EXCEPTION_PREAMBLE
    LOAD_REG_FUNC(r3, dsi_exception)
    mtctr	r3
    bctrl
    b exception_return

VECTOR( 0x400, "ISI" ):
    EXCEPTION_PREAMBLE
    LOAD_REG_FUNC(r3, isi_exception)
    mtctr	r3
    bctrl
    b exception_return

ILLEGAL_VECTOR( 0x500 )
ILLEGAL_VECTOR( 0x600 )
ILLEGAL_VECTOR( 0x700 )

VECTOR( 0x800, "FPU" ):
    mtsprg1	r3
    mfsrr1	r3
    ori	r3,r3,0x2000
    mtsrr1	r3
    mfsprg1	r3
    rfid

ILLEGAL_VECTOR( 0x900 )
ILLEGAL_VECTOR( 0xa00 )
ILLEGAL_VECTOR( 0xb00 )
ILLEGAL_VECTOR( 0xc00 )
ILLEGAL_VECTOR( 0xd00 )
ILLEGAL_VECTOR( 0xe00 )
ILLEGAL_VECTOR( 0xf00 )
ILLEGAL_VECTOR( 0x1000 )
ILLEGAL_VECTOR( 0x1100 )
ILLEGAL_VECTOR( 0x1200 )
ILLEGAL_VECTOR( 0x1300 )
ILLEGAL_VECTOR( 0x1400 )
ILLEGAL_VECTOR( 0x1500 )
ILLEGAL_VECTOR( 0x1600 )
ILLEGAL_VECTOR( 0x1700 )

exception_return:
    addi	r1,r1,16	// pop ABI frame

    lwz	r0,52(r1)
    mtlr	r0
    lwz	r0,56(r1)
    mtcr	r0
    lwz	r0,60(r1)
    mtctr	r0
    lwz	r0,64(r1)
    mtxer	r0

    lwz	r0,0(r1)	// restore r0
    lwz	r2,8(r1)	// restore r2
    lwz	r3,12(r1)	// restore r3
    lwz	r4,16(r1)
    lwz	r5,20(r1)
    lwz	r6,24(r1)
    lwz	r7,28(r1)
    lwz	r8,32(r1)
    lwz	r9,36(r1)
    lwz	r10,40(r1)
    lwz	r11,44(r1)
    lwz	r12,48(r1)
    lwz	r1,4(r1)	// restore r1
    rfid

GLOBL(__vectors_end):

/************************************************************************/
/*	entry								*/
/************************************************************************/

GLOBL(_entry):
	// Ensure MMU is off.
	li		r0, 0
	mtmsr	r0

	// Ensure SF bit is disabled.
	mfmsr	r3
	clrldi	r3, r3, 1
	mtmsrd	r3

    mr r10, r4

	// Copy exception vectors to vector area.
	LOAD_REG_IMMEDIATE(r3, __vectors)
	li		r3, 0
	li		r5, __vectors_end - __vectors + 16
	bl		copy_data

    mr r4, r10

	// Copy first bits to final location.
	LOAD_REG_IMMEDIATE(r3, __vectors)
	LOAD_REG_IMMEDIATE(r5, copy_to_here)
	subf	r5, r3, r5
	bl		copy_data

	// Jump to newly relocated code.
	LOAD_REG_FUNC(r8, jump_point)
	mtctr	r8
    bctr

	.align	8
jump_point:
	// Should now be running 1:1 once more.
	// Copy remaining bits to final location.
	LOAD_REG_IMMEDIATE(r5, _end)
	subf	r5, r3, r5
	bl		copy_data
	b		start_master

// r3 = dest
// r4 = source
// r5 = length
copy_data:
	rlwinm	r5, r5, 0, 0, 28
1:	lwz		r6, 0(r4)
	lwz		r7, 4(r4)
	lwz		r8, 8(r4)
	lwz		r9, 12(r4)
	stw		r6, 0(r3)
	stw		r7, 4(r3)
	stw		r8, 8(r3)
	stw		r9, 12(r3)
	dcbst	0, r3
	sync
	icbi	0, r3
	sync
	addi	r5, r5, -16
	addi	r4, r4, 16
	addi	r3, r3, 16
	cmpwi	r5, 0
	bgt	1b
	isync
	blr

	.align	8
copy_to_here:

start_master:
    // Zero out BSS. The loader running on the IOP does not do this.
    LOAD_REG_IMMEDIATE(r3, _bss)
    LOAD_REG_IMMEDIATE(r5, _ebss)
    sub     r5, r5, r3
    addi    r5, r5, 16
    li      r0, 0
    rlwinm	r5,r5,0,0,28
2:  stw     r0, 0(r3)
    stw     r0, 4(r3)
    stw     r0, 8(r3)
    stw     r0, 12(r3)
    dcbst   0, r3
    sync
    addi	r5, r5, -16
    addi	r3, r3, 16
    cmpwi	r5, 0
	bgt	2b
	isync

    // Configure the stacks.
    LOAD_REG_IMMEDIATE(r1, __exception_stack)
    mtsprg0	r1
    LOAD_REG_IMMEDIATE(r1, __stack)

    // Configure MMU.
	mr	r4, r10
    bl	BRANCH_LABEL(setup_mmu)

    //
    // Jump into the context.
    //
	LOAD_REG_IMMEDIATE(r4, __context)
	PPC_LL  r4, 0(r4)
	PPC_STL r1, (2 * ULONG_SIZE)(r4)

	bl	BRANCH_LABEL(__switch_context_nosave)
1:	nop
	b	1b

#define STKOFF 8
#define SAVE_SPACE 144

GLOBL(of_client_callback):
    PPC_STLU r1, -STACKFRAME_MINSIZE(r1) /* fits within alignment */

    /* save r4 */
    PPC_STL r4, STKOFF(r1)

    /* save lr */
    mflr	r4
    PPC_STL r4, PPC_LR_STKOFF(r1)

    /* switch to OF stack */
    LOAD_REG_IMMEDIATE(r4, __client_stack)

    PPC_STLU r4, -SAVE_SPACE(r4)
    PPC_STL  r1, (STKOFF)(r4)	// save caller stack
    mr	r1,r4

    PPC_STL  r3,  (STKOFF + 5 * ULONG_SIZE)(r1)
    PPC_STL  r2,  (STKOFF + 4 * ULONG_SIZE)(r1)
    PPC_STL  r0,  (STKOFF + 3 * ULONG_SIZE)(r1)

    /* save ctr, cr and xer */
    mfctr	r2
    PPC_STL  r2,  (STKOFF + 6 * ULONG_SIZE)(r1)
    mfcr	r2
    PPC_STL  r2,  (STKOFF + 7 * ULONG_SIZE)(r1)
    mfxer	r2
    PPC_STL  r2,  (STKOFF + 8 * ULONG_SIZE)(r1)

    /* save r5 - r31 */
    PPC_STL  r5,  (STKOFF + 10 * ULONG_SIZE)(r1)
    PPC_STL  r6,  (STKOFF + 11 * ULONG_SIZE)(r1)
    PPC_STL  r7,  (STKOFF + 12 * ULONG_SIZE)(r1)
    PPC_STL  r8,  (STKOFF + 13 * ULONG_SIZE)(r1)
    PPC_STL  r9,  (STKOFF + 14 * ULONG_SIZE)(r1)
    PPC_STL  r10,  (STKOFF + 15 * ULONG_SIZE)(r1)
    PPC_STL  r11,  (STKOFF + 16 * ULONG_SIZE)(r1)
    PPC_STL  r12,  (STKOFF + 17 * ULONG_SIZE)(r1)
    PPC_STL  r13,  (STKOFF + 18 * ULONG_SIZE)(r1)
    PPC_STL  r14,  (STKOFF + 19 * ULONG_SIZE)(r1)
    PPC_STL  r15,  (STKOFF + 20 * ULONG_SIZE)(r1)
    PPC_STL  r16,  (STKOFF + 21 * ULONG_SIZE)(r1)
    PPC_STL  r17,  (STKOFF + 22 * ULONG_SIZE)(r1)
    PPC_STL  r18,  (STKOFF + 23 * ULONG_SIZE)(r1)
    PPC_STL  r19,  (STKOFF + 24 * ULONG_SIZE)(r1)
    PPC_STL  r20,  (STKOFF + 25 * ULONG_SIZE)(r1)
    PPC_STL  r21,  (STKOFF + 26 * ULONG_SIZE)(r1)
    PPC_STL  r22,  (STKOFF + 27 * ULONG_SIZE)(r1)
    PPC_STL  r23,  (STKOFF + 28 * ULONG_SIZE)(r1)
    PPC_STL  r24,  (STKOFF + 29 * ULONG_SIZE)(r1)
    PPC_STL  r25,  (STKOFF + 30 * ULONG_SIZE)(r1)
    PPC_STL  r26,  (STKOFF + 31 * ULONG_SIZE)(r1)
    PPC_STL  r27,  (STKOFF + 32 * ULONG_SIZE)(r1)
    PPC_STL  r28,  (STKOFF + 33 * ULONG_SIZE)(r1)
    PPC_STL  r29,  (STKOFF + 34 * ULONG_SIZE)(r1)
    PPC_STL  r30,  (STKOFF + 35 * ULONG_SIZE)(r1)
    PPC_STL  r31,  (STKOFF + 36 * ULONG_SIZE)(r1)

    bl	BRANCH_LABEL(of_client_interface)

    /* restore r5 - r31 */
    PPC_LL  r5,  (STKOFF + 10 * ULONG_SIZE)(r1)
    PPC_LL  r6,  (STKOFF + 11 * ULONG_SIZE)(r1)
    PPC_LL  r7,  (STKOFF + 12 * ULONG_SIZE)(r1)
    PPC_LL  r8,  (STKOFF + 13 * ULONG_SIZE)(r1)
    PPC_LL  r9,  (STKOFF + 14 * ULONG_SIZE)(r1)
    PPC_LL  r10,  (STKOFF + 15 * ULONG_SIZE)(r1)
    PPC_LL  r11,  (STKOFF + 16 * ULONG_SIZE)(r1)
    PPC_LL  r12,  (STKOFF + 17 * ULONG_SIZE)(r1)
    PPC_LL  r13,  (STKOFF + 18 * ULONG_SIZE)(r1)
    PPC_LL  r14,  (STKOFF + 19 * ULONG_SIZE)(r1)
    PPC_LL  r15,  (STKOFF + 20 * ULONG_SIZE)(r1)
    PPC_LL  r16,  (STKOFF + 21 * ULONG_SIZE)(r1)
    PPC_LL  r17,  (STKOFF + 22 * ULONG_SIZE)(r1)
    PPC_LL  r18,  (STKOFF + 23 * ULONG_SIZE)(r1)
    PPC_LL  r19,  (STKOFF + 24 * ULONG_SIZE)(r1)
    PPC_LL  r20,  (STKOFF + 25 * ULONG_SIZE)(r1)
    PPC_LL  r21,  (STKOFF + 26 * ULONG_SIZE)(r1)
    PPC_LL  r22,  (STKOFF + 27 * ULONG_SIZE)(r1)
    PPC_LL  r23,  (STKOFF + 28 * ULONG_SIZE)(r1)
    PPC_LL  r24,  (STKOFF + 29 * ULONG_SIZE)(r1)
    PPC_LL  r25,  (STKOFF + 30 * ULONG_SIZE)(r1)
    PPC_LL  r26,  (STKOFF + 31 * ULONG_SIZE)(r1)
    PPC_LL  r27,  (STKOFF + 32 * ULONG_SIZE)(r1)
    PPC_LL  r28,  (STKOFF + 33 * ULONG_SIZE)(r1)
    PPC_LL  r29,  (STKOFF + 34 * ULONG_SIZE)(r1)
    PPC_LL  r30,  (STKOFF + 35 * ULONG_SIZE)(r1)
    PPC_LL  r31,  (STKOFF + 36 * ULONG_SIZE)(r1)

    /* restore ctr, cr and xer */
    PPC_LL r2,  (STKOFF + 6 * ULONG_SIZE)(r1)
    mtctr	r2
    PPC_LL r2,  (STKOFF + 7 * ULONG_SIZE)(r1)
    mtcr	r2
    PPC_LL r2,  (STKOFF + 8 * ULONG_SIZE)(r1)
    mtxer	r2

    /* restore r0 and r2 */
    PPC_LL r2,  (STKOFF + 4 * ULONG_SIZE)(r1)
    PPC_LL r0,  (STKOFF + 3 * ULONG_SIZE)(r1)

    /* restore caller stack */
    PPC_LL  r1,  (STKOFF)(r1)

    PPC_LL  r4, PPC_LR_STKOFF(r1)
    mtlr	r4
    PPC_LL  r4, STKOFF(r1)
    PPC_LL  r1, 0(r1)

    blr

    /* rtas glue (must be reloctable) */
GLOBL(of_rtas_start):
    /* r3 = argument buffer, r4 = of_rtas_start */
    /* according to the CHRP standard, cr must be preserved (cr0/cr1 too?) */
    blr
GLOBL(of_rtas_end):


#define CACHE_LINE_SIZE         128
#define LG_CACHE_LINE_SIZE      5

/* flush_icache_range( unsigned long start, unsigned long stop) */
_GLOBAL(flush_icache_range):
        li      r5,CACHE_LINE_SIZE-1
        andc    r3,r3,r5
        subf    r4,r3,r4
        add     r4,r4,r5
        srwi.   r4,r4,LG_CACHE_LINE_SIZE
        beqlr
        mtctr   r4
        mr      r6,r3
1:      dcbst   0,r3
        addi    r3,r3,CACHE_LINE_SIZE
        bdnz    1b
        sync                            /* wait for dcbst's to get to ram */
        mtctr   r4
2:      icbi    0,r6
        addi    r6,r6,CACHE_LINE_SIZE
        bdnz    2b
        sync                            /* additional sync needed on g4 */
        isync
        blr

/* flush_dcache_range( unsigned long start, unsigned long stop) */
_GLOBAL(flush_dcache_range):
        li      r5,CACHE_LINE_SIZE-1
        andc    r3,r3,r5
        subf    r4,r3,r4
        add     r4,r4,r5
        srwi.   r4,r4,LG_CACHE_LINE_SIZE
        beqlr
        mtctr   r4
        mr      r6,r3
1:      dcbst   0,r3
        addi    r3,r3,CACHE_LINE_SIZE
        bdnz    1b
        sync                            /* wait for dcbst's to get to ram */
        mtctr   r4
2:      dcbi    0,r6
        addi    r6,r6,CACHE_LINE_SIZE
        bdnz    2b
        sync
        blr
