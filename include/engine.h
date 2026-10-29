/* Cubolt (https://github.com/nbwzx/cubolt)
 * Copyright (C) 2026 Zixing Wang <zixingwang.cn@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* engine.h -- phase 1: FTM over the 18 face turns, STM over the 27 face and slice turns. */
#ifndef ENGINE_H
#define ENGINE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <windows.h>
#include <process.h>
#include "host.h"
#include "caseout.h"
#include "render.h"
#include "core.h"
#include "subgroup.h"
#include "tablesum.h"
#include "cube.h"

static u16 *corners_move, *twist_move, *flip_move, *ss_move;
/* packed class << 4 | sym, as fs24_clssym holds them */
static u32 *clssym;
/* ct_prun is indexed by corner class, so the permutation needs the same class << 4 | sym pair. */
static u32 *ctclssym;
static u16 *twist_conj;
static u8  *fsst;
static u8  *ctdepth;   /* ct_prun's distances, packed as CT_GET reads them */
static u8  *ct_map;    /* the mapping the packed copy was made from, kept only to release it */
/* Those distances reach 11, so two of them fit in a byte: half the footprint the search walks. */
#define CT_GET(i) ((ctdepth[(i) >> 1] >> (((i) & 1) << 2)) & 15)
/* indexed [3 * distance + trit], filled by build_once: the child's distance */
static s8  dist_tab[42];

/* Every depth, from a job file, the ini, or the GUI's box, is held to MAX_DEPTH. */
#define MOVE_CAP 64
#define MAX_DEPTH (MOVE_CAP - 1)
#define DEFAULT_DEPTH 13

#define NO_JOBS_MSG "no jobs in %s\n"
#define BAD_LINE_FMT "%s line %d %s\n"
#define NO_FACE_TURN_MSG "subgroup names no usable face turn\n"

/* The most completions enumerated, and the bound of the arrays that hold them. */
#define IC_K_CAP     1024
/* Past this many ways one relaxed search beats one ordinary search per way, corners all known. */
#define IC_WAYS_CUT  16
static char tokname[N_MOVE][6];             /* STM token name per column, e.g. "F3B1" */
/* the same token as the notation layer reads it: its axis and its two amounts */
static unsigned char tok_ak[N_MOVE][3];
/* the column of the token that undoes this one: the two amounts negated */
static unsigned char tok_inv[N_MOVE];

/* a token's two faces are its axis's A and B face, render.h's REND_AX_* */
static void build_token_names(void)
{
    for (int ax = 0; ax < 3; ax++)
        for (int k1 = 0; k1 < 4; k1++)
            for (int k2 = 0; k2 < 4; k2++) {
                if (!stm_is_col(k1, k2)) continue;
                int col = stm_column(ax, k1, k2);
                snprintf(tokname[col], 6, "%c%d%c%d",
                         REND_LET[REND_AX_AF[ax]], k1, REND_LET[REND_AX_BF[ax]], k2);
                tok_ak[col][0] = (unsigned char)ax;
                tok_ak[col][1] = (unsigned char)k1;
                tok_ak[col][2] = (unsigned char)k2;
                tok_inv[col] = (unsigned char)stm_column(ax, (4 - k1) & 3, (4 - k2) & 3);
            }
}

/* One thread owns its Thr outright, so N threads walk the read-only tables without any locking. */
typedef struct {
    /* this task, per thread; ParStat adds up the whole pass */
    long long nodecount;
    long long nsol;
    unsigned long long solhash;
    int  sofar[MOVE_CAP];             /* the move list: list positions, not columns */
    int  sp;
    int  split;                       /* frontier-collection depth (0 = a plain worker) */
} Thr;

static long long totnodes;

static int opt_mode = 0, dump_mode = 0, render_mode = 0;
/* 1: the walk runs on the inverse cube, so every answer it finds comes out backwards. */
static int sol_rev = 0;
/* --json makes stdout machine-readable, so the table-load line goes to stderr there. */
static int progress_to_stderr = 0;
static int sol_std = -1;       /* -1 unset, 0 force-print, 1 force-quiet; run_job derives it */
static int silent_sol = 0;
/* Row mode (--bench): one tab-separated line, not the block */
static int   tab_out = 0;
/* --solhash asks for the solutions' hash in any run, --bench's rows included. */
static int   hash_wanted = 0;
/* A parallel walk's workers all count into it, so the increments have to be atomic. */
/* one line apiece: every solution bumps one, and shared lines bounce between the workers */
typedef struct { _Alignas(64) volatile long v; } LenSeen;
static LenSeen len_seen[MOVE_CAP];
/* Solutions that came out as at least one notation; nsol counts those that did not too. */
static volatile long long spelled_sol;
static char *tab_row;          /* the line the last job built; run_batch collects these */
/* Cooperative abort, checked at every node: the GUI flips it from its message thread. */
static volatile int engine_stop = 0;
static volatile int opt_found   = 0;   /* --optimal: set once a rendering is absorbed */
static int par_threads = 0;                 /* 0 = auto (the logical processor count), 1 = serial */
static const long long par_min = 1000;       /* split only if the last pass was at least this big */
static FILE *outfh;                         /* the same solution lines, to disk (Job.outpath) */
/* The file's presence then means the case ran, not that it was solved. */
static const char *outpath;
static void emit_flush(void);
/* Process set-up, not the case's own work, so it is subtracted from the reported wall. */
static double load_wall;

/* What the page walks XOR into, so neither of them is optimized away. */
static volatile unsigned char warm_sink;

/* Eight lanes walk disjoint pages, so the warm-up costs a fraction of the serial walk. */
#define WARM_THREADS 8
struct warm_run { const unsigned char *p; size_t n, stride, off; unsigned char s; };

static unsigned __stdcall warm_pages(void *arg)
{
    struct warm_run *w = arg;
    volatile unsigned char s = 0;
    for (size_t off = w->off; off < w->n; off += w->stride) s ^= w->p[off];
    w->s = (unsigned char)s;
    return 0;
}

static void warm_mapping(const void *view, size_t n)
{
    const unsigned char *p = (const unsigned char *)view;
    struct warm_run w[WARM_THREADS];
    HANDLE h[WARM_THREADS - 1];
    int made = 0;
    size_t stride = (size_t)WARM_THREADS * 4096;
    for (int i = 1; i < WARM_THREADS; i++) {
        w[i].p = p; w[i].n = n; w[i].stride = stride; w[i].off = (size_t)i * 4096; w[i].s = 0;
        h[made] = (HANDLE)_beginthreadex(NULL, 0, warm_pages, &w[i], 0, NULL);
        if (!h[made]) break;
        made++;
    }
    w[0].p = p; w[0].n = n; w[0].stride = stride; w[0].off = 0; w[0].s = 0;
    warm_pages(&w[0]);
    /* a failed join = "no lane ran": their w[i].s is still being written, so it is not read */
    int joined = made;
    if (made && WaitForMultipleObjects((DWORD)made, h, TRUE, INFINITE) == WAIT_FAILED) {
        fprintf(stderr, "cannot join the page-warming threads (error %lu)\n",
                (unsigned long)GetLastError());
        joined = 0;
    }
    for (int i = 0; i < made; i++) CloseHandle(h[i]);
    unsigned char s = w[0].s;
    for (int i = 1; i <= joined; i++) s ^= w[i].s;
    /* the lanes after made never started, so nothing faulted their pages: walk them here */
    for (int i = made + 1; i < WARM_THREADS; i++)
        for (size_t off = (size_t)i * 4096; off < n; off += stride) s ^= p[off];
    warm_sink = s;
}

/* Mapped, not copied: a case visits a fraction of phase1x24_prun; the length is checked by hand. */
static void *load_file(const char *name)
{
    long long want = ts_size(name);
    if (want < 0) { fprintf(stderr, "%s: not a table of this set\n", name); table_bail(); }
    size_t n = (size_t)want;
    const char *full = table_path(name);
    HANDLE hf = CreateFileA(full, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) {
        fprintf(stderr, CANNOT_OPEN, full);
        fprintf(stderr, "  build it first: mktables.exe %s\n", name);
        table_bail();
    }
    /* exact, not a floor: extra bytes would read as distances never computed */
    LARGE_INTEGER sz;
    sz.QuadPart = 0;
    if (!GetFileSizeEx(hf, &sz) || (unsigned long long)sz.QuadPart != (unsigned long long)n) {
        fprintf(stderr, "%s is %llu bytes, %llu expected\n",
                full, (unsigned long long)sz.QuadPart, (unsigned long long)n);
        CloseHandle(hf);        /* table_bail may land the caller elsewhere: this handle is ours */
        table_bail();
    }
    HANDLE hm = CreateFileMappingA(hf, NULL, PAGE_READONLY, 0, 0, NULL);
    void  *view = hm ? MapViewOfFile(hm, FILE_MAP_READ, 0, 0, (SIZE_T)n) : NULL;
    /* the view keeps the mapping alive, and the mapping keeps the file alive */
    if (hm) CloseHandle(hm);
    CloseHandle(hf);
    if (!view) { fprintf(stderr, "cannot map %s\n", full); table_bail(); }
    /* PrefetchVirtualMemory returns before the read-ahead ends, so the page walk is what waits */
    if (n >= 64u * 1024 * 1024) {
        WIN32_MEMORY_RANGE_ENTRY e;
        e.VirtualAddress = view;
        e.NumberOfBytes  = (SIZE_T)n;
        /* Not fatal, just slower: the read faults the pages in on its own. */
        if (!PrefetchVirtualMemory(GetCurrentProcess(), 1, &e, 0)) {
            static int said;
            if (!said) {
                said = 1;
                fprintf(stderr, "  (prefetch is unavailable: the tables are paged in "
                                "by the walk)\n");
            }
        }
        warm_mapping(view, n);
    }
    return view;
}

/* "0.2", "6.66", "10.0": two decimals when they carry information, one otherwise. */
static const char *fmt_secs(double t)
{
    static __thread char buf[24];
    int n = snprintf(buf, sizeof buf, "%.2f", t);
    if (n > 3 && buf[n - 1] == '0' && buf[n - 2] != '.')
        buf[n - 1] = 0;
    return buf;
}

