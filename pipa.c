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

#include <ctype.h>
#include <limits.h>
#include <err.h>
#include <locale.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "tui.h"

typedef char pathbuf_t[PATH_MAX];

struct linebuffer {
	pathbuf_t *data;
	size_t len;
	size_t cap;
};

struct matches {
	const char **data;
	size_t count;
};

struct string {
	char data[128];
	size_t len;
};

static void __dead run(void);
static int  addpath(const char *);
static void mkfilter(const struct linebuffer *, const char *, struct matches *, int);
static void processkey(const int, int *, int *);
static int  rmpath(const char *);
static int  clearhist(void);
static int  printhist(void);
static int  exists(void);
static int  get_histpath(char *, size_t);
static int  touch(const char *);
static int  loadlines(const char *, struct linebuffer *);
static int  dedupcheck(const char *);
static int  openhist(FILE **, const char *);
static void __dead usage(void);

/* globals */
static struct linebuffer lb;
static struct matches m;
static struct string input = {0};

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
	if (apath != NULL)
		if (!addpath(apath))
			err(1, NULL);
	if (rpath != NULL)
		if (!rmpath(rpath))
			err(1, NULL);
	if (cflag)
		if (!clearhist())
			err(1, NULL);
	if (lflag)
		if (!printhist())
			err(1, NULL);
	if (eflag)
		if(!exists())
			err(1, NULL);

	return (0);
}

static void __dead
run(void)
{
	pathbuf_t hist;
	int ch;
	int idx = 0, filter = 1;

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
	m.data = malloc(LINES * sizeof(char *));

	while (1) {
		if (filter)
			mkfilter(&lb, input.data, &m, LINES - 3);

		erase();
		tui_draw(m.data, m.count, lb.len, idx, input.data);
		refresh();

		ch = getch();
		processkey(ch, &idx, &filter);
	}
}

static void
mkfilter(const struct linebuffer *lb, const char *str, struct matches *m, int maxrows)
{
	m->count = 0;
	for (size_t i = 0; i < lb->len && m->count < maxrows; i++)
		if (strstr(lb->data[i], str))
			m->data[m->count++] = lb->data[i];
}

static void
processkey(const int ch, int *idx, int *filter)
{
	*filter = 0;

	switch (ch) {
		case '\n':
		case KEY_ENTER:
			tui_cleanup();
			if (m.count > 0)
				printf("%s\n", m.data[*idx]);
			free(lb.data);
			free(m.data);
			exit(0);
		case 127:
		case KEY_BACKSPACE:
			if (input.len > 0) {
				input.data[--input.len] = '\0';
				*idx = 0;
				*filter = 1;
			}
			break;
		case KEY_UP:
			if (*idx < (int)m.count - 1)
				(*idx)++;
			break;
		case KEY_DOWN:
			if (*idx > 0)
				(*idx)--;
			break;
		default:
			if (isprint(ch))
				if (input.len < sizeof(input.data) - 1) {
					input.data[input.len++] = ch;
					input.data[input.len] = '\0';
					*idx = 0;
					*filter = 1;
				}
	}
}

static int
addpath(const char *path)
{
	FILE *fp;
	pathbuf_t resolved;

	if (!openhist(&fp, "a"))
		return (0);

	if (realpath(path, resolved) == NULL)
		return (0);

	if (dedupcheck(resolved))
		return (1);

	if (fprintf(fp, "%s\n", resolved) < 0) {
		fclose(fp);
		return 0;
	}

	fclose(fp);
	return (1);
}

static int
rmpath(const char *path)
{
	FILE *fp, *tmp;
	pathbuf_t hist;
	pathbuf_t resolved;
	pathbuf_t tmpfile;
	pathbuf_t buf;

	if (!get_histpath(hist, sizeof(hist)))
		return (0);

	if (realpath(path, resolved) == NULL)
		return (0);

	if (snprintf(tmpfile, sizeof(tmpfile), "%s.tmp", hist) < 0)
		return (0);

	fp = fopen(hist, "r");
	if (fp == NULL)
		return (0);

	tmp = fopen(tmpfile, "w");
	if (tmp == NULL) {
		fclose(fp);
		return (0);
	}

	while ((fgets(buf, sizeof(buf), fp)) != NULL) {
		buf[strcspn(buf, "\n")] = '\0';
		if (strcmp(buf, resolved) != 0)
			fprintf(tmp, "%s\n", buf);
	}

	fclose(fp);
	fclose(tmp);

	if (rename(tmpfile, hist) != 0)
		return (0);

	return (1);
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
exists(void)
{
	FILE *fp, *tmp;
	pathbuf_t tmpfile;
	pathbuf_t hist;
	pathbuf_t path;

	if (!get_histpath(hist, sizeof(hist)))
		return (0);

	if (snprintf(tmpfile, sizeof(tmpfile), "%s.tmp", hist) < 0)
		return (0);

	fp = fopen(hist, "r");
	if (fp == NULL)
		return (0);

	tmp = fopen(tmpfile, "w");
	if (tmp == NULL) {
		fclose(fp);
		return (0);
	}

	while (fgets(path, sizeof(path), fp) != NULL) {
		path[strcspn(path, "\n")] = '\0';
		if (access(path, F_OK) == 0)
			fprintf(tmp, "%s\n", path);
	}
	fclose(fp);
	fclose(tmp);

	if (rename(tmpfile, hist) != 0)
		return (0);

	return (1);
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
		lb->data[lb->len][strcspn(lb->data[lb->len], "\n")] = '\0';
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
		buf[strcspn(buf, "\n")] = '\0';

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

static void __dead
usage(void)
{
	(void)fprintf(stderr, "usage: %s [-arcle] path ...\n", getprogname());
	exit(1);
}
