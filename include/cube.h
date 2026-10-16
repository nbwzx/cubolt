/* Cubolt (https://github.com/nbwzx/cubolt)
 * Copyright (C) 2026 Zixing Wang <zixingwang.cn@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* cube.h -- the 54-facelet cube: standard notation in, cube string out; the scramble front end. */
#ifndef CUBE_H
#define CUBE_H

#include "render.h"

/* -1 for anything else; the faces lead REND_LET, so a face's id is also its color's place. */
static int cube_move(char c)
{
    if (!c) return -1;
    const char *p = strchr(REND_LET, c);
    return p ? (int)(p - REND_LET) : -1;
}

static int cube_face(char c)
{
    int m = cube_move(c);
    return m < 6 ? m : -1;
}

#define CUBE_BAD_COLOR "cube definition names a color outside U R F D L B"

static const char *cube_bad_length(const char *s)
{
    /* Thread-local: the GUI's box and a batch worker each keep what this returns. */
    static __thread char err[256];
    snprintf(err, sizeof err, "Error: Cube definition string %s contains %s than 54 facelets.",
             s, strlen(s) < 54 ? "less" : "more");
    return err;
}

/* A turn: a letter, then a mix of 'w' (wide), '\'' (inverse) and a count; r u f l d b are wide. */
typedef struct { char letter; int amt; int wide; } Turn;

/* Why turn_read failed: thread-local (the GUI parses two at once), overwritten by the next. */
static __thread char turn_bad[32];

static const char *turn_bad_letter(char c)
{
    snprintf(turn_bad, sizeof turn_bad, "unknown turn '%c'", c);
    return turn_bad;
}

/* Per thread and per algorithm: turns that fold to nothing ("R4", "R16"). */
static __thread int turn_vanished;

/* One turn: 1 = a turn, 0 = end of the algorithm, -1 = not a turn (turn_bad says what). */
static int turn_read(const char **pp, Turn *t)
{
    const char *p = *pp;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (!*p) { *pp = p; return 0; }

    char c = *p++;
    t->wide = 0;
    switch (c) {
        case 'U': case 'R': case 'F': case 'D': case 'L': case 'B':
        case 'M': case 'E': case 'S':
        case 'x': case 'y': case 'z':
            t->letter = c; break;
        case 'u': case 'r': case 'f': case 'd': case 'l': case 'b':
            t->letter = (char)(c - 'a' + 'A'); t->wide = 1; break;
        case 'm': case 'e': case 's':
            t->letter = (char)(c - 'a' + 'A'); break;
        default:
            turn_bad_letter(c);
            *pp = p;
            return -1;
    }

    /* 'w' widens a face turn; on a slice or a whole-cube turn there is nothing for it to widen */
    int is_face = !!strchr(FACE_LET, t->letter);
    int cnt = 0, has = 0, prime = 0, primes = 0, widened = 0, turned = 0;
    for (; *p; p++) {
        if (*p == 'w' || *p == 'W') {
            if (!is_face) { turned = 2; break; }
            t->wide = 1; widened++;
        }
        else if (*p == '\'')             { prime = !prime; primes++; }
        /* the count stays mod 4, where the whole number lands anyway: R12 is 0 and R5 is 1 */
        else if (*p >= '0' && *p <= '9') {
            /* a digit once a prime and a digit are both in: "R2'2" is two counts for one turn */
            if (prime && has) { turned = 1; break; }
            cnt = (cnt * 10 + (*p - '0')) % 4; has = 1;
        }
        else break;
    }
    /* Two primes, or two w's, is a spelling no one writes: refused, not folded */
    if (primes > 1 || widened > 1) {
        snprintf(turn_bad, sizeof turn_bad, "turn '%c' repeats a modifier", c);
        *pp = p;
        return -1;
    }
    if (turned == 1) {
        snprintf(turn_bad, sizeof turn_bad, "'%c': two counts for one turn", c);
        *pp = p;
        return -1;
    }
    if (turned == 2) {
        snprintf(turn_bad, sizeof turn_bad, "'w' has no meaning on '%c'", c);
        *pp = p;
        return -1;
    }
    int amt = has ? cnt : 1;
    t->amt = prime ? (4 - amt) % 4 : amt;
    if (t->amt == 0) turn_vanished++;
    *pp = p;
    return 1;
}

