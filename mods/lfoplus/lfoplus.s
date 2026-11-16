| SPDX-License-Identifier: GPL-2.0-or-later
| LFO+: the pieces that sit inside stock code paths, so they keep the stock register use.

        .section .run, "ax"

| WAVE's on-screen text. The firmware calls the formatter with (?, value 8.8, out) and the stock
| formatter (0x4005fb26) ends by jumping to the text printer 0x40000e82 with (out, "%s", name).
| Values 0..6 (and anything out of range, which prints ERR) go to the stock formatter.
        .globl  lfo_wave_fmt
lfo_wave_fmt:
        move.l  8(%sp), %d0
        asr.l   #8, %d0
        moveq   #7, %d1
        cmp.l   %d1, %d0
        blt.s   1f
        moveq   #28, %d1
        cmp.l   %d1, %d0
        bgt.s   1f
        subq.l  #7, %d0
        lea     lfo_wave_names, %a0
        move.l  (%a0,%d0.l*4), %d0
        move.l  12(%sp), 4(%sp)
        move.l  %d0, 12(%sp)
        move.l  #0x401d0d7e, %d1                | "%s"
        move.l  %d1, 8(%sp)
        jmp     0x40000e82
1:      jmp     0x4005fb26

| MULT's on-screen text: 24..27 and 28..31 print /2 /4 /8 /16; 0..23 go to the stock formatter
| (0x4005fc3e), which ends the same way with (out, "%d" or "%dk", number).
        .globl  lfo_mult_fmt
lfo_mult_fmt:
        move.l  8(%sp), %d0
        asr.l   #8, %d0
        moveq   #24, %d1
        cmp.l   %d1, %d0
        blt.s   1f
        moveq   #31, %d1
        cmp.l   %d1, %d0
        bgt.s   1f
        subi.l  #24, %d0
        moveq   #3, %d1
        and.l   %d1, %d0
        moveq   #2, %d1
        lsl.l   %d0, %d1                        | 2, 4, 8, 16
        move.l  12(%sp), 4(%sp)
        move.l  %d1, 12(%sp)
        lea     slow_text, %a0
        move.l  %a0, 8(%sp)
        jmp     0x40000e82
1:      jmp     0x4005fc3e
slow_text:
        .asciz  "/%d"
        .even

| The audio LFO engine's MULT step. Entered by "jmp" at 0x400ed7b6 (where the stock tests the step
| it just read into d0), with the engine's stack as it was: the tempo-synced rate base is at 72(sp).
| Out: d0 the step n (rate = speed x base >> (11 - n)) and d1 the base, then on at 0x400ed7e0,
| where the stock continues after doing the same for 0..23. d2 is free there (the stock reloads it).
        .globl  lfo_mult_step
lfo_mult_step:
        tst.l   %d0
        bge.s   1f
        moveq   #0, %d0
1:      moveq   #31, %d1
        cmp.l   %d1, %d0
        ble.s   2f
        moveq   #31, %d0
2:      moveq   #23, %d1
        cmp.l   %d1, %d0
        bgt.s   slow
        moveq   #11, %d1
        cmp.l   %d1, %d0
        bgt.s   free
        move.l  72(%sp), %d1                    | 0..11: in time with the tempo
        jmp     0x400ed7e0
free:   subi.l  #12, %d0                        | 12..23: free-running
        move.l  #14400, %d1
        jmp     0x400ed7e0
slow:   moveq   #27, %d1
        cmp.l   %d1, %d0
        bgt.s   slowfree
        neg.l   %d0                             | 24..27 -> -1..-4, in time with the tempo
        addi.l  #23, %d0
        move.l  72(%sp), %d1
        jmp     0x400ed7e0
slowfree:
        neg.l   %d0                             | 28..31 -> -1..-4, free-running
        addi.l  #27, %d0
        move.l  #14400, %d1
        jmp     0x400ed7e0

| The LFO page's graph, 0x4009700a: d5 the wave, held to 0..28 now; d6 = 12 x d5 here, and the stock
| adds the sheet list 0x4208d6b4 (addi.l #0x4208d6b4,d6, 6 bytes), replaced by "jsr" to this. For 7..28
| d6 becomes our sheet instead.
        .globl  lfo_graph_sheet_s
lfo_graph_sheet_s:
        moveq   #7, %d0
        cmp.l   %d0, %d5
        blt.s   1f
        addi.l  #lfo_graph_sheets - 84, %d6
        rts
1:      addi.l  #0x4208d6b4, %d6
        rts

| Also in 0x4009700a: a byte a wave from a copy of the 7-byte table 0x401cfc10 on its stack (1 for EXP
| and RMP: the graph draws one tile fewer), read at -7(fp,d5). For 7..28 that would read past the copy:
| our waves take 0. Replaces "clr.l -(sp) ; move.b -7(fp,d5.l),d1" (6 bytes) by "jsr"; a0 is free here
| (the stock loads it next).
        .globl  lfo_graph_flag_s
lfo_graph_flag_s:
        move.l  (%sp)+, %a0                     | where to go back to
        clr.l   -(%sp)                          | the stock push
        moveq   #7, %d1
        cmp.l   %d1, %d5
        blt.s   1f
        moveq   #0, %d1
        jmp     (%a0)
1:      move.b  -7(%fp,%d5.l), %d1
        jmp     (%a0)
