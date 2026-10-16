/* Cubolt (https://github.com/nbwzx/cubolt)
 * Copyright (C) 2026 Zixing Wang <zixingwang.cn@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* mktables.c -- the table builder: everything the on-disk tables come from. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "host.h"
#include "tablesum.h"
#include "version.h"

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#endif

#define INVALID32 0xffffffffu
#define INVALID8  255           /* what a table holds where INVALID32 means the same */

/* ---- options + file plumbing ---- */
/* The four flags a builder sees; --manifest, --version and --help return first. */
static int opt_check, opt_force, opt_quiet, opt_threads;
static int check_bad;   /* --check: something was missing or differed, so the run's answer is no */

static int worker_count(void)
{
    return opt_threads > 0 ? opt_threads : cpu_count();
}

/* Progress hook: the GUI's bar or the installer's; the per-table lines print either way. */
void (*g_mt_progress)(const char *table, uint64_t done, uint64_t total);

/* Set to abandon the plan: the BFS loops ask, so a table can be dropped part-way, unwritten. */
static volatile int mt_stop;

/* One table's size, 0 when it is not one of the files the engine maps. */
static unsigned long long mt_size(const char *name)
{
    long long n = ts_size(name);
    return n < 0 ? 0 : (unsigned long long)n;
}

#ifndef MKTABLES_EMBED
/* The installer waits on this run: CUBOLT_PROGRESS_HWND names its bar, reports drive it. */
#define PBM_SETPOS     (WM_USER + 2)
#define PBM_SETRANGE32 (WM_USER + 6)

static HWND g_prog;
static unsigned long long g_prog_total, g_prog_written;
static const char *g_prog_cur;
static DWORD g_prog_next_ms;

/* Posting cannot stall: the installer pumps its queue; a failed post says the window is gone. */
static void mt_prog_post(UINT m, WPARAM w, LPARAM l)
{
    if (g_prog && !PostMessage(g_prog, m, w, l)) g_prog = NULL;
}

static void mt_prog_report(const char *table, uint64_t done, uint64_t total)
{
    if (g_prog_cur && strcmp(g_prog_cur, table)) g_prog_written += mt_size(g_prog_cur);
    g_prog_cur = table;
    if (!g_prog) return;
    /* fs24 reports once per slice, nearly 12000 times; ten a second is all a bar shows */
    DWORD now_ms = GetTickCount();
    if (now_ms < g_prog_next_ms && done != total) return;
    g_prog_next_ms = now_ms + 100;
    /* the range goes out with every report: the bar's 0..100 default would clamp it to the top */
    mt_prog_post(PBM_SETRANGE32, 0, (LPARAM)g_prog_total);
    unsigned long long all = g_prog_written + (total ? mt_size(table) * done / total : 0);
    if (all > g_prog_total) all = g_prog_total;
    mt_prog_post(PBM_SETPOS, (WPARAM)all, 0);
}

static void mt_prog_open(void)
{
    const char *h = getenv("CUBOLT_PROGRESS_HWND");
    if (!h || !*h) return;
    g_prog = (HWND)(uintptr_t)strtoull(h, NULL, 10);
    for (int i = 0; i < TS_NFILES; i++) g_prog_total += (unsigned long long)TABLE_FILES[i].size;
    g_mt_progress = mt_prog_report;
}

/* Canceled mid-build: TerminateProcess, not exit: CRT teardown deadlocks against a writer. */
static char g_stop_path[1024];

static DWORD WINAPI mt_watch(void *arg)
{
    HANDLE inst = (HANDLE)arg;
    for (;;) {
        if (inst && WaitForSingleObject(inst, 100) != WAIT_TIMEOUT) break;
        if (g_stop_path[0] && GetFileAttributesA(g_stop_path) != INVALID_FILE_ATTRIBUTES) break;
        if (!inst) Sleep(100);
    }
    /* Nowhere else says why a build ended, and exit code 3 alone does not either */
    fprintf(stderr, "stopped: %s\n", g_stop_path[0] ? g_stop_path : "the installer exited");
    TerminateProcess(GetCurrentProcess(), 3);
    return 0;
}

static void mt_watch_installer(void)
{
    const char *s = getenv("CUBOLT_STOP_FILE");
    if (s && *s) snprintf(g_stop_path, sizeof g_stop_path, "%s", s);

    DWORD pid = 0;
    if (g_prog) GetWindowThreadProcessId(g_prog, &pid);
    HANDLE inst = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : NULL;
    if (!inst && !g_stop_path[0]) return;
    if (g_stop_path[0]) DeleteFileA(g_stop_path);   /* a stale marker must not end this build */
    /* th is closed at once; inst stays open, the watcher waits on it for the whole build */
    HANDLE th = CreateThread(NULL, 0, mt_watch, inst, 0, NULL);
    if (th) CloseHandle(th);
    else if (inst) CloseHandle(inst);
}
#endif

