/* Cubolt (https://github.com/nbwzx/cubolt)
 * Copyright (C) 2026 Zixing Wang <zixingwang.cn@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* docpanel.h -- the owner-drawn Help / About panel (typed blocks, wrapped and painted). */
#ifndef DOCPANEL_H
#define DOCPANEL_H

#include "version.h"

/* ---- Help > About / Help > Contents ---- */

enum { B_TITLE, B_SUB, B_PARA, B_STEP, B_CODE, B_RULE };

/* B_STEP text is "lead|body" with the lead bold; B_PARA may embed <a href="URL">label</a> */
typedef struct {
    int         kind;
    const char *text;
} Block;

#define DOC_MARGIN   30     /* logical: the text column's left/right/top/bottom margin */
#define DOC_GAP      13
#define DOC_LINE_GAP  6     /* logical: leading added to every line of a paragraph     */
#define DOC_GUTTER   30     /* logical: the number column of a numbered step           */
#define DOC_INDENT   22     /* logical: first-line indent of a paragraph               */
#define DOC_CODE_IND 14
#define DOC_BTN_W    92
#define DOC_BTN_H    26
#define LINK_MAX      6     /* clickable links in one document                         */
#define LINK_RECTS    6

#define DOC_LINK     RGB(0x0b, 0x57, 0xd0)
#define DOC_LINK_HOT RGB(0x06, 0x36, 0x94)
#define DOC_GRAY     RGB(0x66, 0x66, 0x66)
#define DOC_CODE     RGB(0x2d, 0x2d, 0x2d)

/* Faces made on first use and freed with the main window; F_LINK is the body face underlined */
enum { F_TITLE, F_SUB, F_BODY, F_BOLD, F_LINK, F_CODE, F_N };
static HFONT g_doc_font[F_N];
static int   g_doc_lh[F_N];          /* line height of each face, device pixels */
static HPEN  g_doc_rule = NULL;
static int   g_doc_open = 0;

typedef struct {
    const char *url;                 /* points into the block's literal: NOT NUL-terminated */
    int         len;
    RECT        r[LINK_RECTS];       /* one rectangle per line the label wrapped onto */
    int         nr;
} DocLink;

typedef struct {
    const Block *doc;
    int          ndoc;
    int          w;                  /* client width, device pixels       */
    int          h;                  /* client height                     */
    int          footer;             /* the bottom band holding the OK button */
    int          yoff, span;         /* vertical scroll offset and range  */
    int          hot, tracking;
    int          nlink;
    DocLink      link[LINK_MAX];
} Panel;

static const Block ABOUT_DOC[] = {
    { B_TITLE, CUBOLT_NAME },
    { B_SUB,   "An optimal Rubik's Cube solver" },
    { B_SUB,   "Version " CUBOLT_VERSION },
    { B_RULE,  NULL },
    { B_PARA,  "Created by Zixing Wang (<a href=\"mailto:zixingwang.cn@gmail.com\">"
               "zixingwang.cn@gmail.com</a>)." },
    { B_PARA,  "Project home: <a href=\"https://github.com/nbwzx/cubolt\">"
               "https://github.com/nbwzx/cubolt</a>." },
    { B_PARA,  "Licensed under <a href=\"https://www.gnu.org/licenses/\">GPLv3</a>." }
};

static const Block HELP_DOC[] = {
    { B_TITLE, "Contents" },
    { B_SUB,   "The complete Cubolt workflow" },
    { B_RULE,  NULL },
    { B_STEP,  "Scramble box:| Type a maneuver such as R U R' F2; the net follows as you type and "
               "fills the cube definition string.  Edit > Generate WCA Scramble writes one and "
               "applies it.  A move that cannot be read is shown in red beside Reset." },
    { B_STEP,  "Cube definition string:| The reverse works too: paste the 54 stickers and "
               "the net paints itself.  The hint beside Reset counts while the string has fewer "
               "than 54 stickers, and shows in red what is wrong with a complete but impossible "
               "one.  Reset restores the solved cube." },
    { B_STEP,  "Incomplete cube:| A ? in the definition string hides that sticker; right-"
               "clicking one in the net hides it, and a second click restores the color it "
               "had.  The six centers cannot be hidden: they define the frame.  The line under "
               "the net then reports how many of the 20 cubies are still known and in how many "
               "ways the rest could be filled in.  One way means the pattern pins the cube down; "
               "none means the stickers contradict each other.  Up to "
               CUBOLT_STR_(IC_K_CAP) " of those ways are answered one ordinary search each, or "
               "one relaxed search over all of them once they are more than "
               CUBOLT_STR_(IC_WAYS_CUT) " and no corner is left unknown.  Past the cap only the "
               "relaxed search can answer, and the large tables stay unloaded." },
    { B_STEP,  "Subgroup:| Tick the letters whose moves the search may use: the faces U R F D L B; "
               "on the second row, each face's wide twin u r f d l b (a wide turn takes a face "
               "together with the layer behind it); and on the third row, the slices E M S and the "
               "whole-cube rotations x y z.  "
               "Ticking a slice switches the move count to the slice turn metric, in "
               "which a slice turn counts as one move." },
    { B_STEP,  "Search options:| Search Depth bounds the solution length; \"Only optimal\" reports "
               "the shortest one only; and \"Sort solutions\" lists them by length, then "
               "by move order.  Option > Threads sets how many workers the search runs at "
               "once; Auto is one worker per core." },
    { B_STEP,  "Solve:| Press Solve (or F9) to start, and Stop to interrupt.  Solutions are listed "
               "as the search finds them, up to the first " CUBOLT_STR_(LIST_CAP) ", and the "
               "finished list is sorted when \"Sort solutions\" or \"Only optimal\" is ticked.  "
               "Double-click one -- or press Ctrl+C with it selected -- to copy it, or write them "
               "all to a file with Save as .txt (File > Save solutions).  Copy selected and Copy "
               "all put one row, or every row the search found, on the clipboard: the list stops "
               "at " CUBOLT_STR_(LIST_CAP) ", Copy all does not." },
    { B_STEP,  "Log:| The box below the net keeps the run's own lines: the tables it loaded, the "
               "search it was given, and afterwards what it took in nodes and in time; the "
               "maneuvers go to the list instead.  Clear log empties it, and a selection in it is "
               "copied with Ctrl+C." },
    { B_STEP,  "Remembered settings:| The subgroup, search depth, both ticks, and the thread "
               "choice are written to cubolt.ini when the window closes "
               "and restored on the next start.  The file goes next to cubolt.exe, or into "
               "%APPDATA%\\" CUBOLT_NAME " when an installed copy cannot write to its "
               "own folder." },
    { B_STEP,  "Batch solve:| File > Batch solve takes an input file and an output folder.  In a "
               "text file, every line is one case, written as \"name, scramble\" or "
               "\"name, cubestring\"; the name is optional, and blank lines and #comments are "
               "skipped." },
    { B_STEP,  "JSON batches:| A JSON file is an array of cases, each carrying its own "
               "settings:" },
    { B_CODE,  "[\n"
               "  { \"name\": \"flip\", \"scramble\": \"R U R' F2\" },\n"
               "  { \"name\": \"deep\", \"cubestring\": \"UUU...UUU\",\n"
               "    \"depth\": 12, \"only_optimal\": true, \"threads\": 8,\n"
               "    \"goal\": \"UUU...UUU\" }\n"
               "]" },
    { B_PARA,  "The keys are name, scramble or cubestring, subgroup, depth, only_optimal, "
               "sort, dump, goal, and threads; a case that leaves one out keeps the panel's "
               "setting.  dump writes one line per maneuver instead of the standard notation.  "
               "goal is a cubestring to solve to instead of the solved cube, and threads 0 "
               "means one worker per core.  Each case is written to <name>.txt, or "
               "case_NNN.txt, in the output folder." },
    { B_STEP,  "Drop a file:| Dropping a file on the window does what the File menu would: a "
               "single cubestring loads into the net, a single maneuver is applied, and a batch "
               "input (JSON, or one case per line) starts with the panel's settings." },
};

