/* Cubolt (https://github.com/nbwzx/cubolt)
 * Copyright (C) 2026 Zixing Wang <zixingwang.cn@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* search.c -- command-line front end of engine.h: one case, start to finish. */
#include "engine.h"
#include "caseout.h"
#include "jsonlite.h"
#include "batchcase.h"
#include "version.h"
#include "update.h"
#include <fcntl.h>
#include <io.h>

/* --no-sort: list solutions in discovery order, not ranked by STM length */
static int no_sort = 0;

/* stdout when the user asked for it, stderr when it explains an error. */
static void usage(FILE *f)
{
    fprintf(f,
        "usage: search.exe [<name> [<depth> [<subgroup>]]] (--cubestring <cubestring>\n"
        "                  | --scramble <algorithm>) [--out <dir>] [--quiet]\n"
        "                  [--optimal] [--goal <cubestring>] [--threads N]\n"
        "                  [--dump] [--solhash] [--no-sort]\n"
        "       search.exe --print --scramble <alg>\n"
        "       search.exe --scramble-list <file>\n"
        "       search.exe --bench <jobfile> [--threads N] [--solhash] [--out DIR]\n"
        "       search.exe --batch <jobfile> [--threads N] [--out DIR]\n"
        "       search.exe --json <casefile> [--out DIR] [--threads N]\n"
        "       search.exe --check-update\n"
        "\n");
    fprintf(f, "  <depth>    the longest maneuver listed (default %d)\n", DEFAULT_DEPTH);
    fprintf(f, "  <subgroup> the letters a solution is spelled with (default %s)\n", FACE_LET);
    fprintf(f, "  two positionals whose first is all digits are <depth> <subgroup>: 8 URFDLB is\n"
               "           eight moves in those faces.  One positional alone is refused: it could\n"
               "           be either.\n");
    fprintf(f,
        "  ? facelets  an incomplete cube: every known cubie restored, the unknown ones "
        "left anywhere,\n"
        "           except the six centers: they name the frame.  Up to "
                    CUBOLT_STR_(IC_K_CAP) " ways of filling the\n"
        "           rest in are answered one ordinary search each, or by one relaxed search over\n"
        "           all of them once they are more than " CUBOLT_STR_(IC_WAYS_CUT) " and no corner "
        "is unknown.\n"
        "           Past the cap only the relaxed search can answer, which prunes far less but\n"
        "           runs once.\n");
    fprintf(f,
        "  --cubestring <cs>  the position as 54 facelets in URFDLB order, read in the "
        "fixed-center frame\n"
        "  --scramble <alg>   the position as an algorithm: one word on the command line, so\n"
        "           a maneuver with spaces is quoted; a job line reads to the next flag\n"
        "  --out    the directory the solution files are written to (default "
        OUT_DIR_DEFAULT "/ for a <name>)\n"
        "  --quiet  do not print the solution list to stdout (default when a file is written)\n"
        "  --optimal  stop at the shortest solution instead of listing every one up to <depth>\n"
        "  --goal <cubestring>  solve to that cube instead of the solved one; the goal is "
        "read in\n"
        "           the fixed-center frame too, so one held turned is reached by a rotation\n"
        "           the answer carries -- and a subgroup spelling none of them is refused\n"
        "  --threads N    workers walking a deep pass: 0 is one per logical core (the default),\n"
        "                 1 is the serial walk, and 2 to "
                    CUBOLT_STR_(THREAD_CAP) " ask for that many\n"
        "  --dump   print each solution as the solver walks it, in its own tokens, and skip\n"
        "           the standard-notation list\n"
        "  --no-sort  list solutions in the order the walk finds them, not ranked by STM\n"
        "           length (rotations do not count either way); off by default\n"
        "  --solhash  hash the run's solutions and print the sum: with --bench it fills the\n"
        "           row's solhash column.  Off by default.\n"
        "  --print  print the cubestring the algorithm leaves, and nothing else\n"
        "  --complete <cubestring>  fill every '?' the laws of the cube force and print it:\n"
        "           a sticker no legal completion can vary is written, the rest stay '?'\n"
        "  --scramble-list <file>  one cubestring per line of algorithms, in one process:\n"
        "           a bad line answers ! <why> on that line\n"
        "  --bench  a file of cases, one line each: <name> <depth> <subgroup>\n"
        "           all three, in that order: a line that leaves one out ends the file\n"
        "           [--cubestring <cs> | --scramble <alg>] [--goal <cs>]\n"
        "           [--optimal]; # starts a comment.  --threads is the whole file's: every\n"
        "           line is read before the first one runs, so the last line naming one sets\n"
        "           it for all.\n"
        "           One pair of quotes around a field is taken off, so a maneuver with spaces\n"
        "           is written --scramble \"R U R'\", the way a command line takes it.\n"
        "           One row per case, sampled once: the answers, then wall and search seconds.\n"
        "  --batch  the same file, run for the block reports instead of the table: it writes\n"
        "           no file, and a line's --dump lists each solution in its own tokens.\n"
        "  --json   a [ ... ] array of cases.  Every case may carry\n"
        "           name, scramble or cubestring, subgroup, depth, only_optimal, sort, dump,\n"
        "           goal or threads; anything the command line sets is the default.  A case\n"
        "           that leaves a key out keeps that default.\n"
        "           One <name>.txt (else case_NNN.txt) per case in --out (default "
                    OUT_DIR_DEFAULT "/).\n"
        "  --check-update  ask the project's GitHub releases for the latest tag and say whether\n"
        "           this build is behind it -- the only network request Cubolt makes\n"
        VERSION_HELP_TEXT);
}

