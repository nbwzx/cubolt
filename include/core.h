/* Cubolt (https://github.com/nbwzx/cubolt)
 * Copyright (C) 2026 Zixing Wang <zixingwang.cn@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* core.h -- the cubie model: conversions, the four search coordinates, and moveCube. */
#ifndef CORE_H
#define CORE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cube.h"

/* An axis owns N_MOVE_PER_AXIS columns, so a column's axis is col / N_MOVE_PER_AXIS. */
#define N_MOVE_PER_AXIS     9
#define N_MOVE              (N_MOVE_PER_AXIS * 3)
#define N_MOVE_FTM          18
#define N_FLIP              2048
#define N_TWIST             2187
#define N_SLICE_SORTED      11880
#define N_CORNERS           40320
#define N_CORNERS_CLASS     2768
#define N_SYM               48
#define N_SYM_D4H           16
#define N_FLIPSLICESORTED_CLASS 1523864

/* On-disk sizes; phase1x24 packs five trits to a byte. */
#define CT_PRUN_BYTES       (N_CORNERS_CLASS * N_TWIST)
/* the product is past an int: counted as it is written */
#define PHASE1X24_BYTES     (((long long)N_FLIPSLICESORTED_CLASS * N_TWIST + 4) / 5)
#define MOVE_TWIST_BYTES        ((long long)N_TWIST * N_MOVE * 2)
#define MOVE_FLIP_BYTES         ((long long)N_FLIP * N_MOVE * 2)
#define MOVE_SLICE_SORTED_BYTES ((long long)N_SLICE_SORTED * N_MOVE * 2)
#define MOVE_CORNERS_BYTES      ((long long)N_CORNERS * N_MOVE * 2)
#define CONJ_TWIST_BYTES        ((long long)N_TWIST * N_SYM_D4H * 2)
#define CT_CLSYM_BYTES          ((long long)N_CORNERS * 4)
#define FS24_CLSYM_BYTES        ((long long)N_FLIP * N_SLICE_SORTED * 4)
#define FS24_REP_BYTES          ((long long)N_FLIPSLICESORTED_CLASS * 4)

/* A (class, symmetry) pair packed `class << 4 | sym`; 0xffffffff is not one: "not assigned yet". */
#define FS24_PACK(cls, sym) (((u32)(cls) << 4) | (u32)(sym))
#define CLS_OF(v)           ((v) >> 4)
#define SYM_OF(v)           ((v) & 15u)

typedef uint16_t u16;
typedef uint32_t u32;
typedef uint8_t  u8;
typedef int8_t   s8;

/* Place values of the five trits in one distance byte. */
static const u32 POW3[5] = {1, 3, 9, 27, 81};

/* ---- the cubie level ---- */

typedef struct { s8 cp[8]; s8 co[8]; s8 ep[12]; s8 eo[12]; } Cube;

/* the two *_FACELET tables number facelets 0..53, the two *_COLOR tables faces 0..5 */
static const int CORNER_FACELET[8][3] = {
    { 8,  9, 20}, { 6, 18, 38}, { 0, 36, 47}, { 2, 45, 11},
    {29, 26, 15}, {27, 44, 24}, {33, 53, 42}, {35, 17, 51}
};
static const int EDGE_FACELET[12][2] = {
    { 5, 10}, { 7, 19}, { 3, 37}, { 1, 46},
    {32, 16}, {28, 25}, {30, 43}, {34, 52},
    {23, 12}, {21, 41}, {50, 39}, {48, 14}
};
static const int CORNER_COLOR[8][3] = {
    {0, 1, 2}, {0, 2, 4}, {0, 4, 5}, {0, 5, 1},
    {3, 2, 1}, {3, 4, 2}, {3, 5, 4}, {3, 1, 5}
};
static const int EDGE_COLOR[12][2] = {
    {0, 1}, {0, 2}, {0, 4}, {0, 5}, {3, 1}, {3, 2},
    {3, 4}, {3, 5}, {2, 1}, {2, 4}, {5, 4}, {5, 1}
};

/* The six basic face turns U R F D L B; build_move_cubes fills them in. */
static Cube BASIC_MOVE[6];

