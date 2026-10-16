/* Cubolt (https://github.com/nbwzx/cubolt)
 * Copyright (C) 2026 Zixing Wang <zixingwang.cn@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* guinet.h -- the cube panel: the 12x9 net, its colors, and why the net holds no cube. */
#ifndef GUINET_H
#define GUINET_H

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
            unsigned long long nc = incomp_count(cs, &counted);
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

static void swatch_xy(int i, int *x, int *y)
{
    *x = 7 * A + (i % 3) * (A + SZ(6));
    *y = 7 * A + (i / 3) * (A + SZ(6));
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
    for (int i = 0; i < 6; i++) {
        int x, y;
        swatch_xy(i, &x, &y);
        SelectObject(hdc, g_face_brush[i]);
        Rectangle(hdc, x, y, x + A, y + A);
        if (i == g.curcol) {
            SelectObject(hdc, g_mark_pen);
            Rectangle(hdc, x, y, x + A, y + A);
            SelectObject(hdc, g_net_pen);
        }
    }
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_font);
    SelectObject(hdc, old_pen);
    EndPaint(hw, &ps);
}

static void net_toggle_sticker(int fi, int r, int c)
{
    if (g.net[fi][r][c] != '?')
        net_set(fi, r, c, '?');
    else {
        char was = g_net_last[fi][r][c];
        net_set(fi, r, c, memchr(FACES, was, 6) ? was : FACES[fi]);
    }
}

static void canvas_click(HWND hw, int mx, int my, int rmb)
{
    for (int fi = 0; fi < 6; fi++) {
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) {
                int x, y;
                net_cell_xy(fi, r, c, &x, &y);
                if (mx >= x && mx < x + A && my >= y && my < y + A) {
                    /* the center names the frame: painting it would write a cube that is not one */
                    if (r == 1 && c == 1) return;
                    if (rmb) net_toggle_sticker(fi, r, c);
                    else net_set(fi, r, c, FACES[g.curcol]);
                    InvalidateRect(hw, NULL, TRUE);
                    char def[55];
                    build_def(def);
                    SetWindowText(g.cubestring, def);
                    input_hint(NULL, 0);        /* no EN_CHANGE from a click: the hint is ours */
                    net_status();
                    return;
                }
            }
    }
    for (int i = 0; i < 6; i++) {
        int x, y;
        swatch_xy(i, &x, &y);
        if (rmb) continue;
        if (mx >= x && mx < x + A && my >= y && my < y + A) {
            g.curcol = i;
            InvalidateRect(hw, NULL, TRUE);
            return;
        }
    }
}

static LRESULT CALLBACK CanvasProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT)        { paint_canvas(hw); return 0; }
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