/* --check compares against what is there instead of writing. */
static void table_out(const char *name, const void *buf)
{
    size_t nbytes = (size_t)ts_size(name);       /* one spelling for a table's size: TABLE_FILES */
    const char *path = table_path(name);
    FILE *fh = fopen(path, "rb");
    /* --check: a table that is not there is a verdict, not something to build */
    if (opt_check && !fh) {
        printf("  %-22s MISSING\n", name);
        check_bad = 1;
        /* a verdict is work done too, or the bar stops on the last table that was there */
        if (g_mt_progress) g_mt_progress(name, (uint64_t)nbytes, (uint64_t)nbytes);
        return;
    }
    if (opt_check) {
        static unsigned char chunk[1 << 20];
        const unsigned char *b = buf;
        size_t done = 0;
        int differs = 0;
        while (done < nbytes) {
            size_t want = nbytes - done < sizeof chunk ? nbytes - done : sizeof chunk;
            size_t got = fread(chunk, 1, want, fh);
            if (got == 0) break;
            if (memcmp(chunk, b + done, got)) {
                for (size_t i = 0; i < got; i++)
                    if (chunk[i] != b[done + i]) {
                        printf("  %-22s DIFFERS at byte %zu (file %02x, built %02x)\n",
                               name, done + i, chunk[i], b[done + i]);
                        break;
                    }
                differs = check_bad = 1;
                break;
            }
            done += got;
        }
        /* a short file agreed on all it has, and a read stopped by an error looks the same */
        int short_file = (done < nbytes);
        int read_err   = ferror(fh);
        int long_file  = !short_file && !read_err && fgetc(fh) != EOF;
        fclose(fh);
        if (differs) return;
        if (read_err) {
            printf("  %-22s NOT CHECKED: the file stopped being readable at byte %zu\n",
                   name, done);
            check_bad = 1;
        } else if (short_file) {
            printf("  %-22s DIFFERS: the file holds %zu of %zu bytes\n", name, done, nbytes);
            check_bad = 1;
        } else if (long_file) {
            printf("  %-22s DIFFERS: the file is longer than %zu bytes\n", name, nbytes);
            check_bad = 1;
        } else
            printf("  %-22s IDENTICAL (%zu bytes)\n", name, nbytes);
        return;
    }
    /* Only a file that is whole is work already done; a damaged one is written, not skipped. */
    if (fh && !opt_force && ts_verify(name, (long long)nbytes) == TS_OK) {
        fclose(fh);
        printf("  %-22s exists, skipped (use --force)\n", name);
        /* reported done although skipped: otherwise the bar never moves on a complete set */
        if (g_mt_progress) g_mt_progress(name, (uint64_t)nbytes, (uint64_t)nbytes);
        return;
    }
    if (fh) fclose(fh);
    fh = fopen(path, "wb");
    if (!fh) { fprintf(stderr, CANNOT_WRITE, path); exit(1); }
    /* A failed write leaves a short file behind, so the wreckage goes rather than the run. */
    if (fwrite(buf, 1, nbytes, fh) != nbytes) {
        fprintf(stderr, "short write %s\n", name);
        fclose(fh); remove(path); exit(1);
    }
    /* fwrite only buffers: what never landed is knowable only at the close */
    if (fclose(fh)) { fprintf(stderr, "incomplete write %s\n", name); remove(path); exit(1); }
    printf("  %-22s written (%zu bytes)\n", name, nbytes);
    ts_forget(name);
    if (g_mt_progress) g_mt_progress(name, (uint64_t)nbytes, (uint64_t)nbytes);
}

/* The max comes off the histogram: the BFS depth counter runs a level past the productive one. */
static void table_stats(const char *name, const u8 *d, size_t total)
{
    long hist[256];
    memset(hist, 0, sizeof hist);
    for (size_t i = 0; i < total; i++) hist[d[i]]++;
    int maxv = 0;
    for (int i = 0; i < INVALID8; i++) if (hist[i]) maxv = i;
    printf("  %s: max %d, unassigned %ld\n  histogram:", name, maxv, hist[INVALID8]);
    for (int i = 0; i <= maxv; i++) printf(" %d:%ld", i, hist[i]);
    printf("\n");
}

/* Present *and* the size the list says: a half-written one is loaded by nobody, only rebuilt. */
static int have_table(const char *name)
{
    long long want = ts_size(name);
    return want > 0 && ts_size_ok(name, want);
}

/* have_table, and the bytes the manifest says it should hold: damage leaves a file right-sized. */
static int table_sound(const char *name) __attribute__((unused));
static int table_sound(const char *name)
{
    return have_table(name) && ts_verify(name, ts_size(name)) == TS_OK;
}

static int input_sound(const char *name)
{
    if (!have_table(name)) return 0;
    return ts_verify(name, ts_size(name)) == TS_OK;
}

/* ---- the inputs a builder needs ---- */
/* The four move tables are built here when absent; the rest are load-or-die; b_ avoids a clash. */
static u16 *b_twist_move, *b_flip_move, *b_ss_move, *b_corners_move;
static u32 *b_clssym;
static u16 *b_twist_conj;
static u32 *b_fs24_rep;

static void *load_or_die(const char *name)
{
    size_t n = (size_t)ts_size(name);
    void *buf = malloc(n);
    if (!buf) { fprintf(stderr, "out of memory for %s (%.0f MB)\n", name, n / 1048576.0); exit(1); }
    const char *path = table_path(name);
    FILE *fh = fopen(path, "rb");
    if (!fh) { fprintf(stderr, CANNOT_OPEN, path); exit(1); }
    if (fread(buf, 1, n, fh) != n) { fprintf(stderr, "short read %s\n", name); exit(1); }
    fclose(fh);
    return buf;
}

/* One builder's input is another's output: name the one that writes it. */
static void *load_input(const char *name)
{
    if (!have_table(name)) {
        fprintf(stderr, "%s missing -- build it first (mktables.exe %s)\n", name, name);
        exit(1);
    }
    /* Right-sized is not right: the manifest this run writes would bless a wrong table. */
    if (!input_sound(name)) {
        fprintf(stderr, "%s damaged -- not the bytes the manifest has\n", name);
        exit(1);
    }
    return load_or_die(name);
}

static void build_move_table(const char *name, int n, void (*set)(Cube *, int),
                             int (*get)(const Cube *), u16 *dst)
{
    Cube a, b;
    double t0 = now_sec();
    for (int i = 0; i < n; i++) {
        identity_cube(&a); set(&a, i);
        for (int m = 0; m < N_MOVE; m++) {
            b = a; multiply(&b, &moveCube[m]);
            dst[N_MOVE * i + m] = (u16)get(&b);
        }
    }
    if (!opt_quiet) printf("    %-18s done in %.2f s\n", name, now_sec() - t0);
}

static void build_move_tables(void)
{
    b_twist_move   = malloc(MOVE_TWIST_BYTES);
    b_flip_move    = malloc(MOVE_FLIP_BYTES);
    b_ss_move      = malloc(MOVE_SLICE_SORTED_BYTES);
    b_corners_move = malloc(MOVE_CORNERS_BYTES);
    if (!b_twist_move || !b_flip_move || !b_ss_move || !b_corners_move) {
        fprintf(stderr, OOM_TEXT);
        exit(1);
    }
    if (!opt_quiet) printf("  computing the four move tables...\n");
    build_move_table("move_twist", N_TWIST, set_twist, get_twist, b_twist_move);
    build_move_table("move_flip", N_FLIP, set_flip, get_flip, b_flip_move);
    build_move_table("move_slice_sorted", N_SLICE_SORTED, set_slice_sorted, get_slice_sorted,
                     b_ss_move);
    build_move_table("move_corners", N_CORNERS, set_corners, get_corners, b_corners_move);
}

static void do_move_tables(int ftm)
{
    (void)ftm;
    build_move_tables();
    table_out("move_twist", b_twist_move);
    table_out("move_flip", b_flip_move);
    table_out("move_slice_sorted", b_ss_move);
    table_out("move_corners", b_corners_move);
}