/* ---- the displayed cube (facelet sim) ---- */
/* In face order U R F D L B; the 55th byte is the NUL. */
static const char CUBE_SOLVED[55] =
    "UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB";

/* Inverse facelet permutation of a quarter turn: perm[g] = the source sticker landing at g. */
static const int FL_MOVE_PERM[12][54] = {
    /* U */ {   6,  3,  0,  7,  4,  1,  8,  5,  2,
        45, 46, 47, 12, 13, 14, 15, 16, 17,
         9, 10, 11, 21, 22, 23, 24, 25, 26,
        27, 28, 29, 30, 31, 32, 33, 34, 35,
        18, 19, 20, 39, 40, 41, 42, 43, 44,
        36, 37, 38, 48, 49, 50, 51, 52, 53,
    },
    /* R */ {   0,  1, 20,  3,  4, 23,  6,  7, 26,
        15, 12,  9, 16, 13, 10, 17, 14, 11,
        18, 19, 29, 21, 22, 32, 24, 25, 35,
        27, 28, 51, 30, 31, 48, 33, 34, 45,
        36, 37, 38, 39, 40, 41, 42, 43, 44,
         8, 46, 47,  5, 49, 50,  2, 52, 53,
    },
    /* F */ {   0,  1,  2,  3,  4,  5, 44, 41, 38,
         6, 10, 11,  7, 13, 14,  8, 16, 17,
        24, 21, 18, 25, 22, 19, 26, 23, 20,
        15, 12,  9, 30, 31, 32, 33, 34, 35,
        36, 37, 27, 39, 40, 28, 42, 43, 29,
        45, 46, 47, 48, 49, 50, 51, 52, 53,
    },
    /* D */ {   0,  1,  2,  3,  4,  5,  6,  7,  8,
         9, 10, 11, 12, 13, 14, 24, 25, 26,
        18, 19, 20, 21, 22, 23, 42, 43, 44,
        33, 30, 27, 34, 31, 28, 35, 32, 29,
        36, 37, 38, 39, 40, 41, 51, 52, 53,
        45, 46, 47, 48, 49, 50, 15, 16, 17,
    },
    /* L */ {  53,  1,  2, 50,  4,  5, 47,  7,  8,
         9, 10, 11, 12, 13, 14, 15, 16, 17,
         0, 19, 20,  3, 22, 23,  6, 25, 26,
        18, 28, 29, 21, 31, 32, 24, 34, 35,
        42, 39, 36, 43, 40, 37, 44, 41, 38,
        45, 46, 33, 48, 49, 30, 51, 52, 27,
    },
    /* B */ {  11, 14, 17,  3,  4,  5,  6,  7,  8,
         9, 10, 35, 12, 13, 34, 15, 16, 33,
        18, 19, 20, 21, 22, 23, 24, 25, 26,
        27, 28, 29, 30, 31, 32, 36, 39, 42,
         2, 37, 38,  1, 40, 41,  0, 43, 44,
        51, 48, 45, 52, 49, 46, 53, 50, 47,
    },
    /* E */ {   0,  1,  2,  3,  4,  5,  6,  7,  8,
         9, 10, 11, 21, 22, 23, 15, 16, 17,
        18, 19, 20, 39, 40, 41, 24, 25, 26,
        27, 28, 29, 30, 31, 32, 33, 34, 35,
        36, 37, 38, 48, 49, 50, 42, 43, 44,
        45, 46, 47, 12, 13, 14, 51, 52, 53,
    },
    /* M */ {   0, 52,  2,  3, 49,  5,  6, 46,  8,
         9, 10, 11, 12, 13, 14, 15, 16, 17,
        18,  1, 20, 21,  4, 23, 24,  7, 26,
        27, 19, 29, 30, 22, 32, 33, 25, 35,
        36, 37, 38, 39, 40, 41, 42, 43, 44,
        45, 34, 47, 48, 31, 50, 51, 28, 53,
    },
    /* S */ {   0,  1,  2, 43, 40, 37,  6,  7,  8,
         9,  3, 11, 12,  4, 14, 15,  5, 17,
        18, 19, 20, 21, 22, 23, 24, 25, 26,
        27, 28, 29, 16, 13, 10, 33, 34, 35,
        36, 30, 38, 39, 31, 41, 42, 32, 44,
        45, 46, 47, 48, 49, 50, 51, 52, 53,
    },
    /* x */ {  18, 19, 20, 21, 22, 23, 24, 25, 26,
        15, 12,  9, 16, 13, 10, 17, 14, 11,
        27, 28, 29, 30, 31, 32, 33, 34, 35,
        53, 52, 51, 50, 49, 48, 47, 46, 45,
        38, 41, 44, 37, 40, 43, 36, 39, 42,
         8,  7,  6,  5,  4,  3,  2,  1,  0,
    },
    /* y */ {   6,  3,  0,  7,  4,  1,  8,  5,  2,
        45, 46, 47, 48, 49, 50, 51, 52, 53,
         9, 10, 11, 12, 13, 14, 15, 16, 17,
        29, 32, 35, 28, 31, 34, 27, 30, 33,
        18, 19, 20, 21, 22, 23, 24, 25, 26,
        36, 37, 38, 39, 40, 41, 42, 43, 44,
    },
    /* z */ {  42, 39, 36, 43, 40, 37, 44, 41, 38,
         6,  3,  0,  7,  4,  1,  8,  5,  2,
        24, 21, 18, 25, 22, 19, 26, 23, 20,
        15, 12,  9, 16, 13, 10, 17, 14, 11,
        33, 30, 27, 34, 31, 28, 35, 32, 29,
        47, 50, 53, 46, 49, 52, 45, 48, 51,
    },
};

