/* Cubolt (https://github.com/nbwzx/cubolt)
 * Copyright (C) 2026 Zixing Wang <zixingwang.cn@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* guinet.h -- the cube panel: the 12x9 net, its colors, and why the net holds no cube. */
#ifndef GUINET_H
#define GUINET_H

#include <stdlib.h>
#include <math.h>

/* WCA color scheme: U white, R red, F green, D yellow, L orange, B blue. */
static COLORREF face_color(char c)
{
    switch (c) {
    case 'U': return RGB(255, 255, 255);
    case 'R': return RGB(255,   0,   0);
    case 'F': return RGB(0x33, 0xcc, 0x33);
    case 'D': return RGB(255, 255,   0);
    case 'L': return RGB(255, 165,   0);
    case 'B': return RGB(0,    0x70, 0xc0);
    default:  return RGB(200, 200, 200);
    }
}

/* f is a percentage: 100 keeps the color */
static COLORREF darken(COLORREF c, int f)
{
    return RGB(GetRValue(c) * f / 100, GetGValue(c) * f / 100, GetBValue(c) * f / 100);
}

/* 6 for anything but a face letter: the gray brush */
static int face_index(char c)
{
    const char *p = memchr(FACES, c, 6);
    return p ? (int)(p - FACES) : 6;
}

static const int NET_COL[6] = {1, 2, 1, 1, 0, 3};   /* U R F D L B */
static const int NET_ROW[6] = {0, 1, 1, 2, 1, 1};
/* ---- net <-> cubestring ---- */
static void build_def(char *out)
{
    int k = 0;
    for (int fi = 0; fi < 6; fi++)
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                out[k++] = g.net[fi][r][c];
    out[k] = 0;
}

static void net_status(void);

/* The color each sticker had before it went unknown, so un-wiping puts back what was there. */
static char g_net_last[6][3][3];

/* The one way a sticker changes; '?' is not a color, so the undo copy does not record it. */
static void net_set(int fi, int r, int c, char col)
{
    if (col != '?') g_net_last[fi][r][c] = col;
    g.net[fi][r][c] = col;
}

static void net_from_cs(const char *cs)
{
    for (int fi = 0; fi < 6; fi++)
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) {
                char col = cs[fi * 9 + r * 3 + c];
                /* a whole cube's undo copy is taken here: '?' becomes its own face's color */
                g_net_last[fi][r][c] = col != '?' ? col : FACES[fi];
                net_set(fi, r, c, col);
            }
}

static void show_cs(const char *cs)
{
    net_from_cs(cs);
    InvalidateRect(g.canvas, NULL, TRUE);
    SetWindowText(g.cubestring, cs);
    net_status();
}

/* ---- is this a cube? ---- */
/* verify() names the rule that broke: a non-cube that runs quietly gives a meaningless answer */

static void box_text(HWND w, char *buf, size_t cap);

/* Why `cs` is not a cube, or NULL; read_cube()'s message is in a static buffer, so copy it out. */
static const char *cube_problem(const char *cs, char *buf, size_t cap)
{
    Cube cc;
    /* an incomplete cube: the '?' search owns its own rules */
    const char *e = strchr(cs, '?') ? incomp_problem(cs, NULL, NULL, NULL)
                                    : read_cube(cs, &cc, NULL);
    if (!e) return NULL;
    snprintf(buf, cap, "%s", e);
    return buf;
}

/* NULL, or why; a maneuver that does not parse yet leaves the cubestring box alone */
static const char *scramble_into_net(void)
{
    char alg[256], cs[55];
    box_text(g.scramble, alg, sizeof alg);
    const char *e = cube_scramble(alg, cs);
    if (e) return e;
    show_cs(cs);
    return NULL;
}

/* read_cube opens its answers with this; so does an incomplete cube's bad-length one */
static const char *past_error(const char *s)
{
    static const char pre[] = "Error: ";
    return strncmp(s, pre, sizeof pre - 1) ? s : s + sizeof pre - 1;
}

/* past the stickers read_cube printed too */
static const char *one_line_reason(const char *bad, const char *cs)
{
    bad = past_error(bad);
    const char *q = strstr(bad, cs);
    if (q) { q += strlen(cs); while (*q == ' ') q++; bad = q; }
    return bad;
}