/* the four basic symmetries, in this order: a URF 3-cycle, F2, U4, and the LR2 mirror */
static const Cube BASIC_SYM[4] = {
    {{0,4,5,1,3,7,6,2}, {1,2,1,2,2,1,2,1}, {1,8,5,9,3,11,7,10,0,4,6,2}, {1,0,1,0,1,0,1,0,1,1,1,1}},
    {{5,4,7,6,1,0,3,2}, {0,0,0,0,0,0,0,0}, {6,5,4,7,2,1,0,3,9,8,11,10}, {0,0,0,0,0,0,0,0,0,0,0,0}},
    {{3,0,1,2,7,4,5,6}, {0,0,0,0,0,0,0,0}, {3,0,1,2,7,4,5,6,11,8,9,10}, {0,0,0,0,0,0,0,0,1,1,1,1}},
    {{1,0,3,2,5,4,7,6}, {3,3,3,3,3,3,3,3}, {2,1,0,3,6,5,4,7,9,8,11,10}, {0,0,0,0,0,0,0,0,0,0,0,0}}
};

static Cube moveCube[N_MOVE];
static Cube symCube[N_SYM];
static int  inv_idx[N_SYM_D4H];

/* The STM column of FTM move m, three to a face in URFDLB order: m = 3*face + k. */
static int ftm_col[N_MOVE_FTM];

static void identity_cube(Cube *c)
{
    for (int i = 0; i < 8;  i++) { c->cp[i] = (s8)i; c->co[i] = 0; }
    for (int i = 0; i < 12; i++) { c->ep[i] = (s8)i; c->eo[i] = 0; }
}

/* The orientation of a*b; 3..5 is a mirror: twists add below the mirror bit, subtract above. */
static const s8 CORNER_ORI[6][6] = {
    {0, 1, 2, 3, 4, 5},
    {1, 2, 0, 4, 5, 3},
    {2, 0, 1, 5, 3, 4},
    {3, 5, 4, 0, 2, 1},
    {4, 3, 5, 1, 0, 2},
    {5, 4, 3, 2, 1, 0}
};

static void corner_multiply(Cube *a, const Cube *b)
{
    s8 cp[8], co[8];
    for (int c = 0; c < 8; c++) {
        s8 i = b->cp[c];
        cp[c] = a->cp[i];
        co[c] = CORNER_ORI[a->co[i]][b->co[c]];
    }
    for (int c = 0; c < 8; c++) { a->cp[c] = cp[c]; a->co[c] = co[c]; }
}

static void edge_multiply(Cube *a, const Cube *b)
{
    s8 ep[12], eo[12];
    for (int e = 0; e < 12; e++) {
        ep[e] = a->ep[b->ep[e]];
        eo[e] = (s8)(b->eo[e] ^ a->eo[b->ep[e]]);
    }
    for (int e = 0; e < 12; e++) { a->ep[e] = ep[e]; a->eo[e] = eo[e]; }
}

static void multiply(Cube *a, const Cube *b) { corner_multiply(a, b); edge_multiply(a, b); }

/* ---- coordinates (get_* and set_*) ---- */

static void rotate_left(s8 *arr, int r)
{
    s8 temp = arr[0];
    for (int i = 0; i < r; i++) arr[i] = arr[i + 1];
    arr[r] = temp;
}

static void rotate_right(s8 *arr, int r)
{
    s8 temp = arr[r];
    for (int i = r; i > 0; i--) arr[i] = arr[i - 1];
    arr[0] = temp;
}

/* C(n,k) over the range the slice coordinates ask for, which is n < 12 and k < 5. */
static const int C_NK[12][5] = {
    {1,  0,  0,   0,   0},
    {1,  1,  0,   0,   0},
    {1,  2,  1,   0,   0},
    {1,  3,  3,   1,   0},
    {1,  4,  6,   4,   1},
    {1,  5, 10,  10,   5},
    {1,  6, 15,  20,  15},
    {1,  7, 21,  35,  35},
    {1,  8, 28,  56,  70},
    {1,  9, 36,  84, 126},
    {1, 10, 45, 120, 210},
    {1, 11, 55, 165, 330}
};

/* Orientation coordinate: one base-`base` digit per piece 0..n-1; piece n follows from parity. */
static int orient_get(const s8 *arr, int base, int n)
{
    int ret = 0;
    for (int i = 0; i < n; i++) ret = base * ret + arr[i];
    return ret;
}

static void orient_set(s8 *arr, int val, int base, int n)
{
    int parity = 0;
    for (int i = n - 1; i >= 0; i--) { arr[i] = (s8)(val % base); parity += arr[i]; val /= base; }
    arr[n] = (s8)((base - parity % base) % base);
}

/* Setters only mktables reaches; the attribute keeps -Wall quiet in the TUs that only read. */
static void set_twist(Cube *c, int twist) __attribute__((unused));
static void set_flip(Cube *c, int flip) __attribute__((unused));
static int get_twist(const Cube *c) { return orient_get(c->co, 3, 7); }
static void set_twist(Cube *c, int twist) { orient_set(c->co, twist, 3, 7); }
static int get_flip(const Cube *c) { return orient_get(c->eo, 2, 11); }
static void set_flip(Cube *c, int flip) { orient_set(c->eo, flip, 2, 11); }

static int get_slice_sorted(const Cube *c)
{
    int a = 0, x = 0;
    s8 edge4[4] = {0, 0, 0, 0};
    for (int j = 11; j >= 0; j--) {
        if (c->ep[j] >= 8 && c->ep[j] <= 11) {
            a += C_NK[11 - j][x + 1];
            edge4[3 - x] = c->ep[j];
            x++;
        }
    }
    int b = 0;
    for (int j = 3; j > 0; j--) {
        int k = 0;
        while (edge4[j] != j + 8) { rotate_left(edge4, j); k++; }
        b = (j + 1) * b + k;
    }
    return 24 * a + b;
}

/* set_slice_sorted: `ep` is overwritten whole, so the caller must not rely on the rest of it. */
static void set_slice_sorted(Cube *c, int idx) __attribute__((unused));
static void set_slice_sorted(Cube *c, int idx)
{
    s8 slice[4] = {8, 9, 10, 11};           /* FR FL BL BR */
    int otheri = 0;
    int b = idx % 24, a = idx / 24;
    for (int e = 0; e < 12; e++) c->ep[e] = -1;
    int j = 1;
    while (j < 4) {
        int k = b % (j + 1);
        b /= j + 1;
        while (k > 0) { rotate_right(slice, j); k--; }
        j++;
    }
    int x = 4;
    for (int e = 0; e < 12; e++) {
        int nk = C_NK[11 - e][x];
        if (a >= nk) { c->ep[e] = slice[4 - x]; a -= nk; x--; }
    }
    /* the rest in edge order: UR UF UL UB DR DF DL DB */
    for (int e = 0; e < 12; e++)
        if (c->ep[e] == -1) c->ep[e] = (s8)otheri++;
}

/* The Lehmer code: s_i is how many of cp[0..i-1] exceed cp[i]. */
static int get_corners(const Cube *c)
{
    int b = 0;
    for (int i = 7; i > 0; i--) {
        int s = 0, pi = c->cp[i];
        for (int j = i - 1; j >= 0; j--)
            if (c->cp[j] > pi) s++;
        b = (b + s) * i;
    }
    return b;
}

/* g_nth_bit[m][r] = the r-th set bit of m: one load, not a scan, per free corner. */
static s8 g_nth_bit[256][8];

static void build_nth_bit(void)
{
    for (int m = 0; m < 256; m++) {
        int k = 0;
        for (int i = 0; i < 8; i++)
            if (m >> i & 1) g_nth_bit[m][k++] = (s8)i;
    }
}

/* Back the other way: cp[i] is the (i - s_i)-th of the values still free. */
static void set_corners(Cube *c, int idx)
{
    static const int fact[8] = { 1, 1, 2, 6, 24, 120, 720, 5040 };
    int mask = 0xFF;
    for (int i = 7; i > 0; i--) {
        int r = i - idx / fact[i];
        idx %= fact[i];
        int v = g_nth_bit[mask][r];
        c->cp[i] = (s8)v;
        mask &= ~(1 << v);
    }
    c->cp[0] = (s8)__builtin_ctz(mask);
}

static int perm_parity(const int *p, int n)
{
    int s = 0;
    for (int i = n - 1; i > 0; i--)
        for (int j = i - 1; j >= 0; j--)
            if (p[j] > p[i]) s++;
    return s % 2;
}

/* verify's answer when a slot is empty (to_cubie_cube's -1) or a cubie was placed twice. */
#define EDGES_UNDEFINED   "Error: Some edges are undefined."
#define CORNERS_UNDEFINED "Error: Some corners are undefined."