static void ensure_move_tables(void)
{
    /* Guarded: unguarded, each call re-reads the tables and leaks the copies it holds */
    if (!b_twist_move && input_sound("move_twist") && input_sound("move_flip") &&
        input_sound("move_slice_sorted") && input_sound("move_corners")) {
        b_twist_move   = load_or_die("move_twist");
        b_flip_move    = load_or_die("move_flip");
        b_ss_move      = load_or_die("move_slice_sorted");
        b_corners_move = load_or_die("move_corners");
    } else if (!b_twist_move) {
        build_move_tables();
    }
}

/* The one input ct_prun and phase1x24 share; the fs24 pair is phase1x24's alone. */
static void ensure_twist_conj(void)
{
    if (!b_twist_conj) b_twist_conj = load_input("conj_twist");
}

/* conj_twist turned round: [sym * N_TWIST + twist], one lookup then lands in one 4 KB row. */
static u16 *g_tconj;
static void ensure_tconj(void)
{
    if (g_tconj) return;
    g_tconj = malloc(CONJ_TWIST_BYTES);
    if (!g_tconj) { fprintf(stderr, "out of memory for the conjugate table\n"); exit(1); }
    for (int s = 0; s < N_SYM_D4H; s++)
        for (int t = 0; t < N_TWIST; t++)
            g_tconj[(size_t)s * N_TWIST + t] = b_twist_conj[N_SYM_D4H * t + s];
}

static void ensure_sym_tables(void)
{
    /* Guarded: unguarded, the second phase1x24 pass re-reads the tables and leaks the first ones */
    if (!b_clssym) {
        if (input_sound("fs24_clssym") && input_sound("fs24_rep")) {
            b_clssym   = load_or_die("fs24_clssym");
            b_fs24_rep = load_or_die("fs24_rep");
        } else {
            fprintf(stderr, "fs24_clssym / fs24_rep missing -- build them first"
                            " (mktables.exe fs24_clssym)\n");
            exit(1);
        }
    }
    ensure_twist_conj();
}

static int met_n, met_col[N_MOVE];
static void metric_moves(int ftm)
{
    met_n = 0;
    if (ftm) { for (int i = 0; i < N_MOVE_FTM; i++) met_col[met_n++] = ftm_col[i]; }
    else     { for (int i = 0; i < N_MOVE; i++)     met_col[met_n++] = i; }
}

/* A level adding nothing means no later one will; a stopped one adds nothing on purpose. */
static void level_must_add(int depth, uint64_t done, uint64_t before, uint64_t total)
{
    if (done != before || mt_stop) return;
    fprintf(stderr, "  no new entries at depth %d -- %llu of %llu reached, nothing written\n",
            depth, (unsigned long long)done, (unsigned long long)total);
    exit(1);
}

/* A failed join leaves the level half-written; the handles are the caller's to close. */
static void join_threads(HANDLE *th, int n, const char *what)
{
    if (WaitForMultipleObjects(n, th, TRUE, INFINITE) == WAIT_FAILED) {
        fprintf(stderr, "%s: cannot join the build threads (error %lu)\n", what,
                (unsigned long)GetLastError());
        exit(1);
    }
}

/* All n workers at once, joined before this returns; a lone worker runs on this thread. */
static void run_all(unsigned (__stdcall *fn)(void *), int n, const char *what)
{
    if (n == 1) {
        int zero = 0;
        fn(&zero);
        return;
    }
    int idx[THREAD_CAP];
    HANDLE th[THREAD_CAP] = {0};
    for (int t = 0; t < n; t++) {
        idx[t] = t;
        th[t] = (HANDLE)_beginthreadex(NULL, 0, fn, &idx[t], 0, NULL);
        if (!th[t]) {
            fprintf(stderr, "%s: thread %d failed to start\n", what, t);
            mt_stop = 1;            /* the rest of the plan is not worth a half-written table */
            /* none started at t == 0, and WaitForMultipleObjects(0, ...) fails */
            if (t) join_threads(th, t, what);
            exit(1);
        }
    }
    join_threads(th, n, what);
    for (int t = 0; t < n; t++) CloseHandle(th[t]);
}

/* ---- conj_twist ---- */
static void do_conj_twist(int ftm)
{
    (void)ftm;
    u16 *buf = malloc(CONJ_TWIST_BYTES);
    if (!buf) { fprintf(stderr, OOM_TEXT); exit(1); }
    Cube cc, ss;
    for (int t = 0; t < N_TWIST; t++) {
        identity_cube(&cc); set_twist(&cc, t);
        for (int s = 0; s < N_SYM_D4H; s++) {
            ss = symCube[s];
            corner_multiply(&ss, &cc);                    /* s*cc       */
            corner_multiply(&ss, &symCube[inv_idx[s]]);   /* s*cc*s^-1 */
            buf[N_SYM_D4H * t + s] = (u16)get_twist(&ss);
        }
    }
    table_out("conj_twist", buf);
    free(buf);
}

/* ---- fs conjugation: a conjugated (slice, flip) splits; flip corrects the edges it leaves out */
static u16 *g_fsc_ss, *g_fsc_flip, *g_fsc_corr;   /* [N_SYM_D4H][N_SLICE_SORTED] x2, [..][N_FLIP] */

static void build_fs_conj(void)
{
    if (g_fsc_ss) return;
    size_t nslc = (size_t)N_SYM_D4H * N_SLICE_SORTED;
    u16 *m = malloc((2 * nslc + (size_t)N_SYM_D4H * N_FLIP) * sizeof *m);
    if (!m) { fprintf(stderr, OOM_TEXT); exit(1); }
    g_fsc_ss   = m;
    g_fsc_corr = m + nslc;
    g_fsc_flip = m + 2 * nslc;

    Cube cc, ss;
    for (int t = 0; t < N_SYM_D4H; t++) {
        for (int slc = 0; slc < N_SLICE_SORTED; slc++) {
            identity_cube(&cc); set_slice_sorted(&cc, slc);
            ss = symCube[inv_idx[t]];
            edge_multiply(&ss, &cc);                 /* t^-1*cc   */
            edge_multiply(&ss, &symCube[t]);         /* t^-1*cc*t */
            g_fsc_ss[N_SLICE_SORTED * t + slc]   = (u16)get_slice_sorted(&ss);
            g_fsc_corr[N_SLICE_SORTED * t + slc] = (u16)get_flip(&ss);
        }
        for (int flip = 0; flip < N_FLIP; flip++) {
            identity_cube(&cc); set_flip(&cc, flip);
            ss = symCube[inv_idx[t]];
            edge_multiply(&ss, &cc);
            edge_multiply(&ss, &symCube[t]);
            g_fsc_flip[N_FLIP * t + flip] = (u16)get_flip(&ss);
        }
    }
}

