	.file	"examples.c"
	.text
	.globl	sum_list
	.type	sum_list, @function
sum_list:
.LFB0:
	.cfi_startproc
	endbr64
	movl	$0, %eax
.L2:
	testq	%rdi, %rdi
	je	.L4
	addq	(%rdi), %rax
	movq	8(%rdi), %rdi
	jmp	.L2
.L4:
	ret
	.cfi_endproc
.LFE0:
	.size	sum_list, .-sum_list
	.globl	rsum_list
	.type	rsum_list, @function
rsum_list:
.LFB1:
	.cfi_startproc
	endbr64
	testq	%rdi, %rdi
	je	.L7
	pushq	%rbx
	.cfi_def_cfa_offset 16
	.cfi_offset 3, -16
	movq	(%rdi), %rbx
	movq	8(%rdi), %rdi
	call	rsum_list
	addq	%rbx, %rax
	popq	%rbx
	.cfi_def_cfa_offset 8
	ret
.L7:
	.cfi_restore 3
	movl	$0, %eax
	ret
	.cfi_endproc
.LFE1:
	.size	rsum_list, .-rsum_list
	.globl	bubble_sort
	.type	bubble_sort, @function
bubble_sort:
.LFB2:
	.cfi_startproc
	endbr64
	leaq	-8(%rdi,%rsi,8), %rsi
	jmp	.L13
.L14:
	addq	$8, %rax
.L16:
	cmpq	%rsi, %rax
	jnb	.L18
	movq	8(%rax), %rdx
	movq	(%rax), %rcx
	cmpq	%rcx, %rdx
	jge	.L14
	movq	%rcx, 8(%rax)
	movq	%rdx, (%rax)
	jmp	.L14
.L18:
	subq	$8, %rsi
.L13:
	cmpq	%rdi, %rsi
	jbe	.L19
	movq	%rdi, %rax
	jmp	.L16
.L19:
	ret
	.cfi_endproc
.LFE2:
	.size	bubble_sort, .-bubble_sort
	.ident	"GCC: (Ubuntu 9.4.0-1ubuntu1~20.04.1) 9.4.0"
	.section	.note.GNU-stack,"",@progbits
	.section	.note.gnu.property,"a"
	.align 8
	.long	 1f - 0f
	.long	 4f - 1f
	.long	 5
0:
	.string	 "GNU"
1:
	.align 8
	.long	 0xc0000002
	.long	 3f - 2f
2:
	.long	 0x3
3:
	.align 8
4:
