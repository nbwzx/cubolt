/* Cubolt (https://github.com/nbwzx/cubolt)
 * Copyright (C) 2026 Zixing Wang <zixingwang.cn@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* guibatch.h -- File > Batch solve: an input file of cases, one solutions file per case. */
#ifndef GUIBATCH_H
#define GUIBATCH_H

#define RUN_JOB_FAILED_FMT "run_job failed (rc=%d)\n"

static BOOL browse_folder(HWND hw, char *path)
{
    /* BIF_NEWDIALOGSTYLE wants COM here; up in another mode already, the call fails harmlessly */
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    BROWSEINFO bi;
    char buf[MAX_PATH];
    memset(&bi, 0, sizeof bi);
    bi.hwndOwner = hw;
    bi.lpszTitle = "Choose the folder the batch solution files are written to:";
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
    BOOL ok = pidl && SHGetPathFromIDList(pidl, buf);
    if (pidl) CoTaskMemFree(pidl);
    if (ok) strcpy(path, buf);
    if (SUCCEEDED(hr)) CoUninitialize();
    return ok;
}

/* one case's settings: the job, plus the sort flag and thread count it does not carry */
typedef struct {
    Job  j;
    int  sort;
    int  threads;        /* 0 == auto */
} BatchCfg;

/* A snapshot of the panel, which every case overlays its own keys on. */
static BatchCfg batch_cfg_default(void)
{
    BatchCfg c;
    c.j            = job_default();
    c.j.subgroup   = g_req.subgroup;
    c.j.depth      = g_req.depth;
    c.j.opt        = g_req.only_optimal;
    c.sort         = g_req.sort_solutions;
    c.threads      = g_threads;
    return c;
}

/* NULL, or why the case cannot be run */
static const char *batch_cfg_overlay(const JVal *obj, BatchCfg *c)
{
    rend_do_sort = c->sort;                    /* what the overlay reads and writes */
    const char *bad = job_overlay(&c->j, obj, MAX_DEPTH);
    c->sort = rend_do_sort;
    const char *bad2 = job_overlay_threads(obj, &c->threads);
    return bad2 ? bad2 : bad;
}

/* 1 when the case named a goal of its own: the panel's is the solved cube. */
static int batch_has_goal(const BatchCfg *c)
{
    return strcmp(c->j.goalstring, job_default().goalstring) != 0;
}

/* per-case file: input and settings, then the solutions kept; -1 wrote none */
static int batch_write_case(int idx, const char *name, const char *orig, const char *cs,
                            int rc, double dt, const BatchCfg *cfg)
{
    char path[MAX_PATH + 128];
    case_out_path(path, sizeof path, g_batch_out, idx, name);
    /* binary, as the CLI's are: one case then reads the same however it was solved */
    FILE *f = fopen(path, "wb");
    if (!f) {
        printf("  cannot write %s\n", path);
        return -1;
    }
    if (g_batch_text_mode && name)
        fprintf(f, "%s, %s\n", name, orig);
    else {
        fprintf(f, "input:        %s\n", orig);
        fprintf(f, "cubestring:   %s\n", cs);
    }
    fprintf(f, "subgroup:     %s\n", cfg->j.subgroup);
    fprintf(f, "depth:        %d\n", cfg->j.depth);
    fprintf(f, "only optimal: %d\n", cfg->j.opt);
    fprintf(f, "sort:         %d\n", cfg->sort);
    if (batch_has_goal(cfg))
        fprintf(f, "goal:         %s\n", cfg->j.goalstring);
    fprintf(f, "\n");
    /* only_optimal: one shortest standard solution */
    size_t w = cfg->j.opt && rend_on ? 1 : rend_on;
    if (rc)
        fprintf(f, RUN_JOB_FAILED_FMT, rc);
    else {
        fprintf(f, "solutions:    %zu in %.2f s\n\n", w, dt);
        for (size_t i = 0; i < w; i++)
            fprintf(f, "%s\n", rend_text(rend_nth(i)));
    }
    int lost = stream_lost(f, 0);
    if (lost) {
        remove(path);          /* a case's own file, so what is left of it is not an answer */
        printf("  -> %s did not write in full\n", path);
        return -1;
    }
    if (rc)
        printf("  -> %s (run_job failed)\n", path);
    else
        printf("  -> %s (%zu solution(s) in %.2f s)\n", path, w, dt);
    return 0;
}

/* Tells this case's OOM from a Stop: run_job's own hook would end the batch instead. */
static volatile long g_batch_oom;

