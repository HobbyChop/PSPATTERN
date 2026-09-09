/* The drum kit, as data.
 *
 * drumkit.bin is baked during the build by the generator in
 * sources/Resources/bake_drumkit.cpp, from the same synthesis the
 * app carries, and lands in the build
 * directory -- which is where the assembler runs, so the bare name
 * resolves. Nothing here is checked in: change a drum and the next
 * build bakes the change.
 *
 * The four byte alignment is load bearing. The blob opens with a
 * table of ints and the audio behind it is read as shorts, and this
 * is a machine that faults on an unaligned load rather than fixing
 * it up quietly.
 */

	.section .rodata
	.balign 4
	.global drumkitBlob
drumkitBlob:
	.incbin "drumkit.bin"
	.balign 4
	.global drumkitBlobEnd
drumkitBlobEnd:
