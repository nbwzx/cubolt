/* Cubolt (https://github.com/nbwzx/cubolt)
 * Copyright (C) 2026 Zixing Wang <zixingwang.cn@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* incomp.h -- '?' facelets and wildcard cubies, with the pruning tables built at run time. */
#ifndef INCOMP_H
#define INCOMP_H

/* The parsed input: cubie per position (-1/3 unknown); kc/ke = a visible sticker, kfull = all. */
typedef struct {
    int  cp[8];
    int  co[8];
    int  ep[12];
    int  eo[12];
    int  kc[8], ke[12];
    int  kfull_c[8], kfull_e[12];
    int  nkc, nke;                  /* counts of kfull_c / kfull_e (the printed "known" tally) */
    int  has_partial;
    int  pvis_c[8], pvis_e[12];     /* per position: bit t set when slot t was given a color */
    int  pc_pair[8];                /* per partial corner: bit id*3+ori of every pair the visible
                                     * colors allow; id and ori apart admit impossible pairs */
    int  pe_pair[12];               /* per partial edge: bit id*2+flip */
} InCube;

static Cube ic_members[IC_K_CAP];
static int  ic_nmembers;
static InCube ic_in;                          /* the parse the search and the enumeration run on */

/* Slot i holds cubie j in orientation o; one candidate is enough for a partial corner. */
static const char *ic_know_corner(InCube *r, int seen[8], int i, int j, int o)
{
    if (seen[j]) return "a corner cubie appears twice";
    seen[j] = 1;
    r->cp[i] = j; r->co[i] = o;
    r->kc[i] = r->kfull_c[i] = 1; r->nkc++;
    return NULL;
}
static const char *ic_know_edge(InCube *r, int seen[12], int i, int j, int o)
{
    if (seen[j]) return "an edge cubie appears twice";
    seen[j] = 1;
    r->ep[i] = j; r->eo[i] = o;
    r->ke[i] = r->kfull_e[i] = 1; r->nke++;
    return NULL;
}

/* Parse `s` into `r`, colors relabeled to centers; no tables, so the GUI thread may call. */
static const char *ic_parse(const char *s, InCube *r)
{
    memset(r, 0, sizeof *r);
    for (int i = 0; i < 8;  i++) { r->cp[i] = -1; r->co[i] = 3; }
    for (int i = 0; i < 12; i++) r->ep[i] = -1;

    if (strlen(s) != 54) return cube_bad_length(s);

    int f[54];
    for (int i = 0; i < 54; i++) {
        if (s[i] == '?') { f[i] = -1; continue; }
        int c = cube_face(s[i]);
        if (c < 0) return CUBE_BAD_COLOR;
        f[i] = c;
    }
    {
        int map[6], inv[6] = {0};
        for (int g = 0; g < 6; g++) {
            int c = f[9 * g + 4];
            if (c < 0) return "a center is unknown -- the six centers must be given";
            if (inv[c]++) return "two centers show the same color";
            map[c] = g;
        }
        for (int i = 0; i < 54; i++)
            if (f[i] >= 0) f[i] = map[f[i]];
    }

    int seen_c[8] = {0}, seen_e[12] = {0};
    for (int i = 0; i < 8; i++) {
        const int *fac = CORNER_FACELET[i];
        int q = 0;
        for (int t = 0; t < 3; t++) if (f[fac[t]] < 0) q++;
        if (q == 3) continue;
        if (q == 0) {
            int ori = 2;
            for (int t = 0; t < 3; t++)
                if (f[fac[t]] == 0 || f[fac[t]] == 3) { ori = t; break; }
            int col1 = f[fac[(ori + 1) % 3]], col2 = f[fac[(ori + 2) % 3]];
            for (int j = 0; j < 8; j++)
                /* the U/D facelet too: two sides alone would match a mirrored corner */
                if (f[fac[ori]] == CORNER_COLOR[j][0] &&
                    col1 == CORNER_COLOR[j][1] && col2 == CORNER_COLOR[j][2]) {
                    const char *e = ic_know_corner(r, seen_c, i, j, ori);
                    if (e) return e;
                    break;
                }
            if (!r->kc[i]) return "a corner's colors name no cubie";
            continue;
        }
        /* partial corner: every (j, oi) the slots allow; one alone names the cubie */
        int vis = 0, pair = 0, pj = -1, po = 0;
        for (int t = 0; t < 3; t++) if (f[fac[t]] >= 0) vis |= 1 << t;
        for (int j = 0; j < 8; j++) {
            for (int oi = 0; oi < 3; oi++) {
                int ok = 1;
                for (int t = 0; t < 3; t++)
                    if ((vis >> t & 1) && f[fac[t]] != CORNER_COLOR[j][(t - oi + 3) % 3]) ok = 0;
                if (ok) { pair |= 1 << (j * 3 + oi); pj = j; po = oi; }
            }
        }
        if (!pair) return "a corner's visible colors name no cubie";
        if (!(pair & (pair - 1))) {
            const char *e = ic_know_corner(r, seen_c, i, pj, po);
            if (e) return e;
            continue;
        }
        r->pc_pair[i] = pair;
        r->pvis_c[i] = vis;
        r->kc[i] = 1; r->kfull_c[i] = 0;
        r->has_partial = 1;
    }
    for (int i = 0; i < 12; i++) {
        const int *fac = EDGE_FACELET[i];
        int q = 0;
        for (int t = 0; t < 2; t++) if (f[fac[t]] < 0) q++;
        if (q == 2) continue;
        if (q == 0) {
            int a = f[fac[0]], b = f[fac[1]], id = -1, eo = 0;
            for (int j = 0; j < 12; j++) {
                if (a == EDGE_COLOR[j][0] && b == EDGE_COLOR[j][1]) { id = j; eo = 0; break; }
                if (a == EDGE_COLOR[j][1] && b == EDGE_COLOR[j][0]) { id = j; eo = 1; break; }
            }
            if (id < 0) return "an edge's colors name no cubie";
            const char *e = ic_know_edge(r, seen_e, i, id, eo);
            if (e) return e;
            continue;
        }
        /* partial edge: one visible sticker, the same walk as the corner branch two slots wide */
        int vis = 0, pair = 0, pj = -1, po = 0;
        for (int t = 0; t < 2; t++) if (f[fac[t]] >= 0) vis |= 1 << t;
        for (int j = 0; j < 12; j++) {
            for (int eo = 0; eo < 2; eo++) {
                int ok = 1;
                for (int t = 0; t < 2; t++)
                    if ((vis >> t & 1) && f[fac[t]] != EDGE_COLOR[j][t ^ eo]) ok = 0;
                if (ok) { pair |= 1 << (j * 2 + eo); pj = j; po = eo; }
            }
        }
        if (!pair) return "an edge's visible color names no cubie";
        if (!(pair & (pair - 1))) {
            const char *e = ic_know_edge(r, seen_e, i, pj, po);
            if (e) return e;
            continue;
        }
        r->pe_pair[i] = pair;
        r->pvis_e[i] = vis;
        r->ke[i] = 1; r->kfull_e[i] = 0;
        r->has_partial = 1;
    }
    if (r->nkc + r->nke == 0 && !r->has_partial)
        return "no cubie is known -- name at least one cubie";
    return NULL;
}

/* ---- walking the completions ---- */
/* One member at a time: ic_expand wants them all, Auto complete only what they agree on.  The
   state travels with the walk, so the GUI can hold one while a search holds its own. */
typedef struct {
    const InCube *in;
    Cube *dst;                            /* where the members go */
    int cap;                              /* stop this many members in */
    long budget;                          /* nodes left to seat; negative for no counting */
    int n, stop;                          /* members written; stop: the walk was cut short */
    const unsigned char *cord, *eord;     /* order to seat in; NULL for ascending */
    int cp[8], co[8], ep[12], eo[12];
} IcSeat;

/* The (cubie, orientation) pairs a position still allows: bit id*3+ori, or id*2+flip. */
static unsigned ic_pairs(const InCube *in, int corner, int i)
{
    if (corner)
        return in->kfull_c[i] ? 1u << (in->cp[i] * 3 + in->co[i])
             : in->kc[i] ? (unsigned)in->pc_pair[i] : 0xFFFFFFu;
    return in->kfull_e[i] ? 1u << (in->ep[i] * 2 + in->eo[i])
         : in->ke[i] ? (unsigned)in->pe_pair[i] : 0xFFFFFFu;
}

/* Room for one more node?  stop tells the caller which of the two ways the walk ended. */
static int ic_done(IcSeat *w)
{
    if (w->n < w->cap && (w->budget < 0 || w->budget-- > 0)) return 0;
    w->stop = 1;
    return 1;
}

static void ic_seat_edge(IcSeat *w, int k, int used, int flips)
{
    if (ic_done(w)) return;
    if (k == 12) {
        /* even flip sum and agreeing parities are the only laws left */
        if (!(flips & 1) && !((perm_parity(w->cp, 8) ^ perm_parity(w->ep, 12)) & 1)) {
            Cube c;
            for (int i = 0; i < 8;  i++) { c.cp[i] = (s8)w->cp[i]; c.co[i] = (s8)w->co[i]; }
            for (int i = 0; i < 12; i++) { c.ep[i] = (s8)w->ep[i]; c.eo[i] = (s8)w->eo[i]; }
            w->dst[w->n++] = c;
        }
        return;
    }
    int pos = w->eord ? w->eord[k] : k;
    unsigned pairs = ic_pairs(w->in, 0, pos);
    for (int j = 0; j < 12; j++) {
        if (used >> j & 1) continue;
        for (int f = 0; f < 2; f++) {
            if (!(pairs >> (j * 2 + f) & 1)) continue;
            w->ep[pos] = j; w->eo[pos] = f;
            ic_seat_edge(w, k + 1, used | 1 << j, flips + f);
            if (w->stop) return;
        }
    }
}

static void ic_seat_corner(IcSeat *w, int k, int used, int tw)
{
    if (ic_done(w)) return;
    if (k == 8) {
        if (!(tw % 3)) ic_seat_edge(w, 0, 0, 0);   /* the twists of a whole cube sum to zero */
        return;
    }
    int pos = w->cord ? w->cord[k] : k;
    unsigned pairs = ic_pairs(w->in, 1, pos);
    for (int j = 0; j < 8; j++) {
        if (used >> j & 1) continue;
        for (int o = 0; o < 3; o++) {
            if (!(pairs >> (j * 3 + o) & 1)) continue;
            w->cp[pos] = j; w->co[pos] = o;
            ic_seat_corner(w, k + 1, used | 1 << j, tw + o);
            if (w->stop) return;
        }
    }
}

