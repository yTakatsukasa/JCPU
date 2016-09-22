	.text
	.org 0x100


	.global _start
_start:
	l.addi	r1, r0, 0x7f00
	l.addi	r2, r1, 0x0

	l.mfspr	r3, r0, 17
	l.ori	r3, r3, 0x4
	l.mtspr	r0, r3, 17

    /* Invalidate Instruction Tag */
    l.ori r8,r0,16
    l.ori r9,r0,16
    l.muli r9,r9,511
_ic_tag_invalidate_start:
    l.ori r10,r0,((0x4<<11) | 2)
    l.mtspr r10,r9,0
    l.sfne r9,r0
    l.bf _ic_tag_invalidate_start
    l.sub r9,r9,r8

    /* Enable Instruction Cache */
    l.mfspr r10,r0,17
    l.ori r10,r10,0x10
    l.mtspr r0,r10,17
    l.nop                           /* Flush the pipeline. */
    l.nop
    l.nop
    l.nop
    l.nop

    /* Invalidate Data Tag */
//    l.ori r8,r0,16
//    l.ori r9,r0,16
//    l.muli r9,r9,511
//_dc_tag_invalidate_start:
//    l.ori r10,r0,((0x3<<11) | 3)
//    l.mtspr r10,r9,0
//    l.sfne r9,r0
//    l.bf _dc_tag_invalidate_start
//    l.sub r9,r9,r8


    /* Enable Data Cache */
//    l.mfspr r10,r0,17
//    l.ori r10,r10,0x08
//    l.mtspr r0,r10,17

// Jump to main()
	l.jal	main
	l.nop
// Jump to exit
	l.jal	_exit
	l.nop


// Loop forever at exit
// If default processor semihost library is loaded this will terminate simulation
	.global _exit
	.global __exit
_exit:
__exit:
    l.movhi r4,0x6000
    l.ori r4,r4,0x8
    l.sb 0(r4),r3
	l.j	_exit
	l.nop

