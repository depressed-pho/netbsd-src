/*	$NetBSD: tetris.c,v 1.34 2023/07/01 10:51:35 nia Exp $	*/

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek and Darren F. Provine.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)tetris.c	8.1 (Berkeley) 5/31/93
 */

#include <sys/cdefs.h>
#ifndef lint
__COPYRIGHT("@(#) Copyright (c) 1992, 1993\
 The Regents of the University of California.  All rights reserved.");
#endif /* not lint */

/*
 * Tetris (or however it is spelled).
 */

#include <sys/time.h>

#include <err.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "input.h"
#include "keymap.h"
#include "randomizer.h"
#include "scores.h"
#include "screen.h"
#include "tetris.h"

cell	board[B_SIZE];		/* 0 => empty, otherwise occupied with an
				 * ANSI color code (see tetris.h) */

size_t	Rows, Cols;		/* current screen size */
size_t	Offset;			/* used to center board & shapes */

int	score;			/* the obvious thing */
gid_t	gid, egid;

int	showpreview;
int	nocolor;

static void elide(struct tetris_rng const *rng, long fallrate);
static void setup_board(void);
static void onintr(int) __dead;
static void usage(void) __dead;

/*
 * Set up the initial board.  The bottom display row is completely set,
 * along with another (hidden) row underneath that.  Also, the left and
 * right edges are set.
 */
static void
setup_board(void)
{
	memset(board, 0, B_SIZE);
	for (size_t row = 0; row < B_ROWS; row++)
		for (size_t col = 0; col < B_COLS; col++)
			if (row > A_LAST_ROW  ||
			    col < A_FIRST_COL ||
			    col > A_LAST_COL)
				board[row * B_COLS + col] = 7; /* white */
}

static bool
is_row_full(size_t row)
{
	for (size_t col = A_FIRST_COL; col <= A_LAST_COL; col++) {
		if (board[row * B_COLS + col] == 0) {
			return false;
		}
	}
	return true;
}

static bool
is_row_empty(size_t row)
{
	for (size_t col = A_FIRST_COL; col <= A_LAST_COL; col++) {
		if (board[row * B_COLS + col] != 0) {
			return false;
		}
	}
	return true;
}

/*
 * Elide any full active rows.
 */
static void
elide(struct tetris_rng const *rng, long fallrate)
{
	/* The first step: clear all rows that are full. */
	bool cleared_any = false;
	for (size_t row = 0; row <= A_LAST_ROW; row++) {
		if (is_row_full(row)) {
			memset(&board[row * B_COLS + A_FIRST_COL], 0, A_COLS);
			cleared_any = true;
		}
	}

	if (cleared_any) {
		/* The second step: move rows down to fill gaps. */
		scr_update(rng);
		tsleep(fallrate);
		for (size_t row = 1; row <= A_LAST_ROW; row++) {
			if (is_row_empty(row)) {
				memmove(&board[B_COLS], &board[0], row * B_COLS);
				memset(&board[A_FIRST_COL], 0, A_COLS);
			}
		}
		scr_update(rng);
		tsleep(fallrate);
	}
}

/*
 * Attempt to rotate a shape either clockwise or counterclockwise. Modify
 * curshape and pos if it succeeds. Also take wall kicks and floor kicks in
 * account.
 */
static void
try_rotate(struct shape const* *cur_shape, int *pos, bool *floor_kickable, bool cw)
{
	struct shape const* const new_shape
	    = &shapes[cw ? (*cur_shape)->rot_cw : (*cur_shape)->rot_ccw];
	int const* const off = cw ? (*cur_shape)->off_cw : (*cur_shape)->off_ccw;
	int const trans = off[0] + off[1] * B_COLS;

	if (fits_in(new_shape, *pos + trans)) {
		*cur_shape = new_shape;
		*pos += trans;
		return;
	}
	for (size_t i = 1; i <= (*cur_shape)->max_kick; i++) {
		/* The basic rotation failed. Try a rightward wall kick. */
		if (fits_in(new_shape, *pos + trans + i)) {
			*cur_shape = new_shape;
			*pos += trans + i;
			return;
		}
		/* It too failed. Try a leftward wall kick. */
		if (fits_in(new_shape, *pos + trans - i)) {
			*cur_shape = new_shape;
			*pos += trans - i;
			return;
		}
		/* It too failed. Try a floor kick if it's
		 * allowed. Performing a floor kick makes the tetromino
		 * unable to kick the floor ever again. This is to prevent
		 * the player from kicking it indefinitely. */
		if (*floor_kickable &&
		    fits_in(new_shape, *pos + trans - i * B_COLS)) {
			*cur_shape = new_shape;
			*pos += trans - i * B_COLS;
			*floor_kickable = false;
			return;
		}
	}
}