/* Every consistent cube, member 0 the canonical fill; cap 1 for run_incomplete, else IC_K_CAP. */
static void ic_enum_members(int cap)
{
    IcSeat w = { .in = &ic_in, .dst = ic_members, .cap = cap, .budget = -1 };
    ic_seat_corner(&w, 0, 0, 0);
    ic_nmembers = w.n;
}

/* Counted over an (ids used, ori sum, parity) memo, not walked. */
#define IC_CNT_SAT  1000000000ULL            /* far above IC_K_CAP, low enough not to overflow */
#define IC_CAND     24                       /* 8 ids x 3 twists, or 12 ids x 2 flips */
#define IC_CNT_MEMO 16384                    /* (mask, ori, parity): 2^12 x 2 x 2 is the widest */

/* The widest ic_iter_fill key: 12 slots, the largest ori sum, the parity: the last slot. */
#define IC_CNT_KEY_MAX (((1 << 12) - 1) * 2 + 1) * 2 + 1
_Static_assert(IC_CNT_KEY_MAX < IC_CNT_MEMO, "IC_CNT_MEMO is smaller than the memo key gets");

typedef struct { int id, ori; } IcCand;

static unsigned long long ic_iter_fill(int depth, int mask, int ori, int parity,
                                       const IcCand *cd, const int *ncd, int npos, int nuni,
                                       unsigned long long *memo, unsigned char *seen,
                                       unsigned long long limit)
{
    int key = (mask * nuni + ori) * 2 + parity;
    if (seen[key]) return memo[key];
    unsigned long long cnt;
    if (depth == npos) {
        cnt = !ori && !parity;
    } else {
        cnt = 0;
        const IcCand *row = cd + (size_t)depth * IC_CAND;
        for (int i = 0; i < ncd[depth]; i++) {
            int piece = row[i].id;
            if (mask >> piece & 1) continue;
            /* seating `piece` here inverts it with every id already seated that outranks it */
            cnt += ic_iter_fill(depth + 1, mask | 1 << piece, (ori + row[i].ori) % nuni,
                                parity ^ (__builtin_popcount((unsigned)(mask >> piece)) & 1),
                                cd, ncd, npos, nuni, memo, seen, limit);
            /* asking only whether any cube is left, the first one found answers */
            if (cnt >= limit) { cnt = limit; break; }
        }
    }
    seen[key] = 1;
    memo[key] = cnt;
    return cnt;
}

/* Counted up to `limit`: a caller that only asks whether any cube is left stops at the first. */
static unsigned long long ic_count_members(const InCube *in, int *ok, unsigned long long limit)
{
    IcCand cd[12][IC_CAND];
    int ncd[12];
    unsigned long long *memo = malloc(IC_CNT_MEMO * sizeof *memo);
    unsigned char *seen = malloc(IC_CNT_MEMO);
    /* per call, not static: the GUI asks the count from its thread while a search may be walking */
    if (!memo || !seen) {
        free(memo);
        free(seen);
        /* Nothing counted: IC_CNT_SAT goes back anyway; the relaxed search answers, slowly. */
        if (ok) *ok = 0;
        return IC_CNT_SAT;
    }
    if (ok) *ok = 1;
    unsigned long long c[2], e[2];

    for (int i = 0; i < 8; i++) {
        int n = 0;
        if (in->kfull_c[i]) { cd[i][n].id = in->cp[i]; cd[i][n].ori = in->co[i]; n++; }
        else for (int j = 0; j < 8; j++)
            for (int oi = 0; oi < 3; oi++)
                if (!in->kc[i] || (in->pc_pair[i] >> (j * 3 + oi) & 1)) {
                    cd[i][n].id = j; cd[i][n].ori = oi; n++;
                }
        ncd[i] = n;
    }
    for (int p = 0; p < 2; p++) {
        memset(seen, 0, IC_CNT_MEMO);
        c[p] = ic_iter_fill(0, 0, 0, p, &cd[0][0], ncd, 8, 3, memo, seen, limit);
    }

    for (int i = 0; i < 12; i++) {
        int n = 0;
        if (in->kfull_e[i]) { cd[i][n].id = in->ep[i]; cd[i][n].ori = in->eo[i]; n++; }
        else for (int j = 0; j < 12; j++)
            for (int eo = 0; eo < 2; eo++)
                if (!in->ke[i] || (in->pe_pair[i] >> (j * 2 + eo) & 1)) {
                    cd[i][n].id = j; cd[i][n].ori = eo; n++;
                }
        ncd[i] = n;
    }
    for (int p = 0; p < 2; p++) {
        memset(seen, 0, IC_CNT_MEMO);
        e[p] = ic_iter_fill(0, 0, 0, p, &cd[0][0], ncd, 12, 2, memo, seen, limit);
    }

    /* the halves are independent but for the parity they share; each counted its own sum */
    unsigned long long t = c[0] * e[0] + c[1] * e[1];
    free(memo); free(seen);
    return t >= limit ? limit : t;
}

/* Why `s` is unusable, or NULL; nkc/nke/npart are set either way, and any may be NULL. */
static const char *incomp_problem(const char *s, int *nkc, int *nke, int *npart)
{
    InCube r;
    const char *err = ic_parse(s, &r);
    if (nkc) *nkc = r.nkc;
    if (nke) *nke = r.nke;
    if (npart) {
        int n = 0;
        for (int i = 0; i < 8;  i++) if (r.kc[i] && !r.kfull_c[i]) n++;
        for (int i = 0; i < 12; i++) if (r.ke[i] && !r.kfull_e[i]) n++;
        *npart = n;
    }
    return err;
}

/* '?' is none of from_string's colors, so a wildcard cube is the incomplete side's to read */
static const char *cs_problem(const char *s, int f[54]) __attribute__((unused));
static const char *cs_problem(const char *s, int f[54])
{
    return strchr(s, '?') ? incomp_problem(s, NULL, NULL, NULL) : from_string(s, f);
}

/* How many cubes the stickers leave: the panel's number; counts no further than `limit`. */
static unsigned long long incomp_count(const char *s, int *ok, unsigned long long limit)
    __attribute__((unused));
static unsigned long long incomp_count(const char *s, int *ok, unsigned long long limit)
{
    InCube r;
    if (ic_parse(s, &r)) { if (ok) *ok = 0; return 0; }
    return ic_count_members(&r, ok, limit);
}

/* Another color possible on facelet `f`?  Drop the pairs that show `but` there, then count. */
static int ic_color_elsewhere(const InCube *in, int f, int but)
{
    for (int edge = 0; edge < 2; edge++) {
        int np = edge ? 12 : 8, no = edge ? 2 : 3;   /* positions; orientations apiece */
        for (int i = 0; i < np; i++)
            for (int s = 0; s < no; s++) {
                if ((edge ? EDGE_FACELET[i][s] : CORNER_FACELET[i][s]) != f) continue;
                unsigned keep = ic_pairs(in, !edge, i);
                for (int j = 0; j < np; j++)
                    for (int o = 0; o < no; o++)
                        if ((edge ? EDGE_COLOR[j][s ^ o]
                                  : CORNER_COLOR[j][(s - o + 3) % 3]) == but)
                            keep &= ~(1u << (j * no + o));
                if (!keep) return 0;
                InCube t = *in;
                int ok = 1;
                if (edge) { t.ke[i] = 1; t.pe_pair[i] = (int)keep; }
                else      { t.kc[i] = 1; t.pc_pair[i] = (int)keep; }
                unsigned long long n = ic_count_members(&t, &ok, 1);
                return ok && n > 0;
            }
    }
    return 0;
}

/* ---- what a '?' cube leaves fixed ---- */
#define IC_AC_CAP   128      /* completions to look at before any counting */
#define IC_AC_NODES 300000   /* nodes one walk may spend: past that the count decides */

/* The most constrained position first: the same walk, but a branch that cannot be finished
   dies near the top, where it costs a few placements instead of a subtree. */
static void ic_ac_order(const InCube *in, int corner, unsigned char *ord, int n)
{
    for (int i = 0; i < n; i++) {
        int c = __builtin_popcount(ic_pairs(in, corner, i)), j = i;
        while (j > 0 && __builtin_popcount(ic_pairs(in, corner, ord[j - 1])) > c) {
            ord[j] = ord[j - 1];
            j--;
        }
        ord[j] = i;
    }
}

/* Fill every '?' the laws of the cube force; how many were filled, -1 when the input is not one. */
static int ic_complete(const char *in, char *out) __attribute__((unused));
static int ic_complete(const char *in, char *out)
{
    InCube r;
    if (ic_parse(in, &r)) return -1;
    strcpy(out, in);

    /* Two completions that disagree settle a facelet with no counting at all; the count answers
       only where the walk stayed in doubt. */
    Cube seen[IC_AC_CAP];
    IcSeat w = { .in = &r, .dst = seen, .cap = IC_AC_CAP, .budget = IC_AC_NODES };
    unsigned char cord[8], eord[12];
    ic_ac_order(&r, 1, cord, 8);
    ic_ac_order(&r, 0, eord, 12);
    w.cord = cord; w.eord = eord;
    ic_seat_corner(&w, 0, 0, 0);
    if (!w.n) return 0;                  /* no completion here: nothing the laws fix */

    unsigned char shown[54] = {0};    /* per facelet: a color a completion showed sets its bit */
    for (int k = 0; k < w.n; k++) {
        const Cube *m = &seen[k];
        for (int i = 0; i < 8; i++)
            for (int t = 0; t < 3; t++)
                shown[CORNER_FACELET[i][t]] |=
                    (unsigned char)(1u << CORNER_COLOR[m->cp[i]][(t - m->co[i] + 3) % 3]);
        for (int i = 0; i < 12; i++)
            for (int t = 0; t < 2; t++)
                shown[EDGE_FACELET[i][t]] |=
                    (unsigned char)(1u << EDGE_COLOR[m->ep[i]][t ^ m->eo[i]]);
    }

    int filled = 0;
    for (int f = 0; f < 54; f++) {
        if (out[f] != '?') continue;
        int b = shown[f];
        if (b & (b - 1)) continue;       /* two completions disagree, so nothing is forced */
        /* A walk cut short may have missed the second color; the count goes looking for it. */
        if (w.stop && ic_color_elsewhere(&r, f, __builtin_ctz(b))) continue;
        out[f] = in[9 * __builtin_ctz(b) + 4];
        filled++;
    }
    return filled;
}

/* A '?' cube takes one path or the other by engine.h's rule; run_incomplete is the relaxed one. */

/* Set by ic_expand when the count itself ran out of memory: no legal completion is then a guess. */
static int ic_count_oom;