/* malloc'd: Job.outpath keeps it and run_json_batch frees it once every case is through */
static char *json_case_out(const char *outdir, int idx, const char *name)
{
    char *p = case_out_alloc(outdir, idx, name);
    if (!p) { fprintf(stderr, OOM_TEXT); exit(1); }
    return p;
}

/* Nothing here lives on the parse loop's stack: the output path is named when the case runs */
typedef struct {
    Job  j;
    int  idx;
    char name[160];
    char cs[55];                /* the cubestring, when the case gave one rather than a scramble */
    int  slice;                 /* 1 = STM engine, 0 = FTM: which pruning table the case wants */
    int  sort;                  /* this case's rend_do_sort, restored before it runs */
    int  threads;               /* -1 = the command line's --threads, 0 = auto, n = n workers */
} JCase;

/* --json <casefile> [--out DIR]: one process, one table load per engine, one file per case. */
static int run_json_batch(const char *path, const char *outdir, char **tok, int n,
                          const char *cli_scr)
{
    progress_to_stderr = 1;
    Job dflt = job_default();
    const char *err = NULL;
    if (parse_job(tok, n, &dflt, &err)) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    if (!outdir) outdir = OUT_DIR_DEFAULT;

    const char *why = NULL;
    char *text = batch_read_text(path, &why);
    if (!text) { fprintf(stderr, "%s\n", why); return 1; }

    const char *p = batch_start(text);
    if (*p != '[') {
        fprintf(stderr, "%s: JSON input must be a [ ... ] array of cases\n", path);
        free(text);
        return 1;
    }
    JVal *doc = j_parse(p);
    if (!doc) {
        fprintf(stderr, "%s: %s\n", path, j_err);
        free(text);
        return 1;
    }

    const JVal *cases = doc;
    /* No cases is a failed run, not a finished one */
    if (cases->n == 0) {
        fprintf(stderr, NO_JOBS_MSG, path);
        j_free(doc);
        free(text);
        return 1;
    }

    int n_ok = 0, n_bad = 0, n_skip = 0, idx = 0, f[54];
    JCase *cs = calloc((size_t)cases->n, sizeof *cs);
    if (!cs) { fprintf(stderr, OOM_TEXT); j_free(doc); free(text); return 1; }
    int nc = 0;
    int sort_default = no_sort ? 0 : rend_do_sort;
    for (int i = 0; i < cases->n; i++) {
        const JVal *c = cases->items[i];
        const char *bad = (c->type != J_OBJ) ? CASE_SKIP_OBJ : NULL;
        if (case_skipped(i, bad, &n_skip, stderr)) continue;
        BatchCase bc;
        bad = batch_case_cube(c, f, &bc, cli_scr, dflt.cubestring);
        if (case_skipped(i, bad, &n_skip, stderr)) continue;
        const char *name = j_str(c, "name");
        JCase *k = &cs[nc];
        k->j = dflt;
        rend_do_sort = sort_default;
        bad = job_overlay(&k->j, c, MAX_DEPTH);
        if (case_skipped(i, bad, &n_skip, stderr)) continue;
        k->sort    = rend_do_sort;
        k->threads = -1;
        if (case_skipped(i, job_overlay_threads(c, &k->threads), &n_skip, stderr)) continue;
        memcpy(k->cs, bc.cs, sizeof k->cs);
        if (bc.scr) k->j.scramble   = bc.scr;
        else        k->j.cubestring = k->cs;
        if (!name || !*name) case_default_name(k->name, sizeof k->name, idx + 1);
        else if (strlen(name) >= sizeof k->name) {
            /* refused, not cut down: two names that start alike would land on one file */
            static char toolong[160];
            snprintf(toolong, sizeof toolong,
                     "name is %d bytes, %d is the longest a case name can be",
                     (int)strlen(name), (int)sizeof k->name - 1);
            if (case_skipped(i, toolong, &n_skip, stderr)) continue;
        }
        else snprintf(k->name, sizeof k->name, "%s", name);
        idx++;
        k->j.name = k->name;
        k->idx = idx;
        k->slice = job_slice_mode(k->j.subgroup);
        nc++;
    }

    /* a file nothing came of gets no table mapped and no folder made */
    if (nc) {
        if (!make_outdir(outdir)) { free(cs); j_free(doc); free(text); return 1; }
        load_shared_tables();
    }

    /* STM first, then FTM: alternating would swap the pruning table */
    int cli_threads = par_threads;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < nc; i++) {
            if (cs[i].slice != (pass == 0)) continue;
            rend_do_sort = no_sort ? 0 : (cs[i].sort || cs[i].j.opt);
            int thr = cs[i].threads >= 0 ? cs[i].threads : cli_threads;
            par_threads = thr;
            printf(JOB_MARK, cs[i].j.name);
            fflush(stdout);
            cs[i].j.outpath = json_case_out(outdir, cs[i].idx, cs[i].name);
            if (run_job(&cs[i].j)) {
                /* stdout only: a reader of it has to see every case end one way or the other */
                printf(FAIL_MARK, cs[i].j.name);
                n_bad++;
            } else {
                printf(END_MARK, cs[i].j.name);
                n_ok++;
            }
            fflush(stdout);
            free((char *)cs[i].j.outpath);
            cs[i].j.outpath = NULL;
        }
    free(cs);
    j_free(doc);
    free(text);
    printf("json batch done: %d solved, %d failed, %d skipped\n", n_ok, n_bad, n_skip);
    /* Skipped counts as failure: the file asked for the case and the run never tried it. */
    return (n_bad || n_skip) ? 1 : 0;
}

