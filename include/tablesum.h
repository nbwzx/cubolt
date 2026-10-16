/* Cubolt (https://github.com/nbwzx/cubolt)
 * Copyright (C) 2026 Zixing Wang <zixingwang.cn@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* tablesum.h -- the integrity guard for the on-disk tables (manifest digests). */
#ifndef TABLESUM_H
#define TABLESUM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "host.h"

#define TABLE_MANIFEST "tables.manifest"
#define TS_ROW_FMT     "%016llx %lld %s\n"
#define TS_CHUNK       (8u << 20)

/* Where a caller that can carry on lands instead of ending the process; one slot per thread. */
static __thread jmp_buf *g_table_jmp;
static void table_bail(void) { if (g_table_jmp) longjmp(*g_table_jmp, 1); exit(1); }

static const struct { const char *name; long long size; } TABLE_FILES[] = {
    {"move_twist",           MOVE_TWIST_BYTES},
    {"move_flip",            MOVE_FLIP_BYTES},
    {"move_slice_sorted",    MOVE_SLICE_SORTED_BYTES},
    {"move_corners",         MOVE_CORNERS_BYTES},
    {"conj_twist",           CONJ_TWIST_BYTES},
    {"ct_clssym",            CT_CLSYM_BYTES},
    {"fs24_clssym",          FS24_CLSYM_BYTES},
    {"fs24_rep",             FS24_REP_BYTES},
    {"phase1x24_prun",       PHASE1X24_BYTES},
    {"ct_prun",              CT_PRUN_BYTES},
    {"phase1x24_prun_ftm",   PHASE1X24_BYTES},
    {"ct_prun_ftm",          CT_PRUN_BYTES},
    {NULL, 0}
};

#define TS_NFILES ((int)(sizeof TABLE_FILES / sizeof TABLE_FILES[0]) - 1)

/* Cached per table, failures included; cleared on a rebuild, or a table found bad stays bad. */
static signed char ts_state[TS_NFILES];
static signed char ts_last[TS_NFILES];

/* TS_CORRUPT is the one case a length check cannot see: the right size, not the blessed bytes. */
enum { TS_OK = 0, TS_MISSING = 1, TS_SIZE = 2, TS_SHORT = 3, TS_CORRUPT = 4 };

static const char *ts_strerror(int st)
{
    switch (st) {
    case TS_MISSING: return "missing";
    case TS_SIZE:    return "wrong size";
    case TS_SHORT:   return "short read";
    case TS_CORRUPT: return "checksum mismatch";
    }
    return "ok";
}

#define TS_MIX 0x9e3779b97f4a7c15ULL
/* Eight bytes at a step off a golden-ratio multiplier. */
static unsigned long long ts_digest(unsigned long long h, const unsigned char *p, size_t n)
{
    while (n >= 8) {
        unsigned long long v;
        memcpy(&v, p, 8);
        h = (h ^ v) * TS_MIX;
        h ^= h >> 29;
        p += 8;
        n -= 8;
    }
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * TS_MIX;
    return h;
}

#define TS_BASIS 14695981039346656037ULL

/* Without it the last bytes folded in barely reach the low bits. */
static unsigned long long ts_avalanche(unsigned long long h)
{
    h ^= h >> 30; h *= 0xbf58476d1ce4e5b9ULL;
    h ^= h >> 27; h *= 0x94d049bb133111ebULL;
    h ^= h >> 31;
    return h;
}

static int ts_file_sum(const char *path, long long want, unsigned long long *dig)
{
    *dig = 0;
    FILE *fh = fopen(path, "rb");
    if (!fh) return TS_MISSING;
    if (fseek(fh, 0, SEEK_END) != 0) { fclose(fh); return TS_SHORT; }
    long long n = (long long)ftell(fh);
    if (n != want) { fclose(fh); return TS_SIZE; }
    rewind(fh);
    unsigned char *buf = malloc(TS_CHUNK);
    if (!buf) { fclose(fh); fprintf(stderr, OOM_TEXT); table_bail(); }
    unsigned long long h = TS_BASIS;
    for (long long left = n; left > 0; ) {
        size_t w = left < (long long)TS_CHUNK ? (size_t)left : TS_CHUNK;
        if (fread(buf, 1, w, fh) != w) { free(buf); fclose(fh); return TS_SHORT; }
        h = ts_digest(h, buf, w);
        left -= (long long)w;
    }
    free(buf);
    fclose(fh);
    *dig = ts_avalanche(h);
    return TS_OK;
}