/* Every legal completion, each an ordinary cube; 0 if past IC_K_CAP, or none could be listed. */
static int ic_expand(const char *in, char out[][55])
{
    InCube r;
    ic_count_oom = 0;
    if (ic_parse(in, &r)) return 0;

    /* counted before walked: eight wiped corners leave 44 million; the walk stops at IC_K_CAP */
    int ok = 1;
    if (ic_count_members(&r, &ok, IC_K_CAP + 1) > (unsigned long long)IC_K_CAP) {
        if (!ok) {
            ic_count_oom = 1;
            fprintf(stderr, "warning: the completions could not be counted (out of memory); "
                            "falling back to the relaxed search, which is much slower\n");
        }
        return 0;
    }
    ic_in = r;
    ic_enum_members(IC_K_CAP);
    if (ic_nmembers < 1) return 0;
    char ch[6];                     /* the input's own letter for each face: the center of face g */
    for (int g = 0; g < 6; g++) ch[g] = in[9 * g + 4];
    for (int k = 0; k < ic_nmembers; k++) {
        const Cube *m = &ic_members[k];
        char *cs = out[k];
        memcpy(cs, in, 55);
        for (int i = 0; i < 8; i++) {
            const int *fac = CORNER_FACELET[i];
            for (int t = 0; t < 3; t++)
                cs[fac[t]] = ch[CORNER_COLOR[m->cp[i]][(t - m->co[i] + 3) % 3]];
        }
        for (int i = 0; i < 12; i++) {
            const int *fac = EDGE_FACELET[i];
            for (int t = 0; t < 2; t++)
                cs[fac[t]] = ch[EDGE_COLOR[m->ep[i]][t ^ m->eo[i]]];
        }
    }
    return ic_nmembers;
}

/* ---- the state, tables, search ---- */

/* Two halves a batch: 12P6 leaves no room for a move table, and a small half reads cheaper.
 * Six tokens to a batch and twelve known at most, so two batches carry every token. */
#define IC_EP_TAIL 3
#define IC_BATCH   6
#define IC_NB_MAX  ((12 + IC_BATCH - 1) / IC_BATCH)

typedef struct {
    int cperm, cori;
    int twist;                      /* cori's first seven digits as the twist; ic_ct_on only */
    int eperm[IC_NB_MAX];           /* the lead tokens' code, by batch */
    int etail[IC_NB_MAX];           /* the tail tokens' code, by batch */
    int eori, eknown;
    int cdown, edown;               /* where the four D corners and four D edges sit */
} IState;

static u16 *ic_cp_move, *ic_co_move;         /* [40320][ncols], [65536][ncols] */
static u16 *ic_em_move;                      /* [4096][ncols]: where a mask's bits go, per column */
static u16  ic_em_flip[N_MOVE];              /* the flip a column brings, indexed like mv_col */
static u16 *ic_ep_moves[IC_EP_TAIL + 1];     /* by half size: slot k holds the 12Pk table */
static u16 *ic_ep_lmove[IC_NB_MAX], *ic_ep_tmove[IC_NB_MAX]; /* the halves', per batch in use */
static int  ic_ep_tsize[IC_NB_MAX];          /* the tail half's size, to fold the two into one */
static u8  *ic_cp_prun, *ic_co_prun;         /* [40320], [65536] */
static u8  *ic_ep_prun[IC_NB_MAX];           /* one per batch, two entries per byte */
static u8  *ic_ct_prun;                      /* 15 = unreached */
static u8  *ic_ct_raw;                       /* whole bytes while the walk fills it */
static u16 *ic_cdown_move, *ic_edown_move;   /* [1680][ncols], [11880][ncols] */
static u8  *ic_df_prun;                      /* the D layer's distance */

/* Unreached reads as 255: the bound wants the infinity a whole byte gave. */
static int ic_prun(const u8 *tab, long i)
{
    int v = (tab[i >> 1] >> ((i & 1) << 2)) & 15;
    return v == 15 ? 255 : v;
}

/* A nibble holds 14, so a longer way home folds down to it: a bound that reads low prunes less. */
static void ic_pack_nibble(u8 *dst, const u8 *raw, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        u8 v = raw[i];
        if (v != 0xff && v > 14) v = 14;
        if (i & 1) dst[i >> 1] |= (u8)(v << 4);
        else       dst[i >> 1] = (u8)(v & 15);
    }
}
static u32 *ic_edown_cls;                    /* [IC_EDOWN_N]: class << 4 | the symmetry */
static u16 *ic_edown_rep;                    /* one raw edge coordinate per class */
static u16 *ic_cdown_conj;                   /* [IC_CDOWN_N][ic_df_nsym] */
static u32 *ic_df_stab;                      /* [IC_EDOWN_N]: one bit per symmetry, per class */
static u32 *ic_ct_cls;                       /* [N_CORNERS]: class << 4 | symmetry */
static int *ic_ct_rep;                       /* one corner permutation per class, the BFS's own */
static unsigned *ic_ct_sym;                  /* the symmetries leaving that representative alone */
static int  ic_ct_keep[N_SYM_D4H];           /* the symmetries this move list is closed under */
static int  ic_ct_nkeep, ic_ct_ncls;
static int  ic_ct_on;                        /* ct in play: no wiped corner, enough symmetry */
static int  ic_df_on;                        /* the eight D cubies known, so the layer is one */
static int  ic_df_sym[N_SYM_D4H];            /* the ones kept, and ic_df_inv their inverses */
static int  ic_df_inv[N_SYM_D4H], ic_df_nsym, ic_df_ncls;

static int  ic_nb;                           /* edge batches in use */
static int  ic_batch_tok[IC_NB_MAX][IC_BATCH]; /* the edge ids serving as tokens, per batch */
static int  ic_batch_n[IC_NB_MAX];

/* What is in memory: no table reads the start, only the shape, so a repeat rebuilds nothing. */
static struct {
    int nmoves;                      /* the subgroup's column count */
    int mv_col[N_MOVE];
    int src_mask;                    /* ic_src_cperm_mask: what the corner tables aim at */
    int df_nsym, df_sym[N_SYM_D4H];  /* what the D layer's table was folded by */
    int nb, batch_n[IC_NB_MAX], batch_tok[IC_NB_MAX][IC_BATCH];
    int has_move, has_corner, has_edge, has_down;
} ic_built;

static IState ic_start;
static Cube   ic_fill;                       /* the start with the unknowns filled, for checking */

typedef struct {
    int  path[MOVE_CAP];
    long long nodes, nsol;
    unsigned long long hash;
} IcThr;

/* One task == one subtree: the state at the cut depth, resumed with the recursive call's args. */
typedef struct {
    IState st;
    int togo, acc_std, sp;
    unsigned rhos;                  /* the rotations the prefix still leaves open, as in search() */
    int path[MOVE_CAP];             /* the walk prefix, as deep as the walk itself can go */
} IcTask;

static IcTask *ic_tasks;
static int ic_ntasks, ic_task_cap;
static volatile long ic_next_task;
static int ic_split;                         /* 0 = serial; else package tasks at this depth */

static int ic_eperm_n(int k) { int n = 1; for (int t = 0; t < k; t++) n *= 12 - t; return n; }

static int ic_ep_lead(int k) { return k > IC_EP_TAIL ? k - IC_EP_TAIL : 0; }
static int ic_ep_tail(int k) { return k > IC_EP_TAIL ? IC_EP_TAIL : k; }

/* Mixed radix 12, 11, ...: a token's slot as its rank among those the earlier tokens left free. */
static int ic_eperm_encode(const int *slot, int k)
{
    unsigned used = 0, idx = 0;
    for (int t = 0; t < k; t++) {
        idx = idx * (12 - t) + __builtin_popcount(~used & ((1u << slot[t]) - 1));
        used |= 1u << slot[t];
    }
    return (int)idx;
}
static void ic_eperm_decode(int idx, int *slot, int k)
{
    int used[12] = {0}, digit[6];
    for (int t = k - 1; t >= 0; t--) { digit[t] = idx % (12 - t); idx /= (12 - t); }
    for (int t = 0; t < k; t++) {
        int free_no = 0, j = 0;
        for (; j < 12; j++) {
            if (used[j]) continue;
            if (free_no == digit[t]) break;
            free_no++;
        }
        slot[t] = j; used[j] = 1;
    }
}

static void ic_free_tasks(void)
{
    free(ic_tasks);
    ic_tasks = NULL;
    ic_task_cap = ic_ntasks = 0;
}

static void ic_free_edge_prun(void)
{
    for (int b = 0; b < IC_NB_MAX; b++) free(ic_ep_prun[b]);
    memset(ic_ep_prun, 0, sizeof ic_ep_prun);
    ic_built.has_edge = 0;
}

/* The D layer answers to the move list alone, so it outlives the corner tables. */
static void ic_free_down_tables(void)
{
    free(ic_cdown_move); free(ic_edown_move); free(ic_df_prun);
    free(ic_edown_cls); free(ic_edown_rep); free(ic_cdown_conj); free(ic_df_stab);
    ic_cdown_move = NULL; ic_edown_move = NULL; ic_df_prun = NULL;
    ic_edown_cls = NULL; ic_edown_rep = NULL; ic_cdown_conj = NULL; ic_df_stab = NULL;
    ic_built.has_down = 0;
}

static void ic_free_corner_prun(void)
{
    /* ic_have_edge() asks ic_have_corner() first, so a pointer left behind still reads as good. */
    ic_free_edge_prun();
    free(ic_cp_prun); free(ic_co_prun);
    free(ic_ct_prun); free(ic_ct_raw); free(ic_ct_cls); free(ic_ct_rep); free(ic_ct_sym);
    ic_cp_prun = ic_co_prun = NULL;
    ic_ct_prun = ic_ct_raw = NULL;
    ic_ct_cls = NULL; ic_ct_rep = NULL; ic_ct_sym = NULL;
    ic_ct_on = 0;
    ic_built.has_corner = 0;
}

static void ic_free_move_tables(void)
{
    free(ic_cp_move); free(ic_co_move); free(ic_em_move);
    for (int k = 0; k <= IC_EP_TAIL; k++) free(ic_ep_moves[k]);
    memset(ic_ep_moves, 0, sizeof ic_ep_moves);
    /* also the per-batch halves: they index into ic_ep_moves */
    memset(ic_ep_lmove, 0, sizeof ic_ep_lmove);
    memset(ic_ep_tmove, 0, sizeof ic_ep_tmove);
    ic_cp_move = ic_co_move = NULL;
    ic_em_move = NULL;
    ic_built.has_move = 0;
}