/* flushed to show mid-run; +0.0001 keeps the rate off /0; nsol is all depths, nodes this one */
static void print_depth_line(int depth, long long nsol, long long nodes, double t1)
{
    if (depth <= 10 || tab_out) return;
    double t = now_sec() - t1 + 0.0001;
    printf("depth %d %s %s s, %lld solutions so far, %lld nodes generated, "
           "about %lld nodes/s\n",
           depth, engine_stop ? "stopped after" : "done in", fmt_secs(t),
           nsol, nodes, (long long)(nodes / t + 0.5));
    fflush(stdout);
}

/* The ct coordinate out of a class+symmetry pair: the class, and the twist in its frame. */
static inline u32 ct_coord(u32 cs, int twist)
{
    return (u32)N_TWIST * CLS_OF(cs) + twist_conj[(twist << 4) + SYM_OF(cs)];
}

/* (flip, slice) reduced to its class, the twist carried into that class's frame: /16 symmetries. */
static inline u32 fsst_index(int ss, int flip, int twist)
{
    return ct_coord(clssym[N_FLIP * ss + flip], twist);
}

/* A byte holds five trits (3^5 = 243 <= 256): entry ix in byte ix/5 at digit ix%5. */
static u16 trit_dec[256];

static inline int fsst_get(u32 i) { return (trit_dec[fsst[i / 5]] >> ((i % 5) << 1)) & 3; }

/* list position -> column, the column on the other two axes, its cost: a list fits N_MOVE */
static int mv_col[N_MOVE], mv_mrl[N_MOVE], mv_mfb[N_MOVE];
/* Two moves of a group are one move apart, so one out of reach takes its group with it. */
static int mv_grp[N_MOVE];
/* the last list position of mv_grp[i]'s group: one index turns a whole run over */
static unsigned char mv_grp_end[N_MOVE];
static unsigned long long ban_tab[N_MOVE];
static int slice_mode, stdmax_eff;
static int nmoves_in;

static inline int mv_grp_skip(int i, int g, int togo)
{
    return g > togo ? mv_grp_end[i] : i;
}

/* Bit mv_col[i]; a frame-aware ban holds only where every frame of the set agrees. */
static inline unsigned long long child_banmask(int i, unsigned rhos)
{
    int col = mv_col[i];
    if (!slice_mode) return ban_tab[col];
    if (sg_blind) return sg_ban_f[col * 24];
    unsigned long long m = ~0ULL;
    unsigned rest = rhos;
    while (rest) {
        int r = __builtin_ctz(rest);
        rest &= rest - 1;
        m &= sg_ban_f[col * 24 + r];
    }
    return m;
}

/* 0 = no rendering carries the prefix on; the rows are columns, so FTM hands the set on. */
static inline unsigned child_open(unsigned long long ban, int i, unsigned rhos)
{
    if ((ban >> mv_col[i]) & 1) return 0;         /* mergeable with the move behind it */
    if (!slice_mode || sg_blind) return rhos;
    return sg_rho_apply(i, rhos);
}

/* What the child carries: STM only, a floor on the total; -1 = the floor spends the budget. */
static inline int stm_child_std(int acc_std, int i, int togo)
{
    if (!slice_mode) return acc_std;
    int nacc = acc_std + sg_mc[i];
    return nacc + togo - 1 > stdmax_eff ? -1 : nacc;
}

/* Walk back from the stored distance mod 3 to the real depth. */
static int phasex24_depth(int ss, int flip, int twist)
{
    int depth_mod3 = fsst_get(fsst_index(ss, flip, twist));
    int depth = 0;
    /* Every column is a step of the table's own BFS, so the walk back may take any of them. */
    int nwalk = slice_mode ? N_MOVE : N_MOVE_FTM;
    while (flip != 0 || ss != 0 || twist != 0) {
        if (depth_mod3 == 0) depth_mod3 = 3;
        int found = 0;
        for (int m = 0; m < nwalk && !found; m++) {
            int mc = slice_mode ? m : ftm_col[m];
            int t1 = twist_move[N_MOVE * twist + mc];
            int f1 = flip_move[N_MOVE * flip + mc];
            int s1 = ss_move[N_MOVE * ss + mc];
            if (fsst_get(fsst_index(s1, f1, t1)) == depth_mod3 - 1) {
                depth++; twist = t1; flip = f1; ss = s1; depth_mod3--;
                found = 1;
            }
        }
        if (!found) {
            /* not only a bad table: a caller that left slice_mode on the other metric lands here */
            fprintf(stderr, "phasex24 walk-back is stuck -- the loaded pruning table and this "
                            "engine's metric (slice_mode %d) disagree: rebuild them with "
                            "mktables.exe --force\n", slice_mode);
            table_bail();      /* a bad table is a table fault, so the GUI lands on one too */
        }
    }
    return depth;
}

/* ---- the parallel frontier ---- */

/* The set of renderings is global: a thread renders on its own and inserts under rend_lock. */
static SRWLOCK rend_lock = SRWLOCK_INIT;

/* One task == one contiguous slice of the walk. */
typedef struct {
    int UD_flip, RL_flip, FB_flip, UD_twist, RL_twist, FB_twist;
    int UD_ss, RL_ss, FB_ss, corners;
    int UD_d, RL_d, FB_d, togo, acc_std, sp;
    unsigned rhos;
    unsigned long long banmask;
    int sofar[MOVE_CAP];
} Task;

static Task *tasks;
static int ntasks, task_cap;
static volatile long next_task;             /* the work queue: a fetch-and-add over task indices */

/* ---- the search ---- */

/* A line costs less than the call writing it: gathered here, handed over in mouthfuls. */
#define EMIT_BUF 65536
_Static_assert(MOVE_CAP * 6 + 1 <= EMIT_BUF && REND_LEAF_BUF <= EMIT_BUF,
               "a line longer than the emit buffer would overrun it");
static char   emit_buf[EMIT_BUF];
static size_t emit_used;

/* Whether anything takes a solution line at all: neither printed nor filed means none is. */
static int emit_kept(void) { return !silent_sol || outpath; }

static void emit_line(const char *s)
{
    if (!emit_kept()) return;
    size_t n = strlen(s);
    if (emit_used + n + 1 > EMIT_BUF) emit_flush();
    memcpy(emit_buf + emit_used, s, n);
    emit_buf[emit_used + n] = '\n';
    emit_used += n + 1;
}

/* Render from the columns the token names were built from: no text to parse back. */
static int rend_alg_cols(const unsigned char *cols, int sp)
{
    int ck[REND_MAXTOK];
    for (int i = 0; i < sp; i++)
        ck[i] = rend_col_key(tok_ak[cols[i]][0], tok_ak[cols[i]][1], tok_ak[cols[i]][2]);
    return rend_render_plans(ck, sp);
}

static void join_cols(char *dst, const unsigned char *c, int sp)
{
    int n = 0;
    for (int i = 0; i < sp; i++) {
        const char *t = tokname[c[i]];
        while (*t) dst[n++] = *t++;
        if (i + 1 < sp) dst[n++] = ' ';
    }
    dst[n] = 0;
}

/* FNV-1a over what join_cols would write; callers add them, so a set's hash is order-free */
static unsigned long long fnv_cols(const unsigned char *c, int sp)
{
    unsigned long long h = 1469598103934665603ULL;
    for (int i = 0; i < sp; i++) {
        const char *t = tokname[c[i]];
        while (*t) { h ^= (unsigned char)*t++; h *= 1099511628211ULL; }
        if (i + 1 < sp) { h ^= ' '; h *= 1099511628211ULL; }
    }
    return h;
}

    /* what a thread holds before one rend_lock take hands the batch over */
#define EMIT_BATCH 32
static __thread int emit_held;

static void emit_drain(void)
{
    /* nothing to hand over, and no --optimal probe to run: leave its lock alone */
    if (!render_mode || (!emit_held && !opt_mode)) return;
    AcquireSRWLockExclusive(&rend_lock);
    /* accepted at least one: that is what ends an --optimal walk */
    if (rend_absorb() && opt_mode) opt_found = 1;
    ReleaseSRWLockExclusive(&rend_lock);
    emit_held = 0;
}

static void emit_record(int sp, long long *nsol, unsigned long long *hash,
                        const unsigned char *cols)
{
    (*nsol)++;
    InterlockedIncrement(&len_seen[sp].v); /* the `lengths` column of --bench */
    if (hash_wanted) *hash += fnv_cols(cols, sp);
    /* spelled only to be dropped when nothing is taking it, which a billion of them adds up */
    if (dump_mode && emit_kept()) {
        char line[MOVE_CAP * 6];        /* a token runs to four letters, plus the space after */
        join_cols(line, cols, sp);
        AcquireSRWLockExclusive(&rend_lock);
        emit_line(line);
        ReleaseSRWLockExclusive(&rend_lock);
    }
    if (!render_mode) return;
    if (rend_alg_cols(cols, sp)) InterlockedIncrement64(&spelled_sol);
    /* --optimal must not wait for a batch to fill */
    if (opt_mode || ++emit_held == EMIT_BATCH) emit_drain();
}

static void emit_solution(Thr *th)
{
    /* the walk on the inverse cube answers backwards, and each step undone is its own token */
    unsigned char tok[MOVE_CAP];
    for (int i = 0; i < th->sp; i++) {
        int c = mv_col[th->sofar[i]];
        tok[sol_rev ? th->sp - 1 - i : i] = sol_rev ? tok_inv[c] : (unsigned char)c;
    }
    emit_record(th->sp, &th->nsol, &th->solhash, tok);
}

/* P holds every child's table indices for the passes that read them; it belongs to the
   call, since the workers run search() at once. */
struct PNode {
    unsigned nrho;
    int nacc, pass;
    int c1, ut1, rt1, ft1;
    int uf1, rf1, ff1, us1, rs1, gs1;
    u32 ct_idx, cls_ud, cls_rl, cls_fb, fs_ud, fs_rl, fs_fb;
    u32 cs_ud, cs_rl, cs_fb;   /* the clssym index each axis reads, built above */
    int ctg;                   /* the corner gate's verdict, asked one pass early */
    int ud1, rl1, fb1;         /* the three lower bounds, kept for the survivors */
};