static HFONT doc_face(int px, int weight, int underline, DWORD pitch, const char *name)
{
    return CreateFont(-SZ(px), 0, 0, 0, weight, 0, underline, 0, DEFAULT_CHARSET,
                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, pitch, name);
}

static void doc_fonts(void)
{
    if (g_doc_font[F_BODY]) return;
    g_doc_font[F_TITLE] = doc_face(26, FW_SEMIBOLD, 0, DEFAULT_PITCH | FF_DONTCARE, UI_FACE);
    g_doc_font[F_SUB]   = doc_face(14, FW_NORMAL,   0, DEFAULT_PITCH | FF_DONTCARE, UI_FACE);
    g_doc_font[F_BODY]  = doc_face(15, FW_NORMAL,   0, DEFAULT_PITCH | FF_DONTCARE, UI_FACE);
    g_doc_font[F_BOLD]  = doc_face(15, FW_BOLD,     0, DEFAULT_PITCH | FF_DONTCARE, UI_FACE);
    g_doc_font[F_LINK]  = doc_face(15, FW_NORMAL,   1, DEFAULT_PITCH | FF_DONTCARE, UI_FACE);
    g_doc_font[F_CODE]  = doc_face(13, FW_NORMAL,   0, FIXED_PITCH | FF_MODERN,     MONO_FACE);
    g_doc_rule = CreatePen(PS_SOLID, 1, RGB(0xd2, 0xd2, 0xd2));
}

/* ---- turning a block's text into draws: links in, wrapped words out ---- */

/* One stretch of text drawn in one face.  link < 0 means "not part of a link". */
typedef struct {
    const char *p;
    int         n;
    int         f;        /* index into g_doc_font */
    int         link;
} Seg;
#define SEG_MAX 64
#define LINK_TAG "<a href=\""

static int doc_segs(const char *s, int base, int lead, Seg *sg, int cap, Panel *p)
{
    int n = 0;
    const char *q = s;
    while (*q && n < cap) {
        if (!lead && strncmp(q, LINK_TAG, sizeof LINK_TAG - 1) == 0) {
            const char *u  = q + sizeof LINK_TAG - 1;
            const char *ue = strchr(u, '"');
            if (ue && ue[1] == '>') {
                const char *lb = ue + 2;
                const char *le = strstr(lb, "</a>");
                if (le) {
                    int id = -1;
                    if (p->nlink < LINK_MAX) {
                        id = p->nlink++;
                        p->link[id].url = u;
                        p->link[id].len = (int)(ue - u);
                        p->link[id].nr  = 0;
                    }
                    sg[n].f = id >= 0 ? F_LINK : F_BODY;
                    sg[n].p = lb; sg[n].n = (int)(le - lb); sg[n].link = id;
                    n++;
                    q = le + 4;
                    continue;
                }
            }
        }
        const char *e = q;
        while (*e && !(!lead && strncmp(e, LINK_TAG, sizeof LINK_TAG - 1) == 0)
                    && !(lead && *e == '|'))
            e++;
        /* an unclosed <a href=" stops the scan on its own '<', so take that one character */
        if (e == q && !lead) e++;
        if (e > q) {
            sg[n].p = q; sg[n].n = (int)(e - q);
            sg[n].f = lead ? F_BOLD : base; sg[n].link = -1;
            n++;
        }
        q = e;
        if (lead && *q == '|') { lead = 0; q++; }
    }
    return n;
}

/* Closing punctuation never gets a line of its own; a word is never empty here */
static int doc_glue(const char *p, int n)
{
    for (int i = 0; i < n; i++)
        if (!strchr(")]}.,;:!?'\"", p[i])) return 0;
    return 1;
}

