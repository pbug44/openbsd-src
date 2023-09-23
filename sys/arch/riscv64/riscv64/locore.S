/*	$OpenBSD: locore.S,v 1.15 2022/12/02 12:27:08 jca Exp $	*/

/*-
 * Copyright (c) 2015-2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "assym.h"

#include <sys/syscall.h>
#include <machine/asm.h>
#include <machine/param.h>
#include <machine/vmparam.h>
#include <machine/riscvreg.h>
#include <machine/pte.h>

#define VM_EARLY_DTB_ADDRESS	(VM_MAX_KERNEL_ADDRESS - (2 * L2_SIZE))

	.globl	kernbase
	.set	kernbase, KERNBASE

	.text
/*
 * Main entry point.  Arguments are as follows:
 *  - a0 = esym
 *  - a2 = dtbp
 *
 * It is expected that only a single CPU will enter here.
 */
	.globl _start_kern_bootstrap
_start_kern_bootstrap:

	/* Set the global pointer */
.option push
.option norelax
	lla	gp, __global_pointer$
.option pop

	/*
	 * Page tables setup
	 */

	/* Get the kernel's load address */
	jal	get_physmem


	mv	s5, a0			/* save a0 */
	mv	s6, a1		 	/* save a1 */

	/* Add L1 entry for kernel */
	lla	s1, pagetable_l1	/* address of L1 pagetable l1 */
	lla	s3, pagetable_l2	/* Link to next level PN (l2) */

	li	a3, KERNBASE
	li	a4, 1			/* 1 iteration */
	li	a5, L1_SHIFT		/* L1 operation SHIFT */
	li	a6, (PTE_V | PTE_TH)
	li	a7, PTE_PPN0_S		/* s3 is shifted by PTE_PPN0_S */

	jal	rv64_generic_pte

	mv	a0, s5			/* restore a0 */
	mv	a1, s6			/* restore a1 */

	mv	s5, a0			/* save a0 */
	mv	s6, a1		 	/* save a1 */

	/* Level 2 superpages (512 x 2MiB) */
	lla	s1, pagetable_l2	/* init s1 to pagetable l2 */
	mv	s3, s9			/* a PA address */

	li	a3, KERNBASE

	li	a4, 511			/* 512 iteration */
	li	a5, L2_SHIFT		/* L2_SHIFT = 2MiB worth of bits */
	li	a6, (PTE_KERN|PTE_X|PTE_TH)
	li	a7, PTE_PPN1_S

	jal	rv64_generic_pte

	mv	a0, s5			/* restore a0 */
	mv	a1, s6			/* restore a1 */

#if 0
	mv	s5, a0			/* save a0 */
	mv	s6, a1		 	/* save a1 */

	/* Level 2 superpages (512 x 2MiB) */
	lla	s1, pagetable_l2	/* init s1 to pagetable l2 */
	mv	s3, s9			/* a PA address */

	li	a3, KERNBASE
	li	t0, L2_SIZE
	add	a3, a3, t0		/* add 2 MiB to KERNBASE */

	li	a4, 511			/* 511 iterations */
	li	a5, L2_SHIFT		/* L2_SHIFT = 2MiB worth of bits */
	li	a6, (PTE_KERN | PTE_X| PTE_TH)
	li	a7, PTE_PPN1_S

	jal	rv64_generic_pte

	mv	a0, s5			/* restore a0 */
	mv	a1, s6			/* restore a1 */
#endif

	/* Create an L1 page for early devmap */
	mv	s5, a0			/* save a0 */
	mv	s6, a1		 	/* save a1 */

	lla	s1, pagetable_l1	/* address of L1 pagetable l1 */
	lla	s3, pagetable_l2_devmap

	li	a3, (VM_MAX_KERNEL_ADDRESS - L1_SIZE)
	li	a4, 1			/* 1 iteration */
	li	a5, L1_SHIFT		/* L1 operation SHIFT */
	li	a6, (PTE_V | PTE_TH)
	li	a7, PTE_PPN0_S		/* s3 is shifted by PTE_PPN0_S */

	jal	rv64_generic_pte

	mv	a0, s5			/* restore a0 */
	mv	a1, s6			/* restore a1 */

	/* Create an L1 page superpage for DTB */
	mv	s5, a0			/* save a0 */
	mv	s6, a1		 	/* save a1 */

	lla	s1, pagetable_l2_devmap	/* Link to next level PN */
	mv	s3, a1

	srli	s3, s3, PAGE_SHIFT
	/* Mask off any bits that aren't aligned */
	li	t0, ~((1 << (PTE_PPN1_S - PTE_PPN0_S)) - 1)
	and	s3, s3, t0
	slli	s3, s3, PAGE_SHIFT		/* restore to address */

	li	a3, (VM_MAX_KERNEL_ADDRESS - L1_SIZE)
	li	a4, 512
	li	a5, L2_SHIFT
	li	a6, (PTE_KERN | PTE_TH)
	li	a7, PTE_PPN1_S		/* PPN1 shift */

	jal	rv64_generic_pte

	mv	a0, s5			/* restore a0 */
	mv	a1, s6			/* restore a1 */

	/* Page tables END */

	/* turn off any possible FP bits set */
	li	t0, SSTATUS_FS_MASK
	csrc	sstatus, t0

	/* Setup supervisor trap vector */
	lla	t0, va
	sub	t0, t0, s9
	li	t1, KERNBASE
	add	t0, t0, t1
	csrw	stvec, t0

	/* Set page tables base register */
	lla	s2, pagetable_l1
	srli	s2, s2, PAGE_SHIFT
	li	t0, SATP_MODE_SV39
	or	s2, s2, t0
	sfence.vma
	csrw	satp, s2

	.align 2