static void search(Thr *th, int UD_flip, int RL_flip, int FB_flip,
                   int UD_twist, int RL_twist, int FB_twist,
                   int UD_ss, int RL_ss, int FB_ss, int corners,
                   int UD_d, int RL_d, int FB_d, int togo, int acc_std, unsigned rhos,
                   unsigned long long banmask)
{
    if (engine_stop) return;
    if (opt_found)   return;

    /* the whole state is recorded as a task and the recursion returns: nothing extra is counted */
    if (th->split && th->sp == th->split) {
        if (ntasks == task_cap) {
            int want = task_cap ? task_cap * 2 : 1024;
            Task *grown = realloc(tasks, (size_t)want * sizeof *grown);
            if (grown) { task_cap = want; tasks = grown; }
            /* No room to grow: this subtree is walked here instead, and the list stays usable. */
            else th->split = 0;
        }
        if (ntasks < task_cap) {
            Task *t = &tasks[ntasks++];
            t->UD_flip = UD_flip;   t->RL_flip = RL_flip;   t->FB_flip = FB_flip;
            t->UD_twist = UD_twist; t->RL_twist = RL_twist; t->FB_twist = FB_twist;
            t->UD_ss = UD_ss;       t->RL_ss = RL_ss;       t->FB_ss = FB_ss;
            t->corners = corners;
            t->UD_d = UD_d;         t->RL_d = RL_d;         t->FB_d = FB_d;
            t->togo = togo;         t->acc_std = acc_std;   t->rhos = rhos;
            t->banmask = banmask;
            t->sp = th->sp;
            memcpy(t->sofar, th->sofar, (size_t)th->sp * sizeof *t->sofar);
            return;
        }
    }

    /* The root reaches togo 0 with no gate having looked at its corners: the three axes read only
       (slice, flip, twist), so a corner permutation still marks an unsolved cube. */
    if (togo == 0) {
        if (corners == 0)
            emit_solution(th);
        return;
    }

    /* Cached across nested search() calls, which own *th while they run */
    int sp = th->sp;
    long long nodes = th->nodecount;

    /* The index work in two passes, split by dependency level: an fsst index needs its
       clssym class first, so the classes are all asked for before any of them is read.
       pass is what the cheap checks decided: 0 gates it, 1 the ban list closed it, 2 the
       STM floor closed it after the node was counted, 3 it survived every gate. */
    struct PNode P[N_MOVE];
    for (int j = 0; j < nmoves_in; j++) {
        int col = mv_col[j];
        unsigned nrho = child_open(banmask, j, rhos);
        if (!nrho) { P[j].pass = 1; continue; }
        int nacc = stm_child_std(acc_std, j, togo);
        P[j].pass = nacc < 0 ? 2 : 0;
        if (nacc < 0) continue;
        int mrl = mv_mrl[j], mfb = mv_mfb[j];
        P[j].nrho = nrho; P[j].nacc = nacc;

        int c1  = corners_move[N_MOVE * corners + col];
        int ut1 = twist_move[N_MOVE * UD_twist + col];
        P[j].c1 = c1; P[j].ut1 = ut1;
        P[j].ct_idx = ct_coord(ctclssym[c1], ut1);

        int rt1 = twist_move[N_MOVE * RL_twist + mrl];
        int rf1 = flip_move[N_MOVE * RL_flip + mrl];
        int rs1 = ss_move[N_MOVE * RL_ss + mrl];
        P[j].rt1 = rt1; P[j].rf1 = rf1; P[j].rs1 = rs1;

        int ft1 = twist_move[N_MOVE * FB_twist + mfb];
        int ff1 = flip_move[N_MOVE * FB_flip + mfb];
        int gs1 = ss_move[N_MOVE * FB_ss + mfb];
        P[j].ft1 = ft1; P[j].ff1 = ff1; P[j].gs1 = gs1;

        P[j].uf1 = flip_move[N_MOVE * UD_flip + col];
        P[j].us1 = ss_move[N_MOVE * UD_ss + col];
        P[j].cs_ud = (u32)N_FLIP * P[j].us1 + P[j].uf1;
        P[j].cs_rl = (u32)N_FLIP * P[j].rs1 + P[j].rf1;
        P[j].cs_fb = (u32)N_FLIP * P[j].gs1 + P[j].ff1;
        /* Each axis's class index, built from the move this pass already read. */
        __builtin_prefetch(&clssym[P[j].cs_ud]);
        __builtin_prefetch(&clssym[P[j].cs_rl]);
        __builtin_prefetch(&clssym[P[j].cs_fb]);
    }

    for (int j = 0; j < nmoves_in; j++) {
        if (P[j].pass) continue;
        P[j].cls_ud = clssym[P[j].cs_ud];
        P[j].cls_rl = clssym[P[j].cs_rl];
        P[j].cls_fb = clssym[P[j].cs_fb];
        __builtin_prefetch(&ctdepth[P[j].ct_idx]);
        P[j].fs_ud = ct_coord(P[j].cls_ud, P[j].ut1);
        __builtin_prefetch(&fsst[P[j].fs_ud / 5]);
        P[j].fs_rl = ct_coord(P[j].cls_rl, P[j].rt1);
        __builtin_prefetch(&fsst[P[j].fs_rl / 5]);
        P[j].fs_fb = ct_coord(P[j].cls_fb, P[j].ft1);
        __builtin_prefetch(&fsst[P[j].fs_fb / 5]);
    }

    /* The corner gate's verdict, one pass ahead of the walk that reads it. */
    for (int j = 0; j < nmoves_in; j++) {
        if (P[j].pass) continue;
        P[j].ctg = CT_GET(P[j].ct_idx);
    }

    /* The gates in order, counting as the plain loop did: a group skip jumps the rest of the
       face, and those moves never reached the counter above it.  A survivor takes pass 3, so
       the walk below reads the row the gate just filled. */
    for (int j = 0; j < nmoves_in; j++) {
        if (P[j].pass == 1) continue;
        nodes++;
        if (P[j].pass == 2) continue;
        int g = P[j].ctg;
        if (g >= togo) { j = mv_grp_skip(j, g, togo); continue; }
        int ud1 = dist_tab[3 * UD_d + fsst_get(P[j].fs_ud)];
        g = ud1;
        if (g >= togo) { j = mv_grp_skip(j, g, togo); continue; }
        int rl1 = dist_tab[3 * RL_d + fsst_get(P[j].fs_rl)];
        g = rl1;
        if (g >= togo) { j = mv_grp_skip(j, g, togo); continue; }
        int fb1 = dist_tab[3 * FB_d + fsst_get(P[j].fs_fb)];
        g = fb1;
        if (g >= togo) { j = mv_grp_skip(j, g, togo); continue; }
        /* trinomial prune */
        if (ud1 != 0 && ud1 == rl1 && rl1 == fb1) {
            if (ud1 + 1 >= togo) continue;
        }
        P[j].pass = 3;
        P[j].ud1 = ud1; P[j].rl1 = rl1; P[j].fb1 = fb1;
    }

    /* The survivors, in order, once every table read above is done. */
    for (int j = 0; j < nmoves_in; j++) {
        if (P[j].pass != 3) continue;
        th->sofar[sp] = j;
        th->sp = sp + 1;
        th->nodecount = nodes;
        search(th, P[j].uf1, P[j].rf1, P[j].ff1, P[j].ut1, P[j].rt1, P[j].ft1,
                   P[j].us1, P[j].rs1, P[j].gs1, P[j].c1,
                   P[j].ud1, P[j].rl1, P[j].fb1,
                   togo - 1, P[j].nacc, P[j].nrho, child_banmask(j, rhos));
        nodes = th->nodecount;
    }
    th->nodecount = nodes;
}

/* ---- the parallel driver ---- */

/* one per worker, on its own cache line: every finished task writes these counters */
typedef struct { _Alignas(64) long long nodes, nsol; unsigned long long solhash; } ParStat;

static unsigned __stdcall par_worker(void *arg)
{
    ParStat *st = arg;
    Thr th;
    memset(&th, 0, sizeof th);
    st->nodes = 0;
    st->nsol = 0;
    st->solhash = 0;
    for (;;) {
        long idx = InterlockedIncrement(&next_task) - 1;
        if (idx >= ntasks || engine_stop || opt_found) break;
        Task *t = &tasks[idx];
        th.nodecount = 0;
        th.nsol = 0;
        th.solhash = 0;
        th.sp = t->sp;
        memcpy(th.sofar, t->sofar, (size_t)t->sp * sizeof *th.sofar);
        search(&th, t->UD_flip, t->RL_flip, t->FB_flip, t->UD_twist, t->RL_twist, t->FB_twist,
               t->UD_ss, t->RL_ss, t->FB_ss, t->corners,
               t->UD_d, t->RL_d, t->FB_d, t->togo, t->acc_std, t->rhos, t->banmask);
        st->nodes   += th.nodecount;
        st->nsol    += th.nsol;
        st->solhash += th.solhash;
    }
    emit_drain();
    rend_thread_done();
    return 0;
}

/* The caller is worker 0, so `nthr` threads serve one list. */
static void par_fanout(unsigned __stdcall (*worker)(void *), int nthr,
                       long long *nodes, long long *nsol, unsigned long long *hash)
{
    ParStat st[THREAD_CAP];
    HANDLE h[THREAD_CAP];
    int created = 0;
    for (int i = 1; i < nthr; i++) {
        h[created] = (HANDLE)_beginthreadex(NULL, 0, worker, &st[i], 0, NULL);
        if (!h[created]) break;                /* no more threads: the main one takes the slack */
        created++;
    }
    worker(&st[0]);
    if (created) {
        /* a failed join leaves nothing to add up: the walk cannot go on without its workers */
        if (WaitForMultipleObjects((DWORD)created, h, TRUE, INFINITE) == WAIT_FAILED) {
            fprintf(stderr, "cannot join the solver threads (error %lu)\n",
                    (unsigned long)GetLastError());
            exit(1);
        }
    }
    for (int i = 0; i < created; i++) CloseHandle(h[i]);

    for (int i = 0; i < created + 1; i++) {
        *nodes += st[i].nodes;
        *nsol  += st[i].nsol;
        *hash  += st[i].solhash;
    }
}

/* the frontier jumps ~7x a ply, so the count is measured ply by ply, not estimated */
#define SPLIT_TASKS_PER_THREAD 32
/* a pass this size per task dilutes a heavy subtree the workers cannot steal from */
#define SPLIT_TASK_TARGET_NODES 20000
/* task size floor: the split is capped so one task keeps this many nodes */
#define SPLIT_MIN_TASK_NODES    100

