/*	$NetBSD: screen.c,v 1.34 2021/05/02 12:50:46 rillig Exp $	*/

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
 *	@(#)screen.c	8.1 (Berkeley) 5/31/93
 */

/*
 * Tetris screen control.
 */

#include <sys/cdefs.h>
#include <sys/ioctl.h>
#include <sys/param.h>

#include <assert.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <term.h>
#include <termios.h>
#include <unistd.h>

#ifndef sigmask
#define sigmask(s) (1 << ((s) - 1))
#endif

#include "keymap.h"
#include "randomizer.h"
#include "screen.h"
#include "tetris.h"

static cell curscreen[B_SIZE];	/* 1 => standout (or otherwise marked) */
static int curscore;
static int isset;		/* true => terminal is in game mode */
static struct termios oldtt;
static void (*tstp)(int);
static struct tetris_keymap const* saved_km;

static	void	scr_stop(int);
static	void	stopset(int) __dead;


/*
 * Routine used by tputs().
 */
int
put(int c)
{

	return (putchar(c));
}

/*
 * putstr() is for unpadded strings (either as in termcap(5) or
 * simply literal strings); putpad() is for padded strings with
 * count=1.  (See screen.h for putpad().)
 */
#define	putstr(s)	(void)fputs(s, stdout)

static void
moveto(int r, int c)
{
	char *buf;

	buf = tiparm(cursor_address, r, c);
	if (buf != NULL)
		putpad(buf);
}

static void
setcolor(int c)
{
	char *buf;
	char monochrome[] = "\033[0m";
	if (nocolor == 1)
		return;
	if (set_a_foreground == NULL)
		return;

	if (c == 0 || c == 7)
		buf = monochrome;
	else
		buf = tiparm(set_a_foreground, c);
	if (buf != NULL)
		putpad(buf);
}

/*
 * Return true iff the given row and the column is in the actual playing
 * area.
 */
static bool
is_in_field(int row, int col)
{
	return row >= A_FIRST_ROW && row <= A_LAST_ROW &&
	       col >= A_FIRST_COL && col <= A_LAST_COL;
}

/*
 * Set up from termcap.
 */
void
scr_init(void)
{

	setupterm(NULL, 0, NULL);
	if (clear_screen == NULL)
		stop("cannot clear screen");
	if (cursor_address == NULL || cursor_up == NULL)
		stop("cannot do random cursor positioning");
}

/* this foolery is needed to modify tty state `atomically' */
static jmp_buf scr_onstop;

static void
stopset(int sig)
{
	sigset_t set;

	(void) signal(sig, SIG_DFL);
	(void) kill(getpid(), sig);
	sigemptyset(&set);
	sigaddset(&set, sig);
	(void) sigprocmask(SIG_UNBLOCK, &set, (sigset_t *)0);
	longjmp(scr_onstop, 1);
}

static void
scr_stop(int sig)
{
	sigset_t set;
	struct tetris_keymap const* km = saved_km;

	scr_end();
	(void) kill(getpid(), sig);
	sigemptyset(&set);
	sigaddset(&set, sig);
	(void) sigprocmask(SIG_UNBLOCK, &set, (sigset_t *)0);
	scr_set(km);
	scr_msg(tetris_keymap_help(km), 1);
}

/*
 * Set up screen mode.
 */
void
scr_set(struct tetris_keymap const* km)
{
	struct winsize ws;
	struct termios newtt;
	sigset_t nsigset, osigset;
	void (*ttou)(int);

	/* Save the key map for signal handlers. */
	saved_km = km;

	sigemptyset(&nsigset);
	sigaddset(&nsigset, SIGTSTP);
	sigaddset(&nsigset, SIGTTOU);
	(void) sigprocmask(SIG_BLOCK, &nsigset, &osigset);
	if ((tstp = signal(SIGTSTP, stopset)) == SIG_IGN)
		(void) signal(SIGTSTP, SIG_IGN);
	if ((ttou = signal(SIGTTOU, stopset)) == SIG_IGN)
		(void) signal(SIGTTOU, SIG_IGN);
	/*
	 * At last, we are ready to modify the tty state.  If
	 * we stop while at it, stopset() above will longjmp back
	 * to the setjmp here and we will start over.
	 */
	(void) setjmp(scr_onstop);
	(void) sigprocmask(SIG_SETMASK, &osigset, (sigset_t *)0);
	Rows = 0, Cols = 0;
	if (ioctl(0, TIOCGWINSZ, &ws) == 0) {
		Rows = ws.ws_row;
		Cols = ws.ws_col;
	}
	if (Rows == 0)
		Rows = lines;
	if (Cols == 0)
	    Cols = columns;
	if (Rows < MINROWS || Cols < MINCOLS) {
		(void) fprintf(stderr,
		    "the screen is too small: must be at least %dx%d, ",
		    MINCOLS, MINROWS);
		stop("");	/* stop() supplies \n */
	}
	Offset = (Rows - (D_ROWS + 2)) / 2;
	if (tcgetattr(0, &oldtt) < 0)
		stop("tcgetattr() fails");
	newtt = oldtt;
	newtt.c_lflag &= ~(ICANON|ECHO);
	newtt.c_oflag &= ~OXTABS;
	if (tcsetattr(0, TCSADRAIN, &newtt) < 0)
		stop("tcsetattr() fails");
	ospeed = cfgetospeed(&newtt);
	(void) sigprocmask(SIG_BLOCK, &nsigset, &osigset);

	/*
	 * We made it.  We are now in screen mode, modulo TIstr
	 * (which we will fix immediately).
	 */
	const char *tstr;
	if ((tstr = enter_ca_mode) != NULL)
		putstr(tstr);
	if ((tstr = cursor_invisible) != NULL)
		putstr(tstr);
	if (tstp != SIG_IGN)
		(void) signal(SIGTSTP, scr_stop);
	if (ttou != SIG_IGN)
		(void) signal(SIGTTOU, ttou);

	isset = 1;
	(void) sigprocmask(SIG_SETMASK, &osigset, (sigset_t *)0);
	scr_clear();
}

/*
 * End screen mode.
 */
void
scr_end(void)
{
	sigset_t nsigset, osigset;

	sigemptyset(&nsigset);
	sigaddset(&nsigset, SIGTSTP);
	sigaddset(&nsigset, SIGTTOU);
	(void) sigprocmask(SIG_BLOCK, &nsigset, &osigset);
	/* move cursor to last line */
	const char *tstr;
	if ((tstr = cursor_to_ll) != NULL)
		putstr(tstr);
	else
		moveto(Rows - 1, 0);
	/* exit screen mode */
	if ((tstr = exit_ca_mode) != NULL)
		putstr(tstr);
	if ((tstr = cursor_normal) != NULL)
		putstr(tstr);
	(void) fflush(stdout);
	(void) tcsetattr(0, TCSADRAIN, &oldtt);
	isset = 0;
	/* restore signals */
	(void) signal(SIGTSTP, tstp);
	(void) sigprocmask(SIG_SETMASK, &osigset, (sigset_t *)0);

	saved_km = NULL;
}

void
stop(const char *why)
{

	if (isset)
		scr_end();
	(void) fprintf(stderr, "aborting: %s\n", why);
	exit(1);
}

/*
 * Clear the screen, forgetting the current contents in the process.
 */
void
scr_clear(void)
{

	putpad(clear_screen);
	curscore = -1;
	memset((char *)curscreen, 0, sizeof(curscreen));
}

/*
 * Update the screen.
 */
void
scr_update(struct tetris_rng const *rng)
{
	sigset_t nsigset, osigset;
	static const struct shape *lastshape;

	sigemptyset(&nsigset);
	sigaddset(&nsigset, SIGTSTP);
	(void) sigprocmask(SIG_BLOCK, &nsigset, &osigset);

	/* always leave cursor after last displayed point */
	curscreen[(D_LAST_ROW + 1) * B_COLS - 1] = -1;

	if (score != curscore) {
		if (cursor_home)
			putpad(cursor_home);
		else
			moveto(0, 0);
		setcolor(0);
		(void) printf("Score: %d", score);
		curscore = score;
	}

	/* draw preview of nextpattern */
	struct shape const* nextshape = tetris_rng_peek(rng, 0);
	if (showpreview && (nextshape != lastshape)) {
		static int r=5, c=2;
		int tr, tc, t;

		lastshape = nextshape;

		/* clean */
		putpad(exit_standout_mode);
		moveto(r-1, c-1); putstr("          ");
		moveto(r,   c-1); putstr("          ");
		moveto(r+1, c-1); putstr("          ");
		moveto(r+2, c-1); putstr("          ");

		moveto(r-3, c-2);
		setcolor(0);
		putstr("Next shape:");

		/* draw */
		setcolor(nextshape->color);
		if (enter_standout_mode)
			putpad(enter_standout_mode);
		moveto(r, 2*c);
		putstr(CHARS_BLOCK);
		for(int i=0; i<3; i++) {
			t = c + r*B_COLS;
			t += nextshape->off[i];

			tr = t / B_COLS;
			tc = t % B_COLS;

			moveto(tr, 2*tc);
			putstr(enter_standout_mode ?
			       CHARS_BLOCK_SO : CHARS_BLOCK);
		}
		if (exit_standout_mode)
			putpad(exit_standout_mode);
	}

	cell cur_so = 0; /* Non-zero if we are currently in standout mode */
	for (size_t row = D_FIRST_ROW; row <= D_LAST_ROW; row++) {
		cell* bp = &board[row * B_COLS + D_FIRST_COL];
		cell* sp = &curscreen[row * B_COLS + D_FIRST_COL];
		int ccol = -1;
		for (size_t col = D_FIRST_COL; col <= D_LAST_COL; bp++, sp++, col++) {
			cell so;
			/* Skip the cell if it's not been changed since the
			 * last time we drew it. */
			if (*sp == (so = *bp))
				continue;
			*sp = so;
			if ((int)col != ccol) {
				/* This isn't a adjacent to the last cell
				 * we drew. */
				if (cur_so && move_standout_mode) {
					putpad(exit_standout_mode);
					cur_so = 0;
				}
				moveto(RTOD(row + Offset), CTOD(col));
			}
			if (enter_standout_mode) {
				if (so != cur_so) {
					setcolor(so);
					putpad(so ?
					    enter_standout_mode :
					    exit_standout_mode);
					cur_so = so;
				}
#ifdef DEBUG
				char buf[3];
				snprintf(buf, sizeof(buf), "%d%d", so, so);
				putstr(buf);
#else
				if (so)
					putstr(is_in_field(row, col) ?
					       CHARS_BLOCK_SO : CHARS_BOUNDARY);
				else
					putstr(CHARS_EMPTY);
#endif
			}
			else
				if (so)
					putstr(is_in_field(row, col) ?
					       CHARS_BLOCK : CHARS_BOUNDARY);
				else
					putstr(CHARS_EMPTY);
			ccol = col + 1;
			/*
			 * Look ahead a bit, to avoid extra motion if
			 * we will be redrawing the cell after the next.
			 * Motion probably takes four or more characters,
			 * so we save even if we rewrite two cells
			 * `unnecessarily'.  Skip it all, though, if
			 * the next cell is a different color.
			 */
#define	STOP (B_COLS - 3)
			if (col > STOP || sp[1] != bp[1] || so != bp[1])
				continue;
			if (sp[2] != bp[2])
				sp[1] = -1;
			else if (col < STOP && so == bp[2] && sp[3] != bp[3]) {
				sp[2] = -1;
				sp[1] = -1;
			}
		}
	}
	if (cur_so)
		putpad(exit_standout_mode);
	(void) fflush(stdout);
	(void) sigprocmask(SIG_SETMASK, &osigset, (sigset_t *)0);
}

static void
scr_flush_msg(int row, char const* str, int set)
{
	if (set || clr_eol == NULL) {
		ssize_t len = strlen(str);
		int const col = MAX(((Cols - len) >> 1) - 1, 0);

		moveto(row, col);
		if (set)
			putstr(str);
		else
			while (--len >= 0)
				putchar(' ');
	}
	else {
		moveto(row, 0);
		putpad(clr_eol);
	}
}

/*
 * Write a message (set!=0), or clear the same message (set==0).
 * (We need its length in case we have to overwrite with blanks.)
 *
 * TAB characters are treated as soft-linebreak. It is rendered as a
 * sequence of two SPCs as long as the next line delimited by a TAB fits in
 * the same line. Otherwise it behaves as a linebreak.
 */
void
scr_msg(char const *msg, int set)
{
	static char const* soft_br = "  ";
	size_t row = Rows - 2;
	size_t line_len = 0;
	bool last_segment_ended_softly = false;
	char line_buf[Cols + 1];
	while (*msg != '\0' && row < Rows) {
		/* Search for the next TAB, LF, or NUL to find out where to
		 * break the current line. */
		size_t const segment_len = strcspn(msg, "\t\n");
		char const sep = msg[segment_len];

		if (last_segment_ended_softly) {
			/* Does it fit? */
			if (line_len + strlen(soft_br) + segment_len <= Cols) {
				strcat(&line_buf[line_len], soft_br);
				line_len += strlen(soft_br);

				strncat(&line_buf[line_len], msg, segment_len);
				line_len += segment_len;
			}
			else {
				/* No. Flush the buffer and break the
				 * line here. */
				scr_flush_msg(row, line_buf, set);
				row++;

				size_t const n = MIN(segment_len, Cols);
				strncpy(line_buf, msg, n);
				line_buf[n] = '\0';
				line_len = n;
			}
		}
		else {
			assert(line_len == 0);
			size_t const n = MIN(segment_len, Cols);
			strncpy(line_buf, msg, n);
			line_buf[n] = '\0';
			line_len = n;
		}

		if (sep == '\t') {
			/* Don't flush the buffer yet. Try to fit the next
			 * segment in this line. */
			last_segment_ended_softly = true;
		}
		else {
			last_segment_ended_softly = false;
			scr_flush_msg(row, line_buf, set);
			line_len = 0;
			row++;
		}

		msg += segment_len + (sep == '\0' ? 0 : 1);
	}

	if (line_len > 0 && row < Rows)
		scr_flush_msg(row, line_buf, set);
}
