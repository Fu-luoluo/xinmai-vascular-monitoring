ENTRY( _start )

MEMORY
{
	FLASH (rx) : ORIGIN = 0x0006D000, LENGTH = 12K
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
		*(.sdata2.*)
		*(.glue_7)
		*(.glue_7t)
		*(.gnu.linkonce.t.*)
		. = ALIGN(4);
	} >FLASH AT>FLASH

	.fini :
	{
		KEEP(*(SORT_NONE(.fini)))
		. = ALIGN(4);
	} >FLASH AT>FLASH

	PROVIDE( _etext = . );
	PROVIDE( _eitcm = . );

	.dlalign :
	{
		. = ALIGN(4);
		PROVIDE(_data_lma = .);
	} >FLASH AT>FLASH

	.data :
	{
		. = ALIGN(4);
		PROVIDE(_data_vma = .);
    	*(.gnu.linkonce.r.*)
    	*(.data .data.*)
    	*(.gnu.linkonce.d.*)
		. = ALIGN(8);
    	PROVIDE( __global_pointer$ = . + 0x800 );
    	*(.sdata .sdata.*)
    	*(.gnu.linkonce.s.*)
    	. = ALIGN(4);
		PROVIDE( _edata = .);
	} >RAM AT>FLASH

	.bss :
	{
		. = ALIGN(4);
		PROVIDE( _sbss = .);
  	    *(.sbss*)
        *(.gnu.linkonce.sb.*)
		*(.bss*)
     	*(.gnu.linkonce.b.*)
		*(COMMON*)
		. = ALIGN(4);
		PROVIDE( _ebss = .);
	} >RAM AT>FLASH

	PROVIDE( _end = _ebss);
	PROVIDE( end = . );

	.stack ORIGIN(RAM)+LENGTH(RAM) :
	{
		. = ALIGN(4);
		PROVIDE(_eusrstack = . );
	} >RAM
}