static int pick_split(int nthr, int togo, long long walk_nodes)
{
    /* this pass, per walk: the last one's, grown by the factor one level costs */
    long long pass = walk_nodes * nmoves_in / 2;
    long long want = SPLIT_TASKS_PER_THREAD * (long long)nthr;
    if (want > pass / SPLIT_MIN_TASK_NODES) want = pass / SPLIT_MIN_TASK_NODES;
    int k = 1;
    /* Nothing left to fan out into: a pass two deep splits one level and walks the other. */
    if (togo < 2) return 0;
    long long est = nmoves_in;
    while (k < togo - 1 && k < 6 && est < want) {
        k++;
        est = est * nmoves_in / 2;              /* bans and rho roughly halve each level */
    }
    return k;
}

/* One split for the whole layer, or 0 to stay serial. */
static int split_for(int togo, long long prev_nodes, long long walks)
{
    if (prev_nodes < par_min) return 0;
    return pick_split(par_threads, togo, walks ? prev_nodes / walks : 0);
}

/* `name` is the output file's stem and the label the harnesses print. */
typedef struct {
    const char *name;
    const char *cubestring;  /* exactly one of these two names the cube the job runs on */
    const char *scramble;    /* algorithm, e.g. "R U R'" */
    const char *subgroup;
    const char *goalstring;
    const char *outpath;
    int  depth;
    int  opt, dump, quiet;
} Job;

static Job job_default(void)
{
    Job j;
    j.name       = "";
    j.cubestring = NULL;
    j.scramble   = NULL;
    j.subgroup   = FACE_LET;
    j.goalstring = CUBE_SOLVED;
    j.outpath    = NULL;
    j.depth      = DEFAULT_DEPTH;
    j.opt = j.dump = j.quiet = 0;
    return j;
}

/* one: --scramble takes one word; returns the count once its words are compacted out. */
static int split_scramble(char **tok, int n, int one, const char **scr, const char **err)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(tok[i], "--scramble")) continue;
        int e = i + 1;
        for (; e < n && !(tok[e][0] == '-' && tok[e][1] == '-') && !(one && e > i + 1); e++) ;
        /* No words is not an empty maneuver: it says so here, before job_resolve reads one */
        if (e == i + 1) { *err = "--scramble needs a maneuver"; return -1; }
        /* the positionals would read a second word as the name, depth and subgroup instead */
        if (one && e < n && tok[e][0] != '-') {
            *err = "--scramble takes one word: quote a maneuver that has spaces in it";
            return -1;
        }
        size_t len = 1;
        for (int k = i + 1; k < e; k++) len += strlen(tok[k]) + 1;
        char *p = malloc(len);
        if (!p) { fprintf(stderr, OOM_TEXT); exit(1); }
        p[0] = 0;
        for (int k = i + 1; k < e; k++) {
            if (k > i + 1) strcat(p, " ");
            strcat(p, tok[k]);
        }
        *scr = p;
        int cnt = i;
        for (int k = e; k < n; k++) tok[cnt++] = tok[k];
        return cnt;
    }
    return n;
}

static const char *bad_token(const char *t)
{
    static __thread char buf[128];
    snprintf(buf, sizeof buf, "unknown or incomplete flag %s", t);
    return buf;
}

/* The fourth positional: not a flag at all, though it lands in the flag branch's words. */
static const char *bad_extra(const char *t)
{
    static __thread char buf[128];
    snprintf(buf, sizeof buf, "extra argument %s: a case is a name, a depth and a subgroup", t);
    return buf;
}

/* A valued flag at the end of the line: it is one of ours, and it is short of its value. */
static const char *bad_value(const char *t)
{
    static __thread char buf[128];
    snprintf(buf, sizeof buf, "%s needs a value", t);
    return buf;
}

/* A valued flag fails the same way whatever it is; *i ends on the value, so tok[*i-1] names it. */
static int job_value(char **tok, int n, int *i, const char **out, const char **err)
{
    if (*i + 1 >= n) { *err = bad_value(tok[*i]); return 1; }
    *out = tok[++(*i)];
    return 0;
}
static int job_int(char **tok, int n, int *i, int *out, const char **err)
{
    const char *s;
    if (job_value(tok, n, i, &s, err)) return 1;
    if (parse_int(s, out)) { *err = bad_number(tok[*i - 1], tok[*i]); return 1; }
    return 0;
}

/* An impossible run should not map the tables. */
static const char *bad_depth(int d)
{
    static __thread char buf[128];
    snprintf(buf, sizeof buf, "search depth %d exceeds the %d move solution buffer", d, MAX_DEPTH);
    return buf;
}

/* NaN compares false to everything, and casting one to int is undefined (INT_MIN). */
static int json_int_ok(double d)
{
    return d == d && d >= -2147483648.0 && d < 2147483648.0 && d == (double)(int)d;
}

static const char *subgroup_bad_letter(const char *sub)
{
    static __thread char buf[128];
    for (const char *p = sub; *p; p++)
        if (!strchr(REND_LET, *p)) {
            snprintf(buf, sizeof buf, "subgroup letter '%c' is not a move", *p);
            return buf;
        }
    return NULL;
}

/* 1: a value the case cannot take, *err says which; 2: the shape, which the usage answers. */
static int parse_job(char **tok, int n, Job *j, const char **err)
{
    char *pv[3];
    int np = 0;
    for (int i = 0; i < n; i++) {
        if (!strcmp(tok[i], "--threads")) {
            int v;
            if (job_int(tok, n, &i, &v, err)) return 2;
            if (v > THREAD_CAP) { *err = bad_range(tok[i - 1], v, 0, THREAD_CAP); return 1; }
            par_threads = v;
        }
        else if (!strcmp(tok[i], "--optimal")) j->opt = 1;
        else if (!strcmp(tok[i], "--dump"))    j->dump = 1;
        else if (!strcmp(tok[i], "--quiet"))   j->quiet = 1;
        else if (!strcmp(tok[i], "--cubestring")) {
            const char *s;
            if (job_value(tok, n, &i, &s, err)) return 2;
            j->cubestring = s;
            /* read here, as --goal is: refused before this case maps a pruning table */
            if (strlen(j->cubestring) != 54) { *err = cube_bad_length(j->cubestring); return 1; }
            /* a '?' is not a color: the completions ic_expand writes are what get read */
            if (!strchr(j->cubestring, '?')) {
                Cube cs;
                int csp = 0;
                const char *bad = read_cube(j->cubestring, &cs, &csp);
                if (bad) { *err = bad; return 1; }
            }
        }
        else if (!strcmp(tok[i], "--goal")) {
            const char *s;
            if (job_value(tok, n, &i, &s, err)) return 2;
            j->goalstring = s;
            Cube cg;
            int cgp = 0;
            const char *bad = read_cube(j->goalstring, &cg, &cgp);
            if (bad) { *err = bad; return 1; }
        }
        /* one dash too: "-verify" must not become the case's name */
        else if (tok[i][0] == '-') { *err = bad_token(tok[i]); return 2; }
        else if (np < 3) pv[np++] = tok[i];
        else { *err = bad_extra(tok[i]); return 2; }
    }
    /* both name a cube, and job_resolve would take the scramble and drop the other one silently */
    if (j->cubestring && j->scramble) {
        *err = "give --cubestring or --scramble, not both";
        return 2;
    }
    /* one alone reads as either the name or the depth, so it is refused rather than guessed */
    if (np == 1) {
        *err = "positionals: one is a name or a depth -- give none, or"
               " [<name> [<depth> [<subgroup>]]]";
        return 2;
    }
    /* Three words or two: the first word says which, since a name is not a number. */
    const char *dtok = NULL, *sub = NULL;
    int v = 0;
    if (np == 3) { j->name = pv[0]; dtok = pv[1]; sub = pv[2]; }
    else if (np == 2 && !parse_int(pv[0], &v)) { dtok = pv[0]; sub = pv[1]; }
    else if (np == 2) { j->name = pv[0]; dtok = pv[1]; }
    if (!dtok) return 0;                /* flags only: every default stands */
    /* parse_int, not atoi: "abc" as a depth would run a case that reports no solutions */
    if (parse_int(dtok, &v)) { *err = "depth must be a non-negative number"; return 1; }
    j->depth = v;
    if (j->depth > MAX_DEPTH) { *err = bad_depth(j->depth); return 1; }
    if (sub) {
        const char *bad = subgroup_bad_letter(sub);
        if (bad) { *err = bad; return 1; }
        j->subgroup = sub;
    }
    return 0;
}

/* A slice (E M S) has no face-turn spelling and forces STM; a wide letter rides FTM. */
static int job_slice_mode(const char *subgroup)
{
    for (const char *p = subgroup; *p; p++)
        if (strchr(SLICE_LET, *p)) return 1;
    return 0;
}

static void build_once(void)
{
    /* the GUI reaches this from two threads, so the second one waits for the first */
    static volatile long done = 0;      /* 0 = not built, 1 = building, 2 = built */
    if (InterlockedCompareExchange(&done, 1, 0)) {
        /* The build is long enough that the waiter polls, yielding the core to the builder */
        while (done != 2) Sleep(1);
        return;
    }
    for (int i = 0; i < 14; i++)
        for (int j = 0; j < 3; j++) {
            int v = (i / 3) * 3 + j;
            if (i % 3 == 2 && j == 0) v += 3;
            else if (i % 3 == 0 && j == 2) v -= 3;
            dist_tab[3 * i + j] = (s8)v;
        }
    for (int b = 0; b < 256; b++) {
        u16 v = 0;
        for (int k = 0; k < 5; k++) v |= (u16)(((b / POW3[k]) % 3) << (2 * k));
        trit_dec[b] = v;
    }
    build_move_cubes();
    build_token_names();
    build_symmetry_tables();
    InterlockedExchange(&done, 2);
}

static void load_report(const char *what, double t0)
{
    if (!tab_out) fprintf(progress_to_stderr ? stderr : stdout,
                          "%s tables loaded in %.1f s\n", what, now_sec() - t0);
}

/* Mapped once per process, which is the whole point of --batch. */
static void load_shared_tables(void)
{
    double t0 = now_sec();
    corners_move = load_file("move_corners");
    twist_move   = load_file("move_twist");
    flip_move    = load_file("move_flip");
    ss_move      = load_file("move_slice_sorted");
    clssym       = load_file("fs24_clssym");
    ctclssym     = load_file("ct_clssym");
    twist_conj   = load_file("conj_twist");
    load_report("shared", t0);
}

