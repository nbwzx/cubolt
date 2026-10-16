/* Cubolt (https://github.com/nbwzx/cubolt)
 * Copyright (C) 2026 Zixing Wang <zixingwang.cn@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* render.h -- the notation layer: an STM maneuver -> the standard-notation list. */
#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A move code is letter id * 4 + amount (letters "URFDLBEMSxyzurfdlb", amount 1..3; 0 = unused).
 * MD_CODE[frame][letter id * 3 + amount - 1] is what a token becomes in that frame, except in the
 * nine rotation columns: those hold the frame the rotation lands on, not a code of that shape. */
static unsigned char MD_CODE[24][54];

/* Idempotent: MD_CODE is filled once, whichever caller of cube.h's builder comes first. */
static void cube_build_relabel(void);

/* ROT_TAIL[i] -> the 0-terminated trailing tokens of rotation i, with "x2 y2" folded to "z2". */
static const unsigned char ROT_TAIL[24][3] = {
    {0, 0, 0,},
    {41, 0, 0,},
    {42, 0, 0,},
    {43, 0, 0,},
    {37, 0, 0,},
    {37, 41, 0,},
    {37, 42, 0,},
    {37, 43, 0,},
    {38, 0, 0,},
    {38, 41, 0,},
    {46, 0, 0,},
    {38, 43, 0,},
    {39, 0, 0,},
    {39, 41, 0,},
    {39, 42, 0,},
    {39, 43, 0,},
    {45, 0, 0,},
    {45, 41, 0,},
    {45, 42, 0,},
    {45, 43, 0,},
    {47, 0, 0,},
    {47, 41, 0,},
    {47, 42, 0,},
    {47, 43, 0,},
};

/* ---- alphabet ---- */
/* letter ids rank the sort key; the six faces come first, so a face's id is also its number */
enum { R_U = 0, R_R, R_F, R_D, R_L, R_B, R_E, R_M, R_S, R_X, R_Y, R_Z,
       R_WU, R_WR, R_WF, R_WD, R_WL, R_WB };

#define FACE_LET  "URFDLB"
#define SLICE_LET "EMS"
#define ROT_LET   "xyz"
#define WIDE_LET  "urfdlb"

static const char REND_LET[19] = FACE_LET SLICE_LET ROT_LET WIDE_LET;
static const char *const REND_SUF[4] = {"", "", "2", "'"};

static int rend_is_rot(int L)   { return L >= R_X && L < R_WU; }

/* one move code as the cuber writes it, at `p`; returns where the next one goes */
static char *rend_put(char *p, uint8_t c)
{
    *p++ = REND_LET[c >> 2];
    for (const char *s = REND_SUF[c & 3]; *s;) *p++ = *s++;
    return p;
}

/* the FB axis is -1 because S = F' B z whereas E = U D' y' and M = R L' x' */
static const unsigned char REND_AX_SL[3]  = {R_E, R_M, R_S};
static const unsigned char REND_AX_AF[3]  = {R_U, R_R, R_F};
static const unsigned char REND_AX_BF[3]  = {R_D, R_L, R_B};
static const unsigned char REND_AX_ROT[3] = {R_Y, R_X, R_Z};
static const int           REND_AX_SGN[3] = {1, 1, -1};

/* ---- one fold step ---- */
static int rend_resolve(int rot, uint8_t c)
{
    return MD_CODE[rot][(c >> 2) * 3 + (c & 3) - 1];
}

static void rend_fold_step(uint8_t *out, int *n, int *rot, uint8_t c)
{
    if (rend_is_rot(c >> 2)) *rot  = rend_resolve(*rot, c);
    else                     out[(*n)++] = (uint8_t)rend_resolve(*rot, c);
}

#define REND_MAXTOK  64
#define REND_MAXOUT  (3 * REND_MAXTOK + 8)   /* a token emits at most 4 codes, 3 of them counted */
/* the most one maneuver may add: without it a single deep one takes the whole set */
#define REND_MAXREND 10000000

/* one rendering: two chars and a separator a move plus the terminator; the key one byte a move */
#define REND_LEAF_BUF ((REND_MAXOUT + 8) * 3 + 1)
#define REND_LEAF_KEY (REND_MAXOUT + 8)