static int batch_rend_oom(size_t want)
{
    /* one line per case: the workers land here together, so the write is one exchange */
    if (!InterlockedExchange(&g_batch_oom, 1)) printf(REND_OOM_FMT, want / 1048576);
    engine_stop = 1;
    return 0;
}

/* solves one case and writes its file; 1 when Stop was pressed (the caller abandons the batch) */
static int batch_solve_one(int idx, const char *orig, const char *cs, const BatchCfg *cfg,
                           const char *name, int *n_ok, int *n_fail)
{
    if (g_batch_text_mode && name)
        printf("\n%s, %s\n", name, orig);
    else
        printf("\ncase %d: %s\ncubestring: %s\n", idx, orig, cs);
    if (batch_has_goal(cfg))
        printf("goal: %s\n", cfg->j.goalstring);
    Job j = cfg->j;
    j.cubestring   = (char *)cs;
    rend_reset();   /* dump refills nothing: without this the case is written from the last one's */
    par_threads = cfg->threads;
    /* dump's lines are not renderings, so no file carries them: the log is their way out */
    sol_std        = !j.dump;
    rend_do_sort   = cfg->sort || cfg->j.opt;
    rend_oom       = batch_rend_oom;
    double t0 = now_sec();
    int rc = run_job(&j);
    rend_oom       = NULL;
    double dt = now_sec() - t0 - load_wall;
    if (engine_stop) {
        /* A Stop ends the batch; running out of memory is this case's own ceiling. */
        if (g_batch_oom) {
            InterlockedExchange(&g_batch_oom, 0);
            engine_stop = 0;
            printf("\ncase %d out of memory: written with what it has\n", idx);
        } else {
            printf("\nbatch stopped at case %d\n", idx);
            engine_stop = 0;
            return 1;
        }
    }
    int written = batch_write_case(idx, name, orig, cs, rc, dt, cfg);
    if (rc || written) (*n_fail)++; else (*n_ok)++;
    return 0;
}

/* Runs off the GUI thread; Stop aborts the batch */
static unsigned __stdcall batch_worker(void *ctx)
{
    (void)ctx;
    WORKER_TABLE_GUARD();

    /* Once for the batch: a Stop pressed between two cases must not be cleared by the next one. */
    engine_stop = 0;

    const char *why = NULL;
    char *text = batch_read_text(g_batch_in, &why);
    if (!text) {
        printf("%s\n", why);
        g_table_jmp = NULL;
        worker_done(WM_BATCH_DONE, 0);
        return 0;
    }

    int idx = 0, n_ok = 0, n_fail = 0, n_skip = 0, stopped = 0;
    int f[54];
    const char *p = batch_start(text);
    int json = *p == '{' || *p == '[';
    g_batch_text_mode = !json;

    if (json) {
        printf("input format: JSON\n");
        JVal *doc = j_parse(p);
        if (!doc) {
            n_skip++;
            printf("batch input is not valid JSON: %s\n", j_err);
        } else {
            const JVal *cases = doc;
            BatchCfg dflt = batch_cfg_default();
            if (doc->type != J_ARR) {
                n_skip++;
                printf("JSON input must be a [ ... ] array of cases\n");
            } else {
                for (int i = 0; i < cases->n && !stopped; i++) {
                    const JVal *c = cases->items[i];
                    const char *bad = (c->type != J_OBJ) ? CASE_SKIP_OBJ : NULL;
                    if (case_skipped(i, bad, &n_skip, stdout)) continue;
                    BatchCase bc;
                    bad = batch_case_cube(c, f, &bc, NULL, NULL);
                    if (case_skipped(i, bad, &n_skip, stdout)) continue;
                    char cs[55];
                    if (bc.scr) {
                        const char *err = cube_scramble(bc.scr, cs);
                        if (err) {
                            n_skip++;
                            printf("case entry %d skipped (bad maneuver): %s\n  %s\n",
                                   i + 1, err, bc.scr);
                            continue;
                        }
                    } else {
                        memcpy(cs, bc.cs, sizeof cs);
                    }
                    BatchCfg cfg = dflt;
                    bad = batch_cfg_overlay(c, &cfg);
                    if (case_skipped(i, bad, &n_skip, stdout)) continue;
                    const char *name = j_str(c, "name");
                    idx++;
                    stopped = batch_solve_one(idx, bc.orig, cs, &cfg, name, &n_ok, &n_fail);
                }
            }
            j_free(doc);
        }
    } else {
        printf("input format: text (one case per line)\n");
        BatchCfg cfg = batch_cfg_default();
        char *save = text;
        while (*save && !stopped) {
            char line[JOB_LINE_MAX];
            char *nl = strchr(save, '\n');
            size_t len = nl ? (size_t)(nl - save) : strlen(save);
            int too_long = (len >= sizeof line);
            if (too_long) len = sizeof line - 1;
            memcpy(line, save, len);
            line[len] = 0;
            save = nl ? nl + 1 : save + strlen(save);

            strip_crlf(line);
            char *q = line;
            while (*q == ' ' || *q == '\t') q++;
            if (!*q || *q == '#') continue;

            /* a line that did not fit would be solved as its head: a different case */
            if (too_long) {
                n_skip++;
                printf("case %d skipped (line longer than %d characters)\n",
                       idx + 1, (int)sizeof line - 1);
                continue;
            }

            /* text mode takes an optional "name, " prefix; no comma, the line is the case */
            char *comma = strchr(q, ',');
            const char *name = NULL;
            char *rest = q;
            if (comma) {
                *comma = 0;
                char *nm = q;
                while (*nm == ' ' || *nm == '\t') nm++;
                char *e = comma;
                while (e > nm && (e[-1] == ' ' || e[-1] == '\t')) e--;
                *e = 0;
                if (*nm) name = nm;
                if (name && !case_name_ok(name)) {
                    n_skip++;
                    printf("case %d skipped (not usable as a file name): %s\n", idx + 1, name);
                    continue;
                }
                rest = comma + 1;
                while (*rest == ' ' || *rest == '\t') rest++;
            }
            /* cube_scramble("") answers the solved cube, so "name," would run and write silently */
            if (!*rest) {
                n_skip++;
                printf("case %d skipped (empty maneuver)\n", idx + 1);
                continue;
            }
            char cs[55];
            const char *bad = NULL;
            int have_cs = 0;
            if (strlen(rest) == 54) {
                bad = cs_problem(rest, f);
                if (!bad) {
                    strcpy(cs, rest);
                    have_cs = 1;
                }
            }
            /* a maneuver, or 54 chars no parser took: cube_scramble writes cs either way */
            if (!have_cs) bad = cube_scramble(rest, cs);
            if (bad) {
                n_skip++;
                printf("case %d skipped (not a cubestring or maneuver): %s\n  %s\n",
                       idx + 1, bad, rest);
                continue;
            }
            idx++;
            stopped = batch_solve_one(idx, rest, cs, &cfg, name, &n_ok, &n_fail);
        }
    }
    free(text);
    printf("\nbatch done: %d solved, %d failed, %d skipped%s\n",
           n_ok, n_fail, n_skip, stopped ? " (stopped early)" : "");

    g_table_jmp = NULL;
    worker_done(WM_BATCH_DONE, (WPARAM)(n_ok + n_fail));
    return 0;
}