/* The source facelet landing at each position, one row per letter and amount, 0 being identity. */
static unsigned char FL_PERM[R_WB + 1][4][54];

static void fl_perm_init(void)
{
    static int done = 0;
    if (done) return;
    for (int L = 0; L < R_WU; L++) {
        for (int g = 0; g < 54; g++) FL_PERM[L][0][g] = (unsigned char)g;
        for (int a = 1; a <= 3; a++)
            for (int g = 0; g < 54; g++)
                FL_PERM[L][a][g] = FL_PERM[L][a - 1][FL_MOVE_PERM[L][g]];
    }
    /* a wide letter turns a face and the slice beside it, as in Rw = R M' and Lw = L M */
    for (int L = R_WU; L <= R_WB; L++) {
        int f = L - R_WU, ax = f % 3;
        int flip = (f == REND_AX_AF[ax]) ^ (REND_AX_SGN[ax] < 0);
        for (int a = 0; a <= 3; a++) {
            int b = flip ? (4 - a) % 4 : a;
            for (int g = 0; g < 54; g++)
                FL_PERM[L][a][g] = FL_PERM[f][a][FL_PERM[REND_AX_SL[ax]][b][g]];
        }
    }
    done = 1;
}

/* `mv` is a letter id, `amt` a count of quarter turns: one row of FL_PERM is the whole turn. */
static void fl_apply(char out[55], int mv, int amt)
{
    fl_perm_init();
    const unsigned char *P = FL_PERM[mv][amt & 3];
    char tmp[55];
    memcpy(tmp, out, 55);
    for (int g = 0; g < 54; g++) out[g] = tmp[P[g]];
    out[54] = 0;
}

static void apply_rot(char cs[55], int rot)
{
    for (const unsigned char *t = ROT_TAIL[rot]; *t; t++)
        fl_apply(cs, *t >> 2, *t & 3);
}

/* The displayed scramble: each move applied off the solved string; NULL, or a static message. */
static const char *cube_scramble(const char *alg, char out[55]) __attribute__((unused));
static const char *cube_scramble(const char *alg, char out[55])
{
    memcpy(out, CUBE_SOLVED, 55);
    const char *p = alg;
    turn_vanished = 0;
    for (;;) {
        Turn t;
        int r = turn_read(&p, &t);
        if (r < 0) return turn_bad;
        if (r == 0) break;
        if (t.amt == 0) continue;

        /* every letter turn_read hands back is one of REND_LET's, so this is its index */
        int mv = cube_move(t.letter);
        if (t.wide) mv = R_WU + mv;
        fl_apply(out, mv, t.amt);
    }
    if (turn_vanished)
        fprintf(stderr, "scramble %s: %d turn(s) cancel to nothing\n", alg, turn_vanished);
    return NULL;
}