va:
	/* Set the global pointer again, this time with the virtual address. */
.option push
.option norelax
	lla	gp, __global_pointer$
.option pop

	/* Setup supervisor trap vector */
	la	t0, cpu_exception_handler
	csrw	stvec, t0

	/* Ensure sscratch is zero */
	li	t0, 0
	csrw	sscratch, t0

	/* Initialize stack pointer */
	la	sp, initstack_end

	/* Clear frame pointer */
	mv	s0, zero

	/* Allocate space for riscv_bootparams */
	addi	sp, sp, -RISCV_BOOTPARAMS_SIZEOF
	andi	sp, sp, ~STACKALIGNBYTES

	/* Clear BSS */
	la	t0, __bss_start
	la	t1, _end
1:
	sd	zero, 0(t0)
	addi	t0, t0, 8
	bltu	t0, t1, 1b

	/* Fill riscv_bootparams */
	lla	t0, pagetable_l1
	sd	t0, RISCV_BOOTPARAMS_KERN_L1PT(sp)
	sd	s9, RISCV_BOOTPARAMS_KERN_PHYS(sp)

	la	t0, initstack
	sd	t0, RISCV_BOOTPARAMS_KERN_STACK(sp)

	li	t1, (VM_MAX_KERNEL_ADDRESS - L1_SIZE)
	li	t2, (L2_OFFSET)
	and	t2, a1, t2
	add	t0, t1, t2
	sd	t0, RISCV_BOOTPARAMS_DTBP_VIRT(sp)
	sd	a1, RISCV_BOOTPARAMS_DTBP_PHYS(sp)

	/* Set esym to virtual address of symbol table end */
	lla	t0, esym
	sub	t1, a0, s9
	li	t2, KERNBASE
	add	t1, t1, t2
	sd	t1, 0(t0)

	mv	a0, sp
	call	initriscv		/* Off we go */
	call	main

/*
 * Get the physical address the kernel is loaded to. Returned in s9.
 */
get_physmem:
	lla	t0, virt_map	/* physical address of virt_map */
	ld	t1, 0(t0)	/* virtual address of virt_map */
	sub	t1, t1, t0	/* calculate phys->virt delta */
	li	t2, KERNBASE
	sub	s9, t2, t1	/* s9 = physmem base */
	ret


/*
 * rv64_generic_pte, takes 5 arguments and accesses 2 saved registers
 * must save before and restore after a0 and a1
 *
 * a3 - VA	(an address, usually KERNBASE aligned )
 * a4 - iteration slots (between 1...512, truncates to 512 if higher
 * a5 - Lx shift        (L1_SHIFT for L1)
 * a6 - perms           (PTE_R|PTE_W|PTE_X is LEAF, otherwise BRANCH PTE)
 * a7 - pagenumber shift (PTE_PPN0_S for L1)
 *
 * s1 - the respective pagetable address for writing in
 * s3 - the physical address or the pagetable we're referring to
 *
 */

rv64_generic_pte:

	li	t0, 512
	bleu	a4, t0, 5f

	mv	a4, t0			/* programmer mistake? set to 512 */
5:

	li	t0, PAGE_SHIFT
	srl	t1, a3, t0 		/* t1 is a pagenumber of VA a3 */
	sub	t2, a5, t0		/* Lx_SHIFT - PAGE_SHIFT=(30,21,12) */
	srl	t2, t1, t2		/* t2 is a PPN */

	srl	t4, s3, t0 		/* t4 is a pagenumber of PA s3 */
	sub	t5, a5, t0		/* L_SHIFT - PAGE_SHIFT= (30, 21, 12) */
	srl	t5, t4, t5		/* t5 is a PPN */

	andi	t6, t2, 0x1ff		/* t6 is our slot */

	slli	t0, t6, PTE_SHIFT	/* t0 = PTE_SIZE * slot# reduced */
	mv	s2, s1			/* save s1 in s2 */
	add	s1, s1, t0		/* s1 = initial offset in pagetable */


	add	a4, a4, t6		/* limit by adding a4 with slots */
	slli	a4, a4, PTE_SHIFT	/* limit is now an offset in s1 */

	li	t0, 4096

	bleu	a4, t0, 0f		/* if limit <= 4096, branch to 0f */
	mv	a4, t0			/* if limit > 4096 set it to 4096 */
0:
	add	a4, a4, s2		/* initial s1 + offset(a4) is limit */
