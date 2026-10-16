/* Cubolt (https://github.com/nbwzx/cubolt)
 * Copyright (C) 2026 Zixing Wang <zixingwang.cn@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* batchcase.h -- one case of a batch file, and the file both formats are read out of. */
#ifndef BATCHCASE_H
#define BATCHCASE_H

#include "jsonlite.h"
#include "version.h"

/* The CLI hands `scr` to run_job; the GUI resolves it into `cs` first. */
typedef struct {
    char        cs[55];
    const char *orig;
    const char *scr;       /* non-NULL when orig is a maneuver */
} BatchCase;

#define CASE_SKIP_FMT "case entry %d skipped: %s\n"
#define CASE_SKIP_OBJ "not a JSON object"

/* 1 when the case is refused and reported: a skipped case takes no number of its own. */
static int case_skipped(int i, const char *why, int *n_skip, FILE *out)
{
    if (!why) return 0;
    (*n_skip)++;
    fprintf(out, CASE_SKIP_FMT, i + 1, why);
    return 1;
}

/* A case file is read whole, and its own size is the only thing known before it is. */
#define CASE_MAX_MB   32
#define CASE_MAX_TEXT CUBOLT_STR_(CASE_MAX_MB) " MB"
#define CASE_MAX_BYTES ((long long)CASE_MAX_MB * 1024 * 1024)

/* NULL with *why set; nothing is printed here: the CLI goes to stderr, the GUI to its log. */
static char *batch_read_text(const char *path, const char **why)
{
    static __thread char msg[512]; /* one buffer the refusals below write once each */
    FILE *fh = fopen(path, "rb");
    if (!fh) { snprintf(msg, sizeof msg, "cannot open %s", path); *why = msg; return NULL; }
    long sz = -1;
    if (fseek(fh, 0, SEEK_END) == 0) sz = ftell(fh);
    if (sz < 0 || sz > CASE_MAX_BYTES) {
        if (sz < 0) snprintf(msg, sizeof msg, "cannot tell how large %s is", path);
        else        snprintf(msg, sizeof msg, "%s is over the " CASE_MAX_TEXT
                             " case-file limit", path);
        fclose(fh);
        *why = msg;
        return NULL;
    }
    /* three past the last byte: batch_start looks ahead that far for a byte order mark */
    char *text = malloc((size_t)sz + 3);
    rewind(fh);
    if (!text || fread(text, 1, (size_t)sz, fh) != (size_t)sz) {
        snprintf(msg, sizeof msg, "cannot read %s", path);
        free(text);
        fclose(fh);
        *why = msg;
        return NULL;
    }
    text[sz] = text[sz + 1] = text[sz + 2] = 0;
    fclose(fh);
    *why = NULL;
    return text;
}

/* shared by both readers: which format the text holds, the caller decides */
static const char *batch_start(const char *text)
{
    const char *p = text;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB &&
        (unsigned char)p[2] == 0xBF)
        p += 3;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

static const char *batch_case_cube(const JVal *c, int f[54], BatchCase *out,
                                   const char *dflt_scr, const char *dflt_cs)
{
    static __thread char why[256];
    const char *scr  = j_str(c, "scramble");
    const char *cube = j_str(c, "cubestring");
    /* Neither named: the command line's is kept; scramble wins over cubestring. */
    if ((!scr || !*scr) && (!cube || !*cube)) {
        scr  = dflt_scr;
        cube = dflt_scr ? NULL : dflt_cs;
    }
    /* An empty string is no more of a cube than a missing field: exactly one of the two */
    if ((!scr || !*scr) == (!cube || !*cube))
        return "give exactly one of \"scramble\" or \"cubestring\"";
    if (cube && !*cube) cube = NULL;    /* so the rest reads both spellings of "absent" alike */
    out->orig = cube ? cube : scr;
    out->scr  = cube ? NULL : scr;
    out->cs[0] = 0;
    if (!cube) return NULL;
    /* '?' is none of from_string's colors, so a wildcard cube is the incomplete side's to read */
    const char *bad = "it is not 54 facelets";
    if (strlen(cube) == 54)
        bad = strchr(cube, '?') ? incomp_problem(cube, NULL, NULL, NULL) : from_string(cube, f);
    if (bad) {
        snprintf(why, sizeof why, "not a valid cubestring: %s", cube);
        return why;
    }
    snprintf(out->cs, sizeof out->cs, "%s", cube);
    return NULL;
}

