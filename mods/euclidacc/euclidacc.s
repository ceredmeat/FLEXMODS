| SPDX-License-Identifier: GPL-2.0-or-later
| Euclid Accents: the SETTINGS rows. Each row is four callbacks (label, select, draw, change)
| for core_additem. label() returns a std::string through a0, so it is written here, as
| ledcolors' rows are; the other three are in euclidacc.c. STR_CTOR comes from mod.json's defsym.

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
        .globl  acc_row_\name
acc_row_\name:
        .long   \name\()_label, acc_\name\()_select, acc_\name\()_draw, acc_\name\()_change
        .endm

        LABEL   depth, "ACC DEPTH"
        LABEL   t1, "ACC T1"
        LABEL   t2, "ACC T2"
        LABEL   t3, "ACC T3"
        LABEL   t4, "ACC T4"
        LABEL   t5, "ACC T5"
        LABEL   t6, "ACC T6"
        LABEL   t7, "ACC T7"
        LABEL   t8, "ACC T8"