/* Lays sg out in a column w wide; a wider word is broken.  Returns the y below the last line */
static int doc_flow(HDC dc, Panel *p, const Seg *sg, int nsg,
                    int x, int y, int w, int lh, int ind, COLORREF col, int draw)
{
    int cx = x + ind, fresh = 1, any = 0;
    for (int i = 0; i < nsg; i++) {
        HGDIOBJ old = SelectObject(dc, g_doc_font[sg[i].f]);
        SIZE sp; GetTextExtentPoint32A(dc, " ", 1, &sp);
        SetTextColor(dc, sg[i].link >= 0 ? (sg[i].link == p->hot ? DOC_LINK_HOT : DOC_LINK) : col);
        const char *q = sg[i].p, *end = sg[i].p + sg[i].n;
        while (q < end) {
            if (*q == ' ') { cx += sp.cx; q++; continue; }
            const char *e = q;
            while (e < end && *e != ' ') e++;
            int glue = doc_glue(q, (int)(e - q));
            for (const char *c = q; c < e; ) {
                SIZE full; GetTextExtentPoint32A(dc, c, (int)(e - c), &full);
                if (!glue && !fresh && cx + full.cx > x + w) {
                    y += lh; cx = x; fresh = 1; continue;
                }
                int take = (int)(e - c);
                if (!glue && cx + full.cx > x + w) {
                    take = 1;
                    while (c + take < e) {
                        SIZE t; GetTextExtentPoint32A(dc, c, take + 1, &t);
                        if (cx + t.cx > x + w) break;
                        take++;
                    }
                }
                /* full already holds this run's extent, so only a truncated one is measured */
                SIZE sz;
                if (take < (int)(e - c)) GetTextExtentPoint32A(dc, c, take, &sz);
                else                     sz = full;
                if (draw) {
                    TextOutA(dc, cx, y, c, take);
                    if (sg[i].link >= 0) {
                        DocLink *L = &p->link[sg[i].link];
                        RECT r = {cx, y, cx + sz.cx, y + lh};
                        if (L->nr > 0 && L->r[L->nr - 1].bottom == r.bottom)
                            L->r[L->nr - 1].right = r.right;
                        else if (L->nr < LINK_RECTS)
                            L->r[L->nr++] = r;
                    }
                }
                cx += sz.cx;
                fresh = 0;
                any = 1;
                c += take;
                if (c < e) { y += lh; cx = x; fresh = 1; }
            }
            q = e;
        }
        SelectObject(dc, old);
    }
    return any ? y + lh : y;
}

static int doc_hit(const Panel *p, int mx, int my)
{
    for (int i = 0; i < p->nlink; i++)
        for (int j = 0; j < p->link[i].nr; j++) {
            RECT r = p->link[i].r[j];
            InflateRect(&r, SZ(2), SZ(2));
            if (mx >= r.left && mx < r.right && my >= r.top && my < r.bottom) return i;
        }
    return -1;
}

