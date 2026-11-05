/* port_binaries.S -- embed the baked binary blobs that binaries.s provided on
 * the PSX (it used `incbin`). Assembled for x86; the bytes are layout-agnostic,
 * and the 32-bit (-m32) C build interprets them with the correct PSX layout.
 *
 * Paths resolve against the repo root, passed to the assembler via -I. */
	.section .data
	.balign 4
	.globl frogpsx_mwi
frogpsx_mwi:
	.incbin "merge/FROGPSX.MWI"
	.balign 4
	.globl card_image
card_image:
	.incbin "binaries/FROGSAVE.TIM"
