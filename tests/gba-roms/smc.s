@ Self-modifying code test ROM for gpSP (patterns from Mario Golf: Advance Tour).
@ Results go to EWRAM 0x02000000: [0] = 0x600DF00D when done, [1..9] = checksums.
@ The dynarec must give exactly the interpreter's results.

    .syntax unified
    .equ RES,      0x02000000
    .equ ROUNDS,   12               @ whole suite repeated (makes flush cost visible)

    .section .text.start, "ax"
    .arm
    .global _start
_start:
    b reset
    .fill 188, 1, 0
reset:
    mov r0, #0x12
    msr cpsr_c, r0
    ldr sp, =0x03007FA0
    mov r0, #0x1F
    msr cpsr_c, r0
    ldr sp, =0x03007F00

    @ copy the RAM code sections
    ldr r0, =__iwram_lma
    ldr r1, =__iwram_start
    ldr r2, =__iwram_end
    bl copy
    ldr r0, =__ewram_lma
    ldr r1, =__ewram_start
    ldr r2, =__ewram_end
    bl copy

    @ clear results
    ldr r0, =RES
    mov r1, #0
    mov r2, #16
1:  str r1, [r0], #4
    subs r2, r2, #1
    bne 1b

    mov r11, #ROUNDS
round:
    bl testA
    bl testB
    bl testC
    bl testD
    bl testE
    bl testF
    bl testG
    bl testH
    bl testI
    bl testJ
    bl testK
    bl testL
    subs r11, r11, #1
    bne round

    ldr r0, =RES
    ldr r1, =0x600DF00D
    str r1, [r0]
hang:
    b hang

copy:                               @ r0 src, r1 dst, r2 dst end
    cmp r1, r2
    ldrlo r3, [r0], #4
    strlo r3, [r1], #4
    blo copy
    bx lr

@ add r1 into RES[r0]
.macro accum idx
    ldr r2, =RES + \idx * 4
    ldr r3, [r2]
    add r3, r3, r0
    str r3, [r2]
.endm

@ A: a loop in IWRAM whose "add" is patched before every call (like the
@ rasterizer at 03000A28), plus a big routine elsewhere in IWRAM that
@ would be retranslated after each full flush
testA:
    push {r4, r5, lr}
    mov r4, #0
1:  ldr r0, =patchA
    ldr r1, =0xE2800000             @ add r0, r0, #imm
    orr r1, r1, r4
    str r1, [r0]
    mov r0, r4
    ldr r12, =loopA
    mov lr, pc
    bx r12
    accum 1
    ldr r12, =bigP
    mov lr, pc
    bx r12
    accum 9
    add r4, r4, #1
    cmp r4, #64
    blo 1b
    pop {r4, r5, lr}
    bx lr

@ B: a variable kept inside a translated block (like 03000DE8)
testB:
    push {r4, lr}
    mov r4, #50
1:  ldr r12, =varB
    mov lr, pc
    bx r12
    accum 2
    subs r4, r4, #1
    bne 1b
    pop {r4, lr}
    bx lr

@ C: two versions of a routine copied by DMA3 into the same IWRAM spot
@ (like the overlays at 03005614), then called
testC:
    push {r4, r5, lr}
    mov r4, #0
    mov r5, #1