/* ---- per-maneuver state ---- */
#define REND_MAXCAR  10
/* a thread renders the maneuvers it finds: only the set they land in is shared */
static __thread int  rend_ntok;
static __thread int  rend_ck[REND_MAXTOK];
/* the 48 (axis, k1, k2) option sets */
static uint8_t rend_co[48][REND_MAXCAR][4];
static uint8_t rend_con[48][REND_MAXCAR];
static uint8_t rend_coc[48][REND_MAXCAR];
static int     rend_cn[48];

static __thread uint8_t rend_st_rot[REND_MAXTOK + 2];
static __thread uint8_t rend_st_out[REND_MAXTOK + 2][REND_MAXOUT];
static __thread int     rend_st_n[REND_MAXTOK + 2];

/* ---- output set ---- */
/* koff/klen: the move codes themselves, so memcmp compares the letter and then the amount */
typedef struct { size_t koff; uint8_t klen; uint16_t cm; } RendEnt;

static uint8_t *rend_keyar; static size_t rend_kn, rend_kcap;
static RendEnt *rend_ent;   static size_t rend_on, rend_ocap;
static size_t  *rend_ord;
/* 1 = sort by (counted moves, order key), so the first is the cheapest; 0 = discovery order. */
static int      rend_do_sort = 1;
/* called for every accepted string; the callee gets the caller's buffer and may copy it */
static void (*rend_new_hook)(const char *s) = NULL;
/* Asked when a buffer cannot grow: 0 drops this one, anything else or no hook ends it. */
static int (*rend_oom)(size_t want) = NULL;
/* %zu is the size the refused realloc asked for, in MB */
#define REND_OOM_FMT "out of memory: the notation list wants %zu MB more, so this case stops " \
                     "here with what it has.\n"

/* The set keeps keys only: a rendering's text is spelled out when something asks for it. */
static void rend_spell(char *p, const uint8_t *kb, int kn)
{
    for (int k = 0; k < kn; k++) {
        p = rend_put(p, kb[k]);
        if (k + 1 < kn) *p++ = ' ';
    }
    *p = 0;
}

/* One buffer a thread, held to the next call; an identity key has nothing to point into. */
static const char *rend_text(size_t e) __attribute__((unused));
static const char *rend_text(size_t e)
{
    static __thread char buf[REND_LEAF_BUF];
    int kn = rend_ent[e].klen;
    rend_spell(buf, kn ? rend_keyar + rend_ent[e].koff : NULL, kn);
    return buf;
}

static size_t rend_nth(size_t i) __attribute__((unused));
static size_t rend_nth(size_t i) { return rend_ord ? rend_ord[i] : i; }

static int  rend_maxcount;
static unsigned rend_faces, rend_slices, rend_wides;
static unsigned rend_rots_named;         /* the rotation letters the subgroup spells out itself */
static int  rend_prefix_all;                   /* no counted code can be rejected */
static __thread size_t rend_stop;              /* where this run's own REND_MAXREND ends */
static int  rend_mincm;
static int  rend_said;                         /* one dropped-notations warning per case */

/* the trailing rotation in the subgroup's letters; _ok = it can be spelled: M licenses no x. */
static uint8_t rend_tail[24][4];
static uint8_t rend_tail_ok[24];

/* that reorientation undone: a solution undoes the pieces only and leaves the cube reoriented */
static int rend_boost = 0;

#define OOM_TEXT "out of memory\n"

static void *rend_xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n);
    if (q) return q;
    /* 0 from the hook is a soft no: p is still the caller's, and callers check for NULL */
    if (rend_oom && !rend_oom(n)) return NULL;
    fprintf(stderr, "out of memory (%zu bytes)\n", n);
    exit(2);
}

static void rend_reset(void)
{
    /* the keys go with the run: a deep solve's peak would stay mapped and starve the next */
    free(rend_keyar); rend_keyar = NULL; rend_kcap = rend_kn = 0;
    free(rend_ent);   rend_ent   = NULL; rend_ocap = rend_on = 0;
    free(rend_ord);   rend_ord   = NULL;
    rend_mincm = 0x7fffffff;
    /* One process serves a whole batch: a case of its own gets a warning of its own. */
    rend_said = 0;
}

