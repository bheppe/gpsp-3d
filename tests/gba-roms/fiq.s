@ FIQ register banking test for gpSP (OpenLara draws in FIQ mode with IRQs on).
@ The main loop keeps known values in r8-r12 in FIQ mode while VBlank IRQs
@ come in; the screen turns green if they survive, red if not.

    .section .text.start, "ax"
    .arm
    .global _start
_start:
    b reset
    .fill 188, 1, 0
reset:
    mov r0, #0x12            @ IRQ mode stack
    msr cpsr_c, r0
    ldr sp, =0x03007FA0
    mov r0, #0x11            @ FIQ mode: its own r8-r12 (start them at 0)
    msr cpsr_c, r0
    ldr sp, =0x03007E00
    mov r0, #0x1F            @ system mode
    msr cpsr_c, r0
    ldr sp, =0x03007F00
    ldr r0, =0x04000000      @ mode 3, BG2
    ldr r1, =0x0403
    strh r1, [r0]
    ldr r1, =isr             @ BIOS calls [0x03007FFC] on IRQ
    ldr r2, =0x03007FFC
    str r1, [r2]
    mov r1, #8               @ DISPSTAT: VBlank IRQ
    strh r1, [r0, #4]
    add r2, r0, #0x200
    mov r1, #1
    strh r1, [r2]            @ IE = VBlank
    str r1, [r2, #8]         @ IME = 1
    ldr r4, =0x4000        @ iterations
    mov r5, #0               @ errors
loop:
    msr cpsr_c, #0x11        @ like OpenLara's fiq_on (IRQs stay enabled)
    ldr r8,  =0x11111111
    ldr r9,  =0x22222222
    ldr r10, =0x33333333
    ldr r11, =0x44444444
    ldr r12, =0x55555555
    mov r6, #64
1:  subs r6, r6, #1          @ give the VBlank IRQ time to land here
    bne 1b
    ldr r7, =0x11111111
    cmp r8, r7
    addne r5, r5, #1
    ldr r7, =0x55555555
    cmp r12, r7
    addne r5, r5, #1
    msr cpsr_c, #0x1F        @ fiq_off
    subs r4, r4, #1
    bne loop
    ldr r1, =0x03E0          @ green: r8-r12 survived
    cmp r5, #0
    ldrne r1, =0x001F        @ red: corrupted
    ldr r0, =0x06000000
    ldr r2, =38400
2:  strh r1, [r0], #2
    subs r2, r2, #1
    bne 2b
3:  b 3b
isr:                         @ acknowledge VBlank, like tonc's isr_master
    ldr r0, =0x04000200
    mov r1, #1
    strh r1, [r0, #2]        @ IF
    ldr r0, =0x03007FF8
    ldrh r2, [r0]
    orr r2, r2, r1
    strh r2, [r0]            @ BIOS IF
    bx lr
