/* Qin2 Pro v1 board DTB linked into Image. */
	.section .rodata.qin2pro_embedded_dtb, "a"
	.balign 8
	.global qin2pro_embedded_dtb
qin2pro_embedded_dtb:
	.incbin "/home/kevin/los/kernel/sprd/qin2pro/arch/arm64/kernel/qin2pro_merged.dtb"