/* appends the key of a rendering; 1 = it did */
static int rend_store(const uint8_t *kb, int kn, int cm)
{
    if (rend_kn + (size_t)kn > rend_kcap) {
        size_t ncap = rend_kcap ? rend_kcap : (1u << 12);
        while (rend_kn + (size_t)kn > ncap) ncap *= 2;
        void *q = rend_xrealloc(rend_keyar, ncap);
        if (!q) return 0;
        rend_keyar = q; rend_kcap = ncap;
    }
    if (rend_on == rend_ocap) {
        size_t ncap = rend_ocap ? rend_ocap * 2 : 256;
        void *q = rend_xrealloc(rend_ent, ncap * sizeof(*rend_ent));
        if (!q) return 0;
        rend_ent = q; rend_ocap = ncap;
    }
    /* guarded: rend_keyar + rend_kn would be NULL + 0 before the first key */
    if (kn) memcpy(rend_keyar + rend_kn, kb, (size_t)kn);
    rend_ent[rend_on].koff = rend_kn;
    rend_ent[rend_on].klen = kn;
    rend_ent[rend_on].cm   = cm;
    rend_kn += (size_t)kn;
    rend_on++;
    if (cm < rend_mincm) rend_mincm = cm;
    return 1;
}

/* ---- validity ---- */
/* bit i of each mask is letter (base + i) of REND_LET: R_WU, R_E, or 0 for the faces.
 * No code here is a rotation: rend_fold_step hands every one of those to the fold's rot. */
static int rend_codes_allowed(const uint8_t *c, int n, unsigned faces, unsigned slices,
                              unsigned wides)
{
    for (int i = 0; i < n; i++) {
        int L = c[i] >> 2;
        if      (L >= R_WU) { if (!(wides  & (1u << (L - R_WU)))) return 0; }
        else if (L >= R_E)  { if (!(slices & (1u << (L - R_E))))  return 0; }
        else                { if (!(faces  & (1u << L)))          return 0; }
    }
    return 1;
}

/* validity of the *counted* codes; the trailing rotation is not one of them */
static int rend_codes_ok(const uint8_t *c, int n)
{
    return rend_codes_allowed(c, n, rend_faces, rend_slices, rend_wides);
}

/* the fold relabels face->face, slice->slice, wide->wide: wides are all six or none */
static int rend_all_moves(unsigned faces, unsigned slices, unsigned wides)
{
    return faces == 0x3F && slices == 0x7 && (wides == 0 || wides == 0x3F);
}

/* ---- one leaf ---- */
typedef struct { uint8_t kb[REND_LEAF_KEY]; int kn, cm; } RendLeaf;
static __thread RendLeaf *rend_lv;
static __thread size_t    rend_ln, rend_lcap;

/* A worker thread ends here, and a thread is made per depth: the leaves go back with it. */
static void rend_thread_done(void) __attribute__((unused));
static void rend_thread_done(void)
{
    free(rend_lv);
    rend_lv = NULL;
    rend_ln = rend_lcap = 0;
}

static void rend_leaf(int i)
{
    int rot = rend_st_rot[i];
    for (const unsigned char *t = ROT_TAIL[rend_boost]; *t; t++) rot = rend_resolve(rot, *t);
    const uint8_t *tail = NULL;
    if (rot) {
        /* a residual rotation that cannot be spelled is not a true solution */
        if (!rend_tail_ok[rot]) return;
        tail = rend_tail[rot];
    }

    int n = rend_st_n[i];

    if (rend_ln == rend_lcap) {
        size_t ncap = rend_lcap ? rend_lcap * 2 : 64;
        RendLeaf *q = rend_xrealloc(rend_lv, ncap * sizeof(*q));
        if (!q) return;
        rend_lv = q;
        rend_lcap = ncap;
    }
    /* kn == 0 is the identity, whose rendering is the empty maneuver: a rendering, not a failure */
    RendLeaf *e = &rend_lv[rend_ln++];
    int kn = n;
    memcpy(e->kb, rend_st_out[i], (size_t)n);
    if (tail) for (int k = 0; tail[k]; k++) e->kb[kn++] = tail[k];
    e->kn = kn;
    e->cm = n;
}