/* ---- fs24_* ---- */
/* One pass: the first of an orbit is the class representative, the rest claimed with s. */
static void do_fs24(int ftm)
{
    (void)ftm;
    size_t n = (size_t)FS24_CLSYM_BYTES / sizeof(u32);   /* the pairs the table covers */
    u32 *cs  = malloc(FS24_CLSYM_BYTES);
    u32 *rep = malloc(FS24_REP_BYTES);
    if (!cs || !rep) { fprintf(stderr, "out of memory (%.0f MB)\n",
                               (double)FS24_CLSYM_BYTES / 1048576.0); exit(1); }
    /* 0xff == INVALID32 == "not assigned": no real pair is 0xffffffff */
    memset(cs, 0xff, FS24_CLSYM_BYTES);
    build_fs_conj();

    long classcnt = 0;
    for (int slc = 0; slc < N_SLICE_SORTED; slc++) {
        for (int flip = 0; flip < N_FLIP; flip++) {
            long i = (long)N_FLIP * slc + flip;
            if (cs[i] != INVALID32) continue;
            cs[i] = FS24_PACK(classcnt, 0);
            rep[classcnt] = (u32)i;
            for (int s = 1; s < N_SYM_D4H; s++) {  /* s == 0 is the identity: its image is i */
                size_t k = (size_t)N_SLICE_SORTED * s + slc;
                int slc2 = g_fsc_ss[k];
                int flip2 = g_fsc_flip[N_FLIP * s + flip] ^ g_fsc_corr[k];
                long inew = (long)N_FLIP * slc2 + flip2;
                if (cs[inew] == INVALID32) cs[inew] = FS24_PACK(classcnt, s);
            }
            classcnt++;
        }
        if (g_mt_progress)
            g_mt_progress("fs24_clssym", (uint64_t)N_FLIP * (slc + 1), (uint64_t)n);
    }
    table_out("fs24_clssym", cs);
    table_out("fs24_rep", rep);
    free(cs);
    free(rep);
}

/* ---- ct_clssym: corner permutations folded into orbits of one axis' 16 symmetries */
static void do_ct_clssym(int ftm)
{
    (void)ftm;
    u32 *cs = malloc(CT_CLSYM_BYTES);
    if (!cs) { fprintf(stderr, OOM_TEXT); exit(1); }
    memset(cs, 0xff, CT_CLSYM_BYTES);
    Cube cc, ss;
    int classcnt = 0;
    for (int cp = 0; cp < N_CORNERS; cp++) {
        if (cs[cp] != INVALID32) continue;
        cs[cp] = FS24_PACK(classcnt, 0);
        identity_cube(&cc); set_corners(&cc, cp);
        for (int s = 1; s < N_SYM_D4H; s++) {
            ss = symCube[inv_idx[s]];
            corner_multiply(&ss, &cc);
            corner_multiply(&ss, &symCube[s]);
            int inew = get_corners(&ss);
            if (cs[inew] == INVALID32) cs[inew] = FS24_PACK(classcnt, s);
        }
        classcnt++;
    }
    table_out("ct_clssym", cs);
    free(cs);
}

/* ---- ct_prun: index N_TWIST*class+twist; corners top out at 7, so the twist carries the tail */
static u8  *g_ctd;                        /* the table while it is being built */
static u32 *g_ctcls;
static int  g_ctrep[N_CORNERS_CLASS];
/* ct_classes(): the symmetries fixing this class's representative */
static u32  g_ctsym[N_CORNERS_CLASS];
static int  g_ct_depth, g_ct_nt, g_ct_cas;
static uint64_t g_ct_done[THREAD_CAP];

/* the words of the same block: ct_claim is a CAS on the word holding its byte */
static u32 *g_ctdw;

/* 1 iff unassigned: every writer writes the same distance, so a lost claim costs nothing */
static int ct_claim(size_t i, u8 v)
{
    /* volatile: another thread's claim of a neighbor is what makes this byte assigned */
    if (*(volatile u8 *)&g_ctd[i] != 0xff) return 0;
    if (!g_ct_cas) { g_ctd[i] = v; return 1; }
    /* the store goes through the word holding the byte, so writes to neighbors cannot lose one */
    u32 *wp = &g_ctdw[i >> 2];
    int shift = (int)((i & 3) * 8);
    for (;;) {
        u32 cur = *(volatile u32 *)wp;
        if (((cur >> shift) & 0xff) != 0xff) return 0;
        u32 nxt = (cur & ~(0xffu << shift)) | ((u32)v << shift);
        if (InterlockedCompareExchange((volatile LONG *)wp, (LONG)nxt, (LONG)cur) == (LONG)cur)
            return 1;
    }
}

static unsigned __stdcall ct_worker(void *p)
{
    int t = *(int *)p;
    int lo = (int)((uint64_t)t * N_CORNERS_CLASS / g_ct_nt);
    int hi = (int)((uint64_t)(t + 1) * N_CORNERS_CLASS / g_ct_nt);
    uint64_t got = 0;
    u8 want = (u8)g_ct_depth, fill = (u8)(g_ct_depth + 1);
    for (int c = lo; c < hi && !mt_stop; c++) {
        /* the class's own corner permutation, so the move acts in the frame the twist is kept in */
        const u16 *cm = b_corners_move + (size_t)N_MOVE * g_ctrep[c];
        size_t base = (size_t)N_TWIST * c;
        /* per class, not per twist: the row to prefetch, the conj row, the symmetry mask */
        size_t row2[N_MOVE];
        const u16 *trow[N_MOVE];
        unsigned sg2[N_MOVE];
        for (int k = 0; k < met_n; k++) {
            u32 v = g_ctcls[cm[met_col[k]]];
            int c2 = (int)CLS_OF(v);
            row2[k] = (size_t)N_TWIST * c2;
            trow[k] = g_tconj + (size_t)SYM_OF(v) * N_TWIST;
            sg2[k] = g_ctsym[c2];
            for (size_t pf = row2[k]; pf <= row2[k] + N_TWIST; pf += 64)
                __builtin_prefetch(&g_ctd[pf]);
        }
        for (int tw = 0; tw < N_TWIST; tw++) {
            if (*(volatile u8 *)&g_ctd[base + tw] != want) continue;
            const u16 *tm = b_twist_move + (size_t)N_MOVE * tw;
            for (int k = 0; k < met_n; k++) {
                int tw2 = trow[k][tm[met_col[k]]];
                if (ct_claim(row2[k] + tw2, fill)) got++;
                for (unsigned bits = sg2[k] & ~1u; bits; bits &= bits - 1)
                    if (ct_claim(row2[k] +
                                 b_twist_conj[N_SYM_D4H * tw2 + __builtin_ctz(bits)], fill))
                        got++;
            }
        }
    }
    g_ct_done[t] = got;
    return 0;
}

