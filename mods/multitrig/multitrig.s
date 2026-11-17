| SPDX-License-Identifier: GPL-2.0-or-later
| MULTITRIG: the SETTINGS rows' labels (a std::string through a0), as the other mods' rows.

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
        .globl  mt_row_\name
mt_row_\name:
        .long   \name\()_label, mt_\name\()_select, mt_\name\()_draw, mt_\name\()_change
        .endm

        LABEL   mode, "MULTI EDIT"