/* ---- the search ---- */
static void rend_rec(int i, int cost)
{
    if (rend_ln >= rend_stop) {
        /* Once a case: the workers behind the first one have nothing to add. */
        if (!__atomic_exchange_n(&rend_said, 1, __ATOMIC_RELAXED))
            fprintf(stderr, "notations past the %d one maneuver may add were dropped, so every "
                            "count here is short\n", REND_MAXREND);
        return;
    }
    if (cost + (rend_ntok - i) > rend_maxcount) return;
    if (i == rend_ntok) { rend_leaf(i); return; }
    int n0 = rend_st_n[i];              /* already validated: only the new part is checked */
    memcpy(rend_st_out[i + 1], rend_st_out[i], (size_t)n0);
    int k = rend_ck[i];
    for (int j = 0; j < rend_cn[k]; j++) {
        int rot = rend_st_rot[i];
        int n = n0;
        for (int t = 0; t < rend_con[k][j]; t++)
            rend_fold_step(rend_st_out[i + 1], &n, &rot, rend_co[k][j][t]);
        rend_st_rot[i + 1] = rot;
        rend_st_n[i + 1] = n;
        if (!rend_prefix_all && !rend_codes_ok(rend_st_out[i + 1] + n0, n - n0)) continue;
        rend_rec(i + 1, cost + rend_coc[k][j]);
    }
}

/* ---- one maneuver ---- */
/* A carrier T = rot^rho A^a0 B^b0: A^k1 B^k2 = T^t A^(k1-a0t) B^(k2-b0t) rot^(-rho t) */
static const int REND_WIDE_A0[2]  = {0, 1};  /* car 1 = the A-side wide letter, car 2 = B-side */
static const int REND_WIDE_B0[2]  = {1, 0};
static const int REND_WIDE_RHO[2] = {1, -1};

static int rend_car_let(int ax, int car)
{
    if (car == 0) return REND_AX_SL[ax];
    return R_WU + (car == 2 ? REND_AX_BF[ax] : REND_AX_AF[ax]);
}

/* rotation last; `cost` counts the moves that are not rotations; t = 0 is the plain face pair */
static int rend_render(int ax, int k1, int k2, int car, int t, uint8_t *c, int *cost)
{
    int sg = REND_AX_SGN[ax];
    int a0  = car ? REND_WIDE_A0[car - 1]  : sg;
    int b0  = car ? REND_WIDE_B0[car - 1]  : -sg;
    int rho = car ? REND_WIDE_RHO[car - 1] : -sg;
    int pa = (k1 - a0 * t) & 3;
    int pb = (k2 - b0 * t) & 3;
    int pr = (-rho * t) & 3;
    int n = 0, ct = 0;
    if (t)  { c[n++] = (uint8_t)(rend_car_let(ax, car) * 4 + t); ct++; }
    if (pa) { c[n++] = (uint8_t)(REND_AX_AF[ax]  * 4 + pa); ct++; }
    if (pb) { c[n++] = (uint8_t)(REND_AX_BF[ax]  * 4 + pb); ct++; }
    if (pr) { c[n++] = (uint8_t)(REND_AX_ROT[ax] * 4 + pr); }
    *cost = ct;
    return n;
}

/* The caller names the wides mask: the notation search reads rend_wides, the subgroup its own. */
static void rend_opts_init(unsigned wides)
{
    for (int key = 0; key < 48; key++) {
        int axis = key / 16, k1 = (key / 4) & 3, k2 = key & 3, nn = 0;
        for (int car = 0; car < 3; car++) {
            if (car && !wides) continue;
            for (int t = (car ? 1 : 0); t < 4; t++) {
                uint8_t c[4] = {0};     /* four are copied, and rend_render may write fewer */
                int cost = 0;
                /* no two match: car and t set them apart, and t = 0 writes no carrier */
                int cn = rend_render(axis, k1, k2, car, t, c, &cost);
                memcpy(rend_co[key][nn], c, 4);
                rend_con[key][nn] = (uint8_t)cn;
                rend_coc[key][nn] = (uint8_t)cost;
                nn++;
            }
        }
        rend_cn[key] = nn;
    }
}


