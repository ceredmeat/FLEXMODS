| SPDX-License-Identifier: GPL-2.0-or-later
| PROJECT LINK: tells other mods when the Digitakt starts a new project, loads one, or saves one.
| Three of the firmware's first instructions are swapped for a jump here; each stub does the instruction
| it replaced and returns, after noting what happened. The registers it uses are the ones that
| instruction was about to set anyway.
	.text
	.globl pl_new_s, pl_load_s, pl_save_s
| Project::initializeNewProject (0x4001a5e6), at 0x4001a5f0: movel #0x40014898,%d2
pl_new_s:
	moveq	#-1,%d2
	move.l	%d2,proj_slot
	addq.l	#1,proj_gen
	addq.l	#1,proj_news
	move.l	#0x40014898,%d2
	rts
| Project::loadProjectFromMmc(this, slot, done) (0x4001a8fc), at 0x4001a906: movel #0x400149fa,%d3
pl_load_s:
	move.l	12(%fp),%d3
	move.l	%d3,proj_slot
	addq.l	#1,proj_gen
	addq.l	#1,proj_loads
	move.l	#0x400149fa,%d3
	rts
| ProjectManager::interactiveSave(slot, ...) (0x4004f1e6), at 0x4004f1f2: movel %fp@(8),%d2 (4 bytes:
| the next two make six, so this also does the movel %fp,%d4 after it)
pl_save_s:
	move.l	8(%fp),%d2
	move.l	%d2,proj_slot
	addq.l	#1,proj_saves
	move.l	%fp,%d4
	rts
	.data
	.globl proj_slot, proj_gen, proj_loads, proj_news, proj_saves
	.align 4
proj_magic:	.long 0x504c4e4b		| "PLNK", to find it in a memory dump
proj_slot:	.long -2			| the project's slot (0-127), -1 a new unsaved one, -2 not known yet
proj_gen:	.long 0				| up by one at every new project or load
proj_loads:	.long 0
proj_news:	.long 0
proj_saves:	.long 0