static void fl_identity(char s[55])
{
    for (int i = 0; i < 54; i++) s[i] = (char)i;
    s[54] = 0;
}

/* A letter and amount permuting the 54 stickers; 0 means none: no code is 0, amounts start at 1. */
static unsigned char fl_perm_code(const char p[55])
{
    fl_perm_init();
    for (int L = 0; L <= R_WB; L++) {
        if (rend_is_rot(L)) continue;
        for (int a = 1; a <= 3; a++)
            if (!memcmp(FL_PERM[L][a], p, 54)) return (unsigned char)(L * 4 + a);
    }
    return 0;
}

/* MD_CODE: a move letter relabels to r M r^-1, a rotation to the frame r o T lands on. */
static void cube_build_relabel(void)
{
    static int done = 0;
    if (done) return;
    done = 1;

    char rot[24][55];                       /* rot[s][g] = where position g's sticker came from */
    for (int s = 0; s < 24; s++) {
        fl_identity(rot[s]);
        apply_rot(rot[s], s);
    }

    for (int ax = 0; ax < 3; ax++)
        for (int a = 1; a <= 3; a++) {
            char tok[55];
            fl_identity(tok);
            fl_apply(tok, R_X + ax, a);
            for (int r = 0; r < 24; r++) {
                char cp[55];
                for (int i = 0; i < 54; i++) cp[i] = rot[r][(unsigned char)tok[i]];
                cp[54] = 0;
                /* A frame and a whole-cube turn is one of the 24 */
                int s = 0;
                while (s < 24 && memcmp(rot[s], cp, 54)) s++;
                if (s == 24) {
                    fprintf(stderr, "cube: no frame holds rotation %d of axis %d turn %d\n",
                            r, ax, a);
                    exit(1);
                }
                MD_CODE[r][(R_X + ax) * 3 + a - 1] = (unsigned char)s;
            }
        }

    for (int r = 0; r < 24; r++) {
        char inv[55];
        for (int i = 0; i < 54; i++) inv[(unsigned char)rot[r][i]] = (char)i;
        for (int L = 0; L <= R_WB; L++) {
            if (rend_is_rot(L)) continue;
            for (int a = 1; a <= 3; a++) {
                char m[55], c[55];
                fl_identity(m);
                fl_apply(m, L, a);
                for (int h = 0; h < 54; h++)
                    c[h] = rot[r][(unsigned char)m[(unsigned char)inv[h]]];
                c[54] = 0;
                MD_CODE[r][L * 3 + a - 1] = fl_perm_code(c);
            }
        }
    }
}

/* Renamed so its centers read U R F D L B; *tail takes the rotation away. NULL, or a message. */
static const char *cube_canonical(char cs[55], int *tail) __attribute__((unused));
static const char *cube_canonical(char cs[55], int *tail)
{
    if (strlen(cs) != 54) return cube_bad_length(cs);

    int to[6] = { -1, -1, -1, -1, -1, -1 };      /* color -> the face whose center carries it */
    for (int g = 0; g < 6; g++) {
        int c = cube_face(cs[9 * g + 4]);
        if (c < 0 || to[c] >= 0) return "cube centers are not the six face colors";
        to[c] = g;
    }

    int r = -1;
    for (int s = 0; s < 24; s++) {
        char rot[55];
        memcpy(rot, CUBE_SOLVED, 55);
        apply_rot(rot, s);
        int eq = 1;
        for (int g = 0; g < 6; g++)
            if (rot[9 * g + 4] != REND_LET[to[g]]) { eq = 0; break; }
        if (eq) { r = s; break; }
    }
    if (r < 0) return "cube centers are not a whole-cube rotation";

    for (int i = 0; i < 54; i++) {
        int c = cube_face(cs[i]);
        if (c < 0) return CUBE_BAD_COLOR;
        cs[i] = REND_LET[to[c]];
    }
    if (tail) *tail = r;
    return NULL;
}

#endif /* CUBE_H */