int
main(int argc, char *argv[])
{
	int level = 2;
	char *nocolor_env;
	int fd;

	gid = getgid();
	egid = getegid();
	setegid(gid);

	fd = open("/dev/null", O_RDONLY);
	if (fd < 3)
		exit(1);
	close(fd);

	char const* keys = "";
	int ch;
	while ((ch = getopt(argc, argv, "bk:l:ps")) != -1)
		switch(ch) {
		case 'b':
			nocolor = 1;
			break;
		case 'k':
			keys = optarg;
			break;
		case 'l':
			level = atoi(optarg);
			if (level < MINLEVEL || level > MAXLEVEL) {
				errx(1, "level must be from %d to %d",
				     MINLEVEL, MAXLEVEL);
			}
			break;
		case 'p':
			showpreview = 1;
			break;
		case 's':
			showscores(0);
			exit(0);
		case '?':
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc)
		usage();

	nocolor_env = getenv("NO_COLOR");

	if (nocolor_env != NULL && nocolor_env[0] != '\0')
		nocolor = 1;

	long fallrate = 1000000 / level; /* in microseconds */
	long timeout = fallrate;
	bool is_falling = true;
	bool floor_kickable = true;

	struct tetris_keymap* const km = tetris_keymap_alloc(keys);

	(void)signal(SIGINT, onintr);
	scr_init();
	setup_board();
	struct tetris_rng* const rng = tetris_rng_alloc(shapes, 7);

	scr_set(km);

	int pos = A_FIRST_ROW*B_COLS + (B_COLS/2)-1;
	struct shape const* curshape = tetris_rng_draw(rng);

	scr_msg(tetris_keymap_help(km), 1);

	for (;;) {
		place(curshape, pos, 1);
		scr_update(rng);
		place(curshape, pos, 0);

		/* Is it touching the floor? */
		if (fits_in(curshape, pos + B_COLS)) {
			/* No. Moving tetrominoes may cause it to float in
			 * the air, even if it was touching the floor
			 * before that. Switch the timer to fall-delay if
			 * that's the case. */
			if (!is_falling) {
				timeout = fallrate;
				is_falling = true;
			}
		}
		else {
			/* Yes. Switch the timer to lock-delay if it wasn't
			 * previously touching the floor. */
			if (is_falling) {
				timeout = tetris_lock_delay(fallrate);
				is_falling = false;
			}
		}

		int c = tgetchar(&timeout);
		if (c < 0) {
			/*
			 * Timeout.  Move down if possible.
			 */
			if (is_falling) {
				pos += B_COLS;
				/* Moving a tetromino down resets the
				 * fall-delay. */
				timeout = fallrate;
				continue;
			}

			/*
			 * Put up the current shape `permanently',
			 * bump score, and elide any full rows.
			 */
			place(curshape, pos, 1);
			score++;
			elide(rng, fallrate);

			/* Make the fall-delay timer go faster. */
			tetris_fallrate_faster(&fallrate);

			/* Tetrominoes are initially allowed to kick the
			 * floor until they do it once. */
			floor_kickable = true;

			/*
			 * Choose a new shape.  If it does not fit,
			 * the game is over.
			 */
			curshape = tetris_rng_draw(rng);
			pos = A_FIRST_ROW*B_COLS + (B_COLS/2)-1;
			if (!fits_in(curshape, pos))
				break;
			continue;
		}

		/*
		 * Handle command keys.
		 */
		tetris_key_action_t const action = tetris_keymap_get(km, c);
		if (action == KA_QUIT) {
			break;
		}
		else if (action == KA_PAUSE) {
			static char msg[] =
			    "paused - press RETURN to continue";

			place(curshape, pos, 1);
			do {
				scr_update(rng);
				scr_msg(tetris_keymap_help(km), 0);
				scr_msg(msg, 1);
				(void) fflush(stdout);
			} while (rwait(NULL) == -1);
			scr_msg(msg, 0);
			scr_msg(tetris_keymap_help(km), 1);
			place(curshape, pos, 0);
		}
		else if (action == KA_MOVE_LEFT) {
			if (fits_in(curshape, pos - 1))
				pos--;
		}
		else if (action == KA_ROTATE_CW) {
			try_rotate(&curshape, &pos, &floor_kickable, true);
		}
		else if (action == KA_ROTATE_CCW) {
			try_rotate(&curshape, &pos, &floor_kickable, false);
		}
		else if (action == KA_MOVE_RIGHT) {
			if (fits_in(curshape, pos + 1))
				pos++;
		}
		else if (action == KA_HARD_DROP) {
			while (fits_in(curshape, pos + B_COLS)) {
				pos += B_COLS;
				score++;
			}
			/* Hard-drop makes the timer down to zero. The
			 * dropped tetromino will immediately be locked. */
			is_falling = false;
			timeout = 0;
		}
		else if (action == KA_SOFT_DROP) {
			if (fits_in(curshape, pos + B_COLS)) {
				pos += B_COLS;
				score++;
			}
		}
		else if (c == '\f') {
			scr_clear();
			scr_msg(tetris_keymap_help(km), 1);
		}
	}

	tetris_rng_free(rng);
	scr_clear();
	scr_end();
	tetris_keymap_free(km);

	(void)printf("Your score:  %d point%s  x  level %d  =  %d\n",
	    score, score == 1 ? "" : "s", level, score * level);
	savescore(level);

	printf("\nHit RETURN to see high scores, ^C to skip.\n");

	int i;
	while ((i = getchar()) != '\n')
		if (i == EOF)
			break;

	showscores(level);

	exit(0);
}

static void
onintr(int signo __unused)
{
	scr_clear();
	scr_end();
	exit(0);
}

static void
usage(void)
{
	(void)fprintf(stderr, "usage: %s [-bps] [-k keys] [-l level]\n",
	    getprogname());
	exit(1);
}
