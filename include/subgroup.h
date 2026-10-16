/* Cubolt (https://github.com/nbwzx/cubolt)
 * Copyright (C) 2026 Zixing Wang <zixingwang.cn@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* subgroup.h -- the STM search data of one subgroup, derived from its letter set. */
#ifndef SUBGROUP_H
#define SUBGROUP_H

/* the letters the tables ask about, as bit masks over "URFDLB" / "EMS" / "urfdlb" */
static unsigned sg_faces, sg_slices, sg_wides;

/* ---- per-subgroup data, static: col, mc, ban and rho, held for the whole run ---- */
static int                sg_col[N_MOVE];
static int                sg_mc[N_MOVE];
/* the token of each counted move, parsed once rather than once per candidate spelling */
static int                sg_ax[N_MOVE], sg_k1[N_MOVE], sg_k2[N_MOVE];
/* what a move's cheapest spelling costs from each frame; mc reads it */
static int                sg_cost[N_MOVE][24];
/* one bit per move: sg_ban_f[24*column + frame] names what the walk may not follow it with */
static unsigned long long sg_ban_f[N_MOVE * 24];
_Static_assert(N_MOVE <= 64, "a ban row is one bit per move in an unsigned long long");
static unsigned           sg_rho[N_MOVE * 24];
static int                sg_cached;
static int                sg_blind;      /* frames carry one language */
static unsigned           sg_le[24];     /* sg_le[b]: the frames whose language holds b's own */
static int                sg_n;

/* ---- renderings: every rendering of one column from one frame ---- */
/* The frame a rendering folds into, or -1 when the folded moves leave the subgroup. */
static int sg_fold(const uint8_t *c, int cn, int rho)
{
    uint8_t body[4];
    int rot = rho, bn = 0;
    for (int i = 0; i < cn; i++) rend_fold_step(body, &bn, &rot, c[i]);
    return rend_codes_allowed(body, bn, sg_faces, sg_slices, sg_wides) ? rot : -1;
}

static void sg_fold_row(int ax, int k1, int k2, int rho, unsigned *out, int *best)
{
    *out = 0;
    *best = -1;
    int key = rend_col_key(ax, k1, k2);
    for (int j = 0; j < rend_cn[key]; j++) {
        int cost = rend_coc[key][j];
        int rot = sg_fold(rend_co[key][j], rend_con[key][j], rho);
        if (rot < 0) continue;
        *out |= 1u << rot;
        if (*best < 0 || cost < *best) *best = cost;
    }
}

/* mc is the bound the search prunes with: too large prunes real solutions, and 1 is its floor. */
static int sg_min_cost(int i, unsigned reach)
{
    int best = -1;
    for (int r = 0; r < 24; r++) {
        if (!((reach >> r) & 1u)) continue;
        int cost = sg_cost[i][r];
        if (cost < 0) continue;             /* no rendering of it survives this frame */
        if (best < 0 || cost < best) best = cost;
        if (best == 1) break;
    }
    return best < 0 ? 1 : best;
}

/* -1 when the subgroup holds no such column, else its position in the set. */
static int sg_idx_of(int col)
{
    for (int i = 0; i < sg_n; i++)
        if (sg_col[i] == col) return i;
    return -1;
}

static unsigned sg_rho_apply(int k, unsigned set)
{
    unsigned acc = 0, rest = set;
    while (rest) {
        int r = __builtin_ctz(rest);
        rest &= rest - 1;
        acc |= sg_rho[k * 24 + r];
    }
    return acc;
}

/* ---- the ban ---- */
/* Two moves of one axis commute, so a pair goes when a preferred spelling covers its frames. */

