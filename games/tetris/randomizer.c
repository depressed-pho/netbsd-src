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
#include <assert.h>
#include <err.h>
#include <sys/param.h>
#include <stdlib.h>
#include <string.h>

#include "randomizer.h"
#include "tetris.h"

struct tetris_rng_bag {
	/* An array of shapes in the bag. */
	struct shape const**	shapes;
	/* The number of shapes in the bag. */
	size_t			n_shapes;
};

struct tetris_rng {
	struct tetris_rng_bag	initial_bag;
	struct tetris_rng_bag	current_bag;

	/* A dynamically-allocated sequence of tetrominoes to be drawn in
	 * the future.
	 */
	struct shape const**	future;
	size_t			future_alloc_len;
	size_t			future_data_len;
};

static struct shape const*
tetris_rng_generate_one(struct tetris_rng* rng);

struct tetris_rng*
tetris_rng_alloc(struct shape const* shapes_p, size_t n_shapes)
{
	assert(shapes_p);

	struct tetris_rng* const rng = malloc(sizeof(*rng));
	if (!rng)
		err(1, "malloc");

	rng->initial_bag.shapes = calloc(n_shapes, sizeof(struct shape const*));
	if (!rng->initial_bag.shapes)
		err(1, "calloc");
	for (size_t i = 0; i < n_shapes; i++)
		rng->initial_bag.shapes[i] = &shapes_p[i];
	rng->initial_bag.n_shapes = n_shapes;

	rng->current_bag.shapes = calloc(n_shapes, sizeof(struct shape const*));
	if (!rng->current_bag.shapes)
		err(1, "calloc");
	rng->current_bag.n_shapes = 0;

	rng->future = NULL;
	rng->future_alloc_len = 0;
	rng->future_data_len = 0;

	return rng;
}

void
tetris_rng_free(struct tetris_rng *rng)
{
	free(rng->future);
	free(rng->current_bag.shapes);
	free(rng);
}

struct shape const*
tetris_rng_draw(struct tetris_rng *rng)
{
	struct shape const* const s = tetris_rng_peek(rng, 0);

	rng->future_data_len--;
	memmove(&rng->future[0], &rng->future[1], rng->future_data_len);

	return s;
}

struct shape const*
tetris_rng_peek(struct tetris_rng const* rng, size_t i)
{
	struct tetris_rng* mut_rng = __UNCONST(rng);

	/* Do we have enough space for the pre-generated sequence? */
	if (mut_rng->future_alloc_len <= i) {
		mut_rng->future_alloc_len = roundup2(i + 1, 16);
		mut_rng->future = realloc(
		    mut_rng->future,
		    sizeof(struct shape const*) * mut_rng->future_alloc_len);
		if (!mut_rng->future)
			err(1, "realloc");
	}
	assert(mut_rng->future_alloc_len > i);

	/* Do we have enough pre-generated tetrominoes? */
	for (size_t j = mut_rng->future_data_len; j <= i; j++) {
		mut_rng->future[j] = tetris_rng_generate_one(mut_rng);
		mut_rng->future_data_len++;
	}
	assert(mut_rng->future_data_len > i);

	return mut_rng->future[i];
}

static struct shape const*
tetris_rng_generate_one(struct tetris_rng* rng)
{
	/* If the current bag is empty, generate a new bag. */
	if (rng->current_bag.n_shapes == 0) {
		/* First copy the initial bag to the current one. */
		for (size_t i = 0; i < rng->initial_bag.n_shapes; i++) {
			rng->current_bag.shapes[i] = rng->initial_bag.shapes[i];
		}
		rng->current_bag.n_shapes = rng->initial_bag.n_shapes;

		/* Then shuffle it. */
		assert(rng->current_bag.n_shapes > 0);
		for (ssize_t i = rng->current_bag.n_shapes - 1; i > 0; i--) {
			size_t const j = arc4random_uniform(i + 1);
			struct shape const* const tmp =
			    rng->current_bag.shapes[j];

			rng->current_bag.shapes[j] = rng->current_bag.shapes[i];
			rng->current_bag.shapes[i] = tmp;
		}
	}
	assert(rng->current_bag.n_shapes > 0);

	/* Take the last tetromino out of the bag. */
	struct shape const* const s =
	    rng->current_bag.shapes[rng->current_bag.n_shapes - 1];
	rng->current_bag.n_shapes--;

	return s;
}
