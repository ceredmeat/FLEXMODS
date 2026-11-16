| SPDX-License-Identifier: GPL-2.0-or-later
| Velocity Humanise: the SETTINGS rows. Each row is four callbacks (label, select, draw, change)
| for core_additem. label() returns a std::string through a0, so it is written here, as
| ledcolors' rows are; the other three are in humanise.c. STR_CTOR comes from mod.json's defsym.

        .section .run, "ax"

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
        .globl  hum_row_\name
hum_row_\name:
        .long   \name\()_label, hum_\name\()_select, hum_\name\()_draw, hum_\name\()_change
        .endm

        LABEL   t1, "HUMAN T1"
        LABEL   t2, "HUMAN T2"
        LABEL   t3, "HUMAN T3"
        LABEL   t4, "HUMAN T4"
        LABEL   t5, "HUMAN T5"
        LABEL   t6, "HUMAN T6"
        LABEL   t7, "HUMAN T7"
        LABEL   t8, "HUMAN T8"
        LABEL   midi, "HUMAN MIDI"
