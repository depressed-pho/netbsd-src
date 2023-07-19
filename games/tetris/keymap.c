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
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>
#include <sys/rbtree.h>

#include "keymap.h"

struct tetris_keymap_node {
	rb_node_t		node;
	int			key;
	tetris_key_action_t	action;
};

struct tetris_keymap {
	/* A map from int to struct tetris_keymap_action */
	rb_tree_t	keys;

	/* A help message to be shown on the screen. */
	char		msg[255];
};

static int tetris_keymap_compare_nodes(
    void *context, void const *node1, void const *node2)
{
	struct tetris_keymap_node const* const a = node1;
	struct tetris_keymap_node const* const b = node2;

	return a->key - b->key;
}

static int tetris_keymap_compare_key(
    void *context, void const *node, void const *key)
{
	struct tetris_keymap_node const* const a = node;
	int const* const k = key;

	return a->key - *k;
}

static rb_tree_ops_t const tetris_keymap_rbtree_ops = {
	.rbto_compare_nodes = &tetris_keymap_compare_nodes,
	.rbto_compare_key = &tetris_keymap_compare_key,
	.rbto_node_offset = offsetof(struct tetris_keymap_node, node),
	.rbto_context = NULL
};

static char const* const DEFAULT_KEYS = "jkl pqni";

static tetris_key_action_t const KEYMAP_STRING_ORDER[] = {
	KA_MOVE_LEFT,
	KA_ROTATE_CCW,
	KA_MOVE_RIGHT,
	KA_HARD_DROP,
	KA_PAUSE,
	KA_QUIT,
	KA_SOFT_DROP,
	KA_ROTATE_CW
};

static void
tetris_keymap_append_help(struct tetris_keymap* km, size_t *msg_pos,
			  tetris_key_action_t action, char const* label)
{
	struct tetris_keymap_node* node;
	RB_TREE_FOREACH(node, &km->keys) {
		if (node->action == action) {
			char const key[] = {
				(char)node->key,
				'\0'
			};
			char const* desc;
			switch (node->key) {
			case ' ':	desc = "<space>"; break;
			case '\t':	desc = "<tab>"; break;
			case '\n':	desc = "<return>"; break;
			default:	desc = key;
			}
			*msg_pos += snprintf(
			    &km->msg[*msg_pos],
			    sizeof(km->msg) - *msg_pos,
			    "%s%s - %s",
			    *msg_pos == 0 ? "" : "\t",
			    desc,
			    label);
			break;
		}
	}
}

struct tetris_keymap*
tetris_keymap_alloc(char const* keys)
{
	assert(keys);

	struct tetris_keymap* const km = malloc(sizeof(*km));
	if (!km)
		err(1, "malloc");

	rb_tree_init(&km->keys, &tetris_keymap_rbtree_ops);

	/* Assign actions for each player-specified keys. Duplications are
	 * not allowed and exits the process. */
	size_t const keys_len = strlen(keys);
	assert(strlen(DEFAULT_KEYS) >= __arraycount(KEYMAP_STRING_ORDER));

	for (size_t i = 0; i < __arraycount(KEYMAP_STRING_ORDER); i++) {
		int const key = i < keys_len ? keys[i] : DEFAULT_KEYS[i];
		if (rb_tree_find_node(&km->keys, &key)) {
			if (i < keys_len)
				errx(1, "duplicate action keys specified: %c",
				     (char)key);
			else
				/* Do not complain about duplicates in
				 * default keys */
				continue;
		}

		struct tetris_keymap_node* const node = malloc(sizeof(*node));
		if (!node)
			err(1, "malloc");

		node->key = key;
		node->action = KEYMAP_STRING_ORDER[i];

		rb_tree_insert_node(&km->keys, node);
	}

	/* Build the help message out of the key map. */
	size_t msg_pos = 0;
	tetris_keymap_append_help(km, &msg_pos, KA_MOVE_LEFT, "left");
	tetris_keymap_append_help(km, &msg_pos, KA_ROTATE_CCW, "rotate ccw");
	tetris_keymap_append_help(km, &msg_pos, KA_MOVE_RIGHT, "right");
	tetris_keymap_append_help(km, &msg_pos, KA_HARD_DROP, "drop");
	tetris_keymap_append_help(km, &msg_pos, KA_PAUSE, "pause");
	tetris_keymap_append_help(km, &msg_pos, KA_QUIT, "quit");
	tetris_keymap_append_help(km, &msg_pos, KA_SOFT_DROP, "down");
	tetris_keymap_append_help(km, &msg_pos, KA_ROTATE_CW, "rotate cw");

	return km;
}

void
tetris_keymap_free(struct tetris_keymap* km)
{
	assert(km);

	struct tetris_keymap_node* node;
	struct tetris_keymap_node* tmp;
	RB_TREE_FOREACH_SAFE(node, &km->keys, tmp) {
		free(node);
	}
	free(km);
}

char const*
tetris_keymap_help(struct tetris_keymap const* km)
{
	assert(km);
	return (char const*)&km->msg;
}

tetris_key_action_t
tetris_keymap_get(struct tetris_keymap const* km, int key)
{
	assert(km);

	struct tetris_keymap_node* const node =
	    rb_tree_find_node(__UNCONST(&km->keys), &key);

	return node ? node->action : KA_UNASSIGNED;
}
