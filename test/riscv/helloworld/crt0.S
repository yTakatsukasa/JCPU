	.text
	//.org 0x0


	.global _start
	.global _end
_start:
    li sp, 0x20000
    jal main
    j _end

_end:
    li x1, 0x60000008
    sw x0, 0(x1)

_end_loop:
    j _end_loop
	