/* A face turn moves four of each, a rotation need not: parity holds once `centers` is taken off. */
static const char *verify(const Cube *c, int centers)
{
    int cnt[12] = {0}, s = 0;
    for (int i = 0; i < 12; i++) {
        if (c->ep[i] < 0) return EDGES_UNDEFINED;
        cnt[c->ep[i]]++;
    }
    for (int i = 0; i < 12; i++) {
        if (cnt[i] != 1) return EDGES_UNDEFINED;
        s += c->eo[i];
    }
    if (s % 2 != 0) return "Error: Total edge flip is wrong.";

    int ccnt[8] = {0};
    for (int i = 0; i < 8; i++) {
        if (c->cp[i] < 0) return CORNERS_UNDEFINED;
        ccnt[c->cp[i]]++;
    }
    s = 0;
    for (int i = 0; i < 8; i++) {
        if (ccnt[i] != 1) return CORNERS_UNDEFINED;
        s += c->co[i];
    }
    if (s % 3 != 0) return "Error: Total corner twist is wrong.";

    int ep[12], cp[8];
    for (int i = 0; i < 12; i++) ep[i] = c->ep[i];
    for (int i = 0; i < 8;  i++) cp[i] = c->cp[i];
    if ((perm_parity(ep, 12) ^ perm_parity(cp, 8)) != centers)
        return "Error: Wrong edge and corner parity";

    return NULL;
}

/* ---- facelet level -> cubie level ---- */

static const char *from_string(const char *s, int f[54])
{
    /* Thread-local: two threads parse at once, and each keeps what this returns. */
    static __thread char err[256];
    size_t n = strlen(s);
    if (n != 54) return cube_bad_length(s);
    int cnt[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 54; i++) {
        int c = cube_face(s[i]);
        if (c < 0) {
            snprintf(err, sizeof err, "Error: Cube definition string %s contains "
                                      "the invalid character '%c'.", s, s[i]);
            return err;
        }
        f[i] = c; cnt[c]++;
    }
    for (int i = 0; i < 6; i++)
        if (cnt[i] != 9) {
            snprintf(err, sizeof err, "Error: Cube definition string %s does not contain "
                                      "exactly 9 facelets of each color.", s);
            return err;
        }
    return NULL;
}

static void to_cubie_cube(const int f[54], Cube *cc)
{
    for (int i = 0; i < 8;  i++) { cc->cp[i] = -1; cc->co[i] = 0; }
    for (int i = 0; i < 12; i++) { cc->ep[i] = -1; cc->eo[i] = 0; }

    for (int i = 0; i < 8; i++) {
        const int *fac = CORNER_FACELET[i];
        int ori = 2;                       /* no U/D facelet on this corner: ori stays 2 */
        for (int t = 0; t < 3; t++)
            if (f[fac[t]] == 0 || f[fac[t]] == 3) { ori = t; break; }
        int col1 = f[fac[(ori + 1) % 3]];
        int col2 = f[fac[(ori + 2) % 3]];
        for (int j = 0; j < 8; j++)
            /* The U/D facelet too: the two sides alone take a swapped one for a sound corner. */
            if (f[fac[ori]] == CORNER_COLOR[j][0] &&
                col1 == CORNER_COLOR[j][1] && col2 == CORNER_COLOR[j][2]) {
                cc->cp[i] = (s8)j; cc->co[i] = (s8)ori; break;
            }
    }
    for (int i = 0; i < 12; i++) {
        int a = f[EDGE_FACELET[i][0]], b = f[EDGE_FACELET[i][1]];
        for (int j = 0; j < 12; j++) {
            if (a == EDGE_COLOR[j][0] && b == EDGE_COLOR[j][1]) {
                cc->ep[i] = (s8)j; cc->eo[i] = 0; break;
            }
            if (a == EDGE_COLOR[j][1] && b == EDGE_COLOR[j][0]) {
                cc->ep[i] = (s8)j; cc->eo[i] = 1; break;
            }
        }
    }
}

/* Its cubie cube and, with `centers`, the parity verify() needs; NULL or why. */
static const char *read_cube(const char *s, Cube *cc, int *centers) __attribute__((unused));
static const char *read_cube(const char *s, Cube *cc, int *centers)
{
    int f[54];
    const char *err = from_string(s, f);
    if (err) return err;
    to_cubie_cube(f, cc);
    /* Face g shows f[9*g+4], so the centers permute; a face turn leaves it, a slice does not. */
    int p[6];
    for (int g = 0; g < 6; g++) p[g] = f[9 * g + 4];
    int cp = perm_parity(p, 6);
    if (centers) *centers = cp;
    return verify(cc, cp);
}