/* NULL when OUT_DIR_DEFAULT cannot be made */
static const char *batch_out_dir(void);

/* runs on the GUI thread when the batch worker is done */
static void finish_batch(WPARAM written)
{
    finish_worker();
    const char *dir = batch_out_dir();
    /* named, not "the output folder": a dropped file never asked which one to use */
    if (!dir) set_status("Batch finished -- %u solution file(s); %s could not be made",
                         (unsigned)written, OUT_DIR_DEFAULT);
    else set_status("Batch finished -- %u solution file(s) in %s", (unsigned)written, dir);
}

static const char *batch_out_dir(void)
{
    if (g_batch_out[0]) return g_batch_out;
    if (!make_one_dir(OUT_DIR_DEFAULT)) return NULL;
    return OUT_DIR_DEFAULT;
}

/* File > Batch solve once its dialogs are answered, or a dropped file: an input file, a folder */
static void begin_batch(const char *in, const char *out)
{
    /* an entry point that forgets starts a second worker */
    if (g.busy) {
        ui_msg(g.hwnd, "A search is still running.", BATCH_TITLE, MB_OK);
        return;
    }
    if (!tables_ready()) return;
    strcpy(g_batch_in, in);
    /* A dropped file hands back g_batch_out itself once set, and strcpy is declared restrict */
    if (out != g_batch_out) strcpy(g_batch_out, out);

    if (!read_req()) return;

    solve_busy(1);
    /* Nothing of a batch reaches the list box, so the previous run's list is cleared here */
    clear_solutions();
    char head[MAX_PATH * 2 + 160];
    snprintf(head, sizeof head,
             "batch(%s -> %s, depth %d, subgroup %s, only_optimal=%d, sort=%d)\n",
             in, out, g_req.depth, g_req.subgroup,
             g_req.only_optimal, g_req.sort_solutions);
    log_run_start(head);
    log_redirect("a");
    g_worker = start_worker(batch_worker, BATCH_TITLE);
}

#endif /* GUIBATCH_H */