/* Past it a depth is refused, not clamped: settling would answer 999 with the cap itself. */
static const char *job_overlay(Job *j, const JVal *o, int depth_max)
{
    /* Before any key is read: a wrong kind or an unknown key is refused, not absent */
    static const struct { const char *key; JType type; } want[] = {
        { "scramble", J_STR }, { "cubestring", J_STR }, { "name", J_STR },
        { "subgroup", J_STR }, { "goal", J_STR },
        { "depth", J_NUM }, { "threads", J_NUM },
        { "only_optimal", J_BOOL }, { "dump", J_BOOL },
        { "sort", J_BOOL },
    };
    for (int i = 0; i < (int)(sizeof want / sizeof *want); i++) {
        const char *bad = j_want(o, want[i].key, want[i].type);
        if (bad) return bad;
    }
    /* One static buffer for the four refusals: a returned one holds until the next call */
    static __thread char why[160];
    for (int i = 0; i < o->n; i++) {
        int known = 0;
        for (int k = 0; k < (int)(sizeof want / sizeof *want); k++)
            if (!strcmp(o->keys[i], want[k].key)) { known = 1; break; }
        if (!known) {
            snprintf(why, sizeof why, "unknown key \"%s\"", o->keys[i]);
            return why;
        }
        /* Refused, not read past: which of the two was meant is not a guess to make. */
        for (int k = 0; k < i; k++)
            if (!strcmp(o->keys[i], o->keys[k])) {
                snprintf(why, sizeof why, "key \"%s\" appears twice", o->keys[i]);
                return why;
            }
    }
    /* used as it stands, so a name with a separator in it is refused, not escaped */
    const char *nm = j_str(o, "name");
    if (nm && *nm && !case_name_ok(nm)) {
        snprintf(why, sizeof why, "name \"%s\" is not usable as a file name", nm);
        return why;
    }
    const char *s = j_str(o, "subgroup");
    if (s && *s) {
        const char *bad = subgroup_bad_letter(s);
        if (bad) return bad;
        j->subgroup = s;
    }
    if (j_get(o, "depth")) {
        double d = j_num(o, "depth", (double)j->depth);
        if (!json_int_ok(d)) return "depth is not a whole number";
        if (d < 0) return "depth must not be negative";
        if (d > depth_max) {
            snprintf(why, sizeof why, "depth %.0f is past the %d this front end accepts",
                     d, depth_max);
            return why;
        }
        j->depth = (int)d;
    }
    j->opt  = j_bool(o, "only_optimal", j->opt);
    j->dump = j_bool(o, "dump", j->dump);
    rend_do_sort = j_bool(o, "sort", rend_do_sort);
    s = j_str(o, "goal");
    /* read here, as the CLI reads --goal: one that will not parse drops this case */
    if (s && *s) {
        Cube cg;
        int cgp = 0;
        const char *bad = read_cube(s, &cg, &cgp);
        if (bad) return bad;
        j->goalstring = s;
    }
    return NULL;
}

/* NULL leaving *out alone, or after setting a whole count from 0 up; else why it is not one. */
static const char *job_overlay_threads(const JVal *o, int *out)
{
    const JVal *tv = j_get(o, "threads");
    if (!tv || tv->type == J_NULL) return NULL;
    double t = j_num(o, "threads", -1);
    if (!json_int_ok(t) || t < 0) return "threads is not a whole number from 0 up";
    if (t > THREAD_CAP) return bad_range("threads", (int)t, 0, THREAD_CAP);
    *out = (int)t;
    return NULL;
}

#endif /* BATCHCASE_H */