/* Per engine, and only one is live: mapping both would double the pages in play. */
static int tables_slice = -1;

/* ct_prun is read on every candidate the floor lets through, so it is held packed. */
static u8 *ct_pack(const u8 *src)
{
    /* CT_PRUN_BYTES is even, so the nibbles pair off with nothing over */
    size_t n = (size_t)CT_PRUN_BYTES, half = n / 2;
    u8 *dst = malloc(half);
    if (!dst) { fprintf(stderr, "out of memory packing ct_prun\n"); table_bail(); }
    for (size_t j = 0; j < half; j++)
        dst[j] = (u8)(src[2 * j] | (src[2 * j + 1] << 4));
    return dst;
}

static void ensure_tables(int slice)
{
    if (tables_slice == slice) return;
    double t0 = now_sec();
    /* nulled as they go: a half-unmapped engine is never observed */
    if (fsst)    { UnmapViewOfFile(fsst);    fsst    = NULL; }
    if (ctdepth) { free(ctdepth);            ctdepth = NULL; }
    if (ct_map)  { UnmapViewOfFile(ct_map);  ct_map  = NULL; }
    tables_slice = -1;      /* until both loads land: a bail here leaves nothing to return to */
    fsst   = load_file(slice ? "phase1x24_prun" : "phase1x24_prun_ftm");
    ct_map = load_file(slice ? "ct_prun"        : "ct_prun_ftm");
    ctdepth = ct_pack(ct_map);
    load_wall += now_sec() - t0;
    load_report("pruning", t0);
    tables_slice = slice;
}

static int out_failed;
static int out_lost;               /* 1: a solutions write did not land, stdout included */

/* One message for the first write that does not land: a deep case puts tens of MB through here. */
static void out_fail(const char *what)
{
    if (!out_failed) fprintf(stderr, CANNOT_WRITE, what);
    out_failed = 1;
}

static void emit_flush(void)
{
    if (!emit_used) return;
    /* a path case_out_path emptied has already said why, and  cannot write  would name nothing */
    if (outpath && *outpath && !outfh) {
        outfh = fopen(outpath, "wb");
        if (!outfh) { out_fail(outpath); outpath = NULL; }
    }
    /* stdout unless it is muted, and --out when that file is open */
    if (!silent_sol && fwrite(emit_buf, 1, emit_used, stdout) != emit_used) out_fail("stdout");
    if (outfh && fwrite(emit_buf, 1, emit_used, outfh) != emit_used) out_fail(outpath);
    emit_used = 0;
}

static int out_close(void)
{
    emit_flush();
    int bad = out_failed;
    if (outfh) {
        if (fclose(outfh) != 0) { out_fail(outpath); bad = 1; }
    } else if (outpath) {
        FILE *f = fopen(outpath, "wb");
        if (!f) { out_fail(outpath); bad = 1; }
        else if (fclose(f) != 0) { out_fail(outpath); bad = 1; }
    }
    outfh = NULL;
    outpath = NULL;
    out_failed = 0;
    out_lost = bad;
    return bad;
}

typedef struct {
    int UD_twist, RL_twist, FB_twist;
    int UD_flip,  RL_flip,  FB_flip;
    int UD_ss,    RL_ss,    FB_ss;
    int corners;
    int UD_d, RL_d, FB_d;
    int from;                     /* max of the three distances: the first depth worth walking */
} Start;

/* `built_cs` is the caller's and outlives the call, so the return may point into it. */
static const char *job_resolve(const Job *j, char *built_cs)
{
    const char *subgroup = j->subgroup;
    slice_mode = job_slice_mode(subgroup);
    if (j->depth > MAX_DEPTH) {
        fprintf(stderr, "%s\n", bad_depth(j->depth));
        return NULL;
    }

    /* refused before this case maps a table: a line with no cube has nothing to load one for */
    if (!j->cubestring && !(j->scramble && *j->scramble)) {
        fprintf(stderr, "%s\n", j->scramble ? "--scramble is empty"
                                            : "no cube: give --cubestring or --scramble");
        return NULL;
    }

    build_once();
    /* The STM search data follows from the letters and needs tok_ak[] to read the moves. */
    if (slice_mode) sg_build(subgroup, tok_ak);

    /* the printed cube, a slice's rotation still in its centers: --scramble builds it as written */
    if (j->scramble && *j->scramble) {
        const char *serr = cube_scramble(j->scramble, built_cs);
        if (serr) { fprintf(stderr, "scramble %s: %s\n", j->scramble, serr); return NULL; }
    } else {
        memcpy(built_cs, j->cubestring, 55);
    }
    return built_cs;
}

/* NULL, or the error for the caller to print. */
static const char *start_coords(const char *cubestring, const char *goalstring, Start *S)
{
    Cube cc0, ccg;
    int c0 = 0, cg = 0;
    const char *err = read_cube(cubestring, &cc0, &c0);
    if (err) return err;

    /* The goal is renamed as the start was: cc mixes the two, so half-renamed is neither cube. */
    {
        char gcs[55];
        snprintf(gcs, sizeof gcs, "%s", goalstring);
        err = cube_canonical(gcs, NULL);
        if (err) return err;
        err = read_cube(gcs, &ccg, &cg);
        if (err) return err;
    }

    Cube cc;                                   /* cc = ccg^-1 */
    for (int e = 0; e < 12; e++) cc.ep[ccg.ep[e]] = (s8)e;
    for (int e = 0; e < 12; e++) cc.eo[e] = ccg.eo[cc.ep[e]];
    for (int c = 0; c < 8;  c++) cc.cp[ccg.cp[c]] = (s8)c;
    for (int c = 0; c < 8;  c++) {
        int ori = ccg.co[cc.cp[c]];
        if (ori >= 3) cc.co[c] = (s8)ori;
        else          cc.co[c] = (s8)(ori ? 3 - ori : 0);
    }
    multiply(&cc, &cc0);                        /* cc = ccg^-1 * cc0: what the walk runs on */
    err = verify(&cc, c0 ^ cg);                /* the product's parity is the XOR */
    if (err) return err;

    S->UD_twist = get_twist(&cc);
    S->UD_flip  = get_flip(&cc);
    S->UD_ss    = get_slice_sorted(&cc);
    S->corners  = get_corners(&cc);

    Cube ss1 = symCube[16];
    multiply(&ss1, &cc);
    multiply(&ss1, &symCube[32]);              /* symCube[16]*cc*symCube[16]^-1 */
    S->RL_twist = get_twist(&ss1); S->RL_flip = get_flip(&ss1); S->RL_ss = get_slice_sorted(&ss1);

    Cube ss2 = symCube[32];
    multiply(&ss2, &cc);
    multiply(&ss2, &symCube[16]);
    S->FB_twist = get_twist(&ss2); S->FB_flip = get_flip(&ss2); S->FB_ss = get_slice_sorted(&ss2);

    S->UD_d = phasex24_depth(S->UD_ss, S->UD_flip, S->UD_twist);
    S->RL_d = phasex24_depth(S->RL_ss, S->RL_flip, S->RL_twist);
    S->FB_d = phasex24_depth(S->FB_ss, S->FB_flip, S->FB_twist);
    S->from = S->UD_d > S->RL_d ? (S->UD_d > S->FB_d ? S->UD_d : S->FB_d)
                                : (S->RL_d > S->FB_d ? S->RL_d : S->FB_d);
    return NULL;
}

/* Whether `a` opens the deeper walk; ties fall through to the next distance, then the smallest. */
static int opens_deeper(const Start *a, const Start *b)
{
    int x[3] = { a->UD_d, a->RL_d, a->FB_d };
    int y[3] = { b->UD_d, b->RL_d, b->FB_d };
    for (int i = 0; i < 2; i++)
        for (int k = i + 1; k < 3; k++) {
            if (x[i] < x[k]) { int t = x[i]; x[i] = x[k]; x[k] = t; }
            if (y[i] < y[k]) { int t = y[i]; y[i] = y[k]; y[k] = t; }
        }
    for (int i = 0; i < 3; i++)
        if (x[i] != y[i]) return x[i] > y[i];
    return 0;
}

/* Returns the move count, 0 when the subgroup names no face turn. */
static int build_move_list(const char *subgroup, int searchdepth)
{
    memset(ban_tab, 0, sizeof ban_tab);
    if (slice_mode) {
        /* STM: subgroup.h holds the set, bans, costs and rho; the two conjugates are ours */
        nmoves_in = sg_n;
        for (int i = 0; i < nmoves_in; i++) {
            int col = sg_col[i];
            mv_col[i] = col;
            /* an axis's 3 A columns, then its 3 B, then the 3 that turn both faces */
            mv_grp[i] = 3 * (col / N_MOVE_PER_AXIS) + (col % N_MOVE_PER_AXIS) / 3;
        }
        stdmax_eff = searchdepth;
    } else {
        /* FTM: wide w on f is opp_face[f] . rot, so the orbit is reached; face-only, the names. */
        nmoves_in = 0;
        int face_reach[6] = {0,0,0,0,0,0};
        int axes[4] = {0,0,0,0};                    /* axes[1]=x, axes[2]=y, axes[3]=z present */
        static const int opp_face[6] = {3,4,5,0,1,2};  /* U<->D, R<->L, F<->B */
        static const int rot_face[4][6] = {        /* whole-cube 90 deg face maps (faces 0..5) */
            {0,0,0,0,0,0},                          /* unused slot */
            {2,1,3,5,4,0},                          /* x: U->F, F->D, D->B, B->U */
            {0,2,4,3,5,1},                          /* y: R->F, F->L, L->B, B->R */
            {1,3,2,4,0,5}                           /* z: U->R, R->D, D->L, L->U */
        };
        /* EMS and xyz name no face turn, so only the six faces and their wide twins are read. */
        for (int f = 0; f < 6; f++) {
            if (strchr(subgroup, FACE_LET[f])) face_reach[f] = 1;
            if (!strchr(subgroup, REND_LET[R_WU + f])) continue;
            /* a wide's face-turn part is on the opposite face: r = L x, u = D y */
            face_reach[opp_face[f]] = 1;
            axes[(f == R_R || f == R_L) ? 1 : (f == R_F || f == R_B) ? 3 : 2] = 1;
        }
        int changed = 1;
        while (changed) {
            changed = 0;
            for (int ax = 1; ax <= 3; ax++) if (axes[ax])
                for (int f = 0; f < 6; f++) if (face_reach[f]) {
                    int g = rot_face[ax][f];
                    if (!face_reach[g]) { face_reach[g] = 1; changed = 1; }
                }
        }
        for (int v = 0; v < N_MOVE_FTM; v++) {
            int face = v / 3;
            if (!face_reach[face]) continue;
            mv_col[nmoves_in] = ftm_col[v];
            /* a face, not an axis: the ban below reads +/-3 as the opposite face */
            mv_grp[nmoves_in] = face;
            nmoves_in++;
        }
        /* the FTM ban rule, as a mask: no same face, no reversed opposite pair */
        for (int a = 0; a < nmoves_in; a++)
            for (int b = 0; b < nmoves_in; b++) {
                int d = mv_grp[a] - mv_grp[b];
                if (d == 0 || d == (sol_rev ? -3 : 3))
                    ban_tab[mv_col[a]] |= 1ULL << mv_col[b];
            }
    }
    for (int i = 0; i < nmoves_in; i++) {
        int e = i;
        while (e + 1 < nmoves_in && mv_grp[e + 1] == mv_grp[i]) e++;
        mv_grp_end[i] = (unsigned char)e;
    }
    /* RL and FB hold it conjugated by symCube[16] and its square, which cycles the axes. */
    for (int i = 0; i < nmoves_in; i++) {
        mv_mrl[i] = (mv_col[i] + 2 * N_MOVE_PER_AXIS) % N_MOVE;
        mv_mfb[i] = (mv_col[i] + N_MOVE_PER_AXIS) % N_MOVE;
    }
    return nmoves_in;
}

