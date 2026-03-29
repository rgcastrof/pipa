/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2026, Rogério Girão <rogeriogirao1@proton.me>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <err.h>
#include <string.h>

#include "tui.h"

static void setcolors(void);

static struct tui t;

void
tui_setup(void)
{
	/* use /dev/tty to keep TUI separate from stdin/stdout */
	t.term = fopen("/dev/tty", "r+");
	if (t.term == NULL)
		err(1, NULL);

	t.scr = newterm(NULL, t.term, t.term);

	set_term(t.scr);

	noecho();
	keypad(stdscr, TRUE);
	set_escdelay(100);
	setcolors();
}

static void
setcolors(void)
{
	start_color();
	use_default_colors();
	init_pair(1, COLOR_WHITE, COLOR_RED);
	init_pair(2, COLOR_GREEN, -1);
}

void
tui_draw(const char **matches, const int mcount, const int lcount,
     const int selected, const char *input)
{
	int borderlen = 2, dotslen = 3;
	int avaliable = COLS - borderlen - dotslen;

	for (size_t i = 0; i < mcount && (int)i < LINES - 3; i++) {
		if ((int)i == selected)
			attron(COLOR_PAIR(1) | A_BOLD);

		/* truncate paths string that exceed terminal width */
		if ((int)strlen(matches[i]) + borderlen < COLS)
			mvprintw(LINES - 3 - i, 0, "║ %s", matches[i]);
		else
			mvprintw(LINES - 3 - i, 0, "║ %.*s...", avaliable, matches[i]);

		attroff(COLOR_PAIR(1) | A_BOLD);
	}

	attron(COLOR_PAIR(2));
	mvprintw(LINES - 2, 2, "%d/%d", mcount, lcount);
	attroff(COLOR_PAIR(2));
	attron(A_BOLD);
	mvprintw(LINES - 1, 0, "> %s", input);
	attroff(A_BOLD);
}

void
tui_cleanup(void)
{
	endwin();
	delscreen(t.scr);
	fclose(t.term);
	t.term = NULL;
	t.scr = NULL;
}
