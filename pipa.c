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

#include <limits.h>
#include <err.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef char pathbuf_t[PATH_MAX];

struct linebuffer {
	pathbuf_t *data;
	size_t len;
	size_t cap;
};

struct matches {
	char **data;
	size_t count;
};

static int addpath(const char *);
static void mkfilter(const struct linebuffer *, const char *, struct matches *, int);
static int rmpath(const char *);
static int get_histpath(char *, size_t);
static int loadlines(const char *, struct linebuffer *);
static int dedupcheck(const char *, const char *);
static void __dead usage(void);

int
main(int argc, char *argv[])
{
	char *apath = NULL, *rpath = NULL;
	int aflag = 0, rflag = 0;
	int ch;

	while ((ch = getopt(argc, argv, "a:r:lc")) != -1)
		switch (ch) {
		case 'a':
			apath = optarg;
			aflag = 1;
			break;
		case 'r':
			rpath = optarg;
			rflag = 1;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (!aflag && !rflag)
		usage();

	if (apath != NULL)
		if (!addpath(apath))
			err(1, NULL);

	if (rpath != NULL)
		if (!rmpath(rpath))
			err(1, NULL);

	return (0);
}

static void
mkfilter(const struct linebuffer *lb, const char *str, struct matches *m, int maxrows)
{
	m->count = 0;
	for (size_t i = 0; i < lb->len && m->count < maxrows; i++)
		if (strstr(lb->data[i], str))
			m->data[m->count++] = lb->data[i];
}

static int
addpath(const char *path)
{
	FILE *fp;
	pathbuf_t resolved;
	pathbuf_t hist;

	if (!get_histpath(hist, sizeof(hist)))
		return (0);

	if (realpath(path, resolved) == NULL)
		return (0);

	if (dedupcheck(hist, resolved))
		return (1);

	fp = fopen(hist, "a");
	if (fp == NULL)
		return (0);

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
dedupcheck(const char *hist, const char *target)
{
	FILE *fp;
	pathbuf_t buf;

	fp = fopen(hist, "r");
	if (fp == NULL)
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

static void __dead
usage(void)
{
	(void)fprintf(stderr, "usage: %s [-arlc] path ...\n", getprogname());
	exit(1);
}