/* 0 once the subgroup has given the walk moves; on 1 what it printed is the answer. */
static int job_move_list(const Job *j)
{
    if (build_move_list(j->subgroup, j->depth)) return 0;
    fprintf(stderr, NO_FACE_TURN_MSG);
    return 1;
}

/* The line both searches open the run with: the incomplete one puts its own two lines above it. */
static void print_run_line(const Job *j, int from)
{
    char thbuf[32] = "";
    if (par_threads > 1) snprintf(thbuf, sizeof thbuf, ", %d threads", par_threads);
    printf("subgroup  : %s (%s engine, %d moves), searchdepth %d, from %d%s%s\n",
           j->subgroup, slice_mode ? "STM" : "FTM", nmoves_in, j->depth, from,
           opt_mode ? ", only_optimal" : "", thbuf);
}

#define CS_LINE_FMT "cubestring: %s\n"

static void print_header(const Job *j, const char *cubestring, int from)
{
    printf(CS_LINE_FMT, cubestring);
    print_run_line(j, from);
}

    /* the top split plies of each start, expanded serially into one counter-pulled task list */
static long long par_pass(const Start *S, int nm, int togo, int split, long long pass_est,
                          long long *nsol_out, unsigned long long *hash_out)
{
    /* the frontier is measured a ply at a time: the last count is the split that stays */
    long long target = SPLIT_TASKS_PER_THREAD * (par_threads > 1 ? par_threads : 1);
    if (pass_est / SPLIT_TASK_TARGET_NODES > target)
        target = pass_est / SPLIT_TASK_TARGET_NODES;
    if (split > 1) split = 1;          /* the ply walk starts at the top: the count decides */
    long long nodes = 0, nsol = 0;
    unsigned long long hash = 0;
    Thr coll;
    for (;;) {
        ntasks = 0;
        next_task = 0;
        nodes = 0;
        nsol = 0;
        hash = 0;
        for (int m = 0; m < nm; m++) {
            if (togo < S[m].from) continue;
            memset(&coll, 0, sizeof coll);
            coll.split = split;
            search(&coll, S[m].UD_flip, S[m].RL_flip, S[m].FB_flip,
                   S[m].UD_twist, S[m].RL_twist, S[m].FB_twist,
                   S[m].UD_ss, S[m].RL_ss, S[m].FB_ss, S[m].corners,
                   S[m].UD_d, S[m].RL_d, S[m].FB_d, togo, 0, 1u, 0);
            nodes += coll.nodecount;
            nsol  += coll.nsol;
            hash  += coll.solhash;
        }
        if (split == 0 || ntasks == 0 || engine_stop ||
            ntasks >= target || split >= togo - 1 || split >= 6) {
            int nthr = par_threads < ntasks ? par_threads : ntasks;
            par_fanout(par_worker, nthr, &nodes, &nsol, &hash);
            *nsol_out = nsol;
            *hash_out = hash;
            return nodes;
        }
        split++;
    }
}

/* One start per completion, walked a layer at a time: a shallower answer retires the deeper one. */
static void run_passes(const Start *S, int nm, int searchdepth, int from,
                       long long *nsol_out, unsigned long long *hash_out)
{
    opt_found = 0;
    long long prev_nodes = 0;
    for (int togo = from; togo <= searchdepth; togo++) {
        /* checked twice: a Stop during the table mapping never pays for a pass it cannot use */
        if (engine_stop) break;
        /* gated on a node count, not a duration: the choice stays reproducible */
        int sp = par_threads > 1 ? split_for(togo, prev_nodes, nm) : 0;
        /* One list for every completion, but only once they are split and --optimal is off. */
        int batch = (sp && !opt_mode) ? nm : 1;
        long long pass_est = nm ? prev_nodes / nm * nmoves_in / 2 : 0;
        double t1 = now_sec();
        long long nodes = 0, pnsol = 0, nsol_before = *nsol_out;
        unsigned long long phash = 0;
        for (int m = 0; m < nm; m += batch) {
            opt_found = 0;
            long long nd = par_pass(S + m, batch, togo, sp, pass_est, &pnsol, &phash);
            totnodes  += nd;
            nodes     += nd;
            *nsol_out += pnsol;
            *hash_out += phash;
            /* One completion has supplied the shortest: the rest have nothing left to say. */
            if (opt_mode && !render_mode && *nsol_out > nsol_before) break;
        }
        prev_nodes = nodes;
        print_depth_line(togo, *nsol_out, nodes, t1);
        if (engine_stop) break;
        /* --optimal ends on a solution found, or on togo passing the shortest rendering */
        if (opt_mode) {
            if (!render_mode) { if (*nsol_out > nsol_before) break; }
            else if (rend_on && togo >= rend_mincm) break;
        }
    }
}

/* solver's rule: --optimal keeps the shortest converted band, one of it; -1 keeps them all. */
static int std_keep(void)
{
    return opt_mode ? rend_mincm : -1;
}

/* 1 = no row, so the job failed. */
static int emit_tab_row(const Job *j, long long nsol, unsigned long long hash,
                        double t_wall, double dt)
{
    emit_drain();                       /* this thread's batch, before the counts read the set */
    char lens[256];
    int p = 0;
    for (int L = 0; L < MOVE_CAP && p < (int)sizeof lens - 12; L++)
        if (len_seen[L].v) p += snprintf(lens + p, sizeof lens - p, "%s%d", p ? "," : "", L);
    lens[p] = 0;
    int keep = std_keep();
    size_t nstd = 0;
    for (size_t i = 0; i < rend_on; i++) {
        if (keep >= 0 && rend_ent[i].cm != keep) continue;
        nstd++;
        if (opt_mode) break;
    }
    char hbuf[24];
    snprintf(hbuf, sizeof hbuf, "%016llx", hash);
    char row[512];
    const char *shown = j->name ? j->name : "case";
    /* "-" unless --solhash is on: hashing every solution is not free */
    int w = snprintf(row, sizeof row, "%s\t%d\t%s\t%d\t%lld\t%zu\t%s\t%lld\t%s\t%.4f\t%.4f\n",
                     shown, j->depth, j->subgroup, nmoves_in,
                     nsol, nstd, lens, totnodes, hash_wanted ? hbuf : "-", t_wall, dt);
    free(tab_row);                      /* the previous job's row, unless run_batch took it */
    tab_row = NULL;
    /* dropped, not truncated: a cut row would still read as a whole measurement */
    if (w >= (int)sizeof row) {
        fprintf(stderr, "the row for case %s does not fit: its name is too long\n",
                shown);
        return 1;
    }
    tab_row = malloc((size_t)w + 1);
    if (!tab_row) { fprintf(stderr, OOM_TEXT); return 1; }
    memcpy(tab_row, row, (size_t)w + 1);
    return 0;
}

struct rend_sort_arg { int t; };

static void rend_sort_one(void *arg)
{
    rend_sort_run(((struct rend_sort_arg *)arg)->t);
}

typedef void (*rend_task_fn)(void *);
struct rend_pool { rend_task_fn fn; void *slot; size_t sz; int at; };

static unsigned __stdcall rend_pool_worker(void *arg)
{
    struct rend_pool *p = arg;
    p->fn((char *)p->slot + (size_t)p->at * p->sz);
    return 0;
}

/* One thread per task, slot 0 on this one; a thread that will not start runs its task here */
static void rend_parallel(rend_task_fn fn, void *slot, size_t sz, int ntask)
{
    struct rend_pool p[REND_MAXRUN];
    HANDLE th[REND_MAXRUN];
    int made = 0;
    for (int i = 0; i < ntask; i++) {
        p[i].fn = fn; p[i].slot = slot; p[i].sz = sz; p[i].at = i;
    }
    for (int i = 1; i < ntask; i++) {
        th[made] = (HANDLE)_beginthreadex(NULL, 0, rend_pool_worker, &p[i], 0, NULL);
        if (!th[made]) break;
        made++;
    }
    rend_pool_worker(&p[0]);
    if (made && WaitForMultipleObjects((DWORD)made, th, TRUE, INFINITE) == WAIT_FAILED)
        fprintf(stderr, "cannot join the sorting threads (error %lu)\n",
                (unsigned long)GetLastError());
    for (int i = 0; i < made; i++) CloseHandle(th[i]);
    for (int i = made + 1; i < ntask; i++) rend_pool_worker(&p[i]);
}

/* Past this a further level costs more than the qsort saves; two to the n keeps it even */
#define REND_RUNS_CAP 16