/* Lays the document out for p->w, painting when `draw` is set; returns the height it wants */
static int doc_paint(HDC dc, Panel *p, int draw)
{
    /* The text scrolls; the OK row at the bottom does not, so keep the text out of that band. */
    int saved = draw ? SaveDC(dc) : 0;
    if (saved) IntersectClipRect(dc, 0, 0, p->w, p->h - p->footer);

    for (int i = 0; i < F_N; i++) {
        HGDIOBJ old = SelectObject(dc, g_doc_font[i]);
        TEXTMETRIC tm;
        GetTextMetricsA(dc, &tm);
        g_doc_lh[i] = tm.tmHeight;
        SelectObject(dc, old);
    }

    p->nlink = 0;
    int w   = p->w - 2 * SZ(DOC_MARGIN);
    int x   = SZ(DOC_MARGIN);
    int y   = SZ(DOC_MARGIN) - (draw ? p->yoff : 0);
    int lhb = g_doc_lh[F_BODY] + SZ(DOC_LINE_GAP);
    int step = 0;

    for (int b = 0; b < p->ndoc; b++) {
        const Block *blk = &p->doc[b];
        Seg sg[SEG_MAX];
        int n;

        if (blk->kind == B_RULE) {
            y += SZ(10);
            if (draw) {
                HGDIOBJ old = SelectObject(dc, g_doc_rule);
                MoveToEx(dc, x, y, NULL);
                LineTo(dc, x + w, y);
                SelectObject(dc, old);
            }
            y += SZ(18);
            continue;
        }

        if (blk->kind == B_TITLE || blk->kind == B_SUB) {
            int f = blk->kind == B_TITLE ? F_TITLE : F_SUB;
            int len = (int)strlen(blk->text);
            HGDIOBJ old = SelectObject(dc, g_doc_font[f]);
            SIZE sz; GetTextExtentPoint32A(dc, blk->text, len, &sz);
            if (draw) {
                SetTextColor(dc, blk->kind == B_SUB ? DOC_GRAY : GetSysColor(COLOR_WINDOWTEXT));
                TextOutA(dc, (p->w - sz.cx) / 2, y, blk->text, len);
            }
            SelectObject(dc, old);
            y += g_doc_lh[f] + (blk->kind == B_TITLE ? SZ(4) : SZ(2));
            continue;
        }

        if (blk->kind == B_PARA) {
            n = doc_segs(blk->text, F_BODY, 0, sg, SEG_MAX, p);
            if (draw) SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            y = doc_flow(dc, p, sg, n, x, y, w, lhb, SZ(DOC_INDENT),
                         GetSysColor(COLOR_WINDOWTEXT), draw) + SZ(DOC_GAP);
            continue;
        }

        if (blk->kind == B_STEP) {
            char num[16];
            n = doc_segs(blk->text, F_BODY, 1, sg, SEG_MAX, p);
            snprintf(num, sizeof num, "%d.", ++step);
            if (draw) {
                HGDIOBJ old = SelectObject(dc, g_doc_font[F_BOLD]);
                SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
                TextOutA(dc, x, y, num, (int)strlen(num));
                SelectObject(dc, old);
            }
            /* the body hangs off the number, so every wrapped line lines up with the first */
            y = doc_flow(dc, p, sg, n, x + SZ(DOC_GUTTER), y, w - SZ(DOC_GUTTER), lhb, 0,
                         GetSysColor(COLOR_WINDOWTEXT), draw) + SZ(DOC_GAP);
            continue;
        }

        if (blk->kind == B_CODE) {
            int lh = g_doc_lh[F_CODE] + SZ(3), lines = 1;
            const char *s = blk->text, *e;
            for (const char *c = s; *c; c++)
                if (*c == '\n') lines++;
            if (draw) {
                RECT r = {x, y - SZ(5), x + w, y + lines * lh + SZ(1)};
                FillRect(dc, &r, GetSysColorBrush(COLOR_BTNFACE));
            }
            y += SZ(4);
            for (;;) {
                e = strchr(s, '\n');
                int len = e ? (int)(e - s) : (int)strlen(s);
                if (draw) {
                    HGDIOBJ old = SelectObject(dc, g_doc_font[F_CODE]);
                    SetTextColor(dc, DOC_CODE);
                    TextOutA(dc, x + SZ(DOC_CODE_IND), y, s, len);
                    SelectObject(dc, old);
                }
                y += lh;
                if (!e) break;
                s = e + 1;
            }
            y += SZ(6) + SZ(DOC_GAP);
            continue;
        }
    }
    if (saved) RestoreDC(dc, saved);
    return y + SZ(DOC_MARGIN);
}

/* ---- the panel window ---- */

#define DOC_CLASS "CuboltDoc"
#define ID_DOC_OK 1

static void doc_scroll(HWND hw, Panel *p, int dy)
{
    if (!p || p->span <= 0) return;
    int was = p->yoff;
    p->yoff += dy;
    if (p->yoff < 0) p->yoff = 0;
    if (p->yoff > p->span) p->yoff = p->span;
    if (p->yoff == was) return;
    SetScrollPos(hw, SB_VERT, p->yoff, TRUE);
    RECT dr = {0, 0, p->w, p->h - p->footer};
    ScrollWindowEx(hw, 0, was - p->yoff, &dr, &dr, NULL, NULL, SW_INVALIDATE);
    UpdateWindow(hw);
}