/* ---- the generated cubes ---- */

/* One amount at 0, or both turning the same way in space: opposite ways is two turns of one axis */
static int stm_is_col(int k1, int k2)
{
    return (k1 || k2) && (!k1 || !k2 || (k1 + k2) % 4 == 0);
}

/* The column of axis `ax`'s move turning A k1 times and B k2: three A, three B, three both. */
static int stm_column(int ax, int k1, int k2)
{
    int i = !k2 ? k1 - 1 : !k1 ? 2 + k2 : 5 + k1;
    return N_MOVE_PER_AXIS * ax + i;
}

static void build_move_cubes(void)
{
    build_nth_bit();
    for (int f = 0; f < 6; f++) {
        int fc[54];
        for (int i = 0; i < 54; i++) fc[i] = cube_face(CUBE_SOLVED[FL_MOVE_PERM[f][i]]);
        to_cubie_cube(fc, &BASIC_MOVE[f]);
    }

    for (int ax = 0; ax < 3; ax++) {
        for (int k1 = 0; k1 < 4; k1++) {
            Cube c1; identity_cube(&c1);
            for (int t = 0; t < k1; t++) multiply(&c1, &BASIC_MOVE[ax]);
            for (int k2 = 0; k2 < 4; k2++) {
                if (!stm_is_col(k1, k2)) continue;
                Cube cc = c1;
                for (int t = 0; t < k2; t++) multiply(&cc, &BASIC_MOVE[ax + 3]);
                moveCube[stm_column(ax, k1, k2)] = cc;
            }
        }
    }
    /* U/D the UD axis, R/L the RL one, F/B the FB one; each pair's first turns U, R or F. */
    for (int v = 0; v < N_MOVE_FTM; v++) {
        int face = v / 3, k = v % 3;
        ftm_col[v] = stm_column(face % 3, face < 3 ? k + 1 : 0, face < 3 ? 0 : k + 1);
    }
}

static void build_symmetry_tables(void)
{
    Cube cc; identity_cube(&cc);
    int idx = 0;
    for (int urf3 = 0; urf3 < 3; urf3++) {
        for (int f2 = 0; f2 < 2; f2++) {
            for (int u4 = 0; u4 < 4; u4++) {
                for (int lr2 = 0; lr2 < 2; lr2++) {
                    /* the mirror is the one appended last, so lr2 == 0 walks the rotations */
                    symCube[idx++] = cc;
                    multiply(&cc, &BASIC_SYM[3]);
                }
                multiply(&cc, &BASIC_SYM[2]);
            }
            multiply(&cc, &BASIC_SYM[1]);
        }
        multiply(&cc, &BASIC_SYM[0]);
    }
    /* Every inverse is another of the first N_SYM_D4H, so the search stays inside them */
    for (int j = 0; j < N_SYM_D4H; j++) {
        for (int i = 0; i < N_SYM_D4H; i++) {
            Cube c = symCube[j];
            corner_multiply(&c, &symCube[i]);       /* identity iff cp[URF,UFL,ULB] fixed */
            if (c.cp[0] == 0 && c.cp[1] == 1 && c.cp[2] == 2) { inv_idx[j] = i; break; }
        }
    }
}

/* One corner permutation per class and the symmetries leaving it alone: one distance serves all. */
static void ct_classes(const u32 *clssym, int rep[N_CORNERS_CLASS], unsigned sym[N_CORNERS_CLASS])
    __attribute__((unused));
static void ct_classes(const u32 *clssym, int rep[N_CORNERS_CLASS], unsigned sym[N_CORNERS_CLASS])
{
    for (int cp = 0; cp < N_CORNERS; cp++)
        if (SYM_OF(clssym[cp]) == 0) rep[CLS_OF(clssym[cp])] = cp;
    for (int c = 0; c < N_CORNERS_CLASS; c++) {
        Cube cc, ss;
        identity_cube(&cc);
        set_corners(&cc, rep[c]);
        sym[c] = 0;
        for (int s = 0; s < N_SYM_D4H; s++) {
            ss = symCube[s];
            corner_multiply(&ss, &cc);
            corner_multiply(&ss, &symCube[inv_idx[s]]);
            if (get_corners(&ss) == rep[c]) sym[c] |= 1u << s;
        }
    }
}

#endif /* CORE_H */
