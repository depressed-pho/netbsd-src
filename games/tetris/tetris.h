/*	$NetBSD: tetris.h,v 1.16 2020/07/21 02:42:05 nia Exp $	*/

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
 *	@(#)tetris.h	8.1 (Berkeley) 5/31/93
 */

#include <sys/types.h>

/*
 * Definitions for Tetris.
 */

/*
 * The display (`board') is composed of 25 rows of 14 columns of characters
 * (numbered 0..24 and 0..13), stored in a single array for convenience.
 * Columns 2 to 11 of rows 3 to 22 are the actual playing area, where
 * shapes appear.  Columns 0..1 and 12..13 are always occupied, as are all
 * columns of rows 23 and 24.  Rows 0..2 and 23..24 exist as boundary areas
 * so that regions `outside' the visible area can be examined without
 * worrying about addressing problems.
 */

	/* the board */
#define	B_COLS	14
#define	B_ROWS	25
#define	B_SIZE	(B_ROWS * B_COLS)

	/* 0: empty
	   1: occupied; red
	   2: occupied; green
	   3: occupied; yellow
	   4: occupied; blue
	   5: occupied; magenta
	   6: occupied; cyan
	   7: occupied; white */
typedef unsigned char cell;
extern cell	board[B_SIZE];

	/* the displayed area */
#define	D_ROWS		21
#define	D_FIRST_ROW	3
#define	D_LAST_ROW	(D_FIRST_ROW + D_ROWS - 1)
#define	D_COLS		12
#define	D_FIRST_COL	1
#define	D_LAST_COL	(D_FIRST_COL + D_COLS - 1)

	/* the active area */
#define	A_ROWS		20
#define	A_FIRST_ROW	3
#define	A_LAST_ROW	(A_FIRST_ROW + A_ROWS - 1)
#define	A_COLS		10
#define	A_FIRST_COL	2
#define	A_LAST_COL	(A_FIRST_COL + A_COLS - 1)

	/* appearance of blocks and empty cells */
#define	CHARS_BLOCK_SO	"[]" /* Used on a terminal with standout mode */
#define	CHARS_BLOCK	"[]" /* Used on a terminal without standout mode */
#define	CHARS_BOUNDARY	"  "
#define	CHARS_EMPTY	"  "

/*
 * Minimum display size.
 */
#define	MINROWS	23
#define	MINCOLS	40

extern size_t	Rows, Cols;	/* current screen size */
extern size_t	Offset;		/* vert. offset to center board */

/*
 * Translations from board coordinates to display coordinates.
 * As with board coordinates, display coordiates are zero origin.
 */
#define	RTOD(x)	((x) - D_FIRST_ROW)
#define	CTOD(x)	((x) * 2 + (((Cols - 2 * D_COLS) / 2) - 1))

/*
 * A `shape' is the fundamental thing that makes up the game.  There
 * are 7 basic shapes, each consisting of four `blots':
 *
 *	X.X	  X.X	X.X.X	  X.X	X.X.X	X.X.X	X.X.X.X
 *	  X.X	X.X	  X	  X.X	X	    X
 *
 *	  0	  1	  2	  3	  4	  5	  6
 *
 * Except for 3 and 6, the center of each shape is one of the blots.
 * This blot is designated (0,0).  The other three blots can then be
 * described as offsets from the center.  Shape 3 is the same under
 * rotation, so its center is effectively irrelevant; it has been chosen
 * so that it `sticks out' downward and rightward.  Except for shape 6,
 * all the blots are contained in a box going from (-1,-1) to (+1,+1);
 * shape 6's center `wobbles' as it rotates, so that while it `sticks out'
 * rightward, its rotation---a vertical line---`sticks out' downward.
 * The containment box has to include the offset (2,0), making the overall
 * containment box range from offset (-1,-1) to (+2,+1).  (This is why
 * there is only one row above, but two rows below, the display area.)
 *
 * The rotations of shapes 2, 4, 5 and 6 are not purely mathematical
 * rotations. They are actually compositions of rotations and translations.
 *
 * The game works by choosing one of these shapes at random and putting
 * its center at the middle of the first display row (row 1, column 5).
 * The shape is moved steadily downward until it collides with something:
 * either  another shape, or the bottom of the board.  When the shape can
 * no longer be moved downwards, it is merged into the current board.
 * At this time, any completely filled rows are elided, and blots above
 * these rows move down to make more room.  A new random shape is again
 * introduced at the top of the board, and the whole process repeats.
 * The game ends when the new shape will not fit at (1,5).
 *
 * While the shapes are falling, the user can rotate them counterclockwise
 * 90 degrees (in addition to moving them left or right), provided that the
 * rotation puts the blots in empty spaces.  The table of shapes is set up
 * so that each shape contains the index of the new shape obtained by
 * rotating the current shape.  Due to symmetry, each shape has exactly
 * 1, 2, or 4 rotations total; the first 7 entries in the table represent
 * the primary shapes, and the remaining 12 represent their various
 * rotated forms.
 */
struct shape {
	int	color;
	/* index of clockwise-rotated version of this shape */
	int	rot_cw;
	/* like rot_cw but is for counterclockwise rotation */
	int	rot_ccw;
	/* (x, y) translation upon rotating clockwise */
	int	off_cw[2];
	/* (x, y) translation upon rotating counterclockwise */
	int	off_ccw[2];
	/* maximum allowed distance of wall and floor kicks */
	size_t	max_kick;
	/* offsets to other blots if center is at (0,0) */
	int	off[3];
};

extern const struct shape shapes[];

/*
 * Shapes fall at a rate faster than once per second.
 *
 * The initial rate is determined by dividing 1 million microseconds
 * by the game `level'.  (This is at most 1 million, or one second.)
 * Each time the fall-rate is used, it is decreased a little bit,
 * depending on its current value, via the `faster' macro below.
 * The value eventually reaches a limit, and things stop going faster,
 * but by then the game is utterly impossible.
 */
extern long	fallrate;	/* less than 1 million; smaller => faster */
#define	faster() (fallrate -= fallrate / 3000)

/*
 * Game level must be between 1 and 9.  This controls the initial fall rate
 * and affects scoring.
 */
#define	MINLEVEL	1
#define	MAXLEVEL	9

/*
 * Scoring is as follows:
 *
 * When the shape comes to rest, and is integrated into the board,
 * we score one point.  If the shape is high up (at a low-numbered row),
 * and the user hits the space bar, the shape plummets all the way down,
 * and we score a point for each row it falls (plus one more as soon as
 * we find that it is at rest and integrate it---until then, it can
 * still be moved or rotated).
 */
extern int	score;		/* the obvious thing */
extern gid_t	gid, egid;

extern int	showpreview;
extern int	nocolor;

int	fits_in(const struct shape *, int);
void	place(const struct shape *, int, int);
void	stop(const char *) __dead;
