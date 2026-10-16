/* Cubolt (https://github.com/nbwzx/cubolt)
 * Copyright (C) 2026 Zixing Wang <zixingwang.cn@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* host.h -- what the host gives: a directory, wall-clock seconds, the cores, a flag's value. */
#ifndef HOST_H
#define HOST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "version.h"

#define CANNOT_OPEN  "cannot open %s\n"
#define CANNOT_WRITE "cannot write %s\n"

#define VERSION_HELP_TEXT "  --version / -v  print the version and exit\n" \
                          "  --help / -h  print this text and exit\n"
static void print_version(void) __attribute__((unused));
static void print_version(void) { printf("%s %s\n", CUBOLT_NAME, CUBOLT_VERSION); }

/* Every pool here waits for all its workers at once, and one wait holds MAXIMUM_WAIT_OBJECTS. */
#define THREAD_CAP 64

#define PARSE_INT_DIGITS 9

/* atoi answers 0 for what it cannot read, and 0 is a real setting: refuse unreadable input */
static int parse_int(const char *s, int *out)
{
    size_t n = strlen(s);
    if (!n || n > PARSE_INT_DIGITS || strspn(s, "0123456789") != n) return 1;
    *out = atoi(s);
    return 0;
}

/* No newline: the callers' format strings end in one. */
static const char *bad_number(const char *flag, const char *val)
{
    static __thread char buf[128];
    snprintf(buf, sizeof buf, "%s is not a number of %d digits or fewer: %s", flag,
             PARSE_INT_DIGITS, val);
    return buf;
}

static const char *bad_range(const char *flag, int v, int lo, int hi)
{
    static __thread char buf[128];
    snprintf(buf, sizeof buf, "%s is %d, outside %d to %d", flag, v, lo, hi);
    return buf;
}

/* --threads, wherever it appears: 0 keeps the auto count.  *i ends on the value. */
static int threads_flag(char **v, int n, int *i, int *out, const char **err)
    __attribute__((unused));
static int threads_flag(char **v, int n, int *i, int *out, const char **err)
{
    if (*i + 1 >= n) { *err = "--threads needs a count"; return 1; }
    const char *val = v[++(*i)];
    if (parse_int(val, out)) { *err = bad_number("--threads", val); return 1; }
    if (*out > THREAD_CAP)   { *err = bad_range("--threads", *out, 0, THREAD_CAP); return 1; }
    return 0;
}

/* 1 when the directory is there afterwards. */
static int make_one_dir(const char *path) __attribute__((unused));
static int make_one_dir(const char *path)
{
    /* An empty name fails in CreateDirectory too, and "cannot create" does not say whose */
    if (!*path) { fprintf(stderr, "the directory name is empty\n"); return 0; }
    if (CreateDirectoryA(path, NULL)) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        /* That error covers a plain file of the same name as well, so the two are told apart */
        DWORD at = GetFileAttributesA(path);
        if (at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY)) return 1;
        fprintf(stderr, "%s exists and is not a directory\n", path);
        return 0;
    }
    fprintf(stderr, "cannot create %s\n", path);
    return 0;
}

/* One level per CreateDirectory, so each is made in turn; 1 when the directory is there. */
static int make_outdir(const char *dir) __attribute__((unused));
static int make_outdir(const char *dir)
{
    char buf[MAX_PATH];
    int n = snprintf(buf, sizeof buf, "%s", dir);
    if (n < 0 || n >= (int)sizeof buf) {
        fprintf(stderr, "output path is longer than %d characters: %s\n", (int)sizeof buf - 1, dir);
        return 0;
    }
    /* "C:" and "/" root it, not a level: CreateDirectory refuses those, and \\server\share too. */
    int unc = buf[0] == '\\' && buf[1] == '\\';
    int seen = 0;
    for (char *p = buf; *p; p++) {
        if (*p != '/' && *p != '\\') continue;
        char sep = *p;
        *p = 0;
        seen++;
        size_t len = strlen(buf);
        int rooted = len == 0 || buf[len - 1] == ':' || buf[len - 1] == '/' || buf[len - 1] == '\\'
                     || (unc && seen < 5);
        if (!rooted && !make_one_dir(buf)) return 0;
        *p = sep;
    }
    return make_one_dir(buf);
}

/* The exe's directory, where a table opens by bare name; empty reads as the cwd. */
static const char *exe_dir(void)
{
    static char dir[MAX_PATH];
    static int done;
    if (!done) {
        done = 1;
        /* Truncation still reports success, and the cut can land before the last backslash. */
        DWORD got = GetModuleFileNameA(NULL, dir, MAX_PATH);
        if (got == 0 || got >= MAX_PATH) {
            char cwd[MAX_PATH];
            DWORD n = GetCurrentDirectoryA(sizeof cwd, cwd);
            if (n == 0 || n >= sizeof cwd) snprintf(cwd, sizeof cwd, "?");
            fprintf(stderr, "cannot say where the exe is: the tables are opened from %s\n", cwd);
            dir[0] = 0;
        }
        char *slash = strrchr(dir, '\\');
        if (slash) slash[1] = 0;
    }
    return dir;
}

/* Not clock(): on Windows that reads wall time too, at the CRT's millisecond resolution. */
static double now_sec(void)
{
    /* Published with an interlocked store: a thread that sees the flag must see the frequency. */
    static volatile LARGE_INTEGER freq;
    static volatile LONG have_freq;
    LARGE_INTEGER c;
    if (!have_freq) {
        LARGE_INTEGER f;
        if (!QueryPerformanceFrequency(&f) || f.QuadPart <= 0) f.QuadPart = 1000;
        freq = f;
        InterlockedExchange(&have_freq, 1);
    }
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)freq.QuadPart;
}

static int cpu_count(void)
{
    static int n;
    if (n) return n;
    /* GetSystemInfo would name the current group only, and leave the other cores out. */
    n = (int)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (n < 1) n = 1;
    if (n > THREAD_CAP) n = THREAD_CAP;
    return n;
}

/* ferror() cannot see what fclose's flush lost; prior carries a failure seen before this. */
static int stream_lost(FILE *f, int prior)
{
    if (ferror(f)) prior = 1;
    return fclose(f) ? 1 : prior;
}

#endif /* HOST_H */