static void rend_sort_par(void)
{
    /* No order asked for: rend_nth then walks the order the search found. */
    if (!rend_do_sort) return;
    int want = par_threads;
    if (want > REND_RUNS_CAP) want = REND_RUNS_CAP;
    int most = (int)(rend_on / REND_SORT_MIN);
    if (want > most) want = most;
    int nrun = rend_sort_runs(want > 1 ? want : 1);
    if (!nrun) return;
    if (nrun == 1) { rend_sort_run(0); return; }
    struct rend_sort_arg a[REND_MAXRUN];
    for (int t = 0; t < nrun; t++) a[t].t = t;
    rend_parallel(rend_sort_one, a, sizeof(*a), nrun);

    size_t *tmp = rend_merge_tmp();
    if (!tmp) return;
    struct rend_merge m[REND_MAXRUN];
    size_t *cur = rend_ord, *out = tmp;
    while (nrun > 1) {
        int npair = (nrun + 1) / 2;
        for (int w = 0; w < npair; w++) {
            m[w].src = cur; m[w].dst = out; m[w].w = w; m[w].nrun = nrun;
        }
        rend_parallel(rend_merge_one, m, sizeof(*m), npair);
        for (int w = 0; w < npair; w++) {
            int lo = 2 * w;
            rend_rlo[w] = rend_rlo[lo];
            rend_rhi[w] = rend_rhi[lo + 1 < nrun ? lo + 1 : lo];
        }
        nrun = npair;
        size_t *s = cur; cur = out; out = s;
    }
    if (cur != rend_ord) memcpy(rend_ord, cur, rend_on * sizeof(*rend_ord));
    free(tmp);
}

/* Drop duplicate codes: adjacent-run on the sort path, hash set on --no-sort. */
static unsigned long long rend_keyhash(const uint8_t *p, size_t n)
{
    unsigned long long h = 1469598103934665603ULL;   /* FNV-1a */
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

static void rend_dedup(void)
{
    if (!rend_on) return;
    if (rend_ord) {                       /* sort path */
        size_t w = 0;
        for (size_t i = 1; i < rend_on; i++) {
            const RendEnt *a = &rend_ent[rend_ord[w]], *b = &rend_ent[rend_ord[i]];
            int same = a->klen == b->klen &&
                       (a->klen == 0 || !memcmp(rend_keyar + a->koff,
                                                rend_keyar + b->koff, a->klen));
            if (!same) rend_ord[++w] = rend_ord[i];
        }
        rend_on = w + 1;
        return;
    }
    /* --no-sort: hash-set dedup in discovery order. */
    size_t *ord = rend_xrealloc(NULL, rend_on * sizeof(*ord));
    if (!ord) return;                     /* OOM: keep the raw set */
    size_t cap = 1;
    while (cap < rend_on * 2) cap *= 2;
    size_t *tab = rend_xrealloc(NULL, cap * sizeof(*tab));
    size_t w = 0;
    if (tab) {
        for (size_t i = 0; i < cap; i++) tab[i] = (size_t)-1;
        for (size_t i = 0; i < rend_on; i++) {
            const uint8_t *kb = rend_ent[i].klen ? rend_keyar + rend_ent[i].koff : NULL;
            size_t s = rend_keyhash(kb, rend_ent[i].klen) & (cap - 1);
            int dup = 0;
            while (tab[s] != (size_t)-1) {
                size_t j = tab[s];
                if (rend_ent[j].klen == rend_ent[i].klen &&
                    (rend_ent[i].klen == 0 ||
                     !memcmp(rend_keyar + rend_ent[j].koff, kb, rend_ent[i].klen))) {
                    dup = 1; break;
                }
                s = (s + 1) & (cap - 1);
            }
            if (!dup) { tab[s] = i; ord[w++] = i; }
        }
        free(tab);
    } else {                              /* OOM: same result, O(n^2) */
        for (size_t i = 0; i < rend_on; i++) {
            int dup = 0;
            for (size_t j = 0; j < w; j++) {
                size_t k = ord[j];
                if (rend_ent[k].klen == rend_ent[i].klen &&
                    (rend_ent[i].klen == 0 ||
                     !memcmp(rend_keyar + rend_ent[k].koff,
                             rend_keyar + rend_ent[i].koff, rend_ent[i].klen))) {
                    dup = 1; break;
                }
            }
            if (!dup) ord[w++] = i;
        }
    }
    rend_ord = ord;                       /* discovery order */
    rend_on = w;
}

static void report_block(const Job *j, long long nsol, unsigned long long hash,
                         double t_wall, double dt)
{
    emit_drain();
    printf("  search     : %.2f s\n", dt);
    printf("  nodes      : %lld\n", totnodes);
    printf("  solutions  : %lld\n", nsol);
    if (hash_wanted) printf("  solhash    : %016llx\n", hash);
    printf("  wall       : %.2f s\n", t_wall);
    /* A shallow search can finish inside one tick, and dt is then 0. */
    /* The rendering is in dt, so this is not the search alone: modes are not comparable. */
    printf("  node rate  : %.2f M nodes/s\n", totnodes / (dt + 0.0001) / 1e6);

    if (render_mode) {
        double t1 = now_sec();
        int keep = std_keep();
        /* Nothing receives the lines, so the order is not worth buying: emit_line drops them. */
        int sorted = emit_kept();
        if (sorted) rend_sort_par();
        rend_dedup();
        size_t nstd = 0;
        for (size_t i = 0; i < rend_on; i++) {
            size_t e = rend_nth(i);
            if (keep >= 0 && rend_ent[e].cm != keep) continue;
            emit_line(rend_text(e));
            nstd++;
            if (opt_mode) break;
        }
        emit_flush();          /* the lines come out before the count that counts them */
        if (sorted && rend_do_sort)
            printf("  standard   : %zu (sorted in %.2f s)\n", nstd, now_sec() - t1);
        else
            printf("  standard   : %zu\n", nstd);
        if (!nstd && nsol)
            printf("  no standard spelling: each ends in a rotation this subgroup cannot write\n");
        else if (spelled_sol < nsol)
            printf("  %lld of the %lld solutions have no standard spelling in this subgroup\n",
                   nsol - spelled_sol, nsol);
    }
    int lost = out_close();
    if (silent_sol && j->outpath) {
        if (lost) fprintf(stderr, "not written in full: %s\n", j->outpath);
        else      printf("  written to : %s\n", j->outpath);
    }
}

#include "incomp.h"           /* here, not at the top: it needs the move list and the emit path */

/* Set when rend_oom runs: it stops the search, and the caller reports the run as failed. */
static int g_run_oom;

/* One line per case: every worker on its way out lands here, so g_run_oom is the gate. */
static int run_rend_oom(size_t want)
{
    if (!g_run_oom) fprintf(stderr, REND_OOM_FMT, want / 1048576);
    g_run_oom = 1;
    engine_stop = 1;
    return 0;
}

static int run_job(const Job *j)
{
    double t_wall = now_sec();
    /* The case before this one may have run out of memory, and the stop it set outlived it. */
    if (g_run_oom) engine_stop = 0;
    g_run_oom = 0;
    if (!rend_oom) rend_oom = run_rend_oom;
    load_wall = 0;
    if (par_threads == 0) par_threads = cpu_count();

    opt_mode    = j->opt;
    dump_mode   = j->dump;
    /* Row mode always renders: the standard count is one of its columns. */
    render_mode = !j->dump || tab_out;
    sol_rev     = 0;
    totnodes  = 0;
    spelled_sol = 0;
    for (int L = 0; L < MOVE_CAP; L++) len_seen[L].v = 0;
    outfh     = NULL;
    outpath   = j->outpath;
    /* a file captures the list, so stdout need not repeat; --quiet forces it off */
    silent_sol = tab_out ? 1 : (sol_std == 0 ? 0 : (j->quiet || sol_std > 0 || outpath != NULL));

    char built_cs[55] = {0};
    const char *cubestring = job_resolve(j, built_cs);
    if (!cubestring) return 1;

    /* A '?' makes the cube incomplete; no maneuver solves two completions: it makes them equal */
    /* static: IC_K_CAP of them is more than a stack frame should hold */
    static char mcs[IC_K_CAP][55];
    int nm = 1;
    if (strchr(cubestring, '?')) {
        nm = ic_expand(built_cs, mcs);
        if (!nm) return run_incomplete(j, built_cs) || g_run_oom;
    } else memcpy(mcs[0], built_cs, 55);

    /* Before the header and any start: relaxed only wins past IC_WAYS_CUT, corners all known */
    if (nm > IC_WAYS_CUT && ic_in.nkc == 8)
        return run_incomplete(j, built_cs) || g_run_oom;

    /* a subgroup that names no face turn is refused without mapping a pruning table */
    if (job_move_list(j)) return 1;

    /* the printed cube carries a slice's rotation; the search's frame has fixed centers */
    int tail = 0;                                   /* the rotation the rename took away */
    static Start S[IC_K_CAP];
    for (int k = 0; k < nm; k++) {
        const char *crerr = cube_canonical(mcs[k], &tail);
        if (crerr) { fprintf(stderr, "%s\n", crerr); return 1; }
    }
    /* The renamed goal is what the goal-side walk opens on; its own rotation is what counts. */
    char gcs[55];
    int gtail = 0;
    snprintf(gcs, sizeof gcs, "%s", j->goalstring);
    cube_canonical(gcs, &gtail);

    if (render_mode) rend_init(j->subgroup, j->depth);
    /* what the frame took from the start, less the goal's own turns; must follow rend_init */
    rend_boost = tail;
    const unsigned char *gt = ROT_TAIL[gtail];
    int gtn = 0;
    while (gt[gtn]) gtn++;
    for (int k = gtn - 1; k >= 0; k--)              /* the goal's own turns, undone back to front */
        rend_boost = rend_resolve(rend_boost, (uint8_t)((gt[k] & ~3u) | (4u - (gt[k] & 3u))));
    /* A refusal costs no table: the rotations it asks about are known before any is mapped. */
    if (render_mode && rend_boost && !rend_wides && !rend_slices && !rend_tail_ok[rend_boost]) {
        fprintf(stderr, "the cube is held turned and %s cannot write that rotation\n",
                j->subgroup);
        return 1;
    }

    /* One set of pruning tables, and every completion's walk shares it: the walks read it below */
    ensure_tables(slice_mode);

    for (int k = 0; k < nm; k++) {
        const char *err = start_coords(mcs[k], j->goalstring, &S[k]);
        if (err) { fprintf(stderr, "%s\n", err); return 1; }
    }
    /* Phase-1 is not inverse-invariant, so the goal side is another tree, needing fixed centers. */
    if (nm == 1 && !tail && !gtail) {
        /* start_coords renames the goal too, but has nowhere to carry that rotation */
        Start R;
        if (!start_coords(gcs, mcs[0], &R) && opens_deeper(&R, &S[0])) {
            S[0]    = R;
            sol_rev = 1;
            /* Inverting swaps each opposite pair, so the reversed walk bans the other half. */
            if (!slice_mode) build_move_list(j->subgroup, j->depth);
        }
    }

    /* header shows the scramble as written: the rotated frame is internal, only copies rename */
    /* The walk starts at the shallowest completion's bound, so the line says the same depth. */
    int from = j->depth;
    for (int k = 0; k < nm; k++) if (S[k].from < from) from = S[k].from;

    /* A completion past j->depth is dropped, and "solutions: 0" reads the same either way. */
    if (nm > 1) {
        int untried = 0;
        for (int k = 0; k < nm; k++) if (S[k].from > j->depth) untried++;
        if (untried)
            fprintf(stderr, "%d of the %d completions need more than %d moves, "
                            "so this depth never reached them\n", untried, nm, j->depth);
    }

    if (!tab_out) print_header(j, built_cs, from);

    double t_all = now_sec();
    long long nsol_total = 0;
    unsigned long long hash_total = 0;
    run_passes(S, nm, j->depth, from, &nsol_total, &hash_total);
    double dt = now_sec() - t_all;
    double t_wall_total = now_sec() - t_wall - load_wall;

    if (tab_out) {
        int lost_row = emit_tab_row(j, nsol_total, hash_total, t_wall_total, dt);
        /* the "written to" line is per-job noise in a table, so row mode stops here */
        out_close();
        return lost_row || out_lost || g_run_oom;
    }
    report_block(j, nsol_total, hash_total, t_wall_total, dt);
    return out_lost || g_run_oom;
}

/* --batch: one job per line of the file */

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (!p) { fprintf(stderr, OOM_TEXT); exit(1); }
    memcpy(p, s, n);
    return p;
}

