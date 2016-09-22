

MEMORY    
{
    ram         : ORIGIN = 0x00000000, LENGTH = 0x10000
}

SECTIONS

{

	. =	0x0;
	.text			:	{*(.text)}			> ram
	.rodata			:	{*(.rodata)}		> ram
	.rodata.str1.1	:	{*(.rodata.str1.1)}	> ram
	.rodata.cst4	:	{*(.rodata.cst4)}	> ram
	.data			:	{*(.data)}			> ram
	.bss			:	{*(.bss)}			> ram
	.icm			:	{*(.icm)}			> ram
	. =	 0x10000; 
	.stack			:	{*(.stack)}			
}
