/*
 * CH585 JumpIAP reset startup.
 *
 * This is intentionally different from startup_CH585.S: it invokes the
 * EEPROM-based selector before mret switches to application user mode, then
 * enters the selected image in the reset context expected by its startup.
 */

    .section .init,"ax",@progbits
    .global _start
    .align 1
_start:
    j handle_reset

    .section .handle_reset,"ax",@progbits
    .align 1
handle_reset:
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    la sp, _eusrstack

    /* Copy the Flash-resident highcode_init routine into RAM. */
    la a0, _highcode_init_lma
    la a1, _highcode_init_vma_start
    la a2, _highcode_init_vma_end
    bgeu a1, a2, 2f
1:
    lw t0, (a0)
    sw t0, (a1)
    addi a0, a0, 4
    addi a1, a1, 4
    bltu a1, a2, 1b

2:
    la a0, __highcode_init
    jr a0

    .section .highcode_init,"ax",@progbits
    .align 1
__highcode_init:
    jal highcode_init

    /* Copy highcode (including EEPROM access routines) into RAM. */
    la a0, _highcode_lma
    la a1, _highcode_vma_start
    la a2, _highcode_vma_end
    bgeu a1, a2, 2f
1:
    lw t0, (a0)
    sw t0, (a1)
    addi a0, a0, 4
    addi a1, a1, 4
    bltu a1, a2, 1b

    /* Initialize data and bss for the selector C function. */
2:
    la a0, _data_lma
    la a1, _data_vma
    la a2, _edata
    bgeu a1, a2, 4f
3:
    lw t0, (a0)
    sw t0, (a1)
    addi a0, a0, 4
    addi a1, a1, 4
    bltu a1, a2, 3b

4:
    la a0, _sbss
    la a1, _ebss
    bgeu a0, a1, 6f
5:
    sw zero, (a0)
    addi a0, a0, 4
    bltu a0, a1, 5b

6:
    call JumpIAP_SelectTarget
    fence.i
    jr a0