/* Can Y carry every word X can?  On overflow it answers no, and no only costs a ban. */
static int sg_lang_within(unsigned X, unsigned Y)
{
    enum { HB = 14, SMAX = 1 << 12 };
    static unsigned sx[SMAX], sy[SMAX];
    static int slot[1 << HB];
    memset(slot, 0, sizeof slot);
    int nseen = 0;
    sx[nseen] = X; sy[nseen] = Y;
    slot[(X * 2654435761u + Y * 40503u) >> (32 - HB)] = 1;
    nseen++;
    for (int head = 0; head < nseen; head++) {
        for (int k = 0; k < sg_n; k++) {
            unsigned X2 = sg_rho_apply(k, sx[head]), Y2 = sg_rho_apply(k, sy[head]);
            if (X2 && !Y2) return 0;
            if (!X2) continue;
            unsigned h = (X2 * 2654435761u + Y2 * 40503u) >> (32 - HB);
            while (slot[h]) {
                if (sx[slot[h] - 1] == X2 && sy[slot[h] - 1] == Y2) break;
                h = (h + 1) & ((1 << HB) - 1);
            }
            if (slot[h]) continue;
            if (nseen >= SMAX) return 0;
            sx[nseen] = X2; sy[nseen] = Y2; nseen++;
            slot[h] = nseen;
        }
    }
    return 1;
}

/* Every frame carrying the same language: then a replacement only has to be walkable. */
static int sg_frames_alike(unsigned reach)
{
    for (int b = 1; b < 24; b++)
        if ((reach >> b) & 1u && !(sg_lang_within(1u << b, 1u) && sg_lang_within(1u, 1u << b)))
            return 0;
    return 1;
}

static int sg_step_within(int b, int bp)
{
    for (int c = 0; c < sg_n; c++)
        if (sg_rho[c * 24 + b] && !sg_rho[c * 24 + bp]) return 0;
    return 1;
}

static void sg_build_le(unsigned reach)
{
    for (int b = 0; b < 24; b++) {
        if (!((reach >> b) & 1u)) {
            sg_le[b] = 0;
            continue;
        }
        sg_le[b] = sg_blind ? reach : 1u << b;
        if (sg_blind) continue;
        for (int bp = 0; bp < 24; bp++)
            if (bp != b && ((reach >> bp) & 1u) && sg_step_within(b, bp) &&
                sg_lang_within(1u << b, 1u << bp))
                sg_le[b] |= 1u << bp;
    }
}

/* Does Bp take over B?  Every frame of B needs a frame of Bp holding its suffixes. */
static int sg_covers(unsigned B, unsigned Bp)
{
    unsigned rest = B;
    while (rest) {
        int b = __builtin_ctz(rest);
        rest &= rest - 1;
        if (!(sg_le[b] & Bp)) return 0;
    }
    return 1;
}

/* Is there a spelling of the pair (i, j) the walk prefers that reaches every frame of B? */
static int sg_pair_folds(int i, int j, int f, unsigned B)
{
    int lax = sg_ax[i];
    int pk1 = (sg_k1[i] + sg_k1[j]) % 4, pk2 = (sg_k2[i] + sg_k2[j]) % 4;
    int sum = sg_mc[i] + sg_mc[j];
    int both = (sg_k1[i] && sg_k2[i]) + (sg_k1[j] && sg_k2[j]);

    /* the two undo, so what is left of the pair is the frame the walk was already on */
    if (!(pk1 || pk2)) return sg_covers(B, 1u << f);
    /* stm_column reads only the nine columns of an axis, so the product has to be one of them */
    if (stm_is_col(pk1, pk2)) {
        int k = sg_idx_of(stm_column(lax, pk1, pk2));
        if (k >= 0 && sg_covers(B, sg_rho[k * 24 + f])) return 1;
    }
    for (int u = 0; u < sg_n; u++) {
        if (sg_ax[u] != lax) continue;
        unsigned su = sg_rho[u * 24 + f];
        for (int v = 0; v < sg_n; v++) {
            if (sg_ax[v] != lax) continue;
            if ((sg_k1[u] + sg_k1[v]) % 4 != pk1 || (sg_k2[u] + sg_k2[v]) % 4 != pk2) continue;
            int cost = sg_mc[u] + sg_mc[v];
            int plainer = (sg_k1[u] && sg_k2[u]) + (sg_k1[v] && sg_k2[v]);
            /* Shorter wins, then cheaper, then fewer two-face turns, then the earlier index. */
            if (cost > sum) continue;
            if (cost == sum && plainer > both) continue;
            if (cost == sum && plainer == both && (u > i || (u == i && v >= j))) continue;
            if (sg_covers(B, sg_rho_apply(v, su))) return 1;
        }
    }
    return 0;
}