static void do_ctprun(int ftm)
{
    const char *nm = ftm ? "ct_prun_ftm" : "ct_prun";
    size_t total = (size_t)CT_PRUN_BYTES;
    /* allocated as words: the claim is a CAS on a word, and only a byte lvalue may read one */
    size_t words = (total + 3) / 4;
    u8 *d = malloc(words * sizeof(u32));
    if (!d) { fprintf(stderr, "out of memory for ct (%.0f MB)\n", total / 1048576.0); exit(1); }
    u32 *dw = (u32 *)d;          /* one buffer: words for the claim, bytes for the level walk */
    ensure_move_tables();
    ensure_twist_conj();
    ensure_tconj();
    /* Unguarded, the second pass re-reads the table, leaks the copy and recomputes the classes. */
    if (!g_ctcls) {
        g_ctcls = load_input("ct_clssym");
        ct_classes(g_ctcls, g_ctrep, g_ctsym);
    }
    metric_moves(ftm);
    memset(d, 0xff, total);
    d[0] = 0;
    size_t done = 1;
    int depth = 0;
    double t0 = now_sec();
    /* A thread owns a run of corners and writes anywhere; the claims keep the count honest. */
    g_ct_nt = worker_count();
    g_ct_cas = (g_ct_nt > 1);
    while (done != total && !mt_stop) {
        size_t before = done;
        g_ctd  = d;
        g_ctdw = dw;
        g_ct_depth = depth;
        for (int t = 0; t < g_ct_nt; t++) g_ct_done[t] = 0;
        run_all(ct_worker, g_ct_nt, "ct_prun");
        for (int t = 0; t < g_ct_nt; t++) done += g_ct_done[t];
        depth++;
        if (!opt_quiet && depth % 4 == 0)
            printf("  depth %d done: %llu/%llu  (%.0f s)\n", depth, (unsigned long long)done,
                   (unsigned long long)total, now_sec() - t0);
        if (g_mt_progress) g_mt_progress(nm, done, total);
        level_must_add(depth, done, before, total);
    }
    if (mt_stop) { free(d); return; }
    table_out(nm, d);
    table_stats(nm, d, total);
    free(d);
}

/* The symmetries a rep is invariant under, one mask per class. */
static u32 *g_fs_mask;                    /* the built table, kept for the pass that follows */
static int  g_fs_nt;

/* One class's mask: a class writes only its own slot, so a range is a thread's to take. */
static void fs_sym_range(int t)
{
    size_t n = (size_t)N_FLIPSLICESORTED_CLASS;
    size_t lo = (size_t)((uint64_t)t * n / g_fs_nt);
    size_t hi = (size_t)((uint64_t)(t + 1) * n / g_fs_nt);
    for (size_t i = lo; i < hi; i++) {
        int r = (int)b_fs24_rep[i];
        int slc = r / N_FLIP, flip = r % N_FLIP;
        g_fs_mask[i] = 1;                          /* the identity fixes every position */
        for (int s = 1; s < N_SYM_D4H; s++) {
            int inv = inv_idx[s];   /* this pass conjugates by s, the tables are indexed by s^-1 */
            size_t k = (size_t)N_SLICE_SORTED * inv + slc;
            int slc2 = g_fsc_ss[k];
            int flip2 = g_fsc_flip[N_FLIP * inv + flip] ^ g_fsc_corr[k];
            if (slc2 == slc && flip2 == flip) g_fs_mask[i] |= 1u << s;
        }
    }
}

static unsigned __stdcall fs_sym_worker(void *p)
{
    int t = *(int *)p;
    fs_sym_range(t);
    return 0;
}

static u32 *build_fs_sym(void)
{
    /* Nothing here looks at the move set, so one build serves both passes. */
    if (g_fs_mask) return g_fs_mask;
    build_fs_conj();                  /* the ranges run at once, so this happens before them */
    g_fs_mask = malloc((size_t)N_FLIPSLICESORTED_CLASS * 4);
    if (!g_fs_mask) { fprintf(stderr, OOM_TEXT); exit(1); }
    int nt = worker_count();
    g_fs_nt = nt;
    run_all(fs_sym_worker, nt, "fs_sym");
    return g_fs_mask;
}

/* ---- phase1x24_prun: distances mod 3; a class's pairs all take the new one */
static u32 *g_p2;
static u32 *g_fs_sym;                     /* == g_fs_mask once built; read-only during the BFS */
static u16 *g_tmc;                        /* twist move then tconj: [sym * N_MOVE + move] */
static int  g_twrep[N_MOVE];              /* one id per distinct column of the twist move table */
static int g_depth, g_depth3, g_backsearch, g_nt, g_use_cas;
static volatile LONG g_next_cls;
#define CHUNK_N 256
static uint64_t g_local_done[THREAD_CAP];

/* One 2-bit value per entry: 0 = not reached, 1..3 = a residue plus one.  A fill is one CAS
 * publishing value and reachedness together: no reader sees a reached entry without its value. */
#define P2_LO 0x55555555u        /* the low bit of each of the sixteen 2-bit slots */
#define P2_HI 0xAAAAAAAAu        /* the high bit of each of the sixteen 2-bit slots */

static inline int get2(size_t ix)
{
    return (int)((*(volatile u32 *)&g_p2[ix >> 4] >> ((ix & 15) << 1)) & 3);
}

/* Does any slot hold zero?  (v | ((v | 2) - 1)) & 2 keeps a slot's high bit only when it is not
 * zero; no slot borrows, as v | 2 is 2 or 3, so subtracting 1 stays inside the slot. */