/* Everything, tasks included: the out-of-memory path leaves nothing half-built. */
static void ic_free_tables(void)
{
    ic_free_corner_prun();
    ic_free_down_tables();
    ic_free_move_tables();
    ic_free_tasks();
}

/* The orientation digit a move adds in a slot: 3 is an unknown cubie, and a move brings none in. */
static const s8 IC_CO_ADD[4][6] = {
    {0, 1, 2, 0, 1, 2},
    {1, 2, 0, 1, 2, 0},
    {2, 0, 1, 2, 0, 1},
    {3, 3, 3, 3, 3, 3}
};

static void ic_gen_cp_tables(void)
{
    ic_cp_move = malloc((size_t)N_CORNERS * nmoves_in * sizeof(u16));
    if (!ic_cp_move) return;                  /* the caller reports and frees */
    for (int idx = 0; idx < N_CORNERS; idx++)
        for (int v = 0; v < nmoves_in; v++)
            ic_cp_move[nmoves_in * idx + v] = (u16)corners_move[N_MOVE * idx + mv_col[v]];
}

/* ct reads the corners whole, so the orientation half is built only when ct is not in play. */
static void ic_gen_co_tables(void)
{
    ic_co_move = malloc((size_t)65536 * nmoves_in * sizeof(u16));
    if (!ic_co_move) return;
    for (int idx = 0; idx < 65536; idx++) {
        s8 co[8];
        int v = idx;
        for (int i = 0; i < 8; i++) { co[i] = (s8)(v & 3); v >>= 2; }
        for (int j = 0; j < nmoves_in; j++) {
            const Cube *mc = &moveCube[mv_col[j]];
            int out = 0;
            for (int i = 0; i < 8; i++)
                out |= IC_CO_ADD[co[mc->cp[i]]][mc->co[i]] << (2 * i);
            ic_co_move[nmoves_in * idx + j] = (u16)out;
        }
    }
}

static int ic_gen_edge_tables(int k)
{
    int n = ic_eperm_n(k);
    u16 *em = malloc((size_t)n * nmoves_in * sizeof(u16));
    if (!em) return -1;

    static int inv_ep[N_MOVE][12];
    for (int m = 0; m < N_MOVE; m++)
        for (int i = 0; i < 12; i++) inv_ep[m][moveCube[m].ep[i]] = i;

    int slot[6], s2[6];
    for (int idx = 0; idx < n; idx++) {
        ic_eperm_decode(idx, slot, k);
        for (int v = 0; v < nmoves_in; v++) {
            int m = mv_col[v];
            for (int t = 0; t < k; t++) s2[t] = inv_ep[m][slot[t]];
            em[nmoves_in * idx + v] = (u16)ic_eperm_encode(s2, k);
        }
    }
    ic_ep_moves[k] = em;
    return 0;
}

static void ic_gen_flip_tables(void)
{
    ic_em_move = malloc((size_t)4096 * nmoves_in * sizeof(u16));
    if (!ic_em_move) return;
    for (int v = 0; v < nmoves_in; v++) {
        const Cube *mc = &moveCube[mv_col[v]];
        int fm = 0;
        for (int i = 0; i < 12; i++)
            if (mc->eo[i]) fm |= 1 << i;
        ic_em_flip[v] = (u16)fm;
    }
    for (int idx = 0; idx < 4096; idx++) {
        for (int v = 0; v < nmoves_in; v++) {
            const Cube *mc = &moveCube[mv_col[v]];
            int u = 0;
            for (int i = 0; i < 12; i++) {
                int src = mc->ep[i];               /* new slot i takes the cubie of old slot src */
                u |= ((idx >> src) & 1) << i;
            }
            ic_em_move[nmoves_in * idx + v] = (u16)u;
        }
    }
}

/* Refusal when a run-time table or a BFS queue cannot be allocated. */
#define IC_OOM_MSG "out of memory for the incomplete-cube tables\n"

/* Set by a failure that named its own reason; the cleanup only asks whether one came. */
static const char *ic_fail_msg;

/* Nearest goal by BFS over relaxed coords; unreached infinite, -1 if the queue failed. */
static int ic_bfs(u8 *tab, long n, const u16 *mv, int (*src)(long))
{
    int *q = malloc((size_t)n * sizeof *q);
    if (!q) return -1;
    memset(tab, 0xff, (size_t)n);
    long head = 0, tail = 0;
    for (long i = 0; i < n; i++)
        if (src(i)) { tab[i] = 0; q[tail++] = (int)i; }
    while (head < tail) {
        long i = q[head++];
        int d1 = tab[i] + 1;
        for (int v = 0; v < nmoves_in; v++) {
            long at = (long)nmoves_in * i + v;
            int j = mv[at];
            if (tab[j] == 0xff) { tab[j] = (u8)d1; q[tail++] = j; }
        }
    }
    free(q);
    return 0;
}

static int ic_src_cperm_mask;      /* bit h: corner id h is a fully-known cubie */

static int ic_cperm_is_src(long idx)
{
    Cube c; identity_cube(&c);
    set_corners(&c, (int)idx);
    /* Every fully-known cubie has to be home; a partial one says nothing, so its slot is free. */
    for (int h = 0; h < 8; h++)
        if ((ic_src_cperm_mask >> h & 1) && c.cp[h] != h) return 0;
    return 1;
}

/* Over 4^8: a 3 is no orientation but a wiped block, riding the cubie that holds it. A slot a
 * known corner calls home wants 0 or that 3; a slot no sticker has pinned to one asks nothing. */
static int ic_cori_is_src(long idx)
{
    for (int pos = 0; pos < 8; pos++, idx >>= 2) {
        int d = idx & 3;
        if (!(ic_src_cperm_mask >> pos & 1)) continue;
        if (d == 1 || d == 2) return 0;
    }
    return 1;
}

/* Over 3^7, the ct coordinate: cori's first seven digits, highest first, the eighth whatever
 * those seven ask for; no 3 among them, and a known corner's slot leaves its digit 0 alone. */
static int ic_twist_is_src(int tw)
{
    for (int pos = 6; pos >= 0; pos--, tw /= 3)
        if ((ic_src_cperm_mask >> pos & 1) && tw % 3) return 0;
    return 1;
}

/* The walk opens at the one state every token of the batch sits home in. */
static int ic_bfs_pair(u8 *tab, long n, const u16 *lm, const u16 *tm, int tn, long src)
{
    int *q = malloc((size_t)n * sizeof *q);
    if (!q) return -1;
    memset(tab, 0xff, (size_t)n);
    long head = 0, tail = 0;
    tab[src] = 0;
    q[tail++] = (int)src;
    while (head < tail) {
        long i = q[head++], a = i / tn, b = i - a * tn;
        int d1 = tab[i] + 1;
        for (int v = 0; v < nmoves_in; v++) {
            long j = (long)lm[nmoves_in * a + v] * tn + tm[nmoves_in * b + v];
            if (tab[j] == 0xff) { tab[j] = (u8)d1; q[tail++] = (int)j; }
        }
    }
    free(q);
    return 0;
}

/* How much of what is in memory this job can keep; each group rides on the one before it. */
static int ic_have_move(void)
{
    return ic_built.has_move && ic_built.nmoves == nmoves_in &&
           !memcmp(ic_built.mv_col, mv_col, (size_t)nmoves_in * sizeof *mv_col);
}
static int ic_have_corner(void)
{
    return ic_have_move() && ic_built.has_corner && ic_built.src_mask == ic_src_cperm_mask;
}
static int ic_have_down(void)
{
    return ic_have_move() && ic_built.has_down && ic_built.df_nsym == ic_df_nsym &&
           !memcmp(ic_built.df_sym, ic_df_sym, (size_t)ic_df_nsym * sizeof *ic_df_sym);
}

static int ic_have_edge(void)
{
    /* only the tokens a batch names are compared: the rest of the row is left over */
    if (!ic_have_corner() || !ic_built.has_edge || ic_built.nb != ic_nb) return 0;
    for (int b = 0; b < ic_nb; b++) {
        if (ic_built.batch_n[b] != ic_batch_n[b]) return 0;
        if (memcmp(ic_built.batch_tok[b], ic_batch_tok[b],
                   (size_t)ic_batch_n[b] * sizeof **ic_batch_tok)) return 0;
    }
    return 1;
}

/* One level of a walk goes out to the pool whole: the queue stays level-ordered either way. */
typedef struct {
    u8  *raw;                        /* whole bytes until the walk packs them down */
    int *q;                          /* level-ordered; a claim that came to nothing leaves a hole */
    long end;                        /* the state the level stops at */
    u8   d;                          /* the value this level writes */
    int  par;                        /* out with the pool, so a claim is atomic */
    long tail;                       /* the serial cursor, used when it is not */
    volatile LONG take;              /* where the level hands its work out from */
    volatile LONG nxt;               /* where the level after it claims queue slabs */
} IcWalk;

/* One walk at a time: the ct build runs its levels to the end before the D layer starts its own. */
static IcWalk ic_walk;

#define IC_WALK_CHUNK 4096                  /* states per claim: small enough to balance */
#define IC_WALK_FLOOR 65536                  /* under this a level is cheaper on one thread */
#define IC_WALK_SLAB  64                     /* queue slots per claim: one atomic covers a slab */
#define IC_WALK_HOLE (-1)                    /* an unfilled claim; the next level steps over it */

static __thread long ic_thr_lo, ic_thr_hi;   /* the slab this thread is filling */

/* One queue slot for one claimed state, cut from a slab this thread holds. */
static long ic_walk_slot(IcWalk *w)
{
    if (ic_thr_lo == ic_thr_hi) {
        long lo = (long)InterlockedExchangeAdd(&w->nxt, IC_WALK_SLAB);
        for (int i = 0; i < IC_WALK_SLAB; i++) w->q[lo + i] = IC_WALK_HOLE;
        ic_thr_lo = lo;
        ic_thr_hi = lo + IC_WALK_SLAB;
    }
    return ic_thr_lo++;
}

/* Claim a state before queuing it: one that another thread took is not this thread's to walk. */
static void ic_walk_put(IcWalk *w, size_t j, u8 d)
{
    if (w->raw[j] != 0xff) return;  /* reached: the common case, worth a plain load */
    long k;
    if (w->par) {
        u8 cur = 0xff;
        if (!__atomic_compare_exchange_n(&w->raw[j], &cur, d, 0, __ATOMIC_RELAXED,
                                         __ATOMIC_RELAXED))
            return;
        k = ic_walk_slot(w);
    } else {
        w->raw[j] = d;
        k = w->tail++;
    }
    w->q[k] = (int)j;
}

