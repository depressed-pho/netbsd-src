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

#include <sys/types.h>

/*
 * This is a "bag" randomizer commonly used by various Tetris games in the
 * 21st century. It generates a sequence of all seven tetrominoes (I, J, L,
 * O, S, T, Z) permuted randomly, as if they were drawn from a bag. Then it
 * deals all seven tetrominoes to the piece sequence before generating
 * another bag. This makes sure that for any given shape A, there can be at
 * most 12 tetrominoes between one A and the next A, preventing the RNG
 * from generating an unreasoningly long run lacking one specific
 * tetromino.
 *
 * This randomizer does not treat the opening as a special case. Some
 * randomizers exclude O, S, Z from the first bag of a game, but this
 * implementation does nothing special like that.
 */

struct shape;
struct tetris_rng;

/*
 * Allocate a new randomizer. "shapes_p" is an array of possible shapes to
 * be chosen. "n_shapes" is the length of "shapes_p". The array must
 * outlive the allocated randomizer.
 */
struct tetris_rng*
tetris_rng_alloc(struct shape const* shapes_p, size_t n_shapes);

/*
 * Deallocate a randomizer.
 */
void
tetris_rng_free(struct tetris_rng *rng);

/*
 * Draw the next shape from a randomizer.
 */
struct shape const*
tetris_rng_draw(struct tetris_rng *rng);

/*
 * Peek a future shape in a randomizer. This function does not change the
 * state of RNG. i=0 returns the next shape (to be drawn with
 * tetris_rng_draw()), i=1 returns the next shape but one, and so on.
 */
struct shape const*
tetris_rng_peek(struct tetris_rng const* rng, size_t i);