1:  ldr r0, =0x040000D4
    tst r4, #1
    ldreq r1, =__load_start_ovA
    ldrne r1, =__load_start_ovB
    str r1, [r0]                    @ DMA3 source
    ldr r1, =OVL
    str r1, [r0, #4]                @ DMA3 dest
    ldr r1, =0x84000000 | 16        @ enable, 32-bit, 16 words
    str r1, [r0, #8]
    mov r0, r5
    ldr r12, =OVL
    mov lr, pc
    bx r12
    mov r5, r0
    accum 3
    add r4, r4, #1
    cmp r4, #40
    blo 1b
    pop {r4, r5, lr}
    bx lr

@ D: callerD (IWRAM) calls calleeD in another page with a direct BL; then
@ calleeD is patched. A stale direct link would call the old code.
testD:
    push {r4, lr}
    mov r4, #0
1:  ldr r0, =calleeD
    ldr r1, =0xE3A00000             @ mov r0, #imm
    orr r1, r1, r4
    str r1, [r0]
    ldr r12, =callerD
    mov lr, pc
    bx r12
    accum 4
    add r4, r4, #3
    cmp r4, #240
    blo 1b
    pop {r4, lr}
    bx lr

@ E: one block that straddles a 1 KB page boundary; the patched
@ instruction is in the second page, the call enters in the first
testE:
    push {r4, lr}
    mov r4, #0
1:  ldr r0, =patchE
    ldr r1, =0xE2800000             @ add r0, r0, #imm
    orr r1, r1, r4
    str r1, [r0]
    mov r0, #1
    ldr r12, =spanE
    mov lr, pc
    bx r12
    accum 5
    add r4, r4, #5
    cmp r4, #250
    blo 1b
    pop {r4, lr}
    bx lr

@ F: Thumb code patched with 16-bit stores
testF:
    push {r4, lr}
    mov r4, #0
1:  ldr r0, =patchF
    ldr r1, =0x2000                 @ movs r0, #imm
    orr r1, r1, r4
    strh r1, [r0]
    ldr r12, =thumbF + 1
    mov lr, pc
    bx r12
    accum 6
    add r4, r4, #7
    cmp r4, #250
    blo 1b
    pop {r4, lr}
    bx lr

@ G: the same in EWRAM
testG:
    push {r4, lr}
    mov r4, #0
1:  ldr r0, =patchG
    ldr r1, =0xE2800000
    orr r1, r1, r4
    str r1, [r0]
    mov r0, r4
    ldr r12, =loopG
    mov lr, pc
    bx r12
    accum 7
    add r4, r4, #9
    cmp r4, #250
    blo 1b
    pop {r4, lr}
    bx lr

@ H: code that patches a routine and calls it, all from IWRAM (the patcher
@ itself is translated RAM code, like 030009FC)
testH:
    push {r4, lr}
    mov r4, #0
1:  mov r0, r4
    ldr r12, =patcherH
    mov lr, pc
    bx r12
    accum 8
    add r4, r4, #11
    cmp r4, #250
    blo 1b
    pop {r4, lr}
    bx lr

@ I: two instructions patched with one STM (like 030009FC); the first one is
@ the last word of a page, the second the first word of the next page.
@ Only the STM's last word is checked for code.
testI:
    push {r4, lr}
    mov r4, #0
1:  ldr r0, =0xE2800000             @ add r0, r0, #imm
    orr r0, r0, r4
    ldr r1, =0xE2400000             @ sub r0, r0, #imm2
    and r2, r4, #0x0F
    orr r1, r1, r2
    ldr r2, =patchI
    stmia r2, {r0, r1}
    mov r0, #100
    ldr r12, =stmI
    mov lr, pc
    bx r12
    accum 10
    add r4, r4, #13
    cmp r4, #250
    blo 1b
    pop {r4, lr}
    bx lr

@ J: one STM patches two separate routines: the end of one page (jX, which
@ does not reach the next page) and the start of the next (jY). Only the last
@ word is checked, so jX is caught only via the page before.
testJ:
    push {r4, lr}
    mov r4, #0
1:  ldr r0, =0xE2800000             @ add r0, r0, #imm   (jX's patched word)
    orr r0, r0, r4
    ldr r1, =0xE12FFF1E             @ bx lr              (unchanged)
    ldr r2, =0xE2400000             @ sub r0, r0, #imm2  (jY's first word)
    and r3, r4, #0x0F
    orr r2, r2, r3
    ldr r3, =patchJX
    stmia r3, {r0, r1, r2}
    mov r0, #50
    ldr r12, =jX
    mov lr, pc
    bx r12
    accum 11
    mov r0, #200
    ldr r12, =jY
    mov lr, pc
    bx r12
    accum 11
    add r4, r4, #17
    cmp r4, #250
    blo 1b
    pop {r4, lr}
    bx lr

@ K: a buffer that held code is reused for data: copy a routine in by DMA,
@ run it, then write 256 words of data over the same buffer (many games
@ use one IWRAM area for code overlays and work data)
testK:
    push {r4, r5, lr}
    mov r4, #0
1:  ldr r0, =0x040000D4
    ldr r1, =__load_start_ovA
    str r1, [r0]
    ldr r1, =OVL
    str r1, [r0, #4]
    ldr r1, =0x84000000 | 16
    str r1, [r0, #8]
    mov r0, r4
    ldr r12, =OVL
    mov lr, pc
    bx r12
    accum 12
    ldr r1, =OVL                    @ now data over the old code
    mov r2, #256
2:  str r4, [r1], #4
    subs r2, r2, #1
    bne 2b
    ldr r1, =OVL
    ldr r0, [r1, #128]
    accum 12
    add r4, r4, #1
    cmp r4, #40
    blo 1b
    pop {r4, r5, lr}
    bx lr

@ L: no code changes; an IWRAM loop calls a small IWRAM routine in another
@ page many times (cost of calls between RAM blocks)
testL:
    push {r4, lr}
    mov r0, #0
    ldr r12, =callerL
    mov lr, pc
    bx r12
    accum 13
    pop {r4, lr}
    bx lr

    .ltorg

@ ---------------------------------------------------------------- IWRAM
    .section .iwram, "ax"
    .arm
    .align 2
loopA:                              @ r0 = start; 16 x (add #imm; eor)
    mov r2, #16
1:
patchA:
    add r0, r0, #0
    eor r0, r0, r0, lsl #1
    subs r2, r2, #1
    bne 1b
    bx lr

varB:                               @ increments a word inside this block
    ldr r0, varB_val
    cmp r0, #0x100000               @ (always true)
    blo 1f                          @ conditional exit past the data word,
    b 2f                            @ so the scan continues over it
varB_val:
    .word 1000
1:  add r0, r0, #3
    str r0, varB_val
2:  bx lr

    .align 10                       @ next page
callerD:
    push {lr}
    bl calleeD
    add r0, r0, r0, lsl #4
    pop {lr}
    bx lr

patcherH:                           @ patches calleeH with r0, then calls it
    push {r4, lr}
    ldr r1, =0xE2800000             @ add r0, r0, #imm
    and r2, r0, #0xFF
    orr r1, r1, r2
    ldr r2, =patchH
    str r1, [r2]
    mov r0, #7
    bl calleeH
    pop {r4, lr}
    bx lr
    .ltorg

    .align 10
    .space 256                      @ well inside its page (not near the start)
calleeD:
    mov r0, #0
    eor r0, r0, #0x55
    bx lr

calleeH:
patchH:
    add r0, r0, #0
    mov r0, r0, ror #3
    bx lr

    .align 10
    .space 1024 - 12                @ spanE starts 12 bytes before a page boundary
spanE:
    add r0, r0, #2
    add r0, r0, r0
    eor r0, r0, #9
    .rept 40                        @ straight on, 148 bytes into the next page
    eor r0, r0, r0, lsr #1
    .endr
patchE:
    add r0, r0, #0
    mov r0, r0, lsl #2
    bx lr

    .align 10
    .thumb
    .thumb_func
thumbF:
patchF:
    movs r0, #0
    adds r0, r0, #5
    lsls r0, r0, #2
    bx lr
    .arm

    .align 10
    .space 1024 - 64
stmI:
    .rept 15
    add r0, r0, r0, lsr #2
    .endr
patchI:                             @ last word of the page
    add r0, r0, #0
    sub r0, r0, #0                  @ first word of the next page
    eor r0, r0, #0x33
    bx lr

    .align 10
    .space 1024 - 16
jX:                                 @ entirely in this page
    eor r0, r0, r0, lsl #3
    add r0, r0, #1
patchJX:
    add r0, r0, #0                  @ 8 bytes before the page end
    bx lr
jY:                                 @ first word of the next page
    sub r0, r0, #0
    eor r0, r0, #0x77
    bx lr

    .align 10
callerL:                            @ 2000 calls to calleeL
    push {r4, lr}
    ldr r4, =2000
1:  bl calleeL
    subs r4, r4, #1
    bne 1b
    pop {r4, lr}
    bx lr
    .ltorg
    .align 10
    .space 128
calleeL:
    add r0, r0, #3
    eor r0, r0, r0, lsr #5
    bx lr

    .align 10
bigP:                               @ ~4 KB of straight code, called a lot
    mov r0, #0
    .rept 1000
    add r0, r0, #1
    .endr
    bx lr

@ ---------------------------------------------------------------- EWRAM
    .section .ewram, "ax"
    .arm
    .align 2
loopG:
    mov r2, #8
1:
patchG:
    add r0, r0, #0
    eor r0, r0, r0, lsr #3
    subs r2, r2, #1
    bne 1b
    bx lr

@ ---------------------------------------------------------------- overlays
    .section .ovA, "ax"
    .arm
ovA:
    add r0, r0, r0, lsl #1          @ r0 * 3 + 1
    add r0, r0, #1
    bx lr
    .space 64 - 12
    .section .ovB, "ax"
    .arm
ovB:
    eor r0, r0, #0x5A
    add r0, r0, #7
    mov r0, r0, ror #5
    bx lr
    .space 64 - 16
