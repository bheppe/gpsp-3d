    .section .text.start, "ax"
    .arm
    .global _start
_start:
    b reset
    .fill 188, 1, 0      @ header (logo/title not checked by the emulator)
reset:
    mov r0, #0x12        @ IRQ mode
    msr cpsr_c, r0
    ldr sp, =0x03007FA0
    mov r0, #0x1F        @ system mode
    msr cpsr_c, r0
    ldr sp, =0x03007F00
    ldr r0, =main
    bx r0
