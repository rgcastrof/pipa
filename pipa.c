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

#include<sys/errno.h>
#include<sys/stat.h>

#include <ctype.h>
#include <limits.h>
#include <err.h>
#include <locale.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "tui.h"

typedef char pathbuf_t[PATH_MAX];

enum KeyAction {
	ACTION_NONE,
	ACTION_QUIT,
	ACTION_MOVE_UP,
	ACTION_MOVE_DOWN,
	ACTION_INSERT_CHAR,
	ACTION_REMOVE_CHAR,
	ACTION_SELECT_QUIT
};

struct linebuffer {
	pathbuf_t *data;
	size_t len;
	size_t cap;
};

struct match {
	const char *text;
	double distance;
};

struct string {
	char data[128];
	size_t len;
};

static void           __dead run(void);
static void           addpath(const char *);
static void           filterhist(int (*)(const char *, const char *), void *);
static void           mkfilter(const char *, const struct linebuffer*, struct match *, int *);
static int            fuzzy(const char *, const char*, double *);
static int            compmatches(const void *, const void *);
static enum KeyAction processkey(const int);
static int            clearhist(void);
static int            printhist(void);
static int            get_histpath(char *, size_t);
static int            touch(const char *);
static int            isdir(const char *);
static int            loadlines(const char *, struct linebuffer *);
static int            dedupcheck(const char *);
static int            openhist(FILE **, const char *);
static void           chomp(char *);
static int            rmpath(const char *, const char *);
static int            exists(const char *, const char *);
static void           __dead usage(void);