/* The Reset row's hint: red carrying a refusal, gray only progress, empty shows nothing */
static void input_hint(const char *msg, int bad)
{
    int was = g.hint_bad;
    if (msg) msg = past_error(msg);
    SetWindowText(g.inphint, msg ? msg : "");
    g.hint_bad = msg ? bad : 0;
    if (g.hint_bad != was) InvalidateRect(g.inphint, NULL, TRUE);
}

/* ---- Auto complete ---- */
/* Fill in every '?' the laws of the cube force.  It is asked after a color is painted and
   after the box takes a new string; a sticker the user hid is his own answer, not a gap. */
static void autocpl_run(void)
{
    if (!g.autocpl) return;
    char box[CS_STICKERS + 1], out[CS_STICKERS + 1];
    box_text(g.cubestring, box, sizeof box);
    if (!strchr(box, '?')) return;
    if (ic_complete(box, out) > 0) show_cs(out);
}

static void load_cs_box(void)
{
    char cs[160], why[256], note[96];
    box_text(g.cubestring, cs, sizeof cs);
    size_t n = strlen(cs);
    if (n == 0) { input_hint(NULL, 0); return; }
    if (n != CS_STICKERS) {
        snprintf(note, sizeof note, "54 stickers expected, %zu so far.", n);
        input_hint(note, 0);
        return;
    }
    const char *bad = cube_problem(cs, why, sizeof why);
    input_hint(bad ? one_line_reason(bad, cs) : NULL, 1);
    if (bad) return;
    net_from_cs(cs);
    InvalidateRect(g.canvas, NULL, TRUE);
    autocpl_run();
}

/* The line the net starts on and the one it goes back to when nothing is wrong. */
#define NET_OK_TEXT "Cube is valid."

/* The net speaks only while the definition box is empty, the box whenever it holds anything */
static void net_status(void)
{
    char def[55], box[160], msg[256];
    build_def(def);
    box_text(g.cubestring, box, sizeof box);
    const char *cs  = *box ? box : def;
    const char *bad = cube_problem(cs, msg, sizeof msg);
    int         was = g.net_bad;

    if (!bad) {
        g.net_bad = 0;
        if (strchr(cs, '?')) {
            int nkc, nke, npart;
            incomp_problem(cs, &nkc, &nke, &npart);
            int hidden = 0;
            for (const char *q = cs; *q; q++) if (*q == '?') hidden++;
            /* IC_CNT_SAT: counting stopped there, so that many is a floor, not a total */
            int counted = 1;
            unsigned long long nc = incomp_count(cs, &counted, IC_CNT_SAT);
            char partly[24] = "", cases[40];
            if (npart) snprintf(partly, sizeof partly, ", %d partly", npart);
            if (!counted)
                snprintf(cases, sizeof cases, "could not be counted");
            else if (nc == 0)
                snprintf(cases, sizeof cases, "no legal completion");
            else if (nc >= IC_CNT_SAT)
                snprintf(cases, sizeof cases, "over %llu cases", IC_CNT_SAT);
            else
                snprintf(cases, sizeof cases, "%llu case%s", nc, nc == 1 ? "" : "s");
            snprintf(msg, sizeof msg,
                     "Incomplete cube: %d of 20 cubies known%s, %d sticker(s) hidden, %s.",
                     nkc + nke, partly, hidden, cases);
            SetWindowText(g.netstatus, msg);
        } else
            SetWindowText(g.netstatus, NET_OK_TEXT);
    } else {
        g.net_bad = 1;
        SetWindowText(g.netstatus, one_line_reason(bad, cs));
    }
    if (g.net_bad != was) InvalidateRect(g.netstatus, NULL, TRUE);
}

/* ---- canvas painting ---- */

/* a hit test disagreeing with the paint sends the click into another sticker */
static void net_cell_xy(int fi, int r, int c, int *x, int *y)
{
    *x = SZ(10) + (NET_COL[fi] * 3 + c) * A;
    *y = SZ(10) + (NET_ROW[fi] * 3 + r) * A;
}

/* ---- the two cursors over the net ---- */
#define CUR_N  64
#define CUR_C  (CUR_N * 0.5)   /* cursor centre: the bucket's local origin */
#define CUR_SS 8          /* supersampling per axis: AA for the 64x64 bitmap */
#define CUR_OUT RGB(0x35, 0x3a, 0x40)
#define CUR_MD  RGB(0x9d, 0xa9, 0xb6)
#define CUR_SH  RGB(0xfa, 0xfc, 0xfe)
#define BKT_SEG 32   /* pour strand segments */

