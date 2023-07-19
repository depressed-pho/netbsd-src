/* $NetBSD$ */

/*-
 * Copyright (c) 2023 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#pragma once

struct tetris_keymap;

typedef enum {
	/* The first item must always be zero. */
	KA_UNASSIGNED = 0,
	KA_MOVE_LEFT,
	KA_MOVE_RIGHT,
	KA_ROTATE_CW,	/* clockwise */
	KA_ROTATE_CCW,	/* counterclockwise */
	KA_SOFT_DROP,
	KA_HARD_DROP,
	KA_PAUSE,
	KA_QUIT
} tetris_key_action_t;

/*
 * Allocate a new keymap. "keys" is a sequence of letters for move left,
 * rotate counterclockwise, move right, hard drop, pause, quit, soft drop,
 * and rotate clockwise, in this order. The sequence need not specify all
 * the actions. If it is shorter than expected, default keys are assigned
 * to remaining ones.
 */
struct tetris_keymap*
tetris_keymap_alloc(char const* keys);

/*
 * Deallocate a key map.
 */
void
tetris_keymap_free(struct tetris_keymap* km);

/*
 * Obtain the help message to be shown on the screen.
 */
char const*
tetris_keymap_help(struct tetris_keymap const* km);

/*
 * Find a key action assigned to the given key, or KA_UNASSIGNED if none is
 * found.
 */
tetris_key_action_t
tetris_keymap_get(struct tetris_keymap const* km, int key);