static inline int has_zero2(u32 w)
{
    return ((w | ((w | P2_HI) - P2_LO)) & P2_HI) != P2_HI;
}
/* Does any slot equal needle (0..3)?  XOR broadcasts the needle into every slot. */
static inline int has_eq2(u32 w, u32 needle) { return has_zero2(w ^ (needle * P2_LO)); }

/* 1 iff this call won the claim: a slot is zero until then, so the value is an OR. */
static inline int set2_fill(size_t ix, int v)
{
    u32 *wp = &g_p2[ix >> 4];
    int sh = (int)((ix & 15) << 1);
    if (!g_use_cas) {
        u32 cur = *wp;
        if ((cur >> sh) & 3) return 0;
        *wp = cur | ((u32)v << sh);
        return 1;
    }
    for (;;) {
        u32 cur = *(volatile u32 *)wp;
        if ((cur >> sh) & 3) return 0;
        u32 nxt = cur | ((u32)v << sh);
        if (InterlockedCompareExchange((volatile LONG *)wp, (LONG)nxt, (LONG)cur) == (LONG)cur)
            return 1;
    }
}

static unsigned __stdcall p1_worker(void *p)
{
    int t = *(int *)p;
    int nclass = N_FLIPSLICESORTED_CLASS;
    int depth = g_depth, depth3 = g_depth3, backsearch = g_backsearch;
    int fillv = (depth + 1) % 3 + 1;        /* the value a freshly reached entry takes */
    uint64_t reached_here = 0;
    int cls1[N_MOVE], sym1[N_MOVE], dcls[N_MOVE], dsym[N_MOVE];
    int drep[N_MOVE], dmv[N_MOVE], dsg[N_MOVE];
    /* Two passes, name then expand: interleaved, the scan and the expansions evict each other. */
    u16 mtw[N_TWIST];
    u32 ixb[N_TWIST], hitb[N_TWIST];
    for (;;) {
        int lo = InterlockedExchangeAdd(&g_next_cls, CHUNK_N);
        if (lo >= nclass || mt_stop) break;
        int hi = lo + CHUNK_N;
        if (hi > nclass) hi = nclass;
        for (int fsc = lo; fsc < hi; fsc++) {
            size_t base_ix = (size_t)N_TWIST * fsc;
            int nm = 0;
            int need = backsearch ? 0 : depth3 + 1; /* the value an entry this pass wants carries */
            /* A concurrent fill writes a value need never takes: the skip test shares the load. */
            u32 w = *(volatile u32 *)&g_p2[base_ix >> 4];
            for (int twist = 0; twist < N_TWIST; twist++) {
                size_t ix = base_ix + (size_t)twist;
                if ((ix & 15) == 0) {
                    w = *(volatile u32 *)&g_p2[ix >> 4];   /* the sixteen entries [ix, ix+16) */
                    if (twist < N_TWIST - 16 && !has_eq2(w, (u32)need)) {
                        twist += 15; continue;
                    }
                }
                if ((int)((w >> ((ix & 15) << 1)) & 3) == need)
                    mtw[nm++] = (u16)twist;
            }
            if (!nm) continue;

            /* Where a move takes a class holds for all its twists: worked out once per class. */
            int fss = (int)b_fs24_rep[fsc];
            int flip = fss % N_FLIP, slicesorted = fss / N_FLIP;
            for (int k = 0; k < met_n; k++) {
                int m = met_col[k];
                int flip1 = b_flip_move[N_MOVE * flip + m];
                int ss1   = b_ss_move[N_MOVE * slicesorted + m];
                u32 cs1   = b_clssym[N_FLIP * ss1 + flip1];
                cls1[k] = (int)CLS_OF(cs1);
                sym1[k] = (int)SYM_OF(cs1);
            }
            /* Moves agreeing on the twist column and the class they land on share one neighbor. */
            int nd = 0;
            for (int k = 0; k < met_n; k++) {
                int dup = 0;
                for (int j = 0; j < nd; j++)
                    if (dcls[j] == cls1[k] && dsym[j] == sym1[k] &&
                        drep[j] == g_twrep[met_col[k]]) {
                        dup = 1;
                        break;
                    }
                if (dup) continue;
                dcls[nd] = cls1[k]; dsym[nd] = sym1[k]; drep[nd] = g_twrep[met_col[k]];
                dmv[nd] = met_col[k]; dsg[nd] = (int)g_fs_sym[cls1[k]];
                nd++;
            }
            if (!backsearch) {
                /* One move's targets share a class row: walking moves keeps that row resident. */
                for (int k = 0; k < nd; k++) {
                    const u16 *ct = g_tmc + ((size_t)dsym[k] * N_MOVE + dmv[k]) * N_TWIST;
                    size_t row1 = (size_t)N_TWIST * dcls[k];
                    unsigned sg = (unsigned)dsg[k];
                    for (size_t pf = row1 >> 4; pf <= (row1 + N_TWIST) >> 4; pf += 16)
                        __builtin_prefetch(&g_p2[pf]);
                    /* Split: the gather's chain and the hit test then overlap. */
                    for (int q = 0; q < nm; q++)
                        ixb[q] = (u32)(row1 + (size_t)ct[mtw[q]]);
                    int nh = 0;
                    for (int q = 0; q < nm; q++) {
                        size_t ix1 = ixb[q];
                        /* The fill re-reads under the CAS, so a stale zero only costs a claim. */
                        u32 wd = *(u32 *)&g_p2[ix1 >> 4];
                        int hit = ((wd >> ((ix1 & 15) << 1)) & 3) == 0;
                        hitb[nh] = (u32)ix1;
                        nh += hit;
                    }
                    for (int q = 0; q < nh; q++) {
                        size_t ix1 = hitb[q];
                        int twist1 = (int)(ix1 - row1);
                        if (set2_fill(ix1, fillv)) reached_here++;
                        for (unsigned bits = sg & ~1u; bits; bits &= bits - 1) {
                            size_t ix2 = row1 +
                                         b_twist_conj[(twist1 << 4) + __builtin_ctz(bits)];
                            if (get2(ix2) == 0)
                                if (set2_fill(ix2, fillv)) reached_here++;
                        }
                    }
                }
            } else {
                for (int q = 0; q < nm; q++) {
                    int twist = mtw[q];
                    for (int k = 0; k < nd; k++) {
                        int m = dmv[k];
                        int fsc1   = dcls[k];
                        int twist1 = g_tconj[(size_t)dsym[k] * N_TWIST +
                                             b_twist_move[N_MOVE * twist + m]];
                        size_t ix1 = (size_t)N_TWIST * fsc1 + twist1;
                        if (get2(ix1) == depth3 + 1) {
                            if (set2_fill(base_ix + (size_t)twist, fillv)) reached_here++;
                            break;
                        }
                    }
                }
            }
        }
    }
    g_local_done[t] = reached_here;
    return 0;
}