static LRESULT CALLBACK DocProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
    Panel *p = (Panel *)GetWindowLongPtr(hw, GWLP_USERDATA);
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;                          /* WM_PAINT fills the whole client itself */
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hw, &ps);
        FillRect(dc, &ps.rcPaint, GetSysColorBrush(COLOR_WINDOW));
        SetBkMode(dc, TRANSPARENT);
        if (p) {
            doc_paint(dc, p, 1);
            RECT fr = {0, p->h - p->footer, p->w, p->h};
            FillRect(dc, &fr, GetSysColorBrush(COLOR_WINDOW));
            HGDIOBJ old = SelectObject(dc, g_doc_rule);
            MoveToEx(dc, 0, fr.top, NULL);
            LineTo(dc, fr.right, fr.top);
            SelectObject(dc, old);
        }
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (!p) break;
        if (!p->tracking) {
            TRACKMOUSEEVENT t;
            t.cbSize = sizeof t; t.dwFlags = TME_LEAVE;
            t.hwndTrack = hw; t.dwHoverTime = 0;
            TrackMouseEvent(&t);
            p->tracking = 1;
        }
        {
            int id = doc_hit(p, (short)LOWORD(lp), (short)HIWORD(lp));
            if (id != p->hot) { p->hot = id; InvalidateRect(hw, NULL, FALSE); }
        }
        return 0;
    case WM_MOUSELEAVE:
        if (p) {
            p->tracking = 0;
            if (p->hot >= 0) { p->hot = -1; InvalidateRect(hw, NULL, FALSE); }
        }
        return 0;
    case WM_SETCURSOR:
        if (p && LOWORD(lp) == HTCLIENT) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hw, &pt);
            if (doc_hit(p, pt.x, pt.y) >= 0) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                return TRUE;
            }
        }
        break;
    case WM_LBUTTONDOWN:
        if (p) {
            int id = doc_hit(p, (short)LOWORD(lp), (short)HIWORD(lp));
            if (id >= 0) {
                char url[512];
                snprintf(url, sizeof url, "%.*s", p->link[id].len, p->link[id].url);
                if ((INT_PTR)ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL) <= 32)
                    MessageBoxA(hw, "That link could not be opened.", CUBOLT_NAME,
                                MB_OK | MB_ICONINFORMATION);
            }
        }
        return 0;
    case WM_MOUSEWHEEL:
        /* Multiply before dividing: a touchpad's few dozen units over WHEEL_DELTA would be 0 */
        doc_scroll(hw, p, -(int)(short)HIWORD(wp) * SZ(30) / WHEEL_DELTA);
        return 0;
    case WM_VSCROLL: {
        RECT cr;
        if (!p) break;
        GetClientRect(hw, &cr);
        switch (LOWORD(wp)) {
        case SB_LINEUP:        doc_scroll(hw, p, -SZ(28)); break;
        case SB_LINEDOWN:      doc_scroll(hw, p,  SZ(28)); break;
        case SB_PAGEUP:        doc_scroll(hw, p, -cr.bottom); break;
        case SB_PAGEDOWN:      doc_scroll(hw, p,  cr.bottom); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: doc_scroll(hw, p, (int)(short)HIWORD(wp) - p->yoff); break;
        case SB_TOP:           doc_scroll(hw, p, -p->yoff); break;
        /* dy is how far to move, not where to land */
        case SB_BOTTOM:        doc_scroll(hw, p,  p->span - p->yoff); break;
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == VK_RETURN) { DestroyWindow(hw); return 0; }
        if (wp == VK_DOWN)  doc_scroll(hw, p,  SZ(28));
        if (wp == VK_UP)    doc_scroll(hw, p, -SZ(28));
        if (wp == VK_NEXT)  doc_scroll(hw, p,  SZ(320));
        if (wp == VK_PRIOR) doc_scroll(hw, p, -SZ(320));
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_DOC_OK) {
            DestroyWindow(hw);
            if (GetParent(hw)) SetFocus(GetParent(hw));
            return 0;
        }
        break;
    case WM_DESTROY:
        g_doc_open = 0;
        SetWindowLongPtr(hw, GWLP_USERDATA, 0);   /* the caller frees the panel next */
        return 0;
    }
    return DefWindowProc(hw, msg, wp, lp);
}

