ENTRY( _start )

MEMORY
{
	FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 4K
	RAM (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
}

SECTIONS
{
	.init :
	{
		_sinit = .;
		. = ALIGN(4);
		KEEP(*(SORT_NONE(.init)))
		. = ALIGN(4);
		_einit = .;
	} >FLASH AT>FLASH

    .highcode_initlalign :
    {
        . = ALIGN(4);
        PROVIDE(_highcode_init_lma = .);
    } >FLASH AT>FLASH

    .highcode_init :
    {
        PROVIDE(_highcode_init_vma_start = .);
        . = ALIGN(1024);
        *(.vector);
        KEEP(*(SORT_NONE(.vector_handler)))
        *(.highcode_init);
        *(.highcode_init.*);
        . = ALIGN(4);
        PROVIDE(_highcode_init_vma_end = .);
    } >RAM AT>FLASH

    .highcodelalign :
    {
        . = ALIGN(4);
        PROVIDE(_highcode_lma = .);
    } >FLASH AT>FLASH

    .highcode :
    {
        . = ALIGN(4);
        PROVIDE(_highcode_vma_start = .);
        *(.highcode);
        *(.highcode.*);
        *(.highcode_lp);
        *(.highcode_lp.*);
        . = ALIGN(4);
        PROVIDE(_highcode_vma_end = .);
    } >RAM AT>FLASH

	.text :
	{
		. = ALIGN(4);
		KEEP(*(SORT_NONE(.handle_reset)))
		*(.text)
		*(.text.*)
		*(.rodata)
		*(.rodata*)
		. = ALIGN(4);
	} >FLASH AT>FLASH

	.dlalign :
	{
		. = ALIGN(4);
		PROVIDE(_data_lma = .);
	} >FLASH AT>FLASH

	.data :
	{
		. = ALIGN(4);
		PROVIDE(_data_vma = .);
		*(.data .data.*)
		*(.sdata .sdata.*)
		. = ALIGN(4);
		PROVIDE( __global_pointer$ = . + 0x800 );
		PROVIDE( _edata = .);
	} >RAM AT>FLASH

	.bss :
	{
		. = ALIGN(4);
		PROVIDE( _sbss = .);
		*(.bss*)
		*(COMMON*)
		. = ALIGN(4);
		PROVIDE( _ebss = .);
	} >RAM AT>FLASH

	.stack ORIGIN(RAM)+LENGTH(RAM) :
	{
		. = ALIGN(4);
		PROVIDE(_eusrstack = . );
	} >RAM
}
