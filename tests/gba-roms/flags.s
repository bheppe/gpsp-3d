@ Guest flags kept across loads (the dynarec does some loads inline, using
@ the host flags, only where the guest flags are not needed afterwards).
@ Each case sets flags, loads from IWRAM/EWRAM/ROM, then uses the flags.
    .syntax unified
    .arm
    .section .text.start, "ax"
    .global _start
_start:
    b reset
    .fill 188, 1, 0
reset:
    ldr sp, =0x03007F00
    ldr r0, =0x03006000
    ldr r1, =0x02010000
    mov r2, #0
1:  str r2, [r0, r2, lsl #2]
    str r2, [r1, r2, lsl #2]
    add r2, r2, #1
    cmp r2, #64
    blo 1b
    mov r11, #0                     @ checksum
    mov r10, #200                   @ rounds
loop:
    ldr r0, =0x03006000
    ldr r1, =0x02010000
    ldr r2, =romdata
    and r3, r10, #31
    @ 1: cmp, load, conditional move
    cmp r3, #16
    ldr r4, [r0, r3, lsl #2]
    movlo r5, #1
    movhs r5, #2
    add r11, r11, r5
    @ 2: subs sets C, load, adc uses C
    subs r6, r3, #10
    ldrb r4, [r1, r3]
    adc r11, r11, r4
    @ 3: movs lsr sets C from shifter, load, branch on C
    movs r6, r3, lsr #1
    ldrh r4, [r2, #2]
    bcs 2f
    add r11, r11, #7
2:  add r11, r11, r4
    @ 4: adds sets V, load, conditional on V
    ldr r6, =0x7FFFFFF0
    adds r6, r6, r3
    ldrsb r4, [r0, #5]
    addvs r11, r11, #3
    add r11, r11, r4
    @ 5: tst, two loads, conditional
    tst r3, #4
    ldr r4, [r1, #8]
    ldrsh r5, [r2, #4]
    addne r11, r11, r4
    addeq r11, r11, r5
    @ 6: muls sets Z, load, conditional
    muls r6, r3, r3
    ldr r4, [r0, #12]
    addeq r11, r11, #100
    add r11, r11, r4
    @ 7: RRX uses C across a load
    cmp r3, #5
    ldr r4, [r1, #16]
    mov r6, r11, rrx
    add r11, r6, r4
    @ 8: logical S keeps C, load, then C
    cmp r3, #20                     @ C = r3 >= 20
    ands r6, r3, #3                 @ C kept
    ldrb r4, [r0, #3]
    adc r11, r11, r4
    @ 9: flags set, load, flags not needed (inline ok), then set again
    cmp r3, #1
    ldr r4, [r0, #20]
    cmp r4, #2
    addhi r11, r11, #1
    @ 10: conditional load itself
    cmp r3, #8
    ldrlo r4, [r0, #24]
    movhs r4, #9
    add r11, r11, r4
    subs r10, r10, #1
    bne loop
    ldr r0, =0x02000000
    ldr r1, =0x600DF00D
    str r1, [r0]
    str r11, [r0, #4]
hang:
    b hang
    .ltorg
romdata:
    .word 0x80017FFF, 0x12345678, 0xFEDCBA98