static void sg_move_row(int k, int i)
{
    memcpy(&sg_rho[k * 24], &sg_rho[i * 24], sizeof(unsigned) * 24);
    memcpy(&sg_cost[k], &sg_cost[i], sizeof sg_cost[k]);
    sg_col[k] = sg_col[i];
    sg_ax[k] = sg_ax[i]; sg_k1[k] = sg_k1[i]; sg_k2[k] = sg_k2[i];
}

/* Fills the tables of one subgroup's letters and leaves the count in sg_n. */
static void sg_build(const char *sub, unsigned char (*ak)[3])
{
    /* The cache key is the three masks; the rotation letters only say what the output may write. */
    unsigned faces, slices, wides;
    rend_letter_masks(sub, &faces, &slices, &wides, NULL);
    if (sg_cached && faces == sg_faces && slices == sg_slices && wides == sg_wides)
        return;
    sg_cached = 1;
    sg_faces = faces; sg_slices = slices; sg_wides = wides;
    cube_build_relabel();
    rend_opts_init(sg_wides);

    /* A move keeps the frame it starts from when no rendering can be turned away. */
    int use_rho = !rend_all_moves(sg_faces, sg_slices, sg_wides);

    sg_n = 0;
    for (int m = 0; m < N_MOVE; m++) {
        const unsigned char *a = ak[m];
        unsigned *row = &sg_rho[sg_n * 24], alive = 0;
        for (int r = 0; r < 24; r++) {
            unsigned out;
            int best;
            sg_fold_row(a[0], a[1], a[2], r, &out, &best);
            row[r] = use_rho ? out : 1u << r;
            sg_cost[sg_n][r] = best;
            alive |= row[r];
        }
        /* a column is a move when some incoming rotation lets one rendering stay in the subgroup */
        if (!alive) continue;
        sg_col[sg_n] = m;
        sg_ax[sg_n] = a[0]; sg_k1[sg_n] = a[1]; sg_k2[sg_n] = a[2];
        sg_n++;
    }

    /* the closure of {0} under the moves' rows; rows only add frames */
    unsigned reach = 1u;
    for (;;) {
        unsigned grew = reach;
        for (int i = 0; i < sg_n; i++) grew |= sg_rho_apply(i, reach);
        if (grew == reach) break;
        reach = grew;
    }
    /* such a row reaches nothing the walk can be on, so dropping it leaves the closure as it is */
    int k = 0;
    for (int i = 0; i < sg_n; i++) {
        if (!sg_rho_apply(i, reach)) continue;  /* no frame it reaches is one we can be on */
        if (k != i) sg_move_row(k, i);
        k++;
    }
    sg_n = k;

    sg_blind = sg_frames_alike(reach);
    sg_build_le(reach);

    for (int i = 0; i < sg_n; i++)
        sg_mc[i] = sg_min_cost(i, reach);

    memset(sg_ban_f, 0, sizeof sg_ban_f);
    /* One language: frame 0's answer holds for every frame. */
    unsigned ask = sg_blind ? 1u : reach;
    for (int i = 0; i < sg_n; i++) {
        int lm = sg_col[i];
        for (int j = 0; j < sg_n; j++) {
            int nmv = sg_col[j];
            if (sg_ax[i] != sg_ax[j]) continue;
            /* An unreachable pair costs nothing; the rest go when another spelling covers them. */
            for (int f = 0; f < 24; f++) {
                if (!((ask >> f) & 1u)) continue;
                unsigned B = sg_rho_apply(j, sg_rho[i * 24 + f]);
                if (B && !sg_pair_folds(i, j, f, B)) continue;
                unsigned rest = sg_blind ? reach : 1u << f;
                while (rest) {
                    int g = __builtin_ctz(rest);
                    rest &= rest - 1;
                    sg_ban_f[lm * 24 + g] |= 1ULL << nmv;
                }
            }
        }
    }
}
#endif /* SUBGROUP_H */