/* Into the thread's own list, not cleared first: emit_drain absorbs it under the lock. */
static void rend_run(void)
{
    rend_stop = rend_ln + REND_MAXREND;
    rend_st_rot[0] = 0;
    rend_st_n[0] = 0;
    rend_rec(0, 0);
}

static int rend_col_key(int axis, int k1, int k2) __attribute__((unused));
static int rend_col_key(int axis, int k1, int k2)
{
    return axis * 16 + k1 * 4 + k2;
}

/* Moves of one axis commute, so a run writes as their sum's column, whatever they count as. */
static int rend_run_key(const int *ck, int from, int to)
{
    int k1 = 0, k2 = 0;
    for (int i = from; i < to; i++) {
        k1 = (k1 + ((ck[i] >> 2) & 3)) & 3;
        k2 = (k2 + (ck[i] & 3)) & 3;
    }
    return (ck[from] & ~15) | (k1 << 2) | k2;
}

/* Plan bit pick[r] sums run r into one column; pick[r] < 0 marks a run of one move. */
static void rend_one_plan(const int *ck, const int *lo, const int *hi, const int *pick,
                          int nruns, unsigned long long plan)
{
    int o = 0;
    for (int r = 0; r < nruns; r++) {
        if (pick[r] >= 0 && ((plan >> pick[r]) & 1))
            rend_ck[o++] = rend_run_key(ck, lo[r], hi[r]);
        else
            for (int t = lo[r]; t < hi[r]; t++) rend_ck[o++] = ck[t];
    }
    rend_ntok = o;
    rend_run();
}

/* Every run summed first; each move written on its own only when that wrote nothing. */
/* 1 = this maneuver got at least one rendering, which is not the same as one that stays. */
static int rend_render_plans(const int *ck, int n) __attribute__((unused));
static int rend_render_plans(const int *ck, int n)
{
    int lo[REND_MAXTOK], hi[REND_MAXTOK], pick[REND_MAXTOK], nruns = 0, nbit = 0;
    for (int i = 0; i < n; nruns++) {
        int j = i + 1;
        while (j < n && ck[j] / 16 == ck[i] / 16) j++;
        lo[nruns] = i;
        hi[nruns] = j;
        pick[nruns] = j - i > 1 ? nbit++ : -1;
        i = j;
    }
    size_t built = rend_ln;
    rend_one_plan(ck, lo, hi, pick, nruns, nbit ? (1ull << nbit) - 1 : 0);
    if (nbit && rend_ln == built) rend_one_plan(ck, lo, hi, pick, nruns, 0);
    return rend_ln > built;
}

/* Holding the set's lock: what the runs collected; rend_store appends and compares nothing */
static size_t rend_absorb(void) __attribute__((unused));
static size_t rend_absorb(void)
{
    size_t made = 0;
    for (size_t i = 0; i < rend_ln; i++) {
        /* A soft reject leaves it out of the set, so nothing reading the set may hear of it. */
        if (!rend_store(rend_lv[i].kb, rend_lv[i].kn, rend_lv[i].cm)) continue;
        made++;
        if (rend_new_hook) {
            char buf[REND_LEAF_BUF];
            rend_spell(buf, rend_lv[i].kb, rend_lv[i].kn);
            rend_new_hook(buf);
        }
    }
    rend_ln = 0;
    return made;
}

/* ---- the sort ---- */
static int rend_cmp(const void *pa, const void *pb)
{
    size_t i = *(const size_t *)pa, j = *(const size_t *)pb;
    const RendEnt *x = &rend_ent[i], *y = &rend_ent[j];
    if (x->cm != y->cm) return x->cm < y->cm ? -1 : 1;
    int n = x->klen < y->klen ? x->klen : y->klen;
    if (n) {
        int c = memcmp(rend_keyar + x->koff, rend_keyar + y->koff, (size_t)n);
        if (c) return c;
    }
    if (x->klen != y->klen) return x->klen < y->klen ? -1 : 1;
    return i < j ? -1 : (i > j ? 1 : 0);
}