/* One level; *tail is the next start; nonzero if the join failed: a half level prunes answers. */
static int ic_walk_level(IcWalk *w, long head, long *tail, int nthr,
                         unsigned (__stdcall *worker)(void *))
{
    long end = *tail;
    w->end  = end;
    w->d    = (u8)(w->raw[w->q[head]] + 1);
    w->par  = nthr > 1 && end - head >= IC_WALK_FLOOR;
    w->tail = end;
    w->take = (LONG)head;
    w->nxt  = (LONG)end;
    int created = 0, joined = 1;
    HANDLE th[THREAD_CAP];
    if (w->par)
        for (int t = 1; t < nthr; t++) {
            th[created] = (HANDLE)_beginthreadex(NULL, 0, worker, w, 0, NULL);
            if (!th[created]) break;  /* a thread short only costs time: claims cover it */
            created++;
        }
    worker(w);                        /* the calling thread is the first worker */
    if (created && WaitForMultipleObjects((DWORD)created, th, TRUE, INFINITE) == WAIT_FAILED) {
        fprintf(stderr, "cannot join the incomplete-cube threads (error %lu)\n",
                (unsigned long)GetLastError());
        joined = 0;
    }
    for (int i = 0; i < created; i++) CloseHandle(th[i]);
    if (!joined) return -1;
    long lim = w->par ? (long)w->nxt : w->tail;
    long x = end;
    for (long r = end; r < lim; r++)
        if (w->q[r] != IC_WALK_HOLE) w->q[x++] = w->q[r];
    *tail = x;
    return 0;
}

static unsigned __stdcall ic_ct_worker(void *arg)
{
    IcWalk *w = arg;
    ic_thr_lo = ic_thr_hi = 0; /* a slab never straddles two levels */
    const u8 d = w->d;
    /* The whole move list goes out together: each lands anywhere, so the misses overlap. */
    int c2of[MOVE_CAP], tw2of[MOVE_CAP];
    for (;;) {
        long lo = (long)InterlockedExchangeAdd(&w->take, IC_WALK_CHUNK);
        if (lo >= w->end) break;
        long hi = lo + IC_WALK_CHUNK < w->end ? lo + IC_WALK_CHUNK : w->end;
        for (long h = lo; h < hi; h++) {
            int idx = w->q[h];
            if (idx == IC_WALK_HOLE) continue;
            int cls = idx / N_TWIST, tw = idx - N_TWIST * cls;
            const u16 *cm = corners_move + (size_t)N_MOVE * ic_ct_rep[cls];
            const u16 *tm = twist_move + (size_t)N_MOVE * tw;
            for (int v = 0; v < nmoves_in; v++) {
                u32 cs = ic_ct_cls[cm[mv_col[v]]];
                int c2 = (int)CLS_OF(cs);
                int tw2 = twist_conj[(tm[mv_col[v]] << 4) + SYM_OF(cs)];
                c2of[v]  = c2;
                tw2of[v] = tw2;
                __builtin_prefetch(w->raw + (size_t)N_TWIST * c2 + tw2);
            }
            for (int v = 0; v < nmoves_in; v++) {
                int c2 = c2of[v], tw2 = tw2of[v];
                ic_walk_put(w, (size_t)N_TWIST * c2 + tw2, d);
                unsigned sg = ic_ct_sym[c2];
                if (sg != 1)
                    for (int kk = 1; kk < N_SYM_D4H; kk++) {
                        if (!(sg >> kk & 1)) continue;
                        ic_walk_put(w, (size_t)N_TWIST * c2 + twist_conj[(tw2 << 4) + kk], d);
                    }
            }
        }
    }
    return 0;
}

/* Standard corners, symmetries and moves: ct_prun on disk is already this table. */
static int ic_ct_from_disk(void)
{
    if (ic_src_cperm_mask != 0xff || ic_ct_nkeep != N_SYM_D4H) return 0;
    if (nmoves_in != (slice_mode ? N_MOVE : N_MOVE_FTM)) return 0;
    size_t n = (size_t)N_CORNERS_CLASS * N_TWIST, half = n / 2;
    const u8 *src = load_file(slice_mode ? "ct_prun" : "ct_prun_ftm");
    u32 *cls = malloc((size_t)N_CORNERS * sizeof *cls);
    u8 *pr = malloc(half + 1);
    if (!cls || !pr) {
        free(cls); free(pr);
        /* the mapping goes too: this branch is reached when the heap is what ran short */
        UnmapViewOfFile(src);
        return 0;                   /* 0: the walk builds the same table without the file */
    }
    memcpy(cls, ctclssym, (size_t)N_CORNERS * sizeof *cls);
    for (size_t j = 0; j < half; j++) pr[j] = (u8)(src[2 * j] | (src[2 * j + 1] << 4));
    UnmapViewOfFile(src);
    ic_ct_cls = cls;
    ic_ct_prun = pr;
    ic_ct_ncls = N_CORNERS_CLASS;
    return 1;
}

/* ic_bfs cannot step this: every step re-enters through the representative. */
static int ic_bfs_ct(void)
{
    size_t total = (size_t)ic_ct_ncls * N_TWIST;
    memset(ic_ct_raw, 0xff, total);
    int *q = malloc((total + (size_t)THREAD_CAP * IC_WALK_SLAB) * sizeof *q);
    if (!q) return -1;
    long head = 0, tail = 0;
    u8 twist_ok[N_TWIST];
    for (int tw = 0; tw < N_TWIST; tw++) twist_ok[tw] = (u8)ic_twist_is_src(tw);
    for (int cp = 0; cp < N_CORNERS; cp++) {
        if (!ic_cperm_is_src(cp)) continue;
        u32 v = ic_ct_cls[cp];
        for (int tw = 0; tw < N_TWIST; tw++) {
            if (!twist_ok[tw]) continue;
            int idx = ct_coord(v, tw);
            if (ic_ct_raw[idx] == 0xff) { ic_ct_raw[idx] = 0; q[tail++] = idx; }
        }
    }

    ic_walk.raw = ic_ct_raw;
    ic_walk.q = q;
    int nthr = par_threads > 1 ? par_threads : 1;
    while (head < tail) {
        long end = tail;
        if (ic_walk_level(&ic_walk, head, &tail, nthr, ic_ct_worker)) {
            free(q);
            ic_walk.q = NULL;
            ic_fail_msg = "the incomplete-cube tables could not be built\n";
            return -1;
        }
        head = end;
    }
    ic_pack_nibble(ic_ct_prun, ic_ct_raw, total);
    /* the walk is over: its raw bytes and the classing only it read go with it */
    free(ic_ct_raw);
    free(ic_ct_rep);
    free(ic_ct_sym);
    ic_ct_raw = NULL;
    ic_ct_rep = NULL;
    ic_ct_sym = NULL;
    ic_walk.raw = NULL;
    ic_walk.q = NULL;
    free(q);
    return 0;
}

#define IC_CDOWN_N 1680                      /* 8*7*6*5: where the four D corners sit */
#define IC_EDOWN_N 11880                     /* 12*11*10*9: where the four D edges sit */

/* A four-tuple of slots, no two the same, as a number below n*(n-1)*(n-2)*(n-3). */
static int ic_down_enc(const int *p, int n)
{
    int idx = 0, used = 0;
    for (int t = 0; t < 4; t++) {
        int r = p[t];
        for (int j = 0; j < p[t]; j++) if (used >> j & 1) r--;
        idx = idx * (n - t) + r;
        used |= 1 << p[t];
    }
    return idx;
}

static void ic_down_dec(int idx, int n, int *p)
{
    int r[4];
    r[3] = idx % (n - 3); idx /= (n - 3);
    r[2] = idx % (n - 2); idx /= (n - 2);
    r[1] = idx % (n - 1); idx /= (n - 1);
    r[0] = idx;
    p[0] = r[0];                     /* nothing is taken yet, so the digit names the slot */
    int used = 1 << r[0];
    for (int t = 1; t < 4; t++) {
        int k = 0;
        for (int j = 0; j < n; j++) {
            if (used >> j & 1) { k++; continue; }
            if (j == r[t] + k) { p[t] = j; used |= 1 << j; break; }
        }
    }
}

/* Where the four D cubies land when a symmetry that keeps D down conjugates the cube. */
static int ic_df_conj(int idx, int n, int k, int edge)
{
    int p[4], q[4], at[12], fill = 0;
    ic_down_dec(idx, n, p);
    for (int i = 0; i < n; i++) at[i] = -1;
    for (int t = 0; t < 4; t++) at[p[t]] = 4 + t;
    for (int i = 0; i < n; i++)
        if (at[i] < 0) {
            while (fill >= 4 && fill <= 7) fill++;
            at[i] = fill++;
        }
    Cube c, s;
    identity_cube(&c);
    for (int i = 0; i < n; i++) {
        if (edge) c.ep[i] = (s8)at[i];
        else      c.cp[i] = (s8)at[i];
    }
    s = symCube[ic_df_sym[k]];
    multiply(&s, &c);
    multiply(&s, &symCube[inv_idx[ic_df_sym[k]]]);
    for (int t = 0; t < 4; t++) {
        q[t] = 0;
        for (int i = 0; i < n; i++)
            if ((edge ? s.ep[i] : s.cp[i]) == 4 + t) { q[t] = i; break; }
    }
    return ic_down_enc(q, n);
}

/* Fold over the stabiliser too: else one pair reads two distances and the larger one prunes. */
static int ic_df_canon(int ec, int x)
{
    unsigned st = ic_df_stab[ec];
    if (st == 1) return x;              /* only the identity stabilises it, and it moves nothing */
    int best = x;
    for (int k = 0; k < ic_df_nsym; k++)
        if (st >> k & 1) {
            int y = ic_cdown_conj[(size_t)x * ic_df_nsym + k];
            if (y < best) best = y;
        }
    return best;
}

/* 1 iff every column of this run's move list, conjugated by s, is a column of it again. */
static int ic_sym_keeps_moves(int s)
{
    Cube g = symCube[s], gi = symCube[inv_idx[s]];
    for (int v = 0; v < nmoves_in; v++) {
        Cube m = g;
        multiply(&m, &moveCube[mv_col[v]]);
        multiply(&m, &gi);
        int hit = 0;
        for (int w = 0; w < nmoves_in && !hit; w++)
            if (!memcmp(m.cp, moveCube[mv_col[w]].cp, sizeof m.cp) &&
                !memcmp(m.ep, moveCube[mv_col[w]].ep, sizeof m.ep)) hit = 1;
        if (!hit) return 0;
    }
    return 1;
}