/* fill() reports whether (px, py) is inked, and with which colour. */
typedef int (*cur_fill)(double px, double py, const void *ctx, COLORREF *col);

static COLORREF cur_mix(COLORREF a, COLORREF b, double t)
{
    t = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
    return RGB((int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t),
               (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
               (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}

/* One CUR_N x CUR_N cursor, supersampled CUR_SS^2 so its edges stay smooth. */
static HCURSOR cur_build(int hx, int hy, cur_fill fill, const void *ctx)
{
    unsigned *bits = (unsigned *)calloc((size_t)CUR_N * CUR_N, sizeof(unsigned));
    if (!bits) return NULL;
    ICONINFO ii = {0};
    ii.fIcon    = FALSE;
    ii.xHotspot = (DWORD)hx;
    ii.yHotspot = (DWORD)hy;
    ii.hbmMask  = CreateBitmap(CUR_N, CUR_N, 1, 1, bits);
    for (int oy = 0; oy < CUR_N; oy++)
        for (int ox = 0; ox < CUR_N; ox++) {
            unsigned long sr = 0, sg = 0, sb = 0, cnt = 0;
            for (int sy = 0; sy < CUR_SS; sy++)
                for (int sx = 0; sx < CUR_SS; sx++) {
                    double qx = ox + (sx + 0.5) / CUR_SS, qy = oy + (sy + 0.5) / CUR_SS;
                    COLORREF col;
                    if (!fill(qx, qy, ctx, &col)) continue;
                    sr += GetRValue(col); sg += GetGValue(col); sb += GetBValue(col); cnt++;
                }
            if (!cnt) continue;
            double inv = 1.0 / cnt;
            unsigned al = (unsigned)((cnt * 255 + (CUR_SS * CUR_SS) / 2) / (CUR_SS * CUR_SS));
            bits[oy * CUR_N + ox] = (al << 24) | (((unsigned)(sr * inv)) << 16)
                                  | (((unsigned)(sg * inv)) << 8) | ((unsigned)(sb * inv));
        }
    ii.hbmColor = CreateBitmap(CUR_N, CUR_N, 1, 32, bits);
    HCURSOR h = CreateIconIndirect(&ii);
    DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);
    free(bits);
    return h;
}

/* The picker: a bulb on a straight shaft that tapers to a point at the hotspot. */
typedef struct { double bx, by, R, tx, ty, hw, st; } drop_ctx;

static int dropper_fill(double px, double py, const void *v, COLORREF *col)
{
    const drop_ctx *c = (const drop_ctx *)v;
    double vx = c->tx - c->bx, vy = c->ty - c->by, len = sqrt(vx * vx + vy * vy);
    double ux = vx / len, uy = vy / len;
    double wx = px - c->bx, wy = py - c->by;
    double along = wx * ux + wy * uy;
    double side = wx * uy - wy * ux;             /* signed distance across the axis */

    double ec = c->R - sqrt(wx * wx + wy * wy);
    double es = -1.0;
    if (along >= 0.0 && along <= len) {
        double hw = along <= c->st ? c->hw : c->hw * (len - along) / (len - c->st);
        es = hw - fabs(side);
    }
    if (ec <= 0.0 && es <= 0.0) return 0;
    if ((ec > es ? ec : es) < 1.2) { *col = CUR_OUT; return 1; }   /* outline of the union */

    double ps = side / c->hw;
    *col = ps < -0.25 ? CUR_SH
         : ps <  0.20 ? RGB(0xd9, 0xe0, 0xe8)
                      : CUR_MD;
    if (along > c->st)
        *col = cur_mix(*col, CUR_MD, (along - c->st) / (len - c->st) * 0.6);
    return 1;
}

static HCURSOR dropper_make(int hx, int hy)
{
    drop_ctx c = { .bx = 46.5, .by = 11.0, .R = 9.0,
                   .tx = 13.0, .ty = 51.0, .hw = 6.1, .st = 41.0 };
    return cur_build(hx, hy, dropper_fill, &c);
}

/* The bucket: a trapezoid turned ~30 deg left to pour; the paint keeps a level surface. */
typedef struct {
    double E[4][2], nrm[4][2];
    double ca, sa;
    double yliq, ybot;
    double cvx[BKT_SEG + 1], cvy[BKT_SEG + 1];
    double p0y, p2y, streamW;
    double HT, HB, wTop, wBot, stroke, hThick;
    COLORREF paint, c_pb, c_lt;
} bucket_ctx;

static int bucket_fill(double px, double py, const void *v, COLORREF *col)
{
    const bucket_ctx *b = (const bucket_ctx *)v;

    /* squared distance to the pour strand; its end is the rounded cap */
    double d2 = 1e9;
    for (int k = 0; k < BKT_SEG; k++) {
        double ax = b->cvx[k], ay = b->cvy[k], bx = b->cvx[k + 1], by = b->cvy[k + 1];
        double vx = bx - ax, vy = by - ay, len2 = vx * vx + vy * vy;
        double t = (px - ax) * vx + (py - ay) * vy, q2;
        if (t <= 0.0)       q2 = (px - ax) * (px - ax) + (py - ay) * (py - ay);
        else if (t >= len2) q2 = (px - bx) * (px - bx) + (py - by) * (py - by);
        else { double f = t / len2, qx = px - (ax + vx * f), qy = py - (ay + vy * f);
               q2 = qx * qx + qy * qy; }
        if (q2 < d2) d2 = q2;
    }
    if (d2 < b->streamW * b->streamW) {
        *col = cur_mix(b->paint, b->c_pb, (py - b->p0y) / (b->p2y - b->p0y));
        return 1;
    }

    /* rotate the pixel back into the bucket's local (upright) frame */
    double dx = px - CUR_C, dy = py - CUR_C;
    double lx = dx * b->ca + dy * b->sa, ly = -dx * b->sa + dy * b->ca;
    double deep = -1e9;
    int in = 1;
    for (int e = 0; e < 4; e++) {
        double s = (lx - b->E[e][0]) * b->nrm[e][0] + (ly - b->E[e][1]) * b->nrm[e][1];
        if (s > 0.0) in = 0;
        if (s > deep) deep = s;
    }
    if (in) {
        if (-deep < b->stroke * 0.5) { *col = CUR_OUT; return 1; }
        if (py > b->yliq) {                    /* below the level surface: paint */
            *col = cur_mix(b->paint, b->c_pb, (py - b->yliq) / (b->ybot - b->yliq));
            return 1;
        }
        double t = (ly + b->HT) / (b->HB + b->HT);
        *col = cur_mix(b->c_lt, CUR_MD, t);
        double hw = b->wTop + (b->wBot - b->wTop) * t;
        if (lx > 1.5 - hw && lx < 4.5 - hw) *col = cur_mix(*col, CUR_SH, 0.85);
        return 1;
    }

    /* handle: a semicircle of radius wTop about the opening centre */
    double ox = lx, oy = ly + b->HT;
    double r = sqrt(ox * ox + oy * oy);
    double ang = atan2(ox, -oy);
    if (fabs(ang) < 1.5707963 && fabs(r - b->wTop) < b->hThick * 0.5) {
        *col = CUR_OUT;
        return 1;
    }
    return 0;
}

static HCURSOR bucket_make(int hx, int hy, COLORREF paint)
{
    const double a = -0.5235987756;
    bucket_ctx b = {
        .ca = cos(a), .sa = sin(a),
        .HT = 12.0, .HB = 16.0, .wTop = 16.0, .wBot = 11.0,
        .stroke = 2.0, .hThick = 2.4, .paint = paint,
        .c_lt = RGB(0xdf, 0xe6, 0xee),
        .c_pb = cur_mix(paint, RGB(0x20, 0x22, 0x26), 0.28),
    };

    double tl[2] = { -b.wTop, -b.HT }, tr[2] = { b.wTop, -b.HT };
    double br[2] = {  b.wBot,  b.HB }, bl[2] = { -b.wBot,  b.HB };
    double *E[4] = { tl, tr, br, bl };
    double cen[2] = { (tl[0] + tr[0] + br[0] + bl[0]) / 4,
                      (tl[1] + tr[1] + br[1] + bl[1]) / 4 };
    for (int e = 0; e < 4; e++) {
        double *p = E[e], *q = E[(e + 1) & 3];
        double dx = q[0] - p[0], dy = q[1] - p[1], len = sqrt(dx * dx + dy * dy);
        double nx = dy / len, ny = -dx / len;
        double mx = (p[0] + q[0]) / 2, my = (p[1] + q[1]) / 2;
        if ((cen[0] - mx) * nx + (cen[1] - my) * ny > 0.0) { nx = -nx; ny = -ny; }
        b.E[e][0] = p[0]; b.E[e][1] = p[1];
        b.nrm[e][0] = nx; b.nrm[e][1] = ny;
    }

    double Sx = CUR_C + tl[0] * b.ca - tl[1] * b.sa;
    double Sy = CUR_C + tl[0] * b.sa + tl[1] * b.ca;
    double By = CUR_C + bl[0] * b.sa + bl[1] * b.ca;
    double Dy = CUR_C + br[0] * b.sa + br[1] * b.ca;
    b.yliq = Sy - 3.0;
    b.ybot = By > Dy ? By : Dy;
    b.p0y = Sy;
    b.p2y = Sy + 13.0;
    b.streamW = 3.0;

    double p2x = Sx - 6.0, p1x = (Sx + p2x) * 0.5 - 2.0, p1y = (Sy + b.p2y) * 0.5;
    for (int k = 0; k <= BKT_SEG; k++) {
        double u = (double)k / BKT_SEG, w = 1.0 - u;
        b.cvx[k] = w * w * Sx + 2.0 * w * u * p1x + u * u * p2x;
        b.cvy[k] = w * w * Sy + 2.0 * w * u * p1y + u * u * b.p2y;
    }
    return cur_build(hx, hy, bucket_fill, &b);
}

#define CHOSEN_TITLE "Selected Color"

/* The selected color and its caption, in the blank the net leaves under L. */
static RECT chosen_rect(void)
{
    RECT r;
    r.left   = 7 * A + A / 2;
    r.top    = 7 * A;
    r.right  = r.left + A;
    r.bottom = r.top + A;
    return r;
}

/* Auto complete: a box and its label, in the cell the net leaves blank under L.  The whole
   strip is the click target, the way a check box and its caption are one. */
#define AUTOCPL_TITLE "Auto complete"
static RECT autocpl_rect(void)
{
    RECT r;
    r.left   = SZ(12);
    r.top    = SZ(10) + 9 * A - SZ(30);
    r.right  = r.left + SZ(126);
    r.bottom = r.top + SZ(22);
    return r;
}

static void paint_canvas(HWND hw)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hw, &ps);
    RECT rc;
    GetClientRect(hw, &rc);
    FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));

    HGDIOBJ old_pen   = SelectObject(hdc, g_net_pen);
    HGDIOBJ old_font  = SelectObject(hdc, g_net_font);
    HGDIOBJ old_brush = GetCurrentObject(hdc, OBJ_BRUSH);
    SetBkMode(hdc, TRANSPARENT);

    for (int fi = 0; fi < 6; fi++) {
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) {
                int x, y;
                net_cell_xy(fi, r, c, &x, &y);
                SelectObject(hdc, g_face_brush[face_index(g.net[fi][r][c])]);
                Rectangle(hdc, x, y, x + A, y + A);
                if (r == 1 && c == 1) {
                    char f = FACES[fi];
                    SetTextColor(hdc, RGB(0, 0, 0));
                    TextOut(hdc, x + A / 2 - SZ(5), y + A / 2 - SZ(8), &f, 1);
                }
            }
    }
    {
        RECT cr = chosen_rect();
        SelectObject(hdc, g_face_brush[g.curcol]);
        Rectangle(hdc, cr.left, cr.top, cr.right, cr.bottom);

        HGDIOBJ of = SelectObject(hdc, g_font);
        RECT tr = { cr.left - SZ(24), cr.bottom + SZ(3), cr.right + SZ(24), cr.bottom + SZ(21) };
        SetTextColor(hdc, RGB(0x20, 0x20, 0x20));
        DrawText(hdc, CHOSEN_TITLE, -1, &tr,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, of);
    }

    /* drawn here, not as a child button: a window of its own would carry the panel's gray
       into the net's white cell */
    {
        RECT ar = autocpl_rect();
        int side = SZ(14);
        int bx = ar.left, by = ar.top + ((ar.bottom - ar.top) - side) / 2;
        HGDIOBJ ob = SelectObject(hdc, GetStockObject(WHITE_BRUSH));
        Rectangle(hdc, bx, by, bx + side, by + side);
        SelectObject(hdc, ob);
        if (g.autocpl) {
            POINT ck[3] = { { bx + side / 5,     by + side / 2     },
                            { bx + side * 2 / 5, by + side * 7 / 10 },
                            { bx + side * 4 / 5, by + side / 4     } };
            SelectObject(hdc, g_chk_pen);
            Polyline(hdc, ck, 3);
            SelectObject(hdc, g_net_pen);
        }
        /* the net's font is sized for a face letter; a caption takes the panel's */
        HGDIOBJ of = SelectObject(hdc, g_font);
        RECT tr = ar;
        tr.left = bx + side + SZ(6);
        SetTextColor(hdc, RGB(0x20, 0x20, 0x20));
        DrawText(hdc, AUTOCPL_TITLE, -1, &tr,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, of);
    }

    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_font);
    SelectObject(hdc, old_pen);
    EndPaint(hw, &ps);
}

