| SPDX-License-Identifier: MIT
| From DEJA VU 0.3 (Digitakt mk1, OS 1.54): three code sites of the LFO engine. In LFO+ 0.6 (Cere)
| the waves are 29..35, after LFO+'s, and LFO+'s own formatter prints their names.
| The audio tracks' LFO tick is 0x400ed766 (8 tracks x 2 LFOs, per voice
| state at a2 = 0x421f4e5c + 0x50*track - 0x28*lfo, parameters at a3:
| SPD -6, MULT -4, FADE -2, DEST 0, WAVE +2, SPH +4, MODE +6, DEP +8).
| WAVE 29 (DVF), 30 (DVT), 31 (LOG), 32 (HEN), 33 (LOR), 34 (AEN) and 35 (ENV) are
| new; every other value takes its stock path with its stock registers.
| The mode hook also gives each voice the trig bit its TRG source says (follow.c).

        .equ    WAVE_CLAMP, 0x400ed7ea  | moveq #6,d7 ; cmp.l d3,d7 ; bge ; moveq #6,d3
        .equ    WAVE_DONE,  0x400ed7f2  | muls.l d1,d5 (past the clamp)
        .equ    MODE_DONE,  0x400ed818  | movea.l -36(a2),a0
        .equ    DISP_TABLE, 0x400ed952  | WAVE 0-5: the generator table
        .equ    DISP_RAND,  0x400ed97a  | WAVE 6: RND
        .equ    DISP_JOIN,  0x400ed9e6  | move.l d0,-24(a2): a generator's result
        .equ    FMT_ON,     0x4005fb2c  | the WAVE formatter past link/moveq
        .equ    SPRINTF,    0x40000e82
        .equ    FMT_S,      0x401d0d7e  | "%s"
        .equ    NEW_WAVES,  7           | 29..35 in LFO+ 0.6
        .equ    FIRST_WAVE, 29

        .section .run, "ax"

| 0x400ed7e0 (was: mvs.b 2(a3),d3 ; bge.s 0x400ed7ea): WAVE. The new waves
| run the stock bookkeeping as TRI (0); the generator replaces its output.
        .globl  dv_wave_s
dv_wave_s:
        mvs.b   2(%a3), %d3
        move.l  %d3, %d7
        subi.l  #FIRST_WAVE, %d7
        cmpi.l  #NEW_WAVES-1, %d7
        bhi.s   3f
        moveq   #0, %d3
        jmp     WAVE_DONE
3:      tst.l   %d3
        bge.s   2f
        clr.l   %d3
        jmp     WAVE_DONE
2:      jmp     WAVE_CLAMP

| 0x400ed812 (was: mvs.b 6(a3),d1 ; move.l d5,d2): MODE. On the new waves the
| knob is read by the generator, so the engine runs as FRE, or TRG for DVT,
| and a TRG reset starts the phase at 0 (SPH means something else there).
        .globl  dv_mode_s
dv_mode_s:
        mvs.b   6(%a3), %d1
        move.l  %d5, %d2
        tst.b   dv3_any_trg             | an LFO takes another track's trigs
        beq.s   5f
        lea     -16(%sp), %sp           | d0 and d1 are live here
        movem.l %d0-%d1/%a0-%a1, (%sp)
        move.l  16+0x34(%sp), -(%sp)    | the track's trig bit
        move.l  %a2, -(%sp)
        jsr     dv3_trig
        addq.l  #8, %sp
        move.l  %d0, 16+0x34(%sp)       | the one this voice sees
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
5:
        mvs.b   2(%a3), %d7             | d0 is live here: d7 only
        subi.l  #FIRST_WAVE, %d7
        cmpi.l  #NEW_WAVES-1, %d7
        bhi.s   1f
        cmpi.l  #1, %d7                 | DVT: TRG
        beq.s   2f
        moveq   #0, %d7                 | the rest: FRE
2:      move.l  %d7, %d1
        clr.l   0x2c(%sp)
1:      jmp     MODE_DONE

| 0x400ed94c (was: moveq #6,d7 ; cmp.l d3,d7 ; beq.s RND): the waveform.
        .globl  dv_disp_s
dv_disp_s:
        mvs.b   2(%a3), %d7
        subi.l  #FIRST_WAVE, %d7
        cmpi.l  #NEW_WAVES-1, %d7
        bhi.s   9f
        lea     -56(%sp), %sp
        movem.l %d1-%d7/%a0-%a6, (%sp)
        move.l  %a3, -(%sp)
        move.l  60+0x34(%sp), -(%sp)    | the track's trig this tick
        move.l  %d5, -(%sp)
        move.l  %d2, -(%sp)
        move.l  %a2, -(%sp)
        jsr     dv_gen
        lea     20(%sp), %sp
        movem.l (%sp), %d1-%d7/%a0-%a6
        lea     56(%sp), %sp
        jmp     DISP_JOIN
9:      moveq   #6, %d7
        cmp.l   %d3, %d7
        beq.s   1f
        jmp     DISP_TABLE
1:      jmp     DISP_RAND