/* Flags whose own value is a token: it must not be read as the --json file name. */
static int flag_wants_value(const char *t)
{
    return !strcmp(t, "--threads") ||
           !strcmp(t, "--goal") || !strcmp(t, "--cubestring");
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOFBF, 1 << 16);
    /* binary: solution files are fopen'd "wb", so stdout must match or a diff gains a CR */
    _setmode(_fileno(stdout), _O_BINARY);

    /* these three are parse_job's own blind spots: none of them may load a table */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) {
            print_version();
            return 0;
        }
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(stdout);
            return 0;
        }
        if (!strcmp(argv[i], "--check-update")) {
            char tag[64], why[192];
            print_version();
            if (update_latest(tag, sizeof tag, why, sizeof why)) {
                fprintf(stderr, "update check failed: %s\n", why);
                return 1;
            }
            if (ver_cmp(CUBOLT_VERSION, tag) < 0) {
                printf("a newer release is out: %s\n%s\n", tag, CUBOLT_RELEASES);
            } else {
                printf("latest release: %s -- this build is up to date\n", tag);
            }
            return 0;
        }
    }

    /* argv's own pointers: only the array is ours, so no path out of main has to free it. */
    char *tok[argc];
    int ntok = 0;
    for (int i = 1; i < argc; i++) tok[ntok++] = argv[i];

    /* the two flags with no value are pulled out here, so they work anywhere on the line */
    const char *late_mode = NULL;
    {
        int w = 0;
        for (int i = 0; i < ntok; i++) {
            if (!strcmp(tok[i], "--solhash")) { hash_wanted = 1; continue; }
        if (!strcmp(tok[i], "--no-sort")) { no_sort = 1; continue; }
            /* it is read at the head of the line only; the name is kept to say which */
            if (!strcmp(tok[i], "--batch") || !strcmp(tok[i], "--bench")) late_mode = tok[i];
            tok[w++] = tok[i];
        }
        ntok = w;
    }

    /* one table load for the whole file: --bench prints rows, --batch prints blocks */
    if (ntok >= 1 && (!strcmp(tok[0], "--batch") || !strcmp(tok[0], "--bench"))) {
        int bench = !strcmp(tok[0], "--bench");
        const char *outdir = NULL;
        if (ntok < 2) { fprintf(stderr, "%s needs a job file\n", tok[0]); return 1; }
        for (int i = 2; i < ntok; i++) {
            if (!strcmp(tok[i], "--threads")) {
                int tv;
                const char *err;
                if (threads_flag(tok, ntok, &i, &tv, &err)) {
                    fprintf(stderr, "%s\n", err);
                    return 1;
                }
                par_threads = tv;
            } else if (!strcmp(tok[i], "--out")) {
                if (i + 1 >= ntok) { fprintf(stderr, "--out needs a directory\n"); return 1; }
                const char *d = tok[++i];
                if (d[0] == '/' || (d[0] == '\\' && d[1] != '\\')) {
                    fprintf(stderr,
                            "--out %s: a leading / or \\ is read on the current drive\n", d);
                    return 1;
                }
                outdir = d;
            }
            /* --threads and --out are the only ones; anything further is refused, not dropped */
            else if (tok[i][0] == '-') {
                fprintf(stderr, "%s is not an option %s takes\n", tok[i], tok[0]);
                return 1;
            } else {
                fprintf(stderr, "%s takes one job file\n", tok[0]);
                return 1;
            }
        }
        tab_out = bench;
        if (no_sort) rend_do_sort = 0;
        return run_batch(tok[1], outdir);
    }

    const char *scr = NULL, *serr = NULL;
    /* one word: the positionals after --scramble are the case's, not more of the maneuver */
    int n = split_scramble(tok, ntok, 1, &scr, &serr);
    if (n < 0) { fprintf(stderr, "%s\n", serr); return 1; }

    int print_mode = 0, list_mode = 0, json_mode = 0, complete_mode = 0;
    int pend_outdir = 0, cs_given = 0;
    const char *listpath = NULL, *jsonpath = NULL, *cspath = NULL, *outdir = NULL;
    int w = 0;
    for (int i = 0; i < n; i++) {
        if (!strcmp(tok[i], "--cubestring")) cs_given = 1;
        if (!strcmp(tok[i], "--print")) print_mode = 1;
        else if (!strcmp(tok[i], "--scramble-list")) list_mode = 1;
        else if (!strcmp(tok[i], "--json")) json_mode = 1;
        else if (!strcmp(tok[i], "--complete")) complete_mode = 1;
        else if (!strcmp(tok[i], "--out")) pend_outdir = 1;
        else if (pend_outdir) {
            if (tok[i][0] == '-') break;    /* left pending, so the check reports it */
            if (tok[i][0] == '/' || (tok[i][0] == '\\' && tok[i][1] != '\\')) {
                fprintf(stderr, "--out %s: a leading / or \\ is read on the current drive\n",
                        tok[i]);
                return 1;
            }
            outdir = tok[i]; pend_outdir = 0;
        }
        else if (flag_wants_value(tok[i]) && i + 1 < n) { tok[w++] = tok[i]; tok[w++] = tok[++i]; }
        else if (list_mode && !listpath && tok[i][0] != '-') listpath = tok[i];
        else if (json_mode && !jsonpath && tok[i][0] != '-') jsonpath = tok[i];
        else if (complete_mode && !cspath && tok[i][0] != '-') cspath = tok[i];
        else tok[w++] = tok[i];
    }
    n = w;

    if (pend_outdir) { fprintf(stderr, "--out needs a directory\n"); return 1; }
    if (list_mode && !listpath) { fprintf(stderr, "--scramble-list needs a file\n"); return 1; }
    if (json_mode && !jsonpath) { fprintf(stderr, "--json needs a case file\n"); return 1; }
    if (complete_mode && !cspath) { fprintf(stderr, "--complete needs a cubestring\n"); return 1; }
    /* each mode reads its own file and returns: a second one would be swallowed without a word */
    if (!!list_mode + !!json_mode + !!print_mode + !!complete_mode > 1) {
        fprintf(stderr, "--scramble-list, --json, --print and --complete are four modes: "
                        "give one\n");
        return 1;
    }
    /* --out names the directory --json writes into; the other two print and file nothing */
    if (outdir && (print_mode || list_mode)) {
        fprintf(stderr, "--out is the --json mode's: %s prints and files nothing\n",
                print_mode ? "--print" : "--scramble-list");
        return 1;
    }
    /* --print turns the maneuver into a cube: a cubestring beside it is read and then unused */
    if (print_mode && cs_given && scr) {
        fprintf(stderr, "--print reads --scramble: --cubestring would be dropped\n");
        return 1;
    }
    /* neither mode reads a table, and --solhash is the one flag that reports on one */
    if ((print_mode || list_mode) && hash_wanted) {
        fprintf(stderr, "%s reads no table, so --solhash has nothing to say\n",
                print_mode ? "--print" : "--scramble-list");
        return 1;
    }
    if (late_mode) {
        fprintf(stderr, "%s is read at the head of the command line: here it would be dropped\n",
                late_mode);
        return 1;
    }

    /* One process runs the whole list: turning a maneuver into a cube needs no table. */
    if (listpath) {
        FILE *lf = fopen(listpath, "rb");
        if (!lf) { fprintf(stderr, CANNOT_OPEN, listpath); return 1; }
        skip_bom(lf);
        char line[4096], cs[55];
        int bad = 0, lineno = 0;
        for (;;) {
            const char *why = NULL;
            int got = read_line(line, sizeof line, lf, &why);
            if (!got) break;
            lineno++;
            /* one line in, one cubestring out: a line read_line refused is named, not shortened */
            if (got < 0) {
                printf("! " BAD_LINE_FMT, listpath, lineno, why);
                fprintf(stderr, BAD_LINE_FMT, listpath, lineno, why);
                bad++;
                /* nothing more is coming: a list cut short is not a list with a bad line in it */
                if (got == -2) break;
                continue;
            }
            /* blanks only is the blank line it looks like, not a scramble of zero turns */
            char *nb = line + strspn(line, " \t");
            if (!*nb) { printf("\n"); continue; }
            /* a comment, as it is in a batch file */
            if (*nb == '#') { printf("\n"); continue; }
            const char *err = cube_scramble(line, cs);
            if (err) { printf("! " BAD_LINE_FMT, listpath, lineno, err); bad++; }
            else     printf("%s\n", cs);
        }
        fclose(lf);
        return bad ? 1 : 0;
    }

    if (print_mode) {
        if (!scr) { fprintf(stderr, "--print needs --scramble\n"); return 1; }
        char cs[55];
        const char *err = cube_scramble(scr, cs);
        if (err) { fprintf(stderr, "%s: %s\n", scr, err); return 1; }
        printf("%s\n", cs);
        return 0;
    }

    if (complete_mode) {
        char out[55];
        int nfill = ic_complete(cspath, out);
        if (nfill < 0) {
            fprintf(stderr, "%s\n", incomp_problem(cspath, NULL, NULL, NULL));
            return 1;
        }
        printf("%s\n", out);
        return 0;
    }

    if (json_mode) return run_json_batch(jsonpath, outdir, tok, n, scr);

    Job j = job_default();
    j.scramble = scr;
    const char *err = NULL;
    int how = parse_job(tok, n, &j, &err);
    if (how) {
        fprintf(stderr, "%s\n", err);
        /* the usage answers a command it could not read, not a value it would not take */
        if (how == 2) usage(stderr);
        return 1;
    }
    /* Before a directory is made and before the tables are mapped: nothing is searched. */
    if (!j.cubestring && !(j.scramble && *j.scramble)) {
        fprintf(stderr, "%s\n", j.scramble ? "--scramble is empty"
                                           : "no cube: give --cubestring or --scramble");
        return 1;
    }
    /* a '?' cube is read by ic_parse, which does not run until the tables are mapped either */
    if (j.cubestring && strchr(j.cubestring, '?')) {
        const char *bad = incomp_problem(j.cubestring, NULL, NULL, NULL);
        if (bad) { fprintf(stderr, "%s\n", bad); return 1; }
    }
    /* a name that cannot be a file is refused, not swapped for case_001 */
    if (*j.name && !case_name_ok(j.name)) {
        fprintf(stderr, "case name '%s' is not usable as a file name\n", j.name);
        return 1;
    }
    if (*j.name || outdir) {
        const char *dir = outdir ? outdir : OUT_DIR_DEFAULT;
        if (!make_outdir(dir)) return 1;
        char *p = case_out_alloc(dir, 1, *j.name ? j.name : NULL);
        if (!p) { fprintf(stderr, OOM_TEXT); return 1; }
        j.outpath = p;
    }

    if (no_sort) rend_do_sort = 0;
    load_shared_tables();
    int rc = run_job(&j);
    free((char *)j.outpath);
    return rc;
}