/* Repack five trits to a byte, parallel over whole bytes: in place it would be serial. */
static u8      *g_pack_out;
static uint64_t g_pack_total;
static size_t   g_pack_nbytes;

static unsigned __stdcall p1_pack(void *p)
{
    int t = *(int *)p;
    size_t n = g_pack_nbytes;
    size_t lo = (size_t)((uint64_t)t * n / g_nt);
    size_t hi = (size_t)((uint64_t)(t + 1) * n / g_nt);
    for (size_t b = lo; b < hi; b++) {
        size_t ix0 = b * 5;
        u32 acc = 0;
        /* plain loads: the pack runs after the join, and five slots of a byte share one word */
        for (int k = 0; k < 5; k++) {
            size_t ix = ix0 + (size_t)k;
            int v = ix < g_pack_total ? (int)((g_p2[ix >> 4] >> ((ix & 15) << 1)) & 3) : 0;
            if (v) acc += (u32)(v - 1) * POW3[k];
        }
        g_pack_out[b] = (u8)acc;
    }
    return 0;
}

static void do_phase1x24(int ftm)
{
    const char *nm = ftm ? "phase1x24_prun_ftm" : "phase1x24_prun";
    uint64_t total = (uint64_t)N_FLIPSLICESORTED_CLASS * N_TWIST;
    size_t bytes = (size_t)PHASE1X24_BYTES;
    size_t words = (size_t)((total + 15) / 16);        /* sixteen 2-bit slots per word, in memory */
    g_p2 = malloc(words * 4);
    if (!g_p2) {
        fprintf(stderr, "out of memory for the pruning table (%.0f MB)\n",
                words * 4.0 / 1048576.0);
        exit(1);
    }

    ensure_move_tables();
    ensure_sym_tables();
    metric_moves(ftm);
    ensure_tconj();

    /* Composed into one table: the sweep then pays one load per twist, not two. */
    g_tmc = malloc((size_t)N_SYM_D4H * N_MOVE * N_TWIST * 2);
    if (!g_tmc) { fprintf(stderr, "out of memory for the composed twist table\n"); exit(1); }
    for (int sy = 0; sy < N_SYM_D4H; sy++)
        for (int m = 0; m < N_MOVE; m++)
            for (int t = 0; t < N_TWIST; t++)
                g_tmc[((size_t)sy * N_MOVE + m) * N_TWIST + t] =
                    g_tconj[(size_t)sy * N_TWIST + b_twist_move[(size_t)N_MOVE * t + m]];

    /* Two moves twisting the corners alike are one here; U/D twists none, so its 9 are one. */
    for (int m = 0; m < N_MOVE; m++) {
        g_twrep[m] = m;
        for (int m2 = 0; m2 < m; m2++) {
            int same = 1;
            for (int t = 0; t < N_TWIST; t++)
                if (b_twist_move[(size_t)N_MOVE * t + m] != b_twist_move[(size_t)N_MOVE * t + m2]) {
                    same = 0;
                    break;
                }
            if (same) { g_twrep[m] = g_twrep[m2]; break; }
        }
    }

    g_fs_sym = build_fs_sym();

    double t0 = now_sec();
    memset(g_p2, 0, words * 4);
    g_p2[0] = 1u;              /* the solved phase-1 state: distance 0, which is value 1 */
    uint64_t done = 1;
    int depth = 0, backsearch = 0;

    /* The build is bandwidth bound, so more cores, more DRAM requests at once, speeds it. */
    int nt = worker_count();
    g_nt = nt;
    g_use_cas = (nt > 1);
    uint64_t last_added = 0;
    while (done != total && !mt_stop) {
        uint64_t before = done;
        int depth3 = depth % 3;
        /* Forward re-expands a whole class however little is left; backward visits only them. */
        if (!backsearch && last_added && total - done < last_added) {
            if (!opt_quiet)
                printf("  flipping to backwards search at depth %d: %llu left, "
                       "%llu added at depth %d\n",
                       depth, (unsigned long long)(total - done), (unsigned long long)last_added,
                       depth - 1);
            backsearch = 1;
        }
        g_depth = depth; g_depth3 = depth3; g_backsearch = backsearch;
        g_next_cls = 0;
        for (int t = 0; t < nt; t++) g_local_done[t] = 0;
        run_all(p1_worker, nt, "phase1x24");
        for (int t = 0; t < nt; t++) done += g_local_done[t];
        last_added = done - before;
        depth++;
        if (!opt_quiet)
            printf("  depth %d done: %llu/%llu  (%.0f s, %d threads)\n", depth,
                   (unsigned long long)done, (unsigned long long)total, now_sec() - t0, nt);
        if (g_mt_progress)
            g_mt_progress(nm, done, total);
        level_must_add(depth, done, before, total);
    }

    if (mt_stop) {
        free(g_p2);       g_p2     = NULL;
        free(g_tconj);    g_tconj  = NULL;
        free(g_tmc);      g_tmc    = NULL;
        return;
    }

    /* The solver reads five trits per byte, the build wants two: repacking pays for the split. */
    g_pack_out = malloc(bytes);
    if (!g_pack_out) { fprintf(stderr, "out of memory for the packed table\n"); exit(1); }
    g_pack_total = total;
    g_pack_nbytes = bytes;
    run_all(p1_pack, nt, "phase1x24 pack");
    free(g_p2);      g_p2       = NULL;
    table_out(nm, g_pack_out);
    /* A full build runs this twice; held over, the second lands on the first, doubling the peak. */
    free(g_pack_out); g_pack_out = NULL;
    /* g_fs_sym stays: build_fs_sym() keeps it for the FTM build that follows this one. */
    free(g_tconj);   g_tconj   = NULL;
    free(g_tmc);     g_tmc     = NULL;
}

