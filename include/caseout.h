/* Cubolt (https://github.com/nbwzx/cubolt)
 * Copyright (C) 2026 Zixing Wang <zixingwang.cn@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* caseout.h -- a batch case's output path: <name>.txt, or <name>_2.txt, else case_NNN.txt. */
#ifndef CASEOUT_H
#define CASEOUT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define OUT_DIR_DEFAULT "output"

/* A name is refused, never rewritten: separators escape the folder, control bytes name no file */
static int case_name_ok(const char *name)
{
    size_t n = strlen(name);
    while (n > 0 && (name[n - 1] == '.' || name[n - 1] == ' ')) n--;
    /* Windows drops trailing dots and spaces, so "a." would write over another case's "a" */
    if (!n || name[n]) return 0;
    for (const char *s = name; *s; s++)
        if ((unsigned char)*s < 32 || strchr("\\/:*?\"<>|", *s)) return 0;
    return 1;
}

#define CASE_NUM_FMT "case_%03d"

/* On top of the two strlen()s: the separator, ".txt", the NUL and the widest "_%d" -- 17 of 32. */
#define CASE_OUT_SLACK 32

static const char *case_default_name(char *dst, size_t cap, int idx)
{
    snprintf(dst, cap, CASE_NUM_FMT, idx);
    return dst;
}

/* Attributes, not an open: a directory or a read-only file is not taken, so the write fails. */
static int case_name_taken(const char *path)
{
    DWORD a = GetFileAttributes(path);
    if (a == INVALID_FILE_ATTRIBUTES) return 0;
    return !(a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY));
}

static void case_out_path(char *path, size_t cap, const char *dir, int idx, const char *name)
{
    char numbered[24];
    const char *base = name;
    if (!base || !case_name_ok(base)) base = case_default_name(numbered, sizeof numbered, idx);
    size_t dn = strlen(dir);
    const char *sep = dn && (dir[dn - 1] == '/' || dir[dn - 1] == '\\') ? "" : "/";
    int k = 0;
    for (;;) {
        int n = k ? snprintf(path, cap, "%s%s%s_%d.txt", dir, sep, base, k + 1)
                  : snprintf(path, cap, "%s%s%s.txt", dir, sep, base);
        if (n >= (int)cap) {
            fprintf(stderr, "%s does not fit a %d-byte output path: the directory and the "
                            "case name together are %d bytes\n", base, (int)cap, n);
            path[0] = 0;
            return;
        }
        if (!case_name_taken(path)) break;
        k++;
    }
    if (k) fprintf(stderr, "%s already had a file in %s: written to %s\n", base, dir, path);
}

/* malloc'd: the caller owns and frees it. NULL on allocation failure. */
static char *case_out_alloc(const char *dir, int idx, const char *name) __attribute__((unused));
static char *case_out_alloc(const char *dir, int idx, const char *name)
{
    const char *n = name ? name : "";
    size_t cap = strlen(dir) + strlen(n) + CASE_OUT_SLACK;
    char *path = malloc(cap);
    if (path) case_out_path(path, cap, dir, idx, name);
    return path;
}

#endif /* CASEOUT_H */