/* The one way a sticker reaches '?': 1 when the click hid it, 0 when it put a color back. */
static int net_toggle_sticker(int fi, int r, int c)
{
    if (g.net[fi][r][c] != '?') {
        net_set(fi, r, c, '?');
        return 1;
    }
    char was = g_net_last[fi][r][c];
    net_set(fi, r, c, memchr(FACES, was, 6) ? was : FACES[fi]);
    return 0;
}

/* The sticker a point is in; 0 when it is in none. */
static int net_at(int mx, int my, int *fi, int *r, int *c)
{
    for (int f = 0; f < 6; f++)
        for (int y = 0; y < 3; y++)
            for (int x = 0; x < 3; x++) {
                int px, py;
                net_cell_xy(f, y, x, &px, &py);
                if (mx >= px && mx < px + A && my >= py && my < py + A) {
                    *fi = f; *r = y; *c = x;
                    return 1;
                }
            }
    return 0;
}

/* rebuild the bucket cursor so its paint matches the currently selected color */
static void rebuild_bucket_cursor(void)
{
    HCURSOR h = bucket_make(12, 30, face_color(FACES[g.curcol]));
    if (h) {
        if (g_cur_paint) DestroyCursor(g_cur_paint);
        g_cur_paint = h;
    }
}

static void canvas_click(HWND hw, int mx, int my, int rmb)
{
    int fi = 0, r = 0, c = 0;
    if (net_at(mx, my, &fi, &r, &c)) {
        /* a center is where a color is taken from; its own color is the frame's, not a choice */
        if (r == 1 && c == 1) {
            if (rmb || fi == g.curcol) return;
            g.curcol = fi;
            rebuild_bucket_cursor();
            InvalidateRect(hw, NULL, TRUE);
            return;
        }
        int hid = 0;
        if (rmb) hid = net_toggle_sticker(fi, r, c);
        else net_set(fi, r, c, FACES[g.curcol]);
        InvalidateRect(hw, NULL, TRUE);
        char def[55];
        build_def(def);
        SetWindowText(g.cubestring, def);
        input_hint(NULL, 0);        /* no EN_CHANGE from a click: the hint is ours */
        net_status();
        /* a color is a change to answer; a '?' the user made is already an answer */
        if (!hid) autocpl_run();
        return;
    }
    RECT cr = chosen_rect();
    if (!rmb && mx >= cr.left && mx < cr.right && my >= cr.top && my < cr.bottom) {
        g.curcol = (g.curcol + 1) % 6;
        rebuild_bucket_cursor();
        InvalidateRect(hw, NULL, TRUE);
        return;
    }
    RECT ar = autocpl_rect();
    if (!rmb && mx >= ar.left && mx < ar.right && my >= ar.top && my < ar.bottom) {
        g.autocpl = !g.autocpl;
        InvalidateRect(hw, NULL, TRUE);
        /* a tick answers the cube in the box now, not only the next color painted */
        autocpl_run();
    }
}

static LRESULT CALLBACK CanvasProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT)        { paint_canvas(hw); return 0; }
    if (msg == WM_SETCURSOR) {
        if (LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hw, &pt);
            int fi = 0, r = 0, c = 0;
            if (net_at(pt.x, pt.y, &fi, &r, &c)) {
                SetCursor(r == 1 && c == 1 ? g_cur_pick : g_cur_paint);
                return TRUE;
            }
        }
    }
    if (msg == WM_LBUTTONDOWN) {
        int mx = (int)(short)LOWORD(lp), my = (int)(short)HIWORD(lp);
        canvas_click(hw, mx, my, 0);
        return 0;
    }
    if (msg == WM_RBUTTONDOWN) {
        int mx = (int)(short)LOWORD(lp), my = (int)(short)HIWORD(lp);
        canvas_click(hw, mx, my, 1);
        return 0;
    }
    return DefWindowProc(hw, msg, wp, lp);
}

#endif /* GUINET_H */