/* ---- the build plan: every builder and the files it writes; one shape, f(ftm) */
#define B_NBUILD  ((int)(sizeof BUILDERS / sizeof BUILDERS[0]))
#define B_NFILES  4
static const struct {
    const char *files[B_NFILES];      /* what this entry writes; the rest are NULL */
    void      (*build)(int);
    int         ftm;
} BUILDERS[] = {
    {{"move_twist", "move_flip", "move_slice_sorted", "move_corners"}, do_move_tables, 0},
    {{"conj_twist"},         do_conj_twist, 0},
    {{"ct_clssym"},          do_ct_clssym,  0},
    {{"fs24_clssym", "fs24_rep"}, do_fs24,  0},
    {{"ct_prun"},            do_ctprun,     0},
    {{"ct_prun_ftm"},        do_ctprun,     1},
    {{"phase1x24_prun"},     do_phase1x24,  0},
    {{"phase1x24_prun_ftm"}, do_phase1x24,  1}
};

#ifndef MKTABLES_EMBED
/* one slot per table, and how long a table's name may be */
#define ARG_MAX 12
#define ARG_LEN 32
static char arg[ARG_MAX][ARG_LEN];
static int  narg;
static int opt_manifest;

static int in_args(const char *s)
{
    for (int i = 0; i < narg; i++) if (!strcmp(arg[i], s)) return 1;
    return 0;
}

static void usage(void)
{
    printf("usage: mktables.exe [<table> ...] [--check] [--force] [--quiet] [--manifest]\n"
           "                    [--threads N]\n"
           "\n"
           "With no name every table is built; a name builds that one, " CUBOLT_STR_(ARG_MAX)
           " at most.  Tables are\n"
           "written next to the exe.\n"
           "\n"
           "  --check      compare a fresh build against what is on disk, byte for byte: writes\n"
           "               nothing, and exits 1 if a table is missing or differs\n"
           "  --force      rebuild even the tables that are already there\n"
           "  --quiet      print no line per builder\n"
           "  --manifest   rewrite tables.manifest from what is on disk, build nothing\n"
           "  --threads N  workers for the three parallel passes (ct_prun, fs_sym,\n"
           "               phase1x24); 0 is one per logical core\n"
           VERSION_HELP_TEXT);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--check"))      opt_check = 1;
        else if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) {
            print_version();
            return 0;
        }
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(); return 0; }
        else if (!strcmp(argv[i], "--force")) opt_force = 1;
        else if (!strcmp(argv[i], "--quiet")) opt_quiet = 1;
        else if (!strcmp(argv[i], "--manifest")) opt_manifest = 1;
        else if (!strcmp(argv[i], "--threads")) {
            const char *err;
            if (threads_flag(argv, argc, &i, &opt_threads, &err)) {
                fprintf(stderr, "%s\n", err);
                return 1;
            }
        }
        else if (argv[i][0] == '-') { fprintf(stderr, "unknown flag %s\n", argv[i]); return 1; }
        else if (strlen(argv[i]) >= ARG_LEN) {
            fprintf(stderr, "table name is too long: %s\n", argv[i]);
            return 1;
        }
        else if (narg == ARG_MAX) {
            fprintf(stderr, "too many table names (max " CUBOLT_STR_(ARG_MAX) ")\n");
            return 1;
        }
        else snprintf(arg[narg++], ARG_LEN, "%s", argv[i]);
    }

    /* Every name asked for: letting one pass would have exit 0 claim it was built. */
    for (int i = 0; i < narg; i++)
        if (ts_index(arg[i]) < 0) {
            fprintf(stderr, "%s is not a table name\n", arg[i]);
            return 1;
        }

    /* --check promises to write nothing and --manifest only ever writes: they cannot pair. */
    if (opt_check && opt_manifest) {
        fprintf(stderr, "--manifest is not an option --check takes\n");
        return 1;
    }

    /* Re-digest the disk, no building: what is there now is what the manifest is to hold. */
    if (opt_manifest) {
        int rc = ts_manifest_write();
        for (int i = 0; i < TS_NFILES; i++) {
            if (have_table(TABLE_FILES[i].name)) continue;
            /* one exit code is all the installer gets: name the table it would have to rebuild */
            fprintf(stderr, "%s is missing\n", TABLE_FILES[i].name);
            rc = 1;
        }
        return rc;
    }

    build_move_cubes();
    build_symmetry_tables();
    mt_prog_open();
    mt_watch_installer();

    printf("%s the C table builder\n", opt_check ? "checking" : "running");
    /* An input is an output too, so one missing would end the check early: name them all first. */
    if (opt_check) {
        for (int i = 0; i < TS_NFILES; i++) {
            const char *nm = TABLE_FILES[i].name;
            if ((narg && !in_args(nm)) || have_table(nm)) continue;
            printf("  %-22s MISSING\n", nm);
            check_bad = 1;
        }
        if (check_bad) return 1;
    }
    for (int i = 0; i < B_NBUILD; i++) {
        const char *const *f = BUILDERS[i].files;
        int want = narg == 0;
        for (int j = 0; j < B_NFILES && f[j]; j++) want |= in_args(f[j]);
        if (!want) continue;
        /* Every output is sound: the build would end in a skipped write. */
        int all = !opt_force && !opt_check;
        for (int j = 0; all && j < B_NFILES && f[j]; j++) all = table_sound(f[j]);
        if (all) continue;
        /* Before the hours, not after them; "ab", so the probe cannot empty a file to replace */
        if (!opt_check)
            for (int j = 0; j < B_NFILES && f[j]; j++) {
                FILE *probe = fopen(table_path(f[j]), "ab");
                if (!probe) { fprintf(stderr, CANNOT_WRITE, table_path(f[j])); exit(1); }
                fclose(probe);
            }
        /* Wall clock over the whole builder, write included: what a run costs, not a stage's. */
        double tb = now_sec();
        BUILDERS[i].build(BUILDERS[i].ftm);
        if (mt_stop) return 3;      /* the code a stopped build already ends with elsewhere */
        if (!opt_quiet)
            printf("  %-22s %s in %.2f s\n", f[0], opt_check ? "checked" : "built",
                   now_sec() - tb);
    }
    /* Bless what is on disk now; --check is left out, because it may have found the bytes wrong. */
    if (!opt_check) {
        int rc = ts_manifest_write();
        if (rc) return rc;
        /* Every table was asked for: one missing is a build that did not finish. */
        for (int i = 0; !narg && i < TS_NFILES; i++)
            if (!have_table(TABLE_FILES[i].name)) {
                fprintf(stderr, "%s is missing -- the set is not complete\n", TABLE_FILES[i].name);
                return 1;
            }
        return 0;
    }
    /* MISSING and DIFFERS are what --check was asked for; exit 0 would call them intact */
    if (check_bad) return 1;
    return 0;
}
#endif
