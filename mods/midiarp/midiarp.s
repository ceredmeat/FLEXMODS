| SPDX-License-Identifier: GPL-2.0-or-later
| MIDI Arp: the SETTINGS rows. Each row is four callbacks (label, select, draw, change)
| for core_additem. label() returns a std::string through a0, so it is written here, as
| ledcolors' rows are; the other three are in midiarp.c. STR_CTOR comes from mod.json's defsym.

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
        .globl  ma_row_\name
ma_row_\name:
        .long   \name\()_label, ma_\name\()_select, ma_\name\()_draw, ma_\name\()_change
        .endm

        LABEL   track, "ARP TRACK"
        LABEL   mode, "ARP MODE"
        LABEL   rate, "ARP RATE"
        LABEL   oct, "ARP OCT"