1:
	andi 	t0, a6, (PTE_X|PTE_W|PTE_R)	/* determine branch/leaf */
	beqz	t0, 2f				/* if X|R|W unset, goto 2f */

	/*
	 *  we are a LEAF (any level cache can be a LEAF according to docs)
	 *
	 */

	li	t0, PTE_PPN0_S
	beq	t0, a7, 10f

	li	t0, PTE_PPN1_S
	beq	t0, a7, 11f

	/* FALLTHROUGH */

	/* %%%%%%%%%%%%%%%%%%%% L3 PA -> PTE LEAF %%%%%%%%%%%%%%%%%%%%%% */

	add	t0, t6, t5
	sll	t3, t0, a7		/* PTE_PPN0_S */
	or	t3, t3, a6		/* OR permissions */

	j	3f		/* jump over some leafs and BRANCHes */

10:
	/* %%%%%%%%%%%%%%%%%%%% L1 PA -> PTE LEAF %%%%%%%%%%%%%%%%%%%%%% */

	add	t0, t6, t5
	sll	t3, t0, a7		/* PTE_PPN0_S */
	or	t3, t3, a6		/* OR permissions */

	j	3f
11:
	/* %%%%%%%%%%%%%%%%%%%% L2 PA -> PTE LEAF %%%%%%%%%%%%%%%%%%%%%% */

	add	t0, t6, t5
	sll	t3, t0, a7		/* PTE_PPN1_S */
	or	t3, t3, a6		/* OR permissions */

	j	3f			/* jump over the BRANCH */
2:
	/*
	 *
	 * We are a BRANCH (there can be only two in Sv39)
	 *
	 */

	li	t0, PTE_PPN0_S
	beq	t0, a7, 20f

	/* %%%%%%%%%%%%%%%%%%%% L2 PA -> PTE BRANCH %%%%%%%%%%%%%%%%%%%%%% */

	srli	t3, s3, PAGE_SHIFT	/* t3 is in pages */
	sll	t3, t3, a7		/* PTE_PPN1_S */
	or	t3, t3, a6		/* OR permissions */

	/* this comprises the low section of the ''PTE'' */

	j	3f
20:

	/* %%%%%%%%%%%%%%%%%%%% L1 PA -> PTE BRANCH %%%%%%%%%%%%%%%%%%%%%% */

	srli	t3, s3, PAGE_SHIFT	/* t3 is in pages */
	sll	t3, t3, a7		/* PTE_PPN0_S */
	or	t3, t3, a6		/* OR permissions */

	/* this comprises the low section of the ''PTE'' */

	/* FALLTHROUGH */

3:

	/* s1 actually isn't derived from t0 */

	sd	t3, (s1)		/* store Lx PTE to position  */
	addi	s1, s1, PTE_SIZE	/* add slot (PTE_SIZE) to s1 */
	addi	t6, t6, 1		/* inc slot on t6 */
	bltu	s1, a4, 1b		/* loop to 1b if we are not at limit */
4:
	ret


	.data
	.align	4
initstack:
	.space	USPACE
initstack_end:

	.globl	sigfill
sigfill:
	unimp
esigfill:
	.globl	sigfillsiz
sigfillsiz:
	.data
	.word	esigfill - sigfill

	.text
ENTRY(sigcode)
	mv	a0, sp
	addi	a0, a0, SF_SC
	li	t0, SYS_sigreturn
	ecall

	.globl sigcoderet
sigcoderet:
	/* sigreturn failed, exit */
	li	t0, SYS_exit
	ecall
END(sigcode)
	.globl esigcode
esigcode:

	.data
	.global esym
esym:
	.quad	end

	.align	14
pagetable_l1:
	.space	PAGE_SIZE
	.align	14
pagetable_l2:
	.space	PAGE_SIZE
	.align	14
pagetable_l2_devmap:
	.space	PAGE_SIZE

	.align 3
virt_map:
	.quad	virt_map

#ifdef MULTIPROCESSOR

	.text
	.globl cpu_hatch
cpu_hatch:
	ld	tp, CI_SELF(a1)
	ld	a2, CI_SATP(a1)

	/* Set the global pointer */
.option push
.option norelax
	lla	gp, __global_pointer$
.option pop

	/* Setup stack pointer */
	ld	sp, CI_INITSTACK_END(a1)

	/* Get the kernel's load address */
	jal	get_physmem

	/* Setup supervisor trap vector */
	lla	t0, mpva
	sub	t0, t0, s9
	li	t1, KERNBASE
	add	t0, t0, t1
	csrw	stvec, t0

	/* Set page tables base register */
	lla	s2, pagetable_l1
	srli	s2, s2, PAGE_SHIFT
	li	t0, SATP_MODE_SV39
	or	s2, s2, t0
	sfence.vma
	csrw	satp, s2

	.align 2
mpva:
	/* Set the global pointer again, this time with the virtual address. */
.option push
.option norelax
	lla	gp, __global_pointer$
.option pop

	/* Setup supervisor trap vector */
	la	t0, cpu_exception_handler
	csrw	stvec, t0

	/* Ensure sscratch is zero */
	li	t0, 0
	csrw	sscratch, t0

	/* Switch to real kernel page tables */
	csrw	satp, a2
	sfence.vma

	call	cpu_start_secondary

#endif