int
main(int argc, char *argv[])
{
	char *apath = NULL, *rpath = NULL;
	int aflag = 0, rflag = 0, cflag = 0, lflag = 0, eflag = 0;
	int ch;

	while ((ch = getopt(argc, argv, "a:r:cle")) != -1)
		switch (ch) {
		case 'a':
			apath = optarg;
			aflag = 1;
			break;
		case 'r':
			rpath = optarg;
			rflag = 1;
			break;
		case 'c':
			cflag = 1;
			break;
		case 'l':
			lflag = 1;
			break;
		case 'e':
			eflag = 1;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (!aflag && !rflag && !cflag && !lflag && !eflag)
		run();

	if (aflag)
		addpath(apath);

	if (rflag)
		filterhist(rmpath, rpath);

	if (cflag && !clearhist())
		err(1, NULL);

	if (lflag && !printhist())
		err(1, NULL);

	if (eflag)
		filterhist(exists, NULL);

	return (0);
}

static void __dead
run(void)
{
	struct linebuffer lb;
	struct match *matches;
	struct string input = {0};
	enum KeyAction action;
	pathbuf_t hist;
	const char *list[LINES];
	const char *output = NULL;
	int ch, running = 1;
	int selected = 0, filter = 1;
	int matches_count = 0;

	if (!get_histpath(hist, sizeof(hist)))
		errx(1, "get_histpath");

	if (access(hist, F_OK) == -1)
		if (!touch(hist))
			errx(1, "touch");

	if (!loadlines(hist, &lb))
		errx(1, "loadlines");

	setlocale(LC_ALL, "");
	tui_setup();

	/* allocates only what fits in the terminal */
	matches = malloc(LINES * sizeof(struct match));
	if (matches == NULL) {
		tui_cleanup();
		free(lb.data);
		err(1, NULL);
	}

	while (running) {
		if (filter)
			mkfilter(input.data, &lb, matches, &matches_count);

		for (int i = 0; i < LINES - 3; i++)
			list[i] = matches[i].text;

		erase();
		tui_draw(list, matches_count, (int)lb.len, selected, input.data);
		refresh();

		ch = getch();
		action = processkey(ch);

		switch (action) {
			case ACTION_MOVE_UP:
				if (selected < matches_count - 1)
					selected++;
				break;
			case ACTION_MOVE_DOWN:
				if (selected > 0)
					selected--;
				break;
			case ACTION_INSERT_CHAR:
				if (input.len < sizeof(input.data) - 1) {
					input.data[input.len++] = ch;
					input.data[input.len] = '\0';
					selected = 0;
					filter = 1;
				}
				break;
			case ACTION_REMOVE_CHAR:
				if (input.len > 0) {
					input.data[--input.len] = '\0';
					selected = 0;
					filter = 1;
				}
				break;
			case ACTION_QUIT:
				running = 0;
				break;
			case ACTION_SELECT_QUIT:
				if (matches_count > 0)
					output = matches[selected].text;
				running = 0;
				break;
			case ACTION_NONE:
				break;
		}
	}

	tui_cleanup();
	if (output != NULL)
		printf("%s\n", output);
	free(lb.data);
	free(matches);
	exit(0);

}

static void
mkfilter(const char *search, const struct linebuffer *lb, struct match *matches, int *matches_count)
{
	double distance;
	*matches_count = 0;

	for (size_t i = 0; i < lb->len; i++) {
		if (fuzzy(search, lb->data[i], &distance) && *matches_count < LINES) {
			matches[*matches_count].text = lb->data[i];
			matches[*matches_count].distance = distance;
			(*matches_count)++;
		}
	}
	/* sort matches */
	qsort(matches, *matches_count, sizeof(struct match), compmatches);
}

static int
fuzzy(const char *search, const char *text, double *distance)
{
	int pidx = 0; /* search pointer */
	int sidx = -1, eidx = -1;
	int search_len = strlen(search);

	/* input is empty */
	if (search_len == 0) {
		*distance = 0;
		return 1;
	}

	for (size_t i = 0; text[i] != '\0'; i++) {
		if (text[i] == search[pidx]) {
			if (pidx == 0)
				sidx = i;
			pidx++;
		}
		if (pidx == search_len) {
			eidx = i;
			/* calculate distance */
			*distance = log(sidx + 2) + (double)(eidx - sidx - search_len);
			return 1;
		}
	}
	/* match not found */
	return (0);
}

static int
compmatches(const void *a, const void *b)
{
	const struct match *ma = a;
	const struct match *mb = b;

	return ma->distance == mb->distance ? 0 : ma->distance < mb->distance ? -1 : 1;
}

static enum KeyAction
processkey(const int ch)
{
	switch (ch) {
		case 27:			return ACTION_QUIT;
		case '\n':
		case KEY_ENTER: 	return ACTION_SELECT_QUIT;
		case 127:
		case KEY_BACKSPACE: return ACTION_REMOVE_CHAR;
		case KEY_UP: 		return ACTION_MOVE_UP;
		case KEY_DOWN: 		return ACTION_MOVE_DOWN;
		default:
			if (isprint(ch))
				return ACTION_INSERT_CHAR;
				return ACTION_NONE;
	}
}

static void
addpath(const char *path)
{
	pathbuf_t resolved;
	FILE *fp;

	if (!realpath(path, resolved))
		err(1, NULL);

	if (isdir(resolved) != 0)
		errx(1, "%s: %s", path, strerror(ENOTDIR));

	if (!openhist(&fp, "a"))
		errx(1, "openhist");

	if (dedupcheck(resolved))
		exit(0);

	if (fprintf(fp, "%s\n", resolved) < 0) {
		fclose(fp);
		err(1, NULL);
	}

	fclose(fp);
	exit(0);
}

/*
 * rewrite the history file, keeping only the
 * lines for which keep callback returns true
 */
static void
filterhist(int (*keep)(const char *, const char *), void *arg)
{
	FILE *fp, *tmp;
	pathbuf_t hist, tmpfile, buf, resolved;
	const char *path = arg;

	if (path != NULL) {
		if (!realpath(path, resolved))
			err(1, NULL);

		if (isdir(resolved) != 0)
			errx(1, "%s: %s", path, strerror(ENOTDIR));
	}

	if (!openhist(&fp, "r"))
		errx(1, "openhist");

	if (snprintf(tmpfile, sizeof(tmpfile), "%s.tmp", hist) < 0)
		err(1, NULL);

	tmp = fopen(tmpfile, "w");
	if (tmp == NULL) {
		fclose(fp);
		err(1, NULL);
	}

	while ((fgets(buf, sizeof(buf), fp)) != NULL) {
		chomp(buf);
		if (keep(buf, resolved))
			fprintf(tmp, "%s\n", buf);
	}

	fclose(fp);
	fclose(tmp);

	if (rename(tmpfile, hist) != 0)
		return;
}

static int
clearhist(void)
{
	FILE *fp;

	if (!openhist(&fp, "w"))
		return (0);
	fclose(fp);
	return (1);
}

static int
printhist(void)
{
	FILE *fp;
	pathbuf_t path;

	if (!openhist(&fp, "r"))
		return (0);

	while (fgets(path, sizeof(path), fp) != NULL)
		printf("%s", path);
	fclose(fp);
	return (1);
}

static int
rmpath(const char *path, const char *target)
{
	return strcmp(path, target) != 0;
}

static int
exists(const char *path, const char *target)
{
	(void)target;
	return access(path, F_OK) == 0;
}

static int
touch(const char *path)
{
	FILE *fp;
	fp = fopen(path, "a");
	if (fp == NULL)
		return (0);
	fclose(fp);
	return 1;
}

static int
isdir(const char *path)
{
	struct stat st;
	if (stat(path, &st) == -1)
		err(1, "%s", path);

	return S_ISREG(st.st_mode);
}

static int
loadlines(const char *filepath, struct linebuffer *lb)
{
	FILE *fp;
	pathbuf_t *tmp;
	lb->data = NULL;
	lb->cap = 0, lb->len = 0;

	fp = fopen(filepath, "r");
	if (fp == NULL)
		return (0);

	for (;;) {
		if (lb->len == lb->cap) {
			lb->cap = lb->cap ? lb->cap * 2 : 16;
			tmp = reallocarray(lb->data, lb->cap, sizeof(*tmp));
			if (tmp == NULL) {
				free(lb->data);
				lb->data = NULL;
				fclose(fp);
				return (0);
			}
			lb->data = tmp;
		}

		if (fgets(lb->data[lb->len], PATH_MAX, fp) == NULL)
			break;

		/* remove '\n' read by fgets */
		chomp(lb->data[lb->len]);
		lb->len++;
	}
	fclose(fp);
	return (1);
}

static int
get_histpath(char *buf, size_t bufsize)
{
	char *home;

	home = getenv("HOME");
	if (home == NULL)
		return (0);

	if (snprintf(buf, bufsize, "%s/.pipa_history", home) < 0)
		return (0);

	return (1);
}

static int
dedupcheck(const char *target)
{
	FILE *fp;
	pathbuf_t buf;

	if (!openhist(&fp, "r"))
		return (0);

	while ((fgets(buf, sizeof(buf), fp)) != NULL) {
		chomp(buf);

		if (strcmp(buf, target) == 0) {
			fclose(fp);
			return (1);
		}
	}
	fclose(fp);
	return (0);
}

static int
openhist(FILE **fp, const char *mode)
{
	pathbuf_t hist;

	if (!get_histpath(hist, sizeof(hist)))
		return (0);

	*fp = fopen(hist, mode);
	if (*fp == NULL)
		return (0);;
	return (1);
}

static void
chomp(char *s)
{
	s[strcspn(s, "\n")] = '\0';
}

static void __dead
usage(void)
{
	(void)fprintf(stderr, "usage: %s [-lce] [-a path] [-r path]\n", getprogname());
	exit(1);
}