/* D4h symmetries keeping D down that the moves close under: a group, so the fold stays exact. */
static void ic_df_picksym(void)
{
    ic_df_nsym = 0;
    for (int s = 0; s < N_SYM_D4H; s++) {
        int down = 1;
        for (int i = 4; i < 8 && down; i++) {
            if (symCube[s].cp[i] < 4 || symCube[s].cp[i] > 7) down = 0;
            if (symCube[s].ep[i] < 4 || symCube[s].ep[i] > 7) down = 0;
        }
        if (down && ic_sym_keeps_moves(s)) ic_df_sym[ic_df_nsym++] = s;
    }
}

/* Two at least: folding by fewer only costs classes, and the identity alone folds nothing. */
#define IC_CT_SYMS_MIN  2

/* The symmetries the moves close under: those form a group, so the ct fold may reduce by them. */
static int ic_ct_syms(void)
{
    ic_ct_nkeep = 0;
    for (int s = 0; s < N_SYM_D4H; s++)
        if (ic_sym_keeps_moves(s)) ic_ct_keep[ic_ct_nkeep++] = s;
    return ic_ct_nkeep;
}

/* Corner permutations folded into the orbits of those, the way do_ct_clssym folds them into all. */
static int ic_ct_gen_cls(void)
{
    ic_ct_cls = malloc((size_t)N_CORNERS * sizeof *ic_ct_cls);
    ic_ct_rep = malloc((size_t)N_CORNERS * sizeof *ic_ct_rep);
    ic_ct_sym = malloc((size_t)N_CORNERS * sizeof *ic_ct_sym);
    if (!ic_ct_cls || !ic_ct_rep || !ic_ct_sym) return -1;
    for (int cp = 0; cp < N_CORNERS; cp++) ic_ct_cls[cp] = 0xffffffffu;
    Cube cc, ss;
    ic_ct_ncls = 0;
    for (int cp = 0; cp < N_CORNERS; cp++) {
        if (ic_ct_cls[cp] != 0xffffffffu) continue;
        ic_ct_cls[cp] = FS24_PACK(ic_ct_ncls, 0);
        ic_ct_rep[ic_ct_ncls] = cp;
        identity_cube(&cc); set_corners(&cc, cp);
        for (int k = 1; k < ic_ct_nkeep; k++) {
            int s = ic_ct_keep[k];
            ss = symCube[inv_idx[s]];
            corner_multiply(&ss, &cc);
            corner_multiply(&ss, &symCube[s]);
            int inew = get_corners(&ss);
            if (ic_ct_cls[inew] == 0xffffffffu) ic_ct_cls[inew] = FS24_PACK(ic_ct_ncls, s);
        }
        ic_ct_ncls++;
    }
    for (int c = 0; c < ic_ct_ncls; c++) {
        Cube r;
        identity_cube(&r); set_corners(&r, ic_ct_rep[c]);
        ic_ct_sym[c] = 0;
        for (int k = 0; k < ic_ct_nkeep; k++) {
            int s = ic_ct_keep[k];
            ss = symCube[s];
            corner_multiply(&ss, &r);
            corner_multiply(&ss, &symCube[inv_idx[s]]);
            if (get_corners(&ss) == ic_ct_rep[c]) ic_ct_sym[c] |= 1u << s;
        }
    }
    return 0;
}

static u32 *ic_df_cmv;               /* the class each move lands in, per representative */

static unsigned __stdcall ic_df_worker(void *arg)
{
    IcWalk *w = arg;
    ic_thr_lo = ic_thr_hi = 0;
    const u8 d = w->d;
    const int ns = ic_df_nsym;
    for (;;) {
        long lo = (long)InterlockedExchangeAdd(&w->take, IC_WALK_CHUNK);
        if (lo >= w->end) break;
        long hi = lo + IC_WALK_CHUNK < w->end ? lo + IC_WALK_CHUNK : w->end;
        for (long h = lo; h < hi; h++) {
            int at = w->q[h];
            if (at == IC_WALK_HOLE) continue;
            int ec = at / IC_CDOWN_N, cc = at - IC_CDOWN_N * ec;
            const u16 *cm = ic_cdown_move + (size_t)cc * nmoves_in;
            const u32 *cmv = ic_df_cmv + (size_t)ec * nmoves_in;
            for (int i = 0; i < nmoves_in; i++) {
                unsigned c2 = cmv[i];
                int ec2 = (int)(c2 >> 4);
                ic_walk_put(w, (size_t)ec2 * IC_CDOWN_N +
                            ic_df_canon(ec2, ic_cdown_conj[(size_t)cm[i] * ns + (c2 & 15)]), d);
            }
        }
    }
    return 0;
}

static void ic_gen_down_tables(void)
{
    for (int k = 0; k < ic_df_nsym; k++)
        for (int j = 0; j < ic_df_nsym; j++)
            if (inv_idx[ic_df_sym[k]] == ic_df_sym[j]) ic_df_inv[k] = j;
    int pc[8][MOVE_CAP], pe[12][MOVE_CAP];
    for (int i = 0; i < nmoves_in; i++) {
        const Cube *m = &moveCube[mv_col[i]];
        for (int c = 0; c < 8; c++)  pc[m->cp[c]][i] = c;
        for (int e = 0; e < 12; e++) pe[m->ep[e]][i] = e;
    }
    u16 *cj = malloc((size_t)IC_CDOWN_N * ic_df_nsym * sizeof(u16));
    u16 *cm = malloc((size_t)IC_CDOWN_N * nmoves_in * sizeof(u16));
    u16 *em = malloc((size_t)IC_EDOWN_N * nmoves_in * sizeof(u16));
    u32 *ec = malloc((size_t)IC_EDOWN_N * sizeof(u32));
    u16 *er = malloc((size_t)IC_EDOWN_N * sizeof(u16));
    u32 *sb = malloc(IC_EDOWN_N * sizeof(u32));
    if (!cm || !em || !ec || !er || !sb || !cj) {
        free(cm); free(em); free(ec); free(er); free(sb); free(cj); return;
    }
    ic_cdown_conj = cj;
    ic_cdown_move = cm; ic_edown_move = em; ic_edown_cls = ec; ic_edown_rep = er; ic_df_stab = sb;
    memset(ic_df_stab, 0, IC_EDOWN_N * sizeof *ic_df_stab);
    int p[4], q[4];
    for (int idx = 0; idx < IC_CDOWN_N; idx++) {
        ic_down_dec(idx, 8, p);
        for (int i = 0; i < nmoves_in; i++) {
            for (int t = 0; t < 4; t++) q[t] = pc[p[t]][i];
            ic_cdown_move[(size_t)idx * nmoves_in + i] = (u16)ic_down_enc(q, 8);
        }
    }
    for (int idx = 0; idx < IC_EDOWN_N; idx++) {
        ic_down_dec(idx, 12, p);
        for (int i = 0; i < nmoves_in; i++) {
            for (int t = 0; t < 4; t++) q[t] = pe[p[t]][i];
            ic_edown_move[(size_t)idx * nmoves_in + i] = (u16)ic_down_enc(q, 12);
        }
    }
    for (int i = 0; i < IC_EDOWN_N; i++) ic_edown_cls[i] = 0xffffffffu;
    ic_df_ncls = 0;
    for (int i = 0; i < IC_EDOWN_N; i++) {
        if (ic_edown_cls[i] != 0xffffffffu) continue;
        ic_edown_rep[ic_df_ncls] = (u16)i;
        for (int k = 0; k < ic_df_nsym; k++) {
            int n = ic_df_conj(i, 12, k, 1);
            /* n == i is this class's own stabiliser, and it is here that it comes up for free */
            if (n == i) ic_df_stab[ic_df_ncls] |= 1u << k;
            if (ic_edown_cls[n] == 0xffffffffu)
                ic_edown_cls[n] = (u32)((ic_df_ncls << 4) | ic_df_inv[k]);
        }
        ic_df_ncls++;
    }
    for (int i = 0; i < IC_CDOWN_N; i++)
        for (int k = 0; k < ic_df_nsym; k++)
            ic_cdown_conj[(size_t)i * ic_df_nsym + k] = (u16)ic_df_conj(i, 8, k, 0);
    u32 *cmv = malloc((size_t)ic_df_ncls * nmoves_in * sizeof *cmv);
    size_t n = (size_t)ic_df_ncls * IC_CDOWN_N;
    u8 *pr = malloc(n);
    int *qq = pr ? malloc((n + (size_t)THREAD_CAP * IC_WALK_SLAB) * sizeof *qq) : NULL;
    if (!cmv || !qq) { free(cmv); free(pr); free(qq); return; }
    ic_df_prun = malloc(n / 2 + 1);
    if (!ic_df_prun) { free(cmv); free(pr); free(qq); return; }
    /* Only representatives are walked, so carrying the class a move lands in pays. */
    for (int j = 0; j < ic_df_ncls; j++)
        for (int i = 0; i < nmoves_in; i++)
            cmv[(size_t)j * nmoves_in + i] =
                ic_edown_cls[ic_edown_move[(size_t)ic_edown_rep[j] * nmoves_in + i]];
    memset(pr, 0xff, n);
    int home[4] = { 4, 5, 6, 7 };
    int ge = ic_down_enc(home, 12), gc = ic_down_enc(home, 8);
    unsigned cl = ic_edown_cls[ge];
    size_t goal = (size_t)(cl >> 4) * IC_CDOWN_N +
                  ic_df_canon((int)(cl >> 4), ic_cdown_conj[(size_t)gc * ic_df_nsym + (cl & 15)]);
    pr[goal] = 0;
    qq[0] = (int)goal;
    ic_walk.raw = pr;
    ic_walk.q   = qq;
    ic_df_cmv   = cmv;
    long head = 0, tail = 1;
    int failed = 0;
    int nthr = par_threads > 1 ? par_threads : 1;
    while (head < tail) {
        long end = tail;
        if (ic_walk_level(&ic_walk, head, &tail, nthr, ic_df_worker)) {
            failed = 1;
            break;
        }
        head = end;
    }
    ic_walk.q = NULL;
    ic_walk.raw = NULL;
    ic_df_cmv = NULL;
    free(qq);
    free(cmv);
    /* a representative names the class only until the classing of states is built */
    free(ic_edown_rep);
    ic_edown_rep = NULL;
    if (failed) {
        free(pr);
        free(ic_df_prun);
        ic_df_prun = NULL;
        return;
    }
    ic_pack_nibble(ic_df_prun, pr, n);
    free(pr);
}