/* cut into runs so threads can sort it: how many is engine.h's call, the bounds are made here */
#define REND_SORT_MIN 65536
#define REND_MAXRUN   32
static size_t rend_rlo[REND_MAXRUN], rend_rhi[REND_MAXRUN];

/* What losing the order costs: the list comes out walked, and --optimal takes any one of them. */
static void rend_no_order(void) __attribute__((unused));
static void rend_no_order(void)
{
    fprintf(stderr, "the standard notations could not be ordered (out of memory): they come "
                    "out in the order the walk found them\n");
}

/* Ranks 0..rend_on-1 into nrun runs; 0 if the order ran out, which costs the order alone. */
static int rend_sort_runs(int nrun) __attribute__((unused));
static int rend_sort_runs(int nrun)
{
    size_t *q = rend_xrealloc(rend_ord, (rend_on ? rend_on : 1) * sizeof(*q));
    if (!q) {
        free(rend_ord);                    /* a stale one is shorter than rend_on is now */
        rend_ord = NULL;
        rend_do_sort = 0;
        rend_no_order();
        return 0;
    }
    rend_ord = q;
    for (size_t i = 0; i < rend_on; i++) rend_ord[i] = i;
    if (nrun > REND_MAXRUN) nrun = REND_MAXRUN;
    if ((size_t)nrun > rend_on) nrun = rend_on ? (int)rend_on : 1;
    for (int t = 0; t < nrun; t++) {
        rend_rlo[t] = rend_on * (size_t)t / (size_t)nrun;
        rend_rhi[t] = rend_on * (size_t)(t + 1) / (size_t)nrun;
    }
    return nrun;
}

static void rend_sort_run(int t) __attribute__((unused));
static void rend_sort_run(int t)
{
    qsort(rend_ord + rend_rlo[t], rend_rhi[t] - rend_rlo[t], sizeof(*rend_ord), rend_cmp);
}

/* A buffer one order wide, for the merge tree to write a level into; NULL if it did not fit */
static size_t *rend_merge_tmp(void) __attribute__((unused));
static size_t *rend_merge_tmp(void)
{
    size_t *tmp = rend_xrealloc(NULL, (rend_on ? rend_on : 1) * sizeof(*tmp));
    if (!tmp) {
        free(rend_ord);
        rend_ord = NULL;
        rend_do_sort = 0;
        rend_no_order();
    }
    return tmp;
}

/* One merge level: runs 2w and 2w+1 become one, or the odd last run moves over */
struct rend_merge { const size_t *src; size_t *dst; int w, nrun; };

static void rend_merge_one(void *arg) __attribute__((unused));
static void rend_merge_one(void *arg)
{
    struct rend_merge *m = arg;
    int a = 2 * m->w;
    size_t lo = rend_rlo[a], mid = rend_rhi[a];
    size_t i = lo, j = mid, k = lo;
    if (a + 1 < m->nrun) {
        size_t hi = rend_rhi[a + 1];
        while (i < mid && j < hi)
            m->dst[k++] = rend_cmp(&m->src[i], &m->src[j]) <= 0 ? m->src[i++] : m->src[j++];
        while (j < hi) m->dst[k++] = m->src[j++];
    }
    while (i < mid) m->dst[k++] = m->src[i++];
}

/* ---- set up ---- */
static int rend_rot_fits(int rot, unsigned rots)
{
    for (const unsigned char *t = ROT_TAIL[rot]; *t; t++)
        if (!(rots & (1u << ((*t >> 2) - R_X)))) return 0;
    return 1;
}