/* run_batch owns both arrays, and every line copy and scramble in them */
static void batch_jobs_free(Job *jobs, char **lines, int nj)
{
    for (int i = 0; i < nj; i++) {
        free((char *)jobs[i].scramble);
        free(lines[i]);
    }
    free(jobs);
    free(lines);
}

/* The batch protocol's three markers: a case opens with JOB, closes with END, or with FAIL. */
#define JOB_MARK  "### JOB %s\n"
#define FAIL_MARK "### FAIL %s\n"
#define END_MARK  "### END %s\n"

/* One line cap for both readers: --batch's job lines here, the GUI's text mode. */
#define JOB_LINE_MAX 8192

/* No shell stands behind a job line, so the quotes a command line needed are one turn too many. */
static void unquote(char *t)
{
    if (*t == '"') memmove(t, t + 1, strlen(t));    /* the line was cut in place: t ends in NUL */
    size_t len = strlen(t);
    if (len && t[len - 1] == '"') t[len - 1] = 0;
}

static int run_batch(const char *path, const char *outdir) __attribute__((unused));
static int run_batch(const char *path, const char *outdir)
{
    /* --batch's stdout is its blocks, and a line about a table belongs to no case in the file */
    progress_to_stderr = 1;
    FILE *fh = fopen(path, "rb");
    if (!fh) { fprintf(stderr, CANNOT_OPEN, path); return 1; }
    skip_bom(fh);

    Job *jobs = NULL;
    char **lines = NULL;
    int nj = 0, cap = 0, skipped = 0;
    char *copy = NULL;                  /* the line in hand; every refusal frees it */
    char line[JOB_LINE_MAX];
    int lineno = 0;
    for (;;) {
        const char *why = NULL;
        int got = read_line(line, sizeof line, fh, &why);
        if (!got) break;
        lineno++;
        /* a file that stopped being readable is not one the rest of the cases can be read from */
        if (got == -2) {
            fprintf(stderr, BAD_LINE_FMT, path, lineno, why);
            goto fail;
        }
        /* one line that will not read is left out, the way --scramble-list leaves one out */
        if (got < 0) {
            fprintf(stderr, BAD_LINE_FMT, path, lineno, why);
            skipped++;
            continue;
        }
        copy = xstrdup(line);
        /* a field is at least one character and the space after it, so the widest line fits */
        char *tv[sizeof line / 2 + 1];
        int n = 0;
        char *p = copy;
        for (; *p; ) {
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;
            tv[n++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = 0;
            unquote(tv[n - 1]);
        }
        if (n == 0 || tv[0][0] == '#') { free(copy); copy = NULL; continue; }
        if (n < 3) { fprintf(stderr, "%s line %d: bad job line (name depth subgroup [--cubestring"
                                      " <cs> | --scramble <alg>]): %s\n", path, lineno, copy);
                     goto fail; }
        if (nj == cap) {
            int ncap = cap ? cap * 2 : 16;
            Job *njobs = realloc(jobs, (size_t)ncap * sizeof *jobs);
            char **nlines = realloc(lines, (size_t)ncap * sizeof *lines);
            /* one that grew freed the block it replaced, so it is adopted before anything else */
            if (njobs) jobs = njobs;
            if (nlines) lines = nlines;
            if (!njobs || !nlines) { fprintf(stderr, OOM_TEXT); goto fail; }
            cap = ncap;
        }
        jobs[nj] = job_default();
        lines[nj] = copy;
        const char *jname = tv[0];
        const char *err = NULL;
        const char *scr = NULL;
        int m = split_scramble(tv, n, 0, &scr, &err);
        jobs[nj].scramble = scr;
        /* Unlike a line that would not read: one job that will not parse ends the file */
        if (m < 0) {
            fprintf(stderr, "%s line %d: job %s: %s\n", path, lineno, jname, err);
            goto fail;
        }
        if (parse_job(tv, m, &jobs[nj], &err)) {
            fprintf(stderr, "%s line %d: job %s: %s\n", path, lineno, jname, err);
            free((char *)jobs[nj].scramble);
            goto fail;
        }
        /* a line in a file should hear this before the whole file's tables are mapped */
        if (!jobs[nj].cubestring && !(jobs[nj].scramble && *jobs[nj].scramble)) {
            fprintf(stderr, "%s line %d: job %s: %s\n", path, lineno, jname,
                    jobs[nj].scramble ? "--scramble is empty"
                                      : "no cube: give --cubestring or --scramble");
            free((char *)jobs[nj].scramble);
            goto fail;
        }
        /* a '?' cube is ic_parse's to refuse, and ic_parse maps no table */
        if (jobs[nj].cubestring && strchr(jobs[nj].cubestring, '?')) {
            const char *bad = incomp_problem(jobs[nj].cubestring, NULL, NULL, NULL);
            if (bad) {
                fprintf(stderr, "%s line %d: job %s: %s\n", path, lineno, jname, bad);
                free((char *)jobs[nj].scramble);
                goto fail;
            }
        }
        copy = NULL;                    /* lines[nj] owns it now: batch_jobs_free frees it */
        nj++;
    }
    fclose(fh);
    fh = NULL;
    if (nj == 0) { fprintf(stderr, NO_JOBS_MSG, path); goto fail; }
    /* one load serves every case in the file, so a file nothing came of asks for none */
    load_shared_tables();
    /* --batch files each case under --out; --bench prints a table and writes no file */
    const char *dir = outdir ? outdir : OUT_DIR_DEFAULT;
    if (!tab_out && nj && !make_outdir(dir)) goto fail;
    for (int i = 0; i < nj && !tab_out; i++) {
        char *p = case_out_alloc(dir, i + 1, *jobs[i].name ? jobs[i].name : NULL);
        if (!p) { fprintf(stderr, OOM_TEXT); goto fail; }
        jobs[i].outpath = p;
    }
    /* the second pass's tables are mapped lazily; this keeps a failure out of a ### JOB block */
    for (int pass = 0; pass < 2; pass++) {
        int wanted = 0;
        for (int i = 0; i < nj && !wanted; i++)
            wanted = job_slice_mode(jobs[i].subgroup) == (pass == 0);
        if (!wanted) continue;
        const char *a = pass == 0 ? "phase1x24_prun" : "phase1x24_prun_ftm";
        const char *b = pass == 0 ? "ct_prun"        : "ct_prun_ftm";
        if (ts_size_ok(a, PHASE1X24_BYTES) && ts_size_ok(b, CT_PRUN_BYTES)) continue;
        fprintf(stderr, "%s and %s: a case in %s needs them; build them first: mktables.exe\n",
                a, b, path);
        goto fail;
    }

    int bad = 0;
    /* Two passes, STM then FTM: phase1x24_prun follows the engine, so two loads at most */
    char **rows = NULL;
    if (tab_out) {
        rows = calloc((size_t)nj, sizeof *rows);
        if (!rows) { fprintf(stderr, OOM_TEXT); goto fail; }
    }
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < nj; i++) {
            if (job_slice_mode(jobs[i].subgroup) != (pass == 0)) continue;
            if (!tab_out) { printf(JOB_MARK, jobs[i].name); fflush(stdout); }
            free(tab_row);
            tab_row = NULL;
            if (run_job(&jobs[i])) {
                /* one row is one case's sample: a failure must not land in the table */
                fprintf(tab_out ? stderr : stdout, FAIL_MARK, jobs[i].name);
                bad++;
            } else if (tab_out) {
                rows[i] = tab_row;      /* kept in the file's order, so the table reads like it */
                tab_row = NULL;
            } else printf(END_MARK, jobs[i].name);
            fflush(stdout);
        }
    if (tab_out) {
        for (int i = 0; i < nj; i++)
            if (rows[i]) { fputs(rows[i], stdout); free(rows[i]); }
        free(rows);
        fflush(stdout);
    }
    free(tab_row);
    tab_row = NULL;
    for (int i = 0; i < nj; i++) free((char *)jobs[i].outpath);
    batch_jobs_free(jobs, lines, nj);
    if (bad)     fprintf(stderr, "%d job(s) failed\n", bad);
    if (skipped) fprintf(stderr, "%d job line(s) never ran\n", skipped);
    /* the counts go to stderr: the exit code carries only 0/1 */
    return (bad || skipped) ? 1 : 0;

fail:
    if (fh) fclose(fh);
    free(copy);
    batch_jobs_free(jobs, lines, nj);
    return 1;
}

#endif /* ENGINE_H */