static int ic_bound(const IState *s, int lim)
{
    /* Batches reach highest more often than the corner coordinates do, so they lead the retreat. */
    int b = 0;
    for (int k = 0; k < ic_nb; k++) {
        int d = ic_prun(ic_ep_prun[k], (long)s->eperm[k] * ic_ep_tsize[k] + s->etail[k]);
        if (d > b) {
            b = d;
            if (b > lim) return b;
        }
    }
    if (ic_ct_on) {
        /* ct covers both corner coordinates at once; cp and co stand in when it is off */
        int d = ic_prun(ic_ct_prun, ct_coord(ic_ct_cls[s->cperm], s->twist));
        if (d > b) {
            b = d;
            if (b > lim) return b;
        }
    } else {
        if (ic_cp_prun[s->cperm] > b) {
            b = ic_cp_prun[s->cperm];
            if (b > lim) return b;
        }
        if (ic_co_prun[s->cori] > b) {
            b = ic_co_prun[s->cori];
            if (b > lim) return b;
        }
    }
    if (ic_df_on) {
        unsigned cl = ic_edown_cls[s->edown];
        int d = ic_prun(ic_df_prun, (size_t)(cl >> 4) * IC_CDOWN_N +
                           ic_df_canon((int)(cl >> 4),
                                       ic_cdown_conj[(size_t)s->cdown * ic_df_nsym + (cl & 15)]));
        if (d > b) b = d;
    }
    return b;
}

static void ic_emit(IcThr *th, int sp)
{
    Cube c = ic_fill;
    for (int i = 0; i < sp; i++) multiply(&c, &moveCube[mv_col[th->path[i]]]);
    /* Relaxed coords cannot see a partial cubie: judge the stickers against their colors. */
    for (int i = 0; i < 8; i++) {
        if (ic_in.kfull_c[i]) {
            int home = ic_in.cp[i];
            if (c.cp[home] != home || c.co[home] != 0) return;
        } else if (ic_in.kc[i]) {
            int j = ic_fill.cp[i], oi = ic_fill.co[i], q = 0;
            while (c.cp[q] != j) q++;
            int d = (c.co[q] - oi + 3) % 3;
            for (int t = 0; t < 3; t++)
                if ((ic_in.pvis_c[i] >> t & 1) &&
                    CORNER_FACELET[q][(t + d) % 3] / 9 != CORNER_COLOR[j][(t - oi + 3) % 3])
                    return;
        }
    }
    for (int i = 0; i < 12; i++) {
        if (ic_in.kfull_e[i]) {
            int home = ic_in.ep[i];
            if (c.ep[home] != home || c.eo[home] != 0) return;
        } else if (ic_in.ke[i]) {
            int j = ic_fill.ep[i], f0 = ic_fill.eo[i], q = 0;
            while (c.ep[q] != j) q++;
            for (int t = 0; t < 2; t++)
                if ((ic_in.pvis_e[i] >> t & 1) &&
                    EDGE_FACELET[q][t ^ f0 ^ c.eo[q]] / 9 != EDGE_COLOR[j][t ^ f0])
                    return;
        }
    }

    unsigned char tok[MOVE_CAP];
    for (int i = 0; i < sp; i++) tok[i] = (unsigned char)mv_col[th->path[i]];
    /* mv_col indices, the form the main engine hands emit_record too */
    emit_record(sp, &th->nsol, &th->hash, tok);
}

static void ic_dfs(IcThr *th, const IState *s, int togo, int sp, int acc_std, unsigned rhos)
{
    if (opt_found || engine_stop) return;

    /* Parallel only: at the cut depth the subtree becomes a task, so node total cannot change. */
    if (ic_split && sp == ic_split) {
        if (ic_ntasks == ic_task_cap) {
            int want = ic_task_cap ? ic_task_cap * 2 : 1024;
            IcTask *grown = realloc(ic_tasks, (size_t)want * sizeof *grown);
            if (grown) { ic_task_cap = want; ic_tasks = grown; }
            /* The list stays: this subtree is walked here, as one thread would have anyway. */
            else ic_split = 0;
        }
        if (ic_ntasks < ic_task_cap) {
            IcTask *t = &ic_tasks[ic_ntasks++];
            t->st = *s;
            t->togo = togo; t->acc_std = acc_std;   t->sp = sp; t->rhos = rhos;
            memcpy(t->path, th->path, (size_t)sp * sizeof *t->path);
            return;
        }
    }

    if (togo == 0) {
        /* the bound only admitted this leaf: the edge flips and the stickers still decide */
        if ((s->eori & s->eknown) == 0) ic_emit(th, sp);
        return;
    }

    /* the node's rows, kept in locals: a nested ic_dfs owns *th, reloads none of them */
    unsigned long long ban = sp ? child_banmask(th->path[sp - 1], rhos) : 0;
    const u16 *cpm = ic_cp_move + nmoves_in * s->cperm;
    const u16 *com = ic_ct_on ? NULL : ic_co_move + nmoves_in * s->cori;
    const u16 *ekm = ic_em_move + nmoves_in * s->eknown;
    const u16 *efm = ic_em_move + nmoves_in * s->eori;
    const u16 *twm = ic_ct_on ? twist_move + N_MOVE * s->twist : NULL;
    const u16 *epm[IC_NB_MAX], *epn[IC_NB_MAX];
    for (int k = 0; k < ic_nb; k++) {
        epm[k] = ic_ep_lmove[k] + nmoves_in * s->eperm[k];
        epn[k] = ic_ep_tmove[k] + nmoves_in * s->etail[k];
    }
    IState c;
    /* the count too, handed back once the calls are done */
    long long nodes = th->nodes;
    for (int i = 0; i < nmoves_in; i++) {
        unsigned nrho = child_open(ban, i, rhos);
        if (!nrho) continue;
        nodes++;
        /* search()'s floor, and the cheap one: it goes before the child is built */
        int nacc = stm_child_std(acc_std, i, togo);
        if (nacc < 0) continue;
        c.cperm = (int)cpm[i];
        c.cori  = ic_ct_on ? 0 : (int)com[i];
        /* the bound opens on this row of the class table: handed over before the rows after it */
        if (ic_ct_on) __builtin_prefetch(ic_ct_cls + c.cperm);
        else { __builtin_prefetch(ic_cp_prun + c.cperm);
               __builtin_prefetch(ic_co_prun + c.cori); }
        /* the twist rides along instead of being rebuilt from cori's digits at every node */
        if (ic_ct_on) c.twist = twm[mv_col[i]];
        for (int k = 0; k < ic_nb; k++) {
            c.eperm[k] = (int)epm[k][i];
            c.etail[k] = (int)epn[k][i];
        }
        c.eori = (int)efm[i] ^ ic_em_flip[i];
        c.eknown = (int)ekm[i];
        if (ic_df_on) {
            c.cdown = ic_cdown_move[(size_t)s->cdown * nmoves_in + i];
            c.edown = ic_edown_move[(size_t)s->edown * nmoves_in + i];
        }
        int cb = ic_bound(&c, togo - 1);
        if (cb > togo - 1) {
            i = mv_grp_skip(i, cb, togo);
            continue;
        }
        th->path[sp] = i;
        th->nodes = nodes;
        ic_dfs(th, &c, togo - 1, sp + 1, nacc, nrho);
        nodes = th->nodes;
        if (opt_found || engine_stop) { th->nodes = nodes; return; }
    }
    th->nodes = nodes;
}

static unsigned __stdcall ic_worker(void *arg)
{
    ParStat *st = arg;
    IcThr th;
    st->nodes = 0;
    st->nsol = 0;
    st->solhash = 0;
    for (;;) {
        long idx = InterlockedIncrement(&ic_next_task) - 1;
        if (idx >= ic_ntasks || engine_stop || opt_found) break;
        IcTask *t = &ic_tasks[idx];
        th.nodes = 0;
        th.nsol = 0;
        th.hash = 0;
        memcpy(th.path, t->path, (size_t)t->sp * sizeof *t->path);
        ic_dfs(&th, &t->st, t->togo, t->sp, t->acc_std, t->rhos);
        emit_drain();
        st->nodes   += th.nodes;
        st->nsol    += th.nsol;
        st->solhash += th.hash;
    }
    rend_thread_done();
    return 0;
}

/* One depth pass: workers pull tasks cut `split` levels down, so an uneven tree idles threads. */
static void ic_pass(int d, int split,
                    long long *nodes_out, long long *nsol_out, unsigned long long *hash_out)
{
    IcThr coll;
    coll.nodes = 0;
    coll.nsol = 0;
    coll.hash = 0;
    ic_split = split;
    ic_ntasks = 0;
    ic_next_task = 0;
    ic_dfs(&coll, &ic_start, d, 0, 0, 1u);
    ic_split = 0;
    emit_drain();                       /* the collecting walk is this thread's whole turn */

    long long nodes = coll.nodes;
    long long nsol  = coll.nsol;
    unsigned long long hash = coll.hash;
    if (ic_ntasks == 0) {
        *nodes_out = nodes; *nsol_out = nsol; *hash_out = hash;
        return;
    }

    int nthr = par_threads < ic_ntasks ? par_threads : ic_ntasks;
    par_fanout(ic_worker, nthr, &nodes, &nsol, &hash);
    *nodes_out = nodes; *nsol_out = nsol; *hash_out = hash;
}