/* ROT_TAIL when its letters are there; else a shortest spelling of theirs, three tokens long */
static void rend_tail_init(uint8_t tail[24][4], uint8_t ok[24], unsigned rots)
{
    int code[9], ncode = 0;
    for (int ax = 0; ax < 3; ax++)
        if (rots & (1u << ax))
            for (int a = 1; a <= 3; a++)
                code[ncode++] = (R_X + ax) * 4 + a;
    for (int r = 0; r < 24; r++) {
        memset(tail[r], 0, sizeof tail[r]);
        ok[r] = 0;
        if (rend_rot_fits(r, rots)) {
            memcpy(tail[r], ROT_TAIL[r], 3);
            ok[r] = 1;
        }
    }
    /* Breadth first to the shortest spelling, and stopped at the three tokens a tail holds */
    int prev[24], via[24], depth[24], queue[24], qh = 0, qt = 0;
    for (int r = 0; r < 24; r++) prev[r] = -1;
    prev[0] = 0;
    depth[0] = 0;
    queue[qt++] = 0;
    while (qh < qt) {
        int s = queue[qh++];
        if (depth[s] == 3) continue;
        for (int i = 0; i < ncode; i++) {
            int t = rend_resolve(s, (uint8_t)code[i]);
            if (prev[t] >= 0) continue;
            prev[t] = s;
            via[t] = code[i];
            depth[t] = depth[s] + 1;
            queue[qt++] = t;
        }
    }
    for (int r = 1; r < 24; r++) {
        if (ok[r] || prev[r] < 0) continue;
        int s = r;
        for (int k = depth[r]; k > 0; k--) {
            tail[r][k - 1] = (uint8_t)via[s];
            s = prev[s];
        }
        ok[r] = 1;
    }
}

/* filled as faces, slices, wides, rots: not the order REND_LET spells them in */
static void rend_letter_masks(const char *subgroup, unsigned *faces, unsigned *slices,
                              unsigned *wides, unsigned *rots)
{
    unsigned r = 0;
    *faces = *slices = *wides = 0;
    for (const char *p = subgroup; *p; p++) {
        const char *q = strchr(REND_LET, *p);
        if (!q) continue;
        int L = (int)(q - REND_LET);
        if      (L < R_E)  *faces  |= 1u << L;
        else if (L < R_X)  *slices |= 1u << (L - R_E);
        else if (L < R_WU) r       |= 1u << (L - R_X);
        else               *wides  |= 1u << (L - R_WU);
    }
    if (rots) *rots = r;
}

static void rend_init(const char *subgroup, int maxcount) __attribute__((unused));
static void rend_init(const char *subgroup, int maxcount)
{
    cube_build_relabel();
    rend_maxcount = maxcount;
    rend_letter_masks(subgroup, &rend_faces, &rend_slices, &rend_wides, &rend_rots_named);

    rend_prefix_all = rend_all_moves(rend_faces, rend_slices, rend_wides);
    rend_tail_init(rend_tail, rend_tail_ok, rend_rots_named);

    rend_opts_init(rend_wides);
    rend_reset();
}

/* A file a Windows editor saved opens with a UTF-8 BOM, which is not part of its first line. */
static void skip_bom(FILE *fh) __attribute__((unused));
static void skip_bom(FILE *fh)
{
    long at = ftell(fh);
    unsigned char b[3];
    size_t got = fread(b, 1, 3, fh);
    if (!(got == 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF))
        fseek(fh, at, SEEK_SET);
}

/* 1 a line, 0 the end, -1 refused and consumed, -2 unreadable; not fgets: it splits. */
static int read_line(char *buf, size_t cap, FILE *fh, const char **why) __attribute__((unused));
static int read_line(char *buf, size_t cap, FILE *fh, const char **why)
{
    size_t n = 0;
    int c, over = 0, nul = 0;
    /* CR alone ends a line: a CR-only file would otherwise read as one long line */
    while ((c = fgetc(fh)) != EOF && c != '\n' && c != '\r') {
        if (!c) { nul = 1; continue; }
        if (n + 1 < cap) buf[n++] = (char)c;
        else             over = 1;
    }
    if (c == '\r') {                     /* CRLF is one terminator, not two lines */
        int d = fgetc(fh);
        if (d != '\n' && d != EOF) ungetc(d, fh);
    }
    buf[n] = 0;
    /* a read stopped by an error leaves the loop as the end does: a half line must not run whole */
    if (ferror(fh)) { *why = "stopped being readable"; return -2; }
    if (nul)  { *why = "contains a NUL"; return -1; }
    if (over) { *why = "is too long";    return -1; }
    return (c == EOF && !n) ? 0 : 1;
}

#endif /* RENDER_H */
