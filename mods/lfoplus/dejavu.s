| SPDX-License-Identifier: GPL-2.0-or-later
| Deja Vu: the two pieces that need assembly.
|
| dv_rand: the stock random routine 0x400ed3fc now starts with "jmp dv_rand", so we are entered as
| if the LFO engine had called us directly (its return address on the stack). Both LFO engines keep
| the LFO's own state pointer in a2 when they call it: we pass that to dv_pick, so each LFO has its
| own loop, and whether the call comes from a trig. a2 is left as it was; d0 is the result, as the stock routine gives it, and only d0, d1,
| a0 and a1 change (the stock routine changes d0 and d1, and the callers keep nothing in a0 or a1
| across the call).
|
| The SETTINGS rows: four callbacks each (label, select, draw, change) for core_additem. label()
| returns a std::string through a0, so it is written here, as ledcolors' rows are.
| STR_CTOR comes from mod.json's defsym: std::string(this, const char*, alloc&).

        .section .run, "ax"

| Is this call from a trig? The audio engine (returning to 0x400ed9aa) keeps the trig at its
| 52(sp), which is 56(sp) here under the return address; the MIDI engine (returning to 0x400ed690)
| keeps it in d5. Both have the LFO's parameters at a3, its trig mode at 6(a3): in mode FRE (0) a
| trig doesn't restart anything.
        .globl  dv_rand
dv_rand:
        moveq   #0, %d0
        move.l  (%sp), %d1
        cmpi.l  #0x400ed9aa, %d1
        bne.s   1f
        move.l  56(%sp), %d0
        bra.s   2f
1:      cmpi.l  #0x400ed690, %d1
        bne.s   2f
        move.l  %d5, %d0
2:      tst.b   6(%a3)
        bne.s   3f
        moveq   #0, %d0
3:      move.w  4(%a3), %d1                     | the LFO's start phase, 8.8 (0.4: RND LOCK = PHAS)
        ext.l   %d1
        move.l  %d1, -(%sp)
        move.l  %d0, -(%sp)
        move.l  %a2, -(%sp)
        jsr     dv_pick
        lea     12(%sp), %sp
        rts

        .macro  LABEL name, text
\name\()_label:
        link    %a6, #-4
        move.l  %d2, -(%sp)
        pea     -1(%a6)
        pea     \name\()_str
        move.l  %a0, %d2
        move.l  %a0, -(%sp)
        jsr     STR_CTOR
        lea     12(%sp), %sp
        move.l  %d2, %d0
        move.l  -8(%a6), %d2
        unlk    %a6
        rts
\name\()_str:
        .asciz  "\text"
        .even
        .globl  dv_row_\name
dv_row_\name:
        .long   \name\()_label, dv_\name\()_select, dv_\name\()_draw, dv_\name\()_change
        .endm

        LABEL   loop, "RND LOOP"
        LABEL   lock, "RND LOCK"
        LABEL   trig, "RND TRIG"