/* Named bare, beside the exe; one buffer, so a pointer is stale once anything else is named. */
static const char *table_path(const char *name)
{
    static char buf[MAX_PATH + 32];    /* the directory is MAX_PATH, the name comes after it */
    snprintf(buf, sizeof buf, "%s%s", exe_dir(), name);
    return buf;
}

/* Re-read per lookup, never cached: the GUI rebuilds tables mid-process. */
static int ts_manifest_get(const char *name, unsigned long long *dig)
{
    FILE *m = fopen(table_path(TABLE_MANIFEST), "rb");
    if (!m) return 0;
    char line[256];
    int found = 0;
    while (fgets(line, sizeof line, m)) {
        if (line[0] == '#') continue;
        unsigned long long d, sz;
        char nm[128];
        /* Every field is read, size included; a line that does not parse records nothing. */
        if (sscanf(line, "%llx %llu %127s", &d, &sz, nm) != 3) continue;
        if (!strcmp(nm, name)) { *dig = d; found = 1; }
    }
    fclose(m);
    return found;
}

static int ts_index(const char *name);

/* One verdict is kept per table, and it was given about the bytes that were there before it. */
static void ts_forget(const char *name) __attribute__((unused));
static void ts_forget(const char *name)
{
    int i = ts_index(name);
    if (i >= 0) ts_state[i] = 0;
}

/* Absent files are left out: a partial set gets a partial manifest. */
static int ts_manifest_write(void) __attribute__((unused));
static int ts_manifest_write(void)
{
    memset(ts_state, 0, sizeof ts_state);
    FILE *m = fopen(table_path(TABLE_MANIFEST), "wb");
    if (!m) { fprintf(stderr, CANNOT_WRITE, table_path(TABLE_MANIFEST)); return 1; }
    fprintf(m, "# tables.manifest -- 64-bit digest of the table files next to the exe.\n");
    fprintf(m, "# <digest16> <bytes> <name>\n");
    fprintf(m, "# Regenerate: mktables.exe --manifest\n");
    int bad = 0;
    for (int i = 0; i < TS_NFILES; i++) {
        unsigned long long d;
        int st = ts_file_sum(table_path(TABLE_FILES[i].name), TABLE_FILES[i].size, &d);
        if (st) {
            printf("  %-22s %s -- not in the manifest\n", TABLE_FILES[i].name, ts_strerror(st));
            bad = 1;
            continue;
        }
        fprintf(m, TS_ROW_FMT, d, TABLE_FILES[i].size, TABLE_FILES[i].name);
        printf("  %-22s %016llx\n", TABLE_FILES[i].name, d);
    }
    /* Short or not, the rows that landed are the record; the rest are checked by length. */
    if (stream_lost(m, 0)) fprintf(stderr, "%s: written short\n", TABLE_MANIFEST);
    if (bad) printf("  %s written (incomplete set)\n", TABLE_MANIFEST);
    else     printf("  %s written\n", TABLE_MANIFEST);
    return 0;
}

static int ts_index(const char *name)
{
    for (int i = 0; i < TS_NFILES; i++)
        if (!strcmp(TABLE_FILES[i].name, name)) return i;
    return -1;
}

/* -1 for a name the list does not carry. */
static long long ts_size(const char *name) __attribute__((unused));
static long long ts_size(const char *name)
{
    int i = ts_index(name);
    return i < 0 ? -1 : TABLE_FILES[i].size;
}

static int ts_size_ok(const char *name, long long want) __attribute__((unused));
static int ts_size_ok(const char *name, long long want)
{
    FILE *fh = fopen(table_path(name), "rb");
    if (!fh) return 0;
    int ok = 0;
    if (fseek(fh, 0, SEEK_END) == 0) ok = ((long long)ftell(fh) == want);
    fclose(fh);
    return ok;
}

/* A table the manifest has no entry for is judged on its length alone. */
static int ts_verify(const char *name, long long want) __attribute__((unused));
static int ts_verify(const char *name, long long want)
{
    int i = ts_index(name);
    if (i >= 0 && ts_state[i]) return ts_last[i];

    unsigned long long ref = 0, got = 0;
    int have_ref = ts_manifest_get(name, &ref);
    const char *full = table_path(name);
    int st = ts_file_sum(full, want, &got);
    if (st == TS_OK && have_ref && got != ref) st = TS_CORRUPT;
    if (i >= 0) { ts_state[i] = 1; ts_last[i] = (signed char)st; }
    return st;
}

#endif /* TABLESUM_H */
