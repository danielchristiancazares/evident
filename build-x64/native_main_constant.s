	.def	@feat.00;
	.scl	3;
	.type	0;
	.endef
	.globl	@feat.00
@feat.00 = 0
	.file	"native_main_constant.s.tmp.ll"
	.def	evid$main;
	.scl	2;
	.type	32;
	.endef
	.text
	.globl	evid$main                       # -- Begin function evid$main
	.p2align	4
evid$main:                              # @"evid$main"
.seh_proc evid$main
# %bb.0:                                # %entry
	pushq	%rax
	.seh_stackalloc 8
	.seh_endprologue
# %bb.1:                                # %bb0
	movq	$7, (%rsp)
	movq	(%rsp), %rax
	.seh_startepilogue
	popq	%rcx
	.seh_endepilogue
	retq
	.seh_endproc
                                        # -- End function
	.addrsig