/* The relaxed path for a '?' cube: its own tables, then one search from its own bound. */
static int run_incomplete(const Job *j, char *built_cs)
{
    /* the goal is read in the fixed-center frame: one held turned is the solved cube renamed */
    char gchk[55];
    snprintf(gchk, sizeof gchk, "%s", j->goalstring);
    cube_canonical(gchk, NULL);
    if (strcmp(gchk, CUBE_SOLVED)) {
        /* only the per-completion searches can aim at a goal; this run is one relaxed search */
        fprintf(stderr, "--goal is not read on this path: the relaxed search takes these "
                        "completions, and it ends at every known cubie home\n");
        return 1;
    }

    double t_wall = now_sec();
    totnodes = 0;
    opt_found = 0;                  /* per-run flags: a batch runs several jobs in one process */

    if (job_move_list(j)) return 1;

    /* every table from here is laid out over mv_col[0 .. nmoves_in), the list just built */
    {
        const char *err = ic_parse(built_cs, &ic_in);
        if (err) { fprintf(stderr, "%s\n", err); return 1; }
    }

    /* Every legal completion agrees on these, so one member stands in for the unknowns. */
    ic_enum_members(1);
    if (ic_nmembers == 0) {
        fprintf(stderr, ic_count_oom
                        ? "the completions could not be counted (out of memory): whether one of "
                          "them is legal is not known\n"
                        : "no legal completion of the visible stickers\n");
        return 1;
    }
    ic_fill = ic_members[0];

    ic_src_cperm_mask = 0;
    for (int i = 0; i < 8; i++) if (ic_in.kfull_c[i]) ic_src_cperm_mask |= 1 << ic_in.cp[i];

    ic_start.cperm = get_corners(&ic_fill);
    {
        int v = 0, tw = 0;
        for (int i = 0; i < 8; i++) {
            /* the parsed ori for a known corner, the picked one for a partial; d = 3 for wiped */
            int d = ic_in.kc[i] ? ic_fill.co[i] : 3;
            v |= d << (2 * i);
            if (i < 7) tw = 3 * tw + d;
        }
        ic_start.cori = v;
        ic_start.twist = tw;
    }
    {
        /* the flip tracker covers the fully-known edges only; partials are judged at the leaf */
        int mask = 0, fl = 0;
        for (int i = 0; i < 12; i++)
            if (ic_in.kfull_e[i]) {
                mask |= 1 << i; fl |= ic_fill.eo[i] << i;
            }
        ic_start.eknown = mask;
        ic_start.eori = fl;
    }

    /* the edge batches: known edge ids ascending, six at a time bound tighter than apart */
    {
        int ids[12], m = 0;
        for (int e = 0; e < 12; e++)
            for (int i = 0; i < 12; i++) if (ic_in.ke[i] && ic_in.ep[i] == e) ids[m++] = e;
        ic_nb = 0;
        for (int off = 0; off < m; off += IC_BATCH) {
            int k = m - off < IC_BATCH ? m - off : IC_BATCH;
            for (int t = 0; t < k; t++) ic_batch_tok[ic_nb][t] = ids[off + t];
            ic_batch_n[ic_nb] = k;
            int slot[IC_BATCH];
            for (int t = 0; t < k; t++)
                for (int i = 0; i < 12; i++)
                    if (ic_fill.ep[i] == ids[off + t]) {
                        slot[t] = i; break;
                    }
            ic_start.eperm[ic_nb] = ic_eperm_encode(slot, ic_ep_lead(k));
            ic_start.etail[ic_nb] = ic_eperm_encode(slot + ic_ep_lead(k), ic_ep_tail(k));
            ic_nb++;
        }
    }

    /* the tables, all in memory: corners, orientations, the flip trackers, one BFS per batch */
    double t_tables = now_sec();
    ic_df_picksym();
    ic_df_on = 1;                   /* the identity always qualifies; a missing D cubie clears it */
    int pc[4], pe[4];
    for (int b = 4; b < 8; b++) {
        pc[b - 4] = pe[b - 4] = -1;
        for (int i = 0; i < 8;  i++) if (ic_in.kfull_c[i] && ic_in.cp[i] == b) pc[b - 4] = i;
        for (int i = 0; i < 12; i++) if (ic_in.kfull_e[i] && ic_in.ep[i] == b) pe[b - 4] = i;
        if (pc[b - 4] < 0 || pe[b - 4] < 0) ic_df_on = 0;
    }
    if (ic_df_on) {
        ic_start.cdown = ic_down_enc(pc, 8);
        ic_start.edown = ic_down_enc(pe, 12);
    }
    /* Unreusable first: dropping a group drops those on it; ic_ct_on is the last cube's */
    if (!ic_have_move())
        ic_free_tables();       /* no run reaches this point with tasks still queued */
    else {
        if (!ic_have_down()) ic_free_down_tables();
        if (!ic_have_corner()) ic_free_corner_prun();
        else if (!ic_have_edge()) ic_free_edge_prun();
    }
    ic_ct_on = 1;                   /* a wiped corner leaves the twist undefined */
    for (int i = 0; i < 8; i++)
        if (!ic_in.kc[i]) ic_ct_on = 0;
    /* Too few symmetries left and the classes outgrow what the joint bound earns back. */
    if (ic_ct_on && ic_ct_syms() < IC_CT_SYMS_MIN) ic_ct_on = 0;
    if (!ic_cp_move) ic_gen_cp_tables();
    if (!ic_ct_on && !ic_co_move) ic_gen_co_tables();
    if (!ic_cp_move || (!ic_ct_on && !ic_co_move)) goto oom;
    if (ic_df_on && !ic_df_prun) {
        ic_gen_down_tables();
        /* it returns void and can come back with the table still NULL; ic_bound reads it */
        if (!ic_df_prun) goto oom;
    }
    if (!ic_ct_on && !ic_cp_prun) {
        ic_cp_prun = malloc(N_CORNERS);
        ic_co_prun = malloc(65536);
        if (!ic_cp_prun || !ic_co_prun) goto oom;
        if (ic_bfs(ic_cp_prun, N_CORNERS, ic_cp_move, ic_cperm_is_src)) goto oom;
        if (ic_bfs(ic_co_prun, 65536, ic_co_move, ic_cori_is_src)) goto oom;
    }
    if (ic_ct_on && !ic_ct_prun && !ic_ct_from_disk()) {
        if (ic_ct_gen_cls()) goto oom;
        size_t nct = (size_t)ic_ct_ncls * N_TWIST;
        ic_ct_raw = malloc(nct);
        ic_ct_prun = ic_ct_raw ? malloc(nct / 2 + 1) : NULL;
        if (!ic_ct_prun) goto oom;
        if (ic_bfs_ct()) goto oom;
    }
    if (!ic_em_move) ic_gen_flip_tables();
    if (!ic_em_move) goto oom;
    for (int b = 0; b < ic_nb; b++) {
        int k = ic_batch_n[b], ln = ic_ep_lead(k), tn = ic_ep_tail(k);
        if (!ic_ep_moves[ln] && ic_gen_edge_tables(ln)) goto oom;
        if (!ic_ep_moves[tn] && ic_gen_edge_tables(tn)) goto oom;
        ic_ep_lmove[b] = ic_ep_moves[ln];
        ic_ep_tmove[b] = ic_ep_moves[tn];
        ic_ep_tsize[b] = ic_eperm_n(tn);
        if (ic_ep_prun[b]) continue;
        long n = (long)ic_eperm_n(ln) * ic_ep_tsize[b];
        u8 *raw = malloc(n);
        ic_ep_prun[b] = malloc(n / 2 + 1);
        if (!raw || !ic_ep_prun[b]) { free(raw); goto oom; }
        int home[IC_BATCH];
        for (int t = 0; t < k; t++) home[t] = ic_batch_tok[b][t];
        long src = (long)ic_eperm_encode(home, ln) * ic_ep_tsize[b] +
                   ic_eperm_encode(home + ln, tn);
        if (ic_bfs_pair(raw, n, ic_ep_moves[ln], ic_ep_moves[tn], ic_ep_tsize[b], src)) {
            free(raw);
            goto oom;
        }
        ic_pack_nibble(ic_ep_prun[b], raw, (size_t)n);
        free(raw);
    }
    t_tables = now_sec() - t_tables;
    ic_built.nmoves = nmoves_in;
    memcpy(ic_built.mv_col, mv_col, (size_t)nmoves_in * sizeof *mv_col);
    ic_built.src_mask = ic_src_cperm_mask;
    ic_built.df_nsym  = ic_df_nsym;
    memcpy(ic_built.df_sym, ic_df_sym, (size_t)ic_df_nsym * sizeof *ic_df_sym);
    ic_built.has_down = ic_df_prun != NULL;
    ic_built.nb       = ic_nb;
    memcpy(ic_built.batch_n, ic_batch_n, sizeof ic_batch_n);
    memcpy(ic_built.batch_tok, ic_batch_tok, sizeof ic_batch_tok);
    ic_built.has_move = ic_built.has_corner = ic_built.has_edge = 1;

    if (render_mode) rend_init(j->subgroup, j->depth);
    /* the parser relabeled into the fixed-center frame, so the solutions are face moves already */
    rend_boost = 0;

    /* 0xff asks for the true bound, and 255 comes back when the tables never reached the start */
    int from = ic_bound(&ic_start, 0xff);
    if (!tab_out) {
        printf(CS_LINE_FMT, built_cs);
        printf("incomplete: %d corners and %d edges known (of 20), %d edge batch(es); "
               "tables built in %s s\n",
               ic_in.nkc, ic_in.nke, ic_nb, fmt_secs(t_tables));
        /* Known = every sticker of the cubie; one short is still placed by what is showing. */
        int part_c = 0, part_e = 0;
        for (int i = 0; i < 8;  i++) if (ic_in.kc[i] && !ic_in.kfull_c[i]) part_c++;
        for (int i = 0; i < 12; i++) if (ic_in.ke[i] && !ic_in.kfull_e[i]) part_e++;
        if (part_c || part_e)
            printf("incomplete: %d corners and %d edges are known in part as well\n",
                   part_c, part_e);
        print_run_line(j, from);
    }

    /* One walk per depth from the relaxed bound; a pass splits only when the previous was heavy */
    double t_all = now_sec();
    long long nsol_total = 0;
    unsigned long long hash_total = 0;
    long long prev_nodes = 0;
    /* A bench row is a line of columns: the reason goes where it cannot be taken for one */
    if (from == 0xff)
        fprintf(tab_out ? stderr : stdout,
                "no solution: this cube has no route to the goal in subgroup %s\n", j->subgroup);
    for (int d = from; d <= j->depth; d++) {
        /* as in the main engine: a Stop pressed while the tables were built pays for no pass */
        if (engine_stop) break;
        int split = 0;
        if (par_threads > 1)
            split = split_for(d, prev_nodes, 1);
        double t1 = now_sec();
        long long nodes, nsol;
        unsigned long long hash;
        ic_pass(d, split, &nodes, &nsol, &hash);
        totnodes   += nodes;
        nsol_total += nsol;
        hash_total += hash;
        prev_nodes  = nodes;
        print_depth_line(d, nsol_total, nodes, t1);
        if (engine_stop) break;
        if (opt_mode) {
            if (render_mode ? rend_on > 0 : nsol_total > 0) break;
        }
    }
    double dt = now_sec() - t_all;
    /* the tables it built are this path's load_wall: wall leaves them out, as the main one does */
    double t_wall_total = now_sec() - t_wall - t_tables;

    /* The tables stay for the next cube: a shared shape rebuilds less. */
    ic_free_tasks();

    /* Both branches close the solutions file; one asked for and not written fails the job. */
    if (tab_out) {
        int lost_row = emit_tab_row(j, nsol_total, hash_total, t_wall_total, dt);
        out_close();
        return lost_row || out_lost;
    }
    report_block(j, nsol_total, hash_total, t_wall_total, dt);
    return out_lost;
oom:
    if (!ic_fail_msg) fprintf(stderr, IC_OOM_MSG);
    ic_fail_msg = NULL;
    ic_free_tables();
    return 1;
}

#endif /* INCOMP_H */