/* Modal: the owner is disabled and this pumps its own messages until the panel closes */
static void show_doc(HWND owner, const Block *doc, int ndoc, const char *caption, int wlog)
{
    doc_fonts();
    Panel *p = (Panel *)calloc(1, sizeof *p);
    if (!p) return;
    p->doc = doc; p->ndoc = ndoc; p->hot = -1;

    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    HWND hw = CreateWindowEx(0, DOC_CLASS, caption, style,
                             0, 0, SZ(wlog), SZ(300), owner, NULL, NULL, NULL);
    if (!hw) { free(p); return; }
    SetWindowLongPtr(hw, GWLP_USERDATA, (LONG_PTR)p);

    /* The width comes from the window: frame and caption come out of the non-client area. */
    RECT cr;
    GetClientRect(hw, &cr);
    p->w = cr.right - cr.left;

    HDC sdc = GetDC(hw), mdc = CreateCompatibleDC(sdc);
    HBITMAP bm = CreateCompatibleBitmap(sdc, 1, 1);
    HGDIOBJ old = SelectObject(mdc, bm);

    int row  = SZ(DOC_BTN_H) + SZ(36);
    int maxh = (g_wa[3] - g_wa[1]) - SZ(72);
    int dh   = doc_paint(mdc, p, 0);
    int ch   = dh + row;
    int span = 0;
    if (ch > maxh) {
        /* Past the work area: capped; WS_VSCROLL takes its bar from the client, so remeasure */
        ch = maxh;
        SetWindowLongPtr(hw, GWL_STYLE, (LONG_PTR)(style | WS_VSCROLL));
        SetWindowPos(hw, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        GetClientRect(hw, &cr);
        p->w = cr.right - cr.left;
        dh   = doc_paint(mdc, p, 0);
        span = dh - (ch - row);
        SetScrollRange(hw, SB_VERT, 0, span, TRUE);
    }
    p->span = span;
    p->h = ch;
    p->footer = row;

    SelectObject(mdc, old);
    DeleteObject(bm);
    DeleteDC(mdc);
    ReleaseDC(hw, sdc);

    /* The client comes out wlog wide without a bar, wlog minus the bar with one */
    RECT o = {0, 0, SZ(wlog), ch};
    AdjustWindowRect(&o, style, FALSE);
    int ow = o.right - o.left, oh = o.bottom - o.top;

    RECT orc;
    GetWindowRect(owner, &orc);
    int x = orc.left + ((orc.right - orc.left) - ow) / 2;
    int y = orc.top  + ((orc.bottom - orc.top) - oh) / 2;
    if (x < g_wa[0]) x = g_wa[0];
    if (y < g_wa[1]) y = g_wa[1];
    if (x + ow > g_wa[2]) x = g_wa[2] - ow;
    if (y + oh > g_wa[3]) y = g_wa[3] - oh;
    SetWindowPos(hw, NULL, x, y, ow, oh, SWP_NOZORDER | SWP_NOACTIVATE);

    mkf(0, "BUTTON", "OK", BS_DEFPUSHBUTTON,
        p->w - SZ(DOC_MARGIN) - SZ(DOC_BTN_W), ch - SZ(18) - SZ(DOC_BTN_H),
        SZ(DOC_BTN_W), SZ(DOC_BTN_H), hw, ID_DOC_OK);

    EnableWindow(owner, FALSE);
    g_doc_open = 1;
    ShowWindow(hw, SW_SHOW);
    SetFocus(hw);

    MSG m;
    while (g_doc_open) {
        /* NULL takes the thread's messages, so the outer loop's WM_QUIT lands here and goes back */
        if (!GetMessage(&m, NULL, 0, 0)) { PostQuitMessage(0); break; }
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    free(p);
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

static void show_about(HWND hw)
{
    show_doc(hw, ABOUT_DOC, (int)(sizeof ABOUT_DOC / sizeof ABOUT_DOC[0]), "About Cubolt", 440);
}

static void show_contents(HWND hw)
{
    show_doc(hw, HELP_DOC, (int)(sizeof HELP_DOC / sizeof HELP_DOC[0]),
             "Cubolt -- Contents", 500);
}

#endif /* DOCPANEL_H */
