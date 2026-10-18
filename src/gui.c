/* Cubolt (https://github.com/nbwzx/cubolt)
 * Copyright (C) 2026 Zixing Wang <zixingwang.cn@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* gui.c -- the native Win32 front-end for the solver; #includes engine.h. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <io.h>
#include <process.h>
#include "engine.h"
#include "caseout.h"
#include "jsonlite.h"
#include "batchcase.h"
#include "version.h"
#include "update.h"

#include <stdarg.h>
#include <commctrl.h>
#include <shlobj.h>

/* mktables.c compiled in, so cubolt.exe builds a missing table; MKTABLES_EMBED drops its main(). */
#define MKTABLES_EMBED
#include "mktables.c"

/* Named again at every CreateWindow: a name drifting apart there is a NULL, not an error. */
#define GUI_CLASS    "CuboltGui"
#define CANVAS_CLASS "CubeCanvas"
#define ICON_FILE    "cubolt.ico"

/* ---- table-build progress ---- */
/* the lock keeps name and counters together; mixed, the reader divides by another table's total */
static SRWLOCK            g_prog_lock = SRWLOCK_INIT;
static const char         *g_prog_name;
static unsigned long long  g_prog_done, g_prog_total;
static unsigned long long  g_build_total, g_build_done;

static void gui_mt_progress(const char *table, uint64_t done, uint64_t total)
{
    AcquireSRWLockExclusive(&g_prog_lock);
    if (g_prog_name && strcmp(g_prog_name, table)) g_build_done += mt_size(g_prog_name);
    g_prog_name  = table;
    g_prog_done  = done;
    g_prog_total = total;
    ReleaseSRWLockExclusive(&g_prog_lock);
}

/* wall-clock seconds the first-run build took; 0 when nothing was built */
static volatile double g_build_secs;

/* ---- control ids ---- */
enum {
    ID_SCRAMBLE = 1, ID_CUBESTR, ID_RESETCS, ID_CLEARCS, ID_SUB_FIRST,
    /* one command id per subgroup letter, then ID_DEPTH */
    ID_SUB_LAST = ID_SUB_FIRST + 17, ID_DEPTH, ID_ONLYOPT, ID_SORT,
    ID_SOLVE, ID_STOP, ID_LIST, ID_LOG, ID_COPYSEL, ID_COPYALL, ID_CLEARLOG,
    ID_PROG, ID_NETSTAT, ID_INPHINT, ID_STATUS,
    ID_TIMER_LOG = 100,
    /* menu items clear of the control ids; ID_SOLVE / ID_STOP / ID_CLEARLOG double as menu ids */
    IDM_SAVE = 200, IDM_EXIT, IDM_ABOUT, IDM_CONTENTS,
    IDM_WCA, IDM_COPYCS, IDM_COPYSCR, IDM_ONLYOPT, IDM_SORT, IDM_BATCH, IDM_UPDATE,
    /* Option > Threads: IDM_THR_AUTO, or IDM_THR_BASE + n for one of the offered counts */
    IDM_THR_AUTO = 224, IDM_THR_BASE = 225, IDM_THR_LAST = IDM_THR_BASE + THREAD_CAP
};
_Static_assert(IDM_THR_LAST < WM_APP, "the thread menu's ids run into WM_APP");
/* posted once run_job() has returned: the log/list work then happens on the GUI thread */
#define WM_SOLVE_DONE (WM_APP + 1)
#define WM_TABLES_READY (WM_APP + 2)   /* posted by the preload thread once tables are mapped */
#define WM_BATCH_DONE (WM_APP + 3)     /* posted by the batch worker; wParam = cases written */
#define WM_UPDATE_DONE (WM_APP + 4)    /* posted by the update worker; the verdict is in g_up_*/
#define WM_TABLES_FAIL (WM_APP + 5)    /* posted by the preload thread when a table will not load */

/* ---- cube-string front end ---- */
static const char FACES[7] = FACE_LET;

/* The two faces every window is built with; the help panel makes its own from the same names. */
#define UI_FACE   "Segoe UI"
/* the scramble box holds this much, so it is also what a dropped maneuver may be */
#define SCRAMBLE_MAX 255
/* a cubestring is this many stickers; the depth box only holds one number */
#define CS_STICKERS 54
#define DEPTH_CHARS 15
#define MONO_FACE "Consolas"
/* Clear leaves the six centers: the frame their letters name */
static const char CUBE_CLEARED[] =
    "????U????" "????R????" "????F????" "????D????" "????L????" "????B????";
_Static_assert(sizeof CUBE_CLEARED == CS_STICKERS + 1, "a cleared cube is no longer 54 stickers");
/* the cubestring the boxes start from is the same length the boxes will hold */
_Static_assert(sizeof CUBE_SOLVED == CS_STICKERS + 1, "a cubestring is no longer 54 stickers");

/* ---- DPI scaling ---- */
/* every coordinate is a logical 96-dpi value through SZ() */
static int   g_dpi  = 96;
static HFONT g_font = NULL;
static HFONT g_mono_font = NULL;
static HPEN   g_net_pen = NULL;
static HPEN   g_chk_pen = NULL;
static HFONT  g_net_font = NULL;
static HFONT  g_list_font = NULL;  /* the listbox' monospace face, kept so WM_DESTROY frees it */
/* the canvas' two: a dropper over a center, a bucket over the sticker it paints */
static HCURSOR g_cur_pick = NULL, g_cur_paint = NULL;
static HBRUSH g_face_brush[7];    /* one per face letter, then the gray fallback */
/* the two owner-drawn buttons: three accents with pressed shades, made once in build_ui */
#define BTN_N   3
static const COLORREF BTN_BASE[BTN_N] = {   /* disabled, Solve, Stop */
    RGB(0x9a, 0xa0, 0xa6), RGB(0x1f, 0x6f, 0xd6), RGB(0xc0, 0x39, 0x2b)
};
static HBRUSH g_btn_brush[BTN_N * 2];       /* each accent, then its pressed shade */
static HPEN   g_btn_pen[BTN_N];
/* the facelet edge: LOGICAL_A at 96 dpi, A what that comes to at the dpi in force */
#define LOGICAL_A 44
static int   A      = LOGICAL_A;
static int   g_skip_preload = 0;
static int   g_need_build = 0;
static int SZ(int v) { return MulDiv(v, g_dpi, 96); }

#define PANEL_W   456
#define NET_STATUS_H 22
#define LIST_CAP  2000
#define LOG_POLL_MS 150
/* the solutions list: the flexible part of the layout, named so client_h() and it agree */
#define LIST_TOP  392
#define LIST_H    264
#define LIST_FLOOR 80
#define COPY_GAP  8
#define COPY_H    28

/* device pixels, scaled once: SZ() applied twice rounds apart and clips the list */
static int g_list_h = 0;
/* Work area (l,t,r,b): what the reports print, and the box a help panel is kept inside. */
static int g_wa[4] = {0, 0, 0, 0};
static int g_ow, g_oh;

static int client_w(void)
{
    return SZ(8) + (12 * A + SZ(20)) + SZ(12) + SZ(PANEL_W) + SZ(8);
}
static int client_h(void)
{
    int canvas_bottom = SZ(8) + (9 * A + SZ(20)) + SZ(NET_STATUS_H);
    int copy_bottom   = SZ(LIST_TOP) + g_list_h + SZ(COPY_GAP) + SZ(COPY_H) + SZ(8);
    return (canvas_bottom > copy_bottom ? canvas_bottom : copy_bottom) + SZ(NET_STATUS_H);
}

/* ---- message boxes ---- */
typedef HRESULT (WINAPI *PFN_TASKTDIALOGINDIRECT)(const TASKDIALOGCONFIG *, int *, int *, BOOL *);

/* TaskDialogIndirect, or NULL when this build's .res asks for no ComCtl32 v6 */
static PFN_TASKTDIALOGINDIRECT taskdlg(void)
{
    static PFN_TASKTDIALOGINDIRECT pfn;
    static int probed;
    if (!probed) {
        HMODULE m = LoadLibraryA("comctl32.dll");
        if (m) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
            pfn = (PFN_TASKTDIALOGINDIRECT)GetProcAddress(m, "TaskDialogIndirect");
#pragma GCC diagnostic pop
        }
        probed = 1;
    }
    return pfn;
}

/* For the first-run prompt, which is up before WinMain loads the class icons */
static HICON g_msg_icon_sml, g_msg_icon_big;
/* Freed in WM_DESTROY, not WinMain: the --layout run returns before the loop is reached. */
static HICON g_class_icon_sml, g_class_icon_big;

static void ui_msg_icons(void)
{
    if (g_msg_icon_sml || g_msg_icon_big) return;
    g_msg_icon_sml = (HICON)LoadImage(NULL, ICON_FILE, IMAGE_ICON,
                            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                            LR_LOADFROMFILE);
    g_msg_icon_big = (HICON)LoadImage(NULL, ICON_FILE, IMAGE_ICON,
                            GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
                            LR_LOADFROMFILE);
    if (!g_msg_icon_big) g_msg_icon_big = g_msg_icon_sml;
}

static HRESULT CALLBACK ui_msg_created(HWND hw, UINT notif, WPARAM wp, LPARAM lp, LONG_PTR ref)
{
    (void)wp; (void)ref;
    /* an <A HREF> in the body is a link: hand the href to whatever the shell has registered */
    if (notif == TDN_HYPERLINK_CLICKED) {
        /* 32 and under is the shell's "nothing here opens this": say so, not a dead click */
        if ((INT_PTR)ShellExecuteW(NULL, L"open", (LPCWSTR)lp, NULL, NULL, SW_SHOWNORMAL) <= 32)
            MessageBoxW(hw, L"That link could not be opened.", L"Cubolt", MB_ICONWARNING | MB_OK);
        return S_OK;
    }
    if (notif == TDN_CREATED) {
        if (g_msg_icon_sml) SendMessage(hw, WM_SETICON, ICON_SMALL, (LPARAM)g_msg_icon_sml);
        if (g_msg_icon_big) SendMessage(hw, WM_SETICON, ICON_BIG,   (LPARAM)g_msg_icon_big);
    }
    return S_OK;
}

static int ui_msg(HWND owner, const char *text, const char *caption, UINT style)
{
    PFN_TASKTDIALOGINDIRECT pfn = taskdlg();

    if (pfn) {
        wchar_t wt[1024], wc[160];
        /* our own button strings: TaskDialog draws these verbatim, MessageBox localizes */
        wchar_t wok[8] = L"OK", wyes[8] = L"Yes", wno[8] = L"No";
        ui_msg_icons();
        /* the strings here are UTF-8; CP_ACP reads anything outside ASCII by a different table */
        if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wt, 1024) > 0 &&
            MultiByteToWideChar(CP_UTF8, 0, caption, -1, wc, 160) > 0) {
            int yesno = (style & MB_TYPEMASK) == MB_YESNO;
            TASKDIALOG_BUTTON b[2];
            b[0].nButtonID = yesno ? IDYES : IDOK;
            b[0].pszButtonText = yesno ? wyes : wok;
            b[1].nButtonID = IDNO;
            b[1].pszButtonText = wno;

            TASKDIALOGCONFIG tc;
            ZeroMemory(&tc, sizeof tc);
            tc.cbSize          = sizeof tc;
            tc.hwndParent      = owner;
            tc.dwFlags         = TDF_ALLOW_DIALOG_CANCELLATION | TDF_ENABLE_HYPERLINKS |
                                 (owner ? TDF_POSITION_RELATIVE_TO_WINDOW : 0);
            tc.pszWindowTitle  = wc;
            tc.pszContent      = wt;
            UINT icon = style & MB_ICONMASK;   /* not `style & MB_ICONERROR`: 0x30 has bit 0x10 */
            if (icon == MB_ICONERROR)        tc.pszMainIcon = TD_ERROR_ICON;
            else if (icon == MB_ICONWARNING) tc.pszMainIcon = TD_WARNING_ICON;
            tc.cButtons        = yesno ? 2 : 1;
            tc.pButtons        = b;
            tc.nDefaultButton  = b[0].nButtonID;
            tc.pfCallback      = ui_msg_created;

            int hit = IDOK;
            if (SUCCEEDED(pfn(&tc, &hit, NULL, NULL))) return hit;
        }
    }
    return MessageBoxA(owner, text, caption, style);
}

/* a TaskDialog makes the link clickable, the MessageBox fallback can only print it */
static int ui_msg_link(HWND owner, const char *text, const char *caption, UINT style,
                       const char *link)
{
    char body[1024];
    if (!link || !*link) return ui_msg(owner, text, caption, style);
    if (taskdlg())
        snprintf(body, sizeof body, "%s\n<A HREF=\"%s\">%s</A>", text, link, link);
    else
        snprintf(body, sizeof body, "%s\n%s", text, link);
    return ui_msg(owner, body, caption, style);
}

/* ---- GUI state (single instance, so a global is fine) ---- */
typedef struct {
    HWND     hwnd, canvas, scramble, cubestring, depth, list, log, status, prog, netstatus;
    HWND     inphint;
    HWND     solve_btn, stop_btn;
    HWND     hw_onlyopt, hw_sort;   /* kept so the Option menu can mirror the checkboxes */
    char     net[6][3][3];       /* color letter per sticker */
    int      curcol;             /* 0..5: the face whose center carries the selected color */
    int      net_bad;
    int      hint_bad;           /* the input hint is carrying an error, not just progress */
    int      autocpl;            /* the Auto complete box under the net is ticked */
    int      only_optimal;
    int      sort_solutions;
    int      busy;
    volatile int tables_ready;
    char   **solutions;
    int      nsol, capsol;
} G;
static G g;

#include "guinet.h"

/* the log's own paths reset it and its timer reads it, so the resets are interlocked */
static volatile long long g_logpos = 0;

/* ---- subgroup string from the checkboxes ---- */
/* one checkbox each, in layout order: six face turns, each wide twin, the slices, the rotations */
static const char SUBGROUP_LETTERS[] = FACE_LET WIDE_LET SLICE_LET ROT_LET;
#define SUBGROUP_N       ((int)(sizeof SUBGROUP_LETTERS - 1))
_Static_assert(SUBGROUP_N == ID_SUB_LAST - ID_SUB_FIRST + 1,
               "ID_SUB_LAST does not match the number of subgroup letters");
static void subgroup_str(char *out)
{
    int k = 0;
    for (int i = 0; i < SUBGROUP_N; i++)
        if (IsDlgButtonChecked(g.hwnd, ID_SUB_FIRST + i))
            out[k++] = SUBGROUP_LETTERS[i];
    out[k] = 0;
}

/* ---- solutions listbox backing store ---- */
/* only the first LIST_CAP rows go in the box (an insert is quadratic); the store keeps them all */
static char **g_pend = NULL;
static int    g_npend, g_cpend;
static int    g_pend_seen;      /* this run's count; g_npend is per batch, the drain resets it */
static int    g_pend_all;       /* every row handed over, whether the list took it or not */
static int    g_pend_note;      /* the "the live list stopped early" line is written once a run */
static CRITICAL_SECTION g_pend_cs;      /* initialized in build_ui, lives for the process */
static void log_append(const char *txt);

static void pend_flush(void)
{
    EnterCriticalSection(&g_pend_cs);
    for (int i = 0; i < g_npend; i++) free(g_pend[i]);
    free(g_pend);
    g_pend = NULL; g_npend = g_cpend = 0;
    /* under the lock: a worker still finishing a call reads these counters too */
    g_pend_seen = 0;
    g_pend_all = 0;
    g_pend_note = 0;
    LeaveCriticalSection(&g_pend_cs);
}

static void gui_new_sol(const char *s)
{
    EnterCriticalSection(&g_pend_cs);
    g_pend_all++;
    if (g_pend_seen >= LIST_CAP) { LeaveCriticalSection(&g_pend_cs); return; }
    if (g_npend == g_cpend) {
        int ncap = g_cpend ? g_cpend * 2 : 64;
        char **t = realloc(g_pend, (size_t)ncap * sizeof(char *));
        if (!t) { LeaveCriticalSection(&g_pend_cs); return; }
        g_pend = t; g_cpend = ncap;
    }
    char *copy = _strdup(s);
    if (copy) { g_pend[g_npend++] = copy; g_pend_seen++; }
    LeaveCriticalSection(&g_pend_cs);
}

static void add_solution(const char *s, int listed);

static void pend_drain(void)
{
    EnterCriticalSection(&g_pend_cs);
    char **arr = g_pend;
    int n = g_npend;
    g_pend = NULL; g_npend = g_cpend = 0;
    LeaveCriticalSection(&g_pend_cs);
    if (!n) return;
    SendMessage(g.list, WM_SETREDRAW, FALSE, 0);
    for (int i = 0; i < n; i++) {
        add_solution(arr[i], 1);
        free(arr[i]);
    }
    SendMessage(g.list, WM_SETREDRAW, TRUE, 0);
    free(arr);
    InvalidateRect(g.list, NULL, TRUE);
    /* past LIST_CAP a row is counted but not queued: this difference is what went unlisted */
    EnterCriticalSection(&g_pend_cs);
    int over = g_pend_all - g_pend_seen;
    int said = g_pend_note;
    if (over > 0) g_pend_note = 1;
    LeaveCriticalSection(&g_pend_cs);
    if (over > 0 && !said) {
        char note[96];
        /* "at least": fires once, when the cap is first passed; finish_solve reports the count */
        snprintf(note, sizeof note,
                 "\n(the live list is full at %d rows: at least %d more are still to come)\n",
                 LIST_CAP, over);
        log_append(note);
    }
}

static void clear_solutions(void)
{
    for (int i = 0; i < g.nsol; i++) free(g.solutions[i]);
    g.nsol = 0;
    pend_flush();                       /* whatever is still waiting belongs to that same set */
    SendMessage(g.list, LB_RESETCONTENT, 0, 0);
}

static void add_solution(const char *s, int listed)
{
    if (g.nsol == g.capsol) {
        int ncap = g.capsol ? g.capsol * 2 : 64;
        char **t = realloc(g.solutions, (size_t)ncap * sizeof(char *));
        if (!t) return;
        g.solutions = t;
        g.capsol = ncap;
    }
    char *copy = _strdup(s);
    if (!copy) return;
    /* a row the listbox refuses is dropped here too: the two share one index. LB_ERRSPACE is -2 */
    if (listed && SendMessage(g.list, LB_ADDSTRING, 0, (LPARAM)s) < 0) { free(copy); return; }
    g.solutions[g.nsol] = copy;
    g.nsol++;
}

/* ---- log text control ---- */
/* a multiline edit wants CRLF, everything fed to it a bare LF; the caret is kept at the end */
static void log_append(const char *txt)
{
    size_t n = strlen(txt);
    char *tmp = NULL;
    const char *use = txt;

    if (strchr(txt, '\n')) {
        tmp = malloc(n * 2 + 2);
        if (tmp) {
            size_t k = 0;
            for (size_t i = 0; i < n; i++) {
                if (txt[i] == '\n' && (i == 0 || txt[i - 1] != '\r')) tmp[k++] = '\r';
                tmp[k++] = txt[i];
            }
            tmp[k] = 0;
            use = tmp;
        }
    }
    int len = GetWindowTextLength(g.log);
    SendMessage(g.log, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(g.log, EM_REPLACESEL, 0, (LPARAM)use);
    SendMessage(g.log, EM_SCROLLCARET, 0, 0);
    free(tmp);
}

/* One line, one write: a prefix and a value and a newline as three appends is three repaints. */
static void log_appendf(const char *fmt, ...)
{
    va_list ap, cp;
    va_start(ap, fmt);
    va_copy(cp, ap);
    int n = vsnprintf(NULL, 0, fmt, cp);
    va_end(cp);
    if (n < 0) { va_end(ap); return; }
    char *buf = malloc((size_t)n + 1);
    if (buf) {
        vsnprintf(buf, (size_t)n + 1, fmt, ap);
        log_append(buf);
        free(buf);
    }
    va_end(ap);
}

/* ---- status bar (themed, lives at the bottom of the client area) ---- */
static void set_status(const char *fmt, ...)
{
    if (!g.status) return;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    SendMessage(g.status, SB_SETTEXT, 0, (LPARAM)buf);
}

/* no per-table percentage: the plan's tables differ in size by four orders of magnitude */
static void prog_tick(void)
{
    if (!g_need_build || g.tables_ready) return;
    if (!g.prog) return;
    const char *name;
    unsigned long long done, total, bdone, btotal;
    AcquireSRWLockShared(&g_prog_lock);
    name   = g_prog_name;
    done   = g_prog_done;
    total  = g_prog_total;
    bdone  = g_build_done;
    btotal = g_build_total;
    ReleaseSRWLockShared(&g_prog_lock);
    if (!name || !total) return;
    unsigned long long all = bdone + (done * mt_size(name)) / total;
    int pm = btotal ? (int)(all * 1000 / btotal)
                    : (int)(done * 1000 / total);
    if (pm > 1000) pm = 1000;
    SendMessage(g.prog, PBM_SETPOS, pm, 0);
    if (btotal)
        set_status("Building solver tables -- %d%% (%s).", pm / 10, name);
    else
        set_status("Building solver tables -- %s, %d%%.", name, pm / 10);
}

/* ---- live log: stdout the worker has written since the last poll ---- */
/* one EM_REPLACESEL per tick: an EDIT reflows all held text on every replace */
#define MAX_LOG_LINES 4000

/* A search still running means neither the saved file nor the clipboard holds the whole list. */
#define SO_FAR(busy) ((busy) ? " so far" : "")

static void log_trim(void)
{
    int lines = (int)SendMessage(g.log, EM_GETLINECOUNT, 0, 0);
    if (lines <= MAX_LOG_LINES) return;
    int del  = lines - MAX_LOG_LINES;
    int from = (int)SendMessage(g.log, EM_LINEINDEX, 0, 0);
    int to   = (int)SendMessage(g.log, EM_LINEINDEX, del, 0);
    SendMessage(g.log, EM_SETSEL, (WPARAM)from, (LPARAM)to);
    SendMessage(g.log, EM_REPLACESEL, 0, (LPARAM)TEXT(""));
}

/* set in WinMain: %TEMP% plus the pid, so two instances keep their logs apart */
static char g_tmplog[MAX_PATH + 32] = "_gui_stdout.tmp";

static void poll_log(void)
{
    FILE *lf = fopen(g_tmplog, "rb");
    if (!lf) return;
    long long pos = InterlockedExchangeAdd64(&g_logpos, 0);
    if (_fseeki64(lf, pos, SEEK_SET) != 0) { fclose(lf); return; }
    char *acc = NULL; size_t cap = 0, len = 0;
    char buf[8192]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, lf)) > 0) {
        if (len + n + 1 > cap) {
            size_t ncap = (len + n + 1) * 2;
            char *t = realloc(acc, ncap);
            if (!t) { free(acc); fclose(lf); return; }
            acc = t; cap = ncap;
        }
        memcpy(acc + len, buf, n);
        len += n;
    }
    /* a read stopped by an error looks like the end: showing the half tail would lose the rest */
    if (ferror(lf)) { free(acc); fclose(lf); return; }
    fclose(lf);
    if (!acc || len == 0) { free(acc); return; }
    acc[len] = 0;
    /* hand the box only the tail it can keep: log_trim() runs after the append */
    const char *use = acc;
    int kept = 0;
    for (size_t i = len; i > 0; i--)
        if (acc[i - 1] == '\n' && ++kept == MAX_LOG_LINES) { use = acc + i; break; }
    log_append(use);
    /* consumed even where the box drops the lines; a worker's reset in between wins */
    InterlockedCompareExchange64(&g_logpos, pos + (long long)len, pos);
    free(acc);
    log_trim();
}

/* unbuffered so both land in order; stderr takes stdout's file position, not a second open */
static void log_redirect(const char *mode)
{
    if (!freopen(g_tmplog, mode, stdout)) {
        /* the log box is the only place this run can still be read from */
        fprintf(stderr, "cannot redirect the log to %s (error %lu)\n", g_tmplog,
                (unsigned long)GetLastError());
        log_append("The log file could not be opened, so nothing will be shown in here.\n");
        freopen("NUL", "w", stdout);   /* stderr stays: why this failed is worth reading */
        return;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    _dup2(_fileno(stdout), _fileno(stderr));
    setvbuf(stderr, NULL, _IONBF, 0);
}

/* "w" truncates the log, so the tail the box reads starts at byte 0 again */
static void log_redirect_trunc(void)
{
    log_redirect("w");
    InterlockedExchange64(&g_logpos, 0);
}

static void log_restart(void);
static void log_run_start(const char *head)
{
    log_append("\n----------------------------------------------------\n");
    log_append(head);
    log_restart();
}

/* the previous run's file is dropped first, so the first poll cannot replay it */
static void log_restart(void)
{
    remove(g_tmplog);
    FILE *clr = fopen(g_tmplog, "wb");
    if (clr) fclose(clr);
    InterlockedExchange64(&g_logpos, 0);
    SetTimer(g.hwnd, ID_TIMER_LOG, LOG_POLL_MS, NULL);
}

/* ---- clipboard ---- */
/* Another process holding the clipboard is the usual reason a copy fails. */
#define CLIP_FAIL_MSG "The clipboard could not be opened."
#define COPY_TITLE     "Copy"
#define COPY_ALL_TITLE "Copy all"
#define SOLVE_TITLE    "Solve"

static int set_clipboard(const char *s)
{
    size_t n = strlen(s) + 1;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n);
    if (!h) return 0;
    void *p = GlobalLock(h);
    if (!p) { GlobalFree(h); return 0; }
    memcpy(p, s, n);
    GlobalUnlock(h);
    /* SetClipboardData takes ownership only on success; a Copy all can hand it tens of MB */
    int ok = 0;
    if (OpenClipboard(g.hwnd)) {
        EmptyClipboard();
        if (SetClipboardData(CF_TEXT, h)) ok = 1;
        else GlobalFree(h);
        CloseClipboard();
    } else {
        GlobalFree(h);
    }
    return ok;
}

/* 0 = auto ("one worker per logical core"); applied when a search starts, never mid-run */
static int g_threads = 0;

/* ---- the solve worker (runs off the GUI thread) ---- */
typedef struct {
    char cubestring[55];
    int  depth;
    char subgroup[32];
    int  only_optimal;
    int  sort_solutions;
} SolveReq;
static SolveReq g_req;

#define DEPTH_FMT        "Search Depth is not a depth -- put a number from 0 to %d in it."
#define DEPTH_TITLE      "Search Depth"

/* 0 when the depth box asks for nothing this solver can answer; the cubestring is the caller's. */
static int read_req(void)
{
    char depthbuf[16];
    box_text(g.depth, depthbuf, sizeof depthbuf);
    /* Solve can be pressed before a paste reaches the clamp: the panel takes what the CLI takes. */
    if (!depthbuf[0] || parse_int(depthbuf, &g_req.depth)) {
        char why[128];
        snprintf(why, sizeof why, DEPTH_FMT, MAX_DEPTH);
        ui_msg(g.hwnd, why, DEPTH_TITLE, MB_OK | MB_ICONWARNING);
        return 0;
    }
    subgroup_str(g_req.subgroup);
    g_req.only_optimal   = g.only_optimal;
    g_req.sort_solutions = g.sort_solutions;
    return 1;
}
static char g_batch_in[MAX_PATH], g_batch_out[MAX_PATH];
static int  g_batch_text_mode = 0;   /* 1 = text batch: the "name, scramble" record format */

/* results, filled in by the worker and consumed once WM_SOLVE_DONE arrives */
static volatile int    g_res_rc, g_res_stopped;
static volatile double g_res_dt;

/* flushed, then to NUL: freopen() releases the file, which remove() cannot take while open. */
static void detach_log(void)
{
    fflush(stdout);
    fflush(stderr);
    freopen("NUL", "w", stdout);
    freopen("NUL", "w", stderr);
}

static void worker_done(unsigned msg, WPARAM wp)
{
    detach_log();
    PostMessage(g.hwnd, msg, wp, 0);
}

/* its own thread, so a machine that cannot reach GitHub never freezes the window */
static volatile int  g_up_rc;
static volatile char g_up_tag[64], g_up_why[192];
static int  g_up_running;   /* a check is out; its results are the worker's until it posts */
static HANDLE g_up_thread;  /* kept, so the window's exit can wait for the check it started */

static unsigned __stdcall update_worker(void *ctx)
{
    (void)ctx;
    /* volatile for the reader after the post; the callee wants a plain buffer */
    g_up_rc = update_latest((char *)g_up_tag, sizeof g_up_tag,
                            (char *)g_up_why, sizeof g_up_why);
    PostMessage(g.hwnd, WM_UPDATE_DONE, 0, 0);
    return 0;
}

/* One line per solve: every worker on its way out lands here, and the log box keeps the last. */
static volatile long g_rend_oom_said;

/* The list hit the machine's limit: stderr feeds the log box, so the walk stops with its list. */
static int gui_rend_oom(size_t want)
{
    /* the workers reach this together: one exchange, so it is said once between them */
    if (!InterlockedExchange(&g_rend_oom_said, 1))
        fprintf(stderr, REND_OOM_FMT, want / 1048576);
    engine_stop = 1;
    return 0;
}

/* the line WM_TABLES_FAIL appends: written by the worker, which may not SendMessage itself */
static char g_table_fail[128];

/* Where a worker lands when a table will not load: it has no console of its own to end on. */
static void table_fail_post(void)
{
    g_table_jmp = NULL;
    snprintf(g_table_fail, sizeof g_table_fail,
             "\nThe tables could not be loaded -- the reason is above.\n");
    detach_log();
    PostMessage(g.hwnd, WM_TABLES_FAIL, 0, 0);
}

/* The setjmp has to sit in the worker itself: the jump lands in that frame, not in a helper's. */
#define WORKER_TABLE_GUARD()                                    \
    static jmp_buf tbl_jmp;                                     \
    g_table_jmp = &tbl_jmp;                                     \
    if (setjmp(tbl_jmp)) { table_fail_post(); return 0; }

static unsigned __stdcall solve_worker(void *ctx)
{
    (void)ctx;
    /* A Solve loads too: a subgroup that names M, E or S asks for the other engine's table. */
    WORKER_TABLE_GUARD();

    Job j = job_default();
    j.cubestring   = g_req.cubestring;
    j.depth        = g_req.depth;
    j.subgroup     = g_req.subgroup;
    j.opt          = g_req.only_optimal;
    j.dump         = 0;
    sol_std        = 1;          /* quiet: solutions stay in render.h's arena, not stdout */
    rend_do_sort   = g_req.sort_solutions || g_req.only_optimal;
    engine_stop    = 0;          /* fresh run: clear any Stop left over from the last one */
    InterlockedExchange(&g_rend_oom_said, 0);
    /* only_optimal ends on the one solution it keeps, so it needs no hook */
    rend_new_hook  = g_req.only_optimal ? NULL : gui_new_sol;
    rend_oom       = gui_rend_oom;

    double t0 = now_sec();
    g_res_rc = run_job(&j);
    /* the solve time leaves out table set-up (load_wall) */
    g_res_dt = now_sec() - t0 - load_wall;
    g_res_stopped = engine_stop;
    engine_stop = 0;
    rend_new_hook = NULL;        /* nothing else can render until another solve asks for it */
    rend_oom      = NULL;
    g_table_jmp   = NULL;

    /* the GUI thread does the rest: the arena is then only read, never written */
    worker_done(WM_SOLVE_DONE, 0);
    return 0;
}

/* closed as soon as it reports back, so back-to-back batches do not pile up handles */
static HANDLE g_worker = NULL;

static void worker_close(void)
{
    if (g_worker) { CloseHandle(g_worker); g_worker = NULL; }
}

/* every start and finish leaves the buttons in step; the menu reads g.busy when it opens */
static void solve_busy(int busy)
{
    g.busy = busy;
    EnableWindow(g.solve_btn, !busy);
    EnableWindow(g.stop_btn, busy);
}

/* the buttons would otherwise stay busy for a search that never starts */
static void worker_start_failed(const char *title)
{
    solve_busy(0);
    /* without this the timer reads a file nothing will write, every 150 ms, all session */
    KillTimer(g.hwnd, ID_TIMER_LOG);
    ui_msg(g.hwnd, "Cannot start the worker thread.", title, MB_OK | MB_ICONERROR);
}

/* null when it could not, which the message box has already said */
static HANDLE start_worker(unsigned (__stdcall *fn)(void *), const char *title)
{
    HANDLE h = (HANDLE)_beginthreadex(NULL, 0, fn, NULL, 0, NULL);
    if (!h) worker_start_failed(title);
    return h;
}

static void finish_worker(void)
{
    KillTimer(g.hwnd, ID_TIMER_LOG);
    poll_log();
    remove(g_tmplog);
    InterlockedExchange64(&g_logpos, 0);
    worker_close();
    solve_busy(0);
}

static void finish_solve(void)
{
    finish_worker();

    /* the log is finished before the list: the log is the record, the list a convenience */
    if (g_res_rc == 0) {
        int n = (int)rend_on;
        /* only_optimal keeps one solution of the ones there are; forcing 1 with none is wrong */
        int disp = g_req.only_optimal && n ? 1 : n;
        char msg[200];
        if (n)
            snprintf(msg, sizeof msg, "\n%d solution(s) in %.2f s%s.\n", disp, g_res_dt,
                     g_res_stopped ? " (stopped early -- partial list)" : "");
        else
            snprintf(msg, sizeof msg,
                     g_res_stopped
                         ? "\nStopped: no solution found yet within depth %d in subgroup %s.\n"
                         : "\nNo solution found within depth %d in subgroup %s.\n",
                     g_req.depth, g_req.subgroup);
        log_append(msg);
        if (disp > LIST_CAP) {
            char note[96];
            snprintf(note, sizeof note,
                     "the list shows the first %d; Copy all copies all %d.\n", LIST_CAP, disp);
            log_append(note);
        }
        if (disp)
            set_status(g_res_stopped ? "%d solution(s) in %.2f s (stopped early)"
                                     : "%d solution(s) in %.2f s", disp, g_res_dt);
        else
            set_status(g_res_stopped ? "Stopped: no solution within depth %d"
                                     : "No solution within depth %d", g_req.depth);

        /* LIST_CAP caps the listbox, not the copy; the reset stays inside the redraw-off span */
        int shown = disp < LIST_CAP ? disp : LIST_CAP;
        /* rend_nth reads back in the order it asks for; the CLI's copy is gated off here (quiet) */
        if (rend_do_sort && !rend_ord) rend_sort_par();
        SendMessage(g.list, WM_SETREDRAW, FALSE, 0);
        clear_solutions();
        for (int i = 0; i < disp; i++)
            add_solution(rend_text(rend_nth(i)), i < shown);
        SendMessage(g.list, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(g.list, NULL, TRUE);
        if (shown) SendMessage(g.list, LB_SETTOPINDEX, 0, 0);
    } else {
        log_append("\nSolve failed (see messages above).\n");
        set_status("Solve failed -- see the log.");
    }
}

#define BAD_CUBE_TITLE    "Bad cube"

#define BUSY_TITLE       "Please wait"
#define BUSY_TABLES      "The solver is still loading its tables."

#define BATCH_TITLE      "Batch solve"

/* A search asked for now has no table to prune with; 0 also says so. */
static int tables_ready(void)
{
    if (g.tables_ready) return 1;
    ui_msg(g.hwnd, BUSY_TABLES, BUSY_TITLE, MB_OK);
    return 0;
}

static void start_solve(void)
{
    if (g.busy) return;
    if (!tables_ready()) return;
    /* a complete definition in the box is the cube being solved: load it into the net */
    char box[160];
    box_text(g.cubestring, box, sizeof box);
    if (strlen(box) == CS_STICKERS) {
        char reason[256];
        const char *bad = cube_problem(box, reason, sizeof reason);
        /* build_def would read the net */
        if (bad) {
            ui_msg(g.hwnd, bad, BAD_CUBE_TITLE, MB_OK | MB_ICONWARNING);
            return;
        }
        net_from_cs(box);
        InvalidateRect(g.canvas, NULL, TRUE);
    } else if (*box) {
        ui_msg(g.hwnd, "The definition box needs 54 stickers; the hint under it says how many "
                       "are in there now.", BAD_CUBE_TITLE, MB_OK | MB_ICONWARNING);
        return;
    }
    char def[55], why[256];
    build_def(def);
    /* say which rule the cube breaks before mapping the tables, as run_job() would */
    const char *bad = cube_problem(def, why, sizeof why);
    if (bad) {
        ui_msg(g.hwnd, bad, BAD_CUBE_TITLE, MB_OK | MB_ICONWARNING);
        return;
    }
    if (!read_req()) return;

    strcpy(g_req.cubestring, def);
    clear_solutions();
    solve_busy(1);
    char head[160];
    snprintf(head, sizeof head, "solve(%s, %d, %s, only_optimal=%d, sort=%d)\n",
             def, g_req.depth, g_req.subgroup, g_req.only_optimal, g_req.sort_solutions);
    log_run_start(head);
    if (!g.only_optimal)
        log_append("only optimal is OFF: every solution up to depth is enumerated.\n");

    par_threads = g_threads;
    /* Opened here: a failed open is reported into the log box, and a worker may not SendMessage. */
    log_redirect_trunc();
    g_worker = start_worker(solve_worker, SOLVE_TITLE);
}

static int table_ok(const char *name);

/* One table of the build plan prog_tick reads the whole job's percentage from. */
static void plan_add(const char *name)
{
    AcquireSRWLockExclusive(&g_prog_lock);
    g_build_total += mt_size(name);
    ReleaseSRWLockExclusive(&g_prog_lock);
}

/* preload thread; the caller redirected the streams to the log, so progress shows up live */
static void gui_build_missing_tables(void)
{
    mt_stop = 0;
    build_move_cubes();
    build_symmetry_tables();

    /* --force: a file that is there is a damaged one; put back, or the next build forces too. */
    int force_was = opt_force;
    opt_force = 1;

    /* plan_add() names every table first: the bar would otherwise read 100% at every table */

    /* one list of builders, the same mktables.exe walks; one bad file rebuilds the whole group */
    int redo[B_NBUILD];
    for (int i = 0; i < B_NBUILD; i++) {
        const char *const *f = BUILDERS[i].files;
        redo[i] = 0;
        for (int j = 0; j < B_NFILES && f[j]; j++) redo[i] |= !table_ok(f[j]);
    }

    for (int i = 0; i < B_NBUILD; i++)
        if (redo[i])
            for (int j = 0; j < B_NFILES && BUILDERS[i].files[j]; j++)
                plan_add(BUILDERS[i].files[j]);

    for (int i = 0; i < B_NBUILD; i++) {
        if (mt_stop) break;      /* WM_DESTROY: the rest of the plan waits for next start */
        if (redo[i]) BUILDERS[i].build(BUILDERS[i].ftm);
    }
    opt_force = force_was;

    /* Stopped early: no manifest, and the next start loads by size. */
    if (mt_stop) return;

    /* record the digest for --manifest and --check */
    ts_manifest_write();
}

/* ---- startup table preload, on its own thread so the window opens at once ---- */
/* read on the GUI thread: asking the checkboxes from a worker is a SendMessage it cannot pump */
static char g_defsub[SUBGROUP_N + 1];

static unsigned __stdcall preload_worker(void *ctx)
{
    (void)ctx;
    WORKER_TABLE_GUARD();
    build_once();
    if (g_need_build) {
        double tb = now_sec();
        g_mt_progress = gui_mt_progress;
        gui_build_missing_tables();
        g_mt_progress = NULL;
        fflush(stdout);
        fflush(stderr);
        g_build_secs = now_sec() - tb;   /* reported by the GUI thread, after it drains the log */
        /* the loads would exit on it too: this is the shorter way out */
        if (engine_stop) { g_table_jmp = NULL; detach_log(); return 0; }
    }
    load_shared_tables();
    /* the default subgroup's tables are mapped here: a Solve on it has nothing left to load */
    ensure_tables(job_slice_mode(g_defsub));
    /* the frame a jump would land in is about to go: past here a bad table ends the process */
    g_table_jmp = NULL;
    detach_log();
    PostMessage(g.hwnd, WM_TABLES_READY, 0, 0);
    return 0;
}

static int table_state(const char *name)
{
    long long want = (long long)mt_size(name);
    return ts_size_ok(name, want) ? TS_OK : TS_MISSING;
}

static int table_ok(const char *name)
{
    return table_state(name) == TS_OK;
}

/* 0 = the solver can start at once; *bytes totals the tables behind n, not the whole set. */
static int missing_table_count(long long *bytes)
{
    int n = 0;
    long long todo = 0;
    for (int i = 0; TABLE_FILES[i].name; i++) {
        if (table_state(TABLE_FILES[i].name) == TS_OK) continue;
        n++;
        todo += TABLE_FILES[i].size;
    }
    if (bytes) *bytes = todo;
    return n;
}

/* 1 to build, 0 to quit.  The newlines are paragraph breaks: both dialogs wrap the rest */
static int ask_build_tables(int n, long long bytes)
{
    const char *verb = n == 1 ? "is" : "are";
    /* n may be one table of kilobytes, and "0.0 GB" is no answer to how long to wait. */
    int mb = bytes < 1000000000;
    char msg[1024];
    snprintf(msg, sizeof msg,
        "%d of the %d tables Cubolt needs %s missing.  Building them writes %.1f %s next to "
        "cubolt.exe: this run does it, and every later start just loads them.\n"
        "\n"
        "The status bar shows how far along it is; the log window has the details.",
        n, TS_NFILES, verb, mb ? bytes / 1e6 : bytes / 1e9, mb ? "MB" : "GB");
    return ui_msg(NULL, msg, "Build pruning tables?", MB_YESNO | MB_ICONWARNING) == IDYES;
}

/* all coordinates are already device pixels: the caller wraps the logical values in SZ() */
static HWND mkf(DWORD ex, const char *cls, const char *txt, DWORD style,
                int x, int y, int w, int h, HWND parent, int id)
{
    HWND c = CreateWindowEx(ex, cls, txt, WS_CHILD | WS_VISIBLE | style,
                            x, y, w, h, parent,
                            id ? (HMENU)(INT_PTR)id : NULL, NULL, NULL);
    if (c && g_font) SendMessage(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

/* a cubestring is one 54-char line, so no paste or stray Enter may slip a newline in */
static void strip_crlf(char *s)
{
    char *o = s;
    for (char *p = s; *p; p++)
        if (*p != '\r' && *p != '\n') *o++ = *p;
    *o = 0;
}

static void box_text(HWND w, char *buf, size_t cap)
{
    GetWindowText(w, buf, (int)cap);
    strip_crlf(buf);
}

/* EM_SETRECT only takes effect on a multi-line edit, hence the cubestring box's ES_MULTILINE */
static void center_edit_v(HWND e)
{
    RECT rc; GetClientRect(e, &rc);
    HDC dc = GetDC(e);
    HFONT f = (HFONT)SendMessage(e, WM_GETFONT, 0, 0);
    HFONT old = f ? SelectObject(dc, f) : NULL;
    TEXTMETRIC tm; GetTextMetrics(dc, &tm);
    if (old) SelectObject(dc, old);
    ReleaseDC(e, dc);
    int lh = tm.tmHeight;
    int top = (rc.bottom - rc.top - lh) / 2;
    if (top < 0) top = 0;
    RECT fmt = { rc.left, top, rc.right, top + lh };
    SendMessage(e, EM_SETRECT, 0, (LPARAM)&fmt);
}

/* A subclassed edit wanting its parent's button pressed: post the click (the depth box's Enter) */
static void click_parent(HWND w, UINT cmd)
{
    PostMessage(GetParent(w), WM_COMMAND, MAKEWPARAM(cmd, BN_CLICKED), 0);
}

/* Keep an input box a single line: swallow Enter, so the multi-line edit never grows a row */
static LRESULT CALLBACK cs_sub(HWND w, UINT m, WPARAM wp, LPARAM lp,
                               UINT_PTR id, DWORD_PTR data)
{
    (void)id; (void)data;
    if (m == WM_KEYDOWN && wp == VK_RETURN) return 0;
    if (m == WM_CHAR && (wp == '\r' || wp == '\n')) return 0;
    /* the box holds one line, so what is pasted joins before "R U" and "R'" read as one move */
    if (m == WM_PASTE) {
        LRESULT r = DefSubclassProc(w, m, wp, lp);
        char buf[SCRAMBLE_MAX + 1];
        GetWindowText(w, buf, sizeof buf);
        for (char *p = buf; *p; p++)
            if (*p == '\r' || *p == '\n') *p = ' ';
        SetWindowText(w, buf);
        SendMessage(w, EM_SETSEL, 0, -1);
        return r;
    }
    return DefSubclassProc(w, m, wp, lp);
}

/* the box never shows a depth run_job() would refuse */
static void depth_clamp(HWND w)
{
    char buf[16];
    GetWindowText(w, buf, sizeof buf);
    /* parse_int, as read_req does: a paste the box holds but the panel would refuse is no depth */
    int v = 0;
    if (!buf[0] || (!parse_int(buf, &v) && v <= MAX_DEPTH)) return;
    snprintf(buf, sizeof buf, "%d", MAX_DEPTH);
    SetWindowText(w, buf);
    SendMessage(w, EM_SETSEL, 0, -1);
}

static LRESULT CALLBACK depth_sub(HWND w, UINT m, WPARAM wp, LPARAM lp,
                                  UINT_PTR id, DWORD_PTR data)
{
    (void)id; (void)data;
    if (m == WM_KEYDOWN && wp == VK_RETURN) {
        click_parent(w, ID_SOLVE);
        return 0;
    }
    if (m == WM_CHAR && wp >= ' ' && (wp < '0' || wp > '9')) return 0;
    if (m == WM_CHAR || m == WM_PASTE) {
        LRESULT r = DefSubclassProc(w, m, wp, lp);
        depth_clamp(w);
        return r;
    }
    return DefSubclassProc(w, m, wp, lp);
}

static void copy_selected(HWND hw, int quiet);

/* a listbox has no copy of its own; quiet, like the double-click */
static LRESULT CALLBACK list_sub(HWND w, UINT m, WPARAM wp, LPARAM lp,
                                 UINT_PTR id, DWORD_PTR data)
{
    (void)id; (void)data;
    if (m == WM_KEYDOWN && wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        copy_selected(GetParent(w), 1);
        return 0;
    }
    return DefSubclassProc(w, m, wp, lp);
}

/* ---- remembered panel settings ---- */
/* one key=value per line; a missing file or an unknown key leaves the default */
#define INI_FILE "cubolt.ini"

static int  g_ini_on;                  /* ini_load has run, so WM_DESTROY can write one back */
/* The machine is going down: WM_DESTROY gets the seconds Windows gives it, not the search's. */
static int  g_ending;

static char ini_path[MAX_PATH + 32] = INI_FILE;   /* ini_pick_path() may prepend a directory */

/* Program Files is shared: an ini beside the exe there is every user's at once. */
static int under_program_files(const char *dir)
{
    char pf[MAX_PATH];
    /* S_OK is 0: this one answers an HRESULT, not a BOOL */
    if (SHGetFolderPathA(NULL, CSIDL_PROGRAM_FILES, NULL, SHGFP_TYPE_CURRENT, pf) != S_OK ||
        !*pf)
        return 0;
    size_t n = strlen(pf);
    return _strnicmp(dir, pf, n) == 0 && (dir[n] == '\\' || dir[n] == 0);
}

/* Program Files cannot be written or virtualized at 64 bits: every setting would reset */
static void ini_pick_path(void)
{
    /* an ini already there wins even unwritable: a read-only one must not redirect to AppData. */
    if (GetFileAttributesA(INI_FILE) != INVALID_FILE_ATTRIBUTES) return;

    /* knowing a file can be made here is not worth leaving one behind */
    if (!under_program_files(exe_dir())) {
        FILE *probe = fopen(INI_FILE, "a");
        if (probe) {
            fclose(probe);
            /* a failed remove leaves an empty ini, and the next start keeps saving into it */
            if (!remove(INI_FILE)) return;
        }
    }

    char app[MAX_PATH];
    if (!ExpandEnvironmentStringsA("%APPDATA%", app, sizeof app) || !*app) return;
    char dir[MAX_PATH + 16];                /* MAX_PATH, then "\" and CUBOLT_NAME */
    snprintf(dir, sizeof dir, "%s\\%s", app, CUBOLT_NAME);
    /* left alone when it cannot be made: a path into a missing directory loses every setting */
    if (!make_one_dir(dir)) return;
    snprintf(ini_path, sizeof ini_path, "%s\\%s", dir, INI_FILE);
}
/* the built-in defaults, and what --layout, which skips the file, builds from */
static char ini_subgroup[SUBGROUP_N + 1] = FACE_LET;
static char ini_depth[24]    = CUBOLT_STR_(DEFAULT_DEPTH);
static int  ini_only_optimal = 0;
static int  ini_sort         = 1;
static int  ini_threads      = 0;

/* the panel's own letters, without repeats; whether they name a usable turn is the engine's call */
static int subgroup_ok(const char *s)
{
    /* empty is a panel with every box cleared: it cannot solve, but it is the state left there */
    for (const char *p = s; *p; p++) {
        if (!strchr(SUBGROUP_LETTERS, *p)) return 0;
        if (strchr(s, *p) != p) return 0;
    }
    return 1;
}

static void ini_load(void)
{
    g_ini_on = 1;

    ini_pick_path();
    FILE *f = fopen(ini_path, "r");
    if (!f) return;
    skip_bom(f);
    char line[192];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        strip_crlf(v);
        /* the key keeps its own spaces unless they come off too: "depth = 13" was dropped */
        size_t kn = strlen(line);
        while (kn && (line[kn - 1] == ' ' || line[kn - 1] == '\t')) line[--kn] = 0;
        if (!strcmp(line, "subgroup")) {
            if (subgroup_ok(v)) snprintf(ini_subgroup, sizeof ini_subgroup, "%s", v);
        } else if (!strcmp(line, "depth")) {
            /* 0 is a depth of its own, the empty maneuver, so ini_save's 0 must come back */
            int d;
            if (!parse_int(v, &d) && d >= 0 && d <= MAX_DEPTH)
                snprintf(ini_depth, sizeof ini_depth, "%d", d);
        } else if (!strcmp(line, "only_optimal")) {
            int o;
            if (!parse_int(v, &o)) ini_only_optimal = o != 0;
        } else if (!strcmp(line, "sort")) {
            int s;
            if (!parse_int(v, &s)) ini_sort = s != 0;
        } else if (!strcmp(line, "threads")) {
            int t;
            if (!parse_int(v, &t) && t >= 0) ini_threads = t > THREAD_CAP ? THREAD_CAP : t;
        }
    }
    fclose(f);
}

/* the depth comes back from its box: what is remembered is the state the user last saw */
static void ini_save(void)
{
    /* one wording for both failures: either way the next run opens without these settings */
    static const char lost[] = "The settings of this run could not be saved, so the next one "
                               "opens with the defaults.";
    FILE *f = fopen(ini_path, "w");
    if (!f) {
        /* stderr is NUL for most of this window's life, so a message box is what is seen */
        ui_msg(g.hwnd, lost, CUBOLT_NAME, MB_OK | MB_ICONWARNING);
        fprintf(stderr, "cannot write %s (error %lu): the settings of this run are not kept\n",
                ini_path, (unsigned long)GetLastError());
        return;
    }
    char sg[SUBGROUP_N + 1], dep[24];
    subgroup_str(sg);
    box_text(g.depth, dep, sizeof dep);
    fprintf(f, "subgroup=%s\n", sg);
    fprintf(f, "depth=%s\n", dep);
    fprintf(f, "only_optimal=%d\n", g.only_optimal);
    fprintf(f, "sort=%d\n", g.sort_solutions);
    fprintf(f, "threads=%d\n", g_threads);
    /* a truncated file reads back as settings it does not hold, "depth=1" for "depth=13" */
    if (stream_lost(f, 0)) {
        remove(ini_path);
        ui_msg(g.hwnd, lost, CUBOLT_NAME, MB_OK | MB_ICONWARNING);
    }
}

static void build_ui(HWND hw, HINSTANCE hinst)
{
    int cw = 12 * A + SZ(20);
    int chh = 9 * A + SZ(20);
    int x = SZ(8) + cw + SZ(12);

    InitializeCriticalSection(&g_pend_cs);

    g.canvas = CreateWindow(CANVAS_CLASS, NULL, WS_CHILD | WS_VISIBLE,
                            SZ(8), SZ(8), cw, chh, hw, NULL, hinst, NULL);
    g.curcol = 0;                    /* U is picked to begin with */
    g_cur_pick  = dropper_make(13, 51);
    g_cur_paint = bucket_make(12, 30, face_color(FACES[g.curcol]));
    /* ticked to begin with: the laws answer as the user paints, unless he unticks it */
    g.autocpl = 1;

    /* its color is set in WM_CTLCOLORSTATIC, not here */
    g.netstatus = mkf(0, "STATIC", NET_OK_TEXT, SS_LEFT,
                      SZ(8), SZ(8) + chh + SZ(4), cw, SZ(18), hw, ID_NETSTAT);

    /* the log fills the empty column the net leaves at the bottom left */
    int net_bottom = SZ(8) + chh + SZ(NET_STATUS_H);
    int log_top    = net_bottom + SZ(30);
    mkf(0, "STATIC", "Log", 0, SZ(8), net_bottom + SZ(8), SZ(60), SZ(18), hw, 0);
    mkf(0, "BUTTON", "Clear log", 0, SZ(8) + cw - SZ(140), net_bottom + SZ(4),
        SZ(140), SZ(26), hw, ID_CLEARLOG);
    g.log = mkf(WS_EX_CLIENTEDGE, "EDIT", NULL,
                ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
                SZ(8), log_top, cw, client_h() - SZ(NET_STATUS_H) - SZ(8) - log_top, hw, ID_LOG);
    /* an EDIT starts with a 32 KB limit, below what a search's log holds */
    SendMessage(g.log, EM_LIMITTEXT, 0, 0);

    /* group boxes, made before their children so those paint on top */
    mkf(0, "BUTTON", "", BS_GROUPBOX, x, SZ(0),   SZ(PANEL_W), SZ(142), hw, 0);
    mkf(0, "BUTTON", "", BS_GROUPBOX, x, SZ(144), SZ(PANEL_W), SZ(164), hw, 0);
    mkf(0, "BUTTON", "", BS_GROUPBOX, x, SZ(316), SZ(PANEL_W),
        client_h() - SZ(NET_STATUS_H) - SZ(8) + SZ(6) - SZ(316), hw, 0);

    mkf(0, "STATIC", "Scramble", 0,
        x, SZ(6), SZ(300), SZ(18), hw, 0);
    g.scramble = mkf(WS_EX_CLIENTEDGE, "EDIT", "", ES_MULTILINE | ES_AUTOHSCROLL,
                     x, SZ(26), SZ(PANEL_W), SZ(26), hw, ID_SCRAMBLE);
    SendMessage(g.scramble, EM_LIMITTEXT, SCRAMBLE_MAX, 0);
    {
        /* match the cubestring box */
        SendMessage(g.scramble, WM_SETFONT, (WPARAM)g_mono_font, TRUE);
        center_edit_v(g.scramble);
        SetWindowSubclass(g.scramble, cs_sub, 0, 0);
    }

    mkf(0, "STATIC", "Cube definition string", 0,
        x, SZ(58), SZ(300), SZ(18), hw, 0);
    g.cubestring = mkf(WS_EX_CLIENTEDGE, "EDIT", CUBE_SOLVED,
                       ES_MULTILINE | ES_AUTOHSCROLL,
                       x, SZ(78), SZ(PANEL_W), SZ(26), hw, ID_CUBESTR);
    /* the sticker count, not the 159 box_text reads: the hint would name its own count */
    SendMessage(g.cubestring, EM_LIMITTEXT, CS_STICKERS, 0);
    {
        /* PANEL_W fits all 54 stickers, so the mono font shaves none of them off */
        SendMessage(g.cubestring, WM_SETFONT, (WPARAM)g_mono_font, TRUE);
        center_edit_v(g.cubestring);
        SetWindowSubclass(g.cubestring, cs_sub, 0, 0);
    }
    /* Clear leaves the six centers; Reset puts the definition (and, live, the net) back to
       solved.  The hint beside them speaks for whichever ran. */
    mkf(0, "BUTTON", "Clear", 0, x, SZ(110), SZ(110), SZ(26), hw, ID_CLEARCS);
    mkf(0, "BUTTON", "Reset", 0, x + SZ(118), SZ(110), SZ(110), SZ(26), hw, ID_RESETCS);
    g.inphint = mkf(0, "STATIC", "", SS_LEFT,
                    x + SZ(246), SZ(116), SZ(PANEL_W) - SZ(246), SZ(18), hw, ID_INPHINT);

    mkf(0, "STATIC", "Subgroup", 0, x, SZ(146), SZ(90), SZ(18), hw, 0);
    for (int i = 0; i < SUBGROUP_N; i++) {
        int row = i / 6, col = i % 6;
        char lab[2] = { SUBGROUP_LETTERS[i], 0 };
        mkf(0, "BUTTON", lab, BS_AUTOCHECKBOX,
            x + col * SZ(48), SZ(168) + row * SZ(28),
            SZ(46), SZ(26), hw, ID_SUB_FIRST + i);
        if (strchr(ini_subgroup, SUBGROUP_LETTERS[i]))
            CheckDlgButton(hw, ID_SUB_FIRST + i, BST_CHECKED);
    }

    mkf(0, "STATIC", DEPTH_TITLE, 0, x, SZ(256), SZ(90), SZ(18), hw, 0);
    g.depth = mkf(WS_EX_CLIENTEDGE, "EDIT", ini_depth, ES_AUTOHSCROLL,
                  x + SZ(98), SZ(254), SZ(50), SZ(26), hw, ID_DEPTH);
    SendMessage(g.depth, EM_LIMITTEXT, DEPTH_CHARS, 0);
    SetWindowSubclass(g.depth, depth_sub, 0, 0);

    g.only_optimal = ini_only_optimal;
    g.hw_onlyopt = mkf(0, "BUTTON", "Only optimal", BS_AUTOCHECKBOX,
                       x, SZ(288), SZ(150), SZ(26), hw, ID_ONLYOPT);
    if (g.only_optimal) CheckDlgButton(hw, ID_ONLYOPT, BST_CHECKED);

    g.sort_solutions = ini_sort;
    g.hw_sort = mkf(0, "BUTTON", "Sort solutions", BS_AUTOCHECKBOX,
                    x + SZ(156), SZ(288), SZ(190), SZ(26), hw, ID_SORT);
    CheckDlgButton(hw, ID_SORT, g.sort_solutions ? BST_CHECKED : BST_UNCHECKED);

    /* the thread choice is only read when Option > Threads opens or a search starts */
    g_threads = ini_threads;

    /* actions, owner-drawn so Solve/Stop read as accent buttons (see WM_DRAWITEM) */
    g.solve_btn = mkf(0, "BUTTON", SOLVE_TITLE, BS_OWNERDRAW,
                      x, SZ(320), SZ(220), SZ(40), hw, ID_SOLVE);
    g.stop_btn  = mkf(0, "BUTTON", "Stop",  BS_OWNERDRAW,
                      x + SZ(228), SZ(320), SZ(152), SZ(40), hw, ID_STOP);
    EnableWindow(g.stop_btn, FALSE);

    mkf(0, "STATIC", "Solutions (dbl-click or Ctrl+C copies)", 0,
        x, SZ(372), SZ(PANEL_W), SZ(18), hw, 0);
    /* LBS_NOINTEGRALHEIGHT: otherwise the height rounds down and leaves a dead band */
    g.list = mkf(WS_EX_CLIENTEDGE, "LISTBOX", NULL,
                 LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_HSCROLL,
                 x, SZ(LIST_TOP), SZ(PANEL_W), g_list_h, hw, ID_LIST);
    {
        /* fixed-pitch font, plus a wide horizontal extent so a long solution scrolls into view */
        g_list_font = CreateFont(-SZ(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                0, 0, 0, FIXED_PITCH | FF_MODERN, MONO_FACE);
        SendMessage(g.list, WM_SETFONT, (WPARAM)g_list_font, TRUE);
        SendMessage(g.list, LB_SETHORIZONTALEXTENT, SZ(4000), 0);
    }
    SetWindowSubclass(g.list, list_sub, 0, 0);

    /* the copy row sits under the list; Save shares File > Save solutions' command id */
    int copy_y = SZ(LIST_TOP) + g_list_h + SZ(COPY_GAP);
    mkf(0, "BUTTON", "Copy selected", 0, x, copy_y, SZ(150), SZ(COPY_H), hw, ID_COPYSEL);
    mkf(0, "BUTTON", COPY_ALL_TITLE, 0, x + SZ(158), copy_y, SZ(150), SZ(COPY_H), hw, ID_COPYALL);
    mkf(0, "BUTTON", "Save as .txt...", 0, x + SZ(316), copy_y, SZ(140), SZ(COPY_H), hw, IDM_SAVE);

    int sb_y = client_h() - SZ(NET_STATUS_H);
    /* WS_CHILD | WS_VISIBLE: without them this is an owned popup that is never shown */
    g.status = CreateWindowEx(0, STATUSCLASSNAME, "", WS_CHILD | WS_VISIBLE,
                              0, sb_y, client_w(), SZ(NET_STATUS_H), hw, (HMENU)ID_STATUS,
                              hinst, NULL);

    /* the first-run build's bar, in the right-hand pane; WM_TABLES_READY gives the strip back */
    {
        int parts[2] = { client_w() - SZ(280), -1 };
        SendMessage(g.status, SB_SETPARTS, 2, (LPARAM)parts);
        RECT pr;
        SendMessage(g.status, SB_GETRECT, 1, (LPARAM)&pr);
        g.prog = CreateWindowEx(0, PROGRESS_CLASS, NULL, WS_CHILD | PBS_SMOOTH,
                                pr.left + SZ(3), pr.top + SZ(4),
                                (pr.right - pr.left) - SZ(6), (pr.bottom - pr.top) - SZ(8),
                                g.status, (HMENU)ID_PROG, hinst, NULL);
        SendMessage(g.prog, PBM_SETRANGE32, 0, 1000);
    }

    /* made once for the window: a drag stutters if these are made per WM_PAINT */
    g_net_pen  = CreatePen(PS_SOLID, 1, RGB(0x33, 0x33, 0x33));
    /* the tick in the Auto complete box, in the accent the Solve button carries */
    g_chk_pen  = CreatePen(PS_SOLID, SZ(2), BTN_BASE[1]);
    g_net_font = CreateFont(-SZ(15), 0, 0, 0, FW_BOLD, 0, 0, 0,
                            DEFAULT_CHARSET, 0, 0, 0, 0, "Arial");
    for (int i = 0; i < 7; i++) g_face_brush[i] = CreateSolidBrush(face_color(FACES[i]));
    for (int i = 0; i < BTN_N; i++) {
        g_btn_pen[i]           = CreatePen(PS_SOLID, 1, darken(BTN_BASE[i], 70));
        g_btn_brush[i * 2]     = CreateSolidBrush(BTN_BASE[i]);
        g_btn_brush[i * 2 + 1] = CreateSolidBrush(darken(BTN_BASE[i], 82));
    }
}

/* a double-click asks for the same thing; only the button complains when nothing is selected */
static void copy_selected(HWND hw, int quiet)
{
    int sel = (int)SendMessage(g.list, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= g.nsol) {
        if (!quiet) ui_msg(hw, "Select a solution first.", "Nothing selected", MB_OK);
        return;
    }
    if (!set_clipboard(g.solutions[sel])) {
        if (!quiet) ui_msg(hw, CLIP_FAIL_MSG, COPY_TITLE,
                           MB_OK | MB_ICONERROR);
        return;
    }
    log_appendf("copied: %s\n", g.solutions[sel]);
}

static HMENU build_thread_menu(void)
{
    HMENU thr = CreatePopupMenu();
    int n = cpu_count();
    char lab[32];
    AppendMenu(thr, MF_STRING, IDM_THR_AUTO, "&Auto (all cores)");
    for (int v = 1; v <= n; v *= 2) {
        snprintf(lab, sizeof lab, "%d", v);
        AppendMenu(thr, MF_STRING, IDM_THR_BASE + v, lab);
    }
    if (n & (n - 1)) {
        snprintf(lab, sizeof lab, "%d", n);
        AppendMenu(thr, MF_STRING, IDM_THR_BASE + n, lab);
    }
    return thr;
}

/* ---- menu bar (five menus, after CubeExplorer's File/Edit/Run/Option/Help layout) ---- */
static HMENU build_menu(void)
{
    HMENU bar = CreateMenu();

    HMENU file = CreatePopupMenu();
    AppendMenu(file, MF_STRING, IDM_SAVE, "&Save solutions...\tCtrl+S");
    AppendMenu(file, MF_STRING, IDM_BATCH, "Batch &solve...");
    AppendMenu(file, MF_SEPARATOR, 0, NULL);
    AppendMenu(file, MF_STRING, IDM_EXIT, "E&xit");
    AppendMenu(bar, MF_POPUP, (UINT_PTR)file, "&File");

    HMENU edit = CreatePopupMenu();
    AppendMenu(edit, MF_STRING, IDM_WCA, "&Generate WCA Scramble");
    AppendMenu(edit, MF_SEPARATOR, 0, NULL);
    AppendMenu(edit, MF_STRING, IDM_COPYCS, "&Copy Cubestring");
    AppendMenu(edit, MF_STRING, IDM_COPYSCR, "Copy &Scramble");
    AppendMenu(edit, MF_SEPARATOR, 0, NULL);
    AppendMenu(edit, MF_STRING, ID_CLEARLOG, "Clear &Log");
    AppendMenu(bar, MF_POPUP, (UINT_PTR)edit, "&Edit");

    HMENU run = CreatePopupMenu();
    AppendMenu(run, MF_STRING, ID_SOLVE, "&Solve\tF9");
    AppendMenu(run, MF_STRING, ID_STOP, "Sto&p");
    AppendMenu(bar, MF_POPUP, (UINT_PTR)run, "&Run");

    HMENU option = CreatePopupMenu();
    AppendMenu(option, MF_STRING, IDM_ONLYOPT, "Only &optimal");
    AppendMenu(option, MF_STRING, IDM_SORT, "&Sort solutions");
    AppendMenu(option, MF_SEPARATOR, 0, NULL);
    AppendMenu(option, MF_POPUP, (UINT_PTR)build_thread_menu(), "&Threads");
    AppendMenu(bar, MF_POPUP, (UINT_PTR)option, "&Option");

    HMENU help = CreatePopupMenu();
    AppendMenu(help, MF_STRING, IDM_CONTENTS, "&Contents");
    AppendMenu(help, MF_SEPARATOR, 0, NULL);
    AppendMenu(help, MF_STRING, IDM_UPDATE, "Check for &Updates...");
    AppendMenu(help, MF_STRING, IDM_ABOUT, "&About...");
    AppendMenu(bar, MF_POPUP, (UINT_PTR)help, "&Help");
    return bar;
}

/* 20 random face turns, no repeated face and never three same-axis in a row (no "R L R") */
static void gen_wca_scramble(char *out, size_t cap)
{
    /* Per process, not per second: two instances opened together drew the same sequence. */
    static int seeded = 0;
    if (!seeded) { srand((unsigned)(GetTickCount64() ^ GetCurrentProcessId())); seeded = 1; }
    static const int  AXI[6] = {0, 1, 2, 0, 1, 2};   /* FACES is URFDLB: U/D, R/L, F/B */
    int prev = -1, prevprev = -1, pos = 0;
    for (int i = 0; i < 20; i++) {
        int f;
        do {
            f = rand() % 6;
        } while (f == prev
                 || (prev >= 0 && prevprev >= 0
                     && AXI[f] == AXI[prev] && AXI[prev] == AXI[prevprev]));
        prevprev = prev;
        prev = f;
        /* snprintf returns the wanted length, so this guard is what keeps pos inside cap */
        if ((size_t)pos + 4 > cap) break;
        pos += snprintf(out + pos, cap - (size_t)pos, "%c%s ", FACES[f],
                        (const char *[]){"", "2", "'"}[rand() % 3]);
    }
    if (pos > 0) out[pos - 1] = 0;
}

#include "docpanel.h"

/* every solution to a text file, the ones the box dropped included */
#define SAVE_TITLE "Save solutions"
static void save_solutions(HWND hw)
{
    if (!g.nsol) {
        ui_msg(hw, "There are no solutions to save yet.", SAVE_TITLE, MB_OK);
        return;
    }
    char path[MAX_PATH] = "solutions.txt";
    OPENFILENAME of;
    memset(&of, 0, sizeof of);
    of.lStructSize  = sizeof of;
    of.hwndOwner    = hw;
    of.lpstrFile    = path;
    of.nMaxFile     = MAX_PATH;
    of.lpstrFilter  = "Text files (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
    of.nFilterIndex = 1;
    of.lpstrDefExt  = "txt";
    /* OFN_NOCHANGEDIR: the CWD is the exe's directory, which is where the tables live. */
    of.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!GetSaveFileName(&of)) return;

    /* "wb", like the batch files */
    FILE *f = fopen(path, "wb");
    if (!f) {
        ui_msg(hw, "The file could not be opened for writing.",
                   SAVE_TITLE, MB_OK | MB_ICONERROR);
        return;
    }
    /* check both: a stream can fail with fclose still reporting success */
    int bad = 0;
    for (int i = 0; i < g.nsol; i++)
        if (fprintf(f, "%s\n", g.solutions[i]) < 0) { bad = 1; break; }
    bad = stream_lost(f, bad);
    /* Half a file must not pass for a saved answer list. */
    if (bad) remove(path);
    char m[MAX_PATH + 64];
    if (bad) {
        ui_msg(hw, "The file could not be written in full.", SAVE_TITLE,
               MB_OK | MB_ICONERROR);
        snprintf(m, sizeof m, "the solutions were not all written to %s\n", path);
    } else {
        snprintf(m, sizeof m, "saved %d solution(s)%s to %s\n", g.nsol, SO_FAR(g.busy), path);
    }
    log_append(m);
}


#include "guibatch.h"

/* ---- a file dropped on the window ---- */
#define DROP_TITLE "Drop ignored"
/* JSON, or more than one non-blank line, is a batch input; one line is a cube or a maneuver */
static void drop_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ui_msg(g.hwnd, "The dropped file could not be opened.", DROP_TITLE, MB_OK);
        return;
    }
    char head[4096 + 1];
    size_t n = fread(head, 1, 4096, f);
    int bigger = fgetc(f) != EOF;         /* anything past the head is never one case */
    fclose(f);
    head[n] = 0;
    if (n == 0) {
        ui_msg(g.hwnd, "The dropped file is empty.", DROP_TITLE, MB_OK);
        return;
    }

    char *p = head;
    if (n >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB &&
        (unsigned char)p[2] == 0xBF)
        p += 3;                            /* UTF-8 BOM */
    int lines = 0, json = 0, too_long = 0;
    char line[512];
    for (char *q = p; *q; ) {
        char *nl = strchr(q, '\n');
        const char *s = q, *e = nl ? nl : q + strlen(q);
        while (s < e && (*s == ' ' || *s == '\t' || *s == '\r')) s++;
        while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;
        if (s < e) {
            if (!lines) {
                size_t len = (size_t)(e - s);
                json = (*s == '{' || *s == '[');
                /* the box holds SCRAMBLE_MAX: a longer line is cut off */
                too_long = len > SCRAMBLE_MAX;
                if (too_long) len = SCRAMBLE_MAX;
                memcpy(line, s, len);
                line[len] = 0;
            }
            lines++;
        }
        if (!nl) break;
        q = nl + 1;
    }

    /* not empty, yet nothing was read out of it: a text encoding this does not read, say */
    if (lines == 0) {
        ui_msg(g.hwnd, "No case could be read from the dropped file.", DROP_TITLE, MB_OK);
        return;
    }
    /* not one plain case: JSON, several lines, a tail past the head, too long, or "name," */
    if (json || lines > 1 || bigger || too_long || strchr(line, ',')) {
        const char *out = batch_out_dir();
        if (!out) {
            ui_msg(g.hwnd, "The output folder could not be made next to the program.",
                   BATCH_TITLE, MB_OK | MB_ICONWARNING);
            return;
        }
        log_append("dropped batch input: starting it with the panel's settings.\n");
        begin_batch(path, out);
        return;
    }

    /* A 54-char line is a cube only if it parses: cube_scramble takes either spelling */
    char out[55];
    if (strlen(line) == CS_STICKERS) {
        char reason[256];
        if (!cube_problem(line, reason, sizeof reason)) {
            show_cs(line);
            log_append("dropped cubestring loaded into the net.\n");
            return;
        }
    }
    const char *err = cube_scramble(line, out);
    if (err) {
        ui_msg(g.hwnd, err, "Dropped file ignored", MB_OK);
        return;
    }
    SetWindowText(g.scramble, line);       /* no EN_CHANGE from a drop, so the box is set here */
    show_cs(out);
    log_append("dropped maneuver applied to the net.\n");
}

static void draw_owner_button(LPDRAWITEMSTRUCT dis)
{
    HWND btn = dis->hwndItem;
    char txt[64];
    GetWindowText(btn, txt, sizeof txt);
    RECT r = dis->rcItem;
    int id = GetDlgCtrlID(btn);
    int disabled = (dis->itemState & ODS_DISABLED) != 0;
    int pressed  = (dis->itemState & ODS_SELECTED) != 0;

    /* a disabled button paints gray whatever it is; the accents are for one you can press */
    int which = disabled ? 0 : (id == ID_SOLVE ? 1 : 2);
    HDC hdc = dis->hDC;
    HPEN pen = g_btn_pen[which];
    HBRUSH br = g_btn_brush[which * 2 + (pressed ? 1 : 0)];
    if (!pen || !br) return;
    HGDIOBJ old_pen = SelectObject(hdc, pen);
    HGDIOBJ old_brush = SelectObject(hdc, br);
    int rad = (r.bottom - r.top) / 4;
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, rad, rad);
    if (old_brush) SelectObject(hdc, old_brush);
    if (old_pen)   SelectObject(hdc, old_pen);

    SetBkMode(hdc, TRANSPARENT);
    /* the disabled accent is the light one, so its label is the one label that goes dark */
    SetTextColor(hdc, disabled ? darken(BTN_BASE[0], 38) : RGB(255, 255, 255));
    HFONT f = (HFONT)SendMessage(btn, WM_GETFONT, 0, 0);
    if (!f) f = g_font;
    HFONT old = (HFONT)SelectObject(hdc, f);
    DrawText(hdc, txt, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (old) SelectObject(hdc, old);
    if (dis->itemState & ODS_FOCUS) DrawFocusRect(hdc, &r);
}

static LRESULT CALLBACK MainProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g.hwnd = hw;
        build_ui(hw, ((LPCREATESTRUCT)lp)->hInstance);
        g.net_bad = -1;           /* unknown, so the first net_status() paints the line */
        net_status();
        set_status(g_need_build ? "Building solver tables (first run)..."
                                : "Loading solver tables...");
        if (g_need_build)
            log_append("Solver tables are missing -- cubolt.exe is building them in the "
                       "background (one-time; the large pruning tables dominate the time). "
                       "Keep this window open.\n");
        else
            log_append("Tables are loading in the background.\n");
        SetTimer(g.hwnd, ID_TIMER_LOG, LOG_POLL_MS, NULL);
        if (g_need_build) ShowWindow(g.prog, SW_SHOW);
        if (!g_skip_preload) {
            subgroup_str(g_defsub);
            log_redirect_trunc();
            g_worker = start_worker(preload_worker, "Solver tables");
        }
        return 0;
    case WM_COMMAND: {
        int id = (int)LOWORD(wp);
        if (id > IDM_THR_BASE && id <= IDM_THR_LAST) {
            g_threads = id - IDM_THR_BASE;
            log_appendf("next solve: threads %d.\n", g_threads);
            return 0;
        }
        switch (id) {
        case ID_CLEARCS:
        case ID_RESETCS:
            /* an old scramble would disagree with the net */
            input_hint(NULL, 0);
            SetWindowText(g.scramble, "");
            show_cs(id == ID_CLEARCS ? CUBE_CLEARED : CUBE_SOLVED);
            break;
        case ID_SCRAMBLE:
            /* live: the box, the picture and Solve agree; an unreadable move goes to the hint */
            if (HIWORD(wp) == EN_CHANGE) input_hint(scramble_into_net(), 1);
            break;
        case ID_CUBESTR:
            if (HIWORD(wp) == EN_CHANGE) { load_cs_box(); net_status(); }
            break;
        case ID_ONLYOPT:
            g.only_optimal = IsDlgButtonChecked(hw, ID_ONLYOPT);
            break;
        case ID_SORT:
            g.sort_solutions = IsDlgButtonChecked(hw, ID_SORT);
            log_append(g.sort_solutions
                           ? "next solve: solutions sorted by (length, move order).\n"
                           : "next solve: solutions in discovery order.\n");
            break;
        case ID_SOLVE:
            start_solve();
            break;
        case ID_STOP:
            /* the menu item stays enabled until the walk ends, so a second Stop is ignored here */
            if (g.busy && !engine_stop) {
                poll_log();                       /* keep the log in order before the notice */
                engine_stop = 1;                  /* the walk checks it at every node */
                log_append("\nStop requested.\n");
                EnableWindow(g.stop_btn, FALSE);
            }
            break;
        case ID_COPYSEL:
            copy_selected(hw, 0);
            break;
        case ID_COPYALL: {
            if (!g.nsol) {
                ui_msg(hw, "There are no solutions to copy yet.", COPY_ALL_TITLE, MB_OK);
                break;
            }
            size_t n = 0;
            for (int i = 0; i < g.nsol; i++) n += strlen(g.solutions[i]) + 1;
            char *all = malloc(n + 1);
            if (!all) { ui_msg(hw, "Out of memory.", COPY_ALL_TITLE, MB_OK); break; }
            char *w = all;
            for (int i = 0; i < g.nsol; i++) {
                size_t k = strlen(g.solutions[i]);
                memcpy(w, g.solutions[i], k);
                w[k] = '\n';
                w += k + 1;
            }
            *w = 0;
            int copied = set_clipboard(all);
            free(all);
            char m[64];
            if (copied) snprintf(m, sizeof m, "copied %d solution(s)%s\n", g.nsol, SO_FAR(g.busy));
            else {
                ui_msg(hw, CLIP_FAIL_MSG, COPY_ALL_TITLE, MB_OK | MB_ICONERROR);
                snprintf(m, sizeof m, "%d solution(s) were not copied\n", g.nsol);
            }
            log_append(m);
            break;
        }
        case ID_CLEARLOG: {
            /* only past lines are wiped: poll_log() keeps appending from the current position */
            SetWindowText(g.log, TEXT(""));
            SendMessage(g.log, EM_SCROLLCARET, 0, 0);
            break;
        }
        case ID_LIST:
            if (HIWORD(wp) == LBN_DBLCLK) copy_selected(hw, 1);
            break;
        case IDM_SAVE:
            save_solutions(hw);
            break;
        case IDM_BATCH: {
            char in[MAX_PATH] = "";
            OPENFILENAME of;
            memset(&of, 0, sizeof of);
            of.lStructSize = sizeof of;
            of.hwndOwner   = hw;
            of.lpstrFile   = in;
            of.nMaxFile    = MAX_PATH;
            of.lpstrFilter = "Batch input (*.txt; *.json)\0*.txt;*.json\0"
                             "All files (*.*)\0*.*\0";
            of.lpstrTitle  = "Batch input: JSON, or one cubestring/maneuver per line";
            of.nFilterIndex = 1;
            of.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
            if (!GetOpenFileName(&of)) break;
            char out[MAX_PATH];
            if (!browse_folder(hw, out)) break;
            begin_batch(in, out);
            break;
        }
        case IDM_EXIT:
            PostMessage(hw, WM_CLOSE, 0, 0);
            break;
        case IDM_ABOUT:
            show_about(hw);
            break;
        case IDM_CONTENTS:
            show_contents(hw);
            break;
        case IDM_UPDATE: {
            if (g_up_running) { set_status("A check for updates is still running."); break; }
            g_up_running = 1;
            if (g_up_thread) { CloseHandle(g_up_thread); g_up_thread = NULL; }
            g_up_thread = (HANDLE)_beginthreadex(NULL, 0, update_worker, NULL, 0, NULL);
            if (g_up_thread) set_status("Checking for updates...");
            else {
                g_up_running = 0;
                log_append("could not start the update check.\n");
            }
            break;
        }
        case IDM_THR_AUTO:
            g_threads = 0;
            log_append("next solve: threads auto (one worker per logical core).\n");
            break;
        case IDM_WCA: {
            char s[128];
            gen_wca_scramble(s, sizeof s);
            SetWindowText(g.scramble, s);
            /* SetWindowText raises no EN_CHANGE: apply it here, and clear the hint it left */
            input_hint(scramble_into_net(), 1);
            log_appendf("WCA scramble: %s\n", s);
            break;
        }
        case IDM_COPYCS:
        case IDM_COPYSCR: {
            char buf[256];
            box_text(id == IDM_COPYCS ? g.cubestring : g.scramble, buf, sizeof buf);
            if (set_clipboard(buf))
                log_append(id == IDM_COPYCS ? "cubestring copied to the clipboard.\n"
                                            : "scramble copied to the clipboard.\n");
            else
                ui_msg(hw, CLIP_FAIL_MSG, COPY_TITLE, MB_OK | MB_ICONERROR);
            break;
        }
        case IDM_ONLYOPT:
            /* BS_AUTOCHECKBOX: BM_CLICK toggles and notifies the parent, which reads the value */
            SendMessage(g.hw_onlyopt, BM_CLICK, 0, 0);
            break;
        case IDM_SORT:
            SendMessage(g.hw_sort, BM_CLICK, 0, 0);
            break;
        }
        return 0;
    }
    case WM_INITMENUPOPUP: {
        /* keep the Option check marks and the Run enablement in step with the checkboxes */
        HMENU m = (HMENU)wp;
        CheckMenuItem(m, IDM_ONLYOPT, MF_BYCOMMAND |
                      (IsDlgButtonChecked(hw, ID_ONLYOPT) ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(m, IDM_SORT, MF_BYCOMMAND |
                      (IsDlgButtonChecked(hw, ID_SORT) ? MF_CHECKED : MF_UNCHECKED));
        /* the Threads submenu is one radio group: auto or one of the offered counts */
        CheckMenuRadioItem(m, IDM_THR_AUTO, IDM_THR_LAST,
                           g_threads > 0 ? IDM_THR_BASE + g_threads : IDM_THR_AUTO,
                           MF_BYCOMMAND);
        EnableMenuItem(m, ID_SOLVE, MF_BYCOMMAND | (g.busy ? MF_GRAYED : MF_ENABLED));
        EnableMenuItem(m, ID_STOP,  MF_BYCOMMAND | (g.busy ? MF_ENABLED : MF_GRAYED));
        break;
    }
    case WM_TIMER:          /* polling the log tail is the only timer this window ever sets */
        poll_log();
        pend_drain();
        prog_tick();
        return 0;
    case WM_SOLVE_DONE:
        finish_solve();
        return 0;
    case WM_BATCH_DONE:
        finish_batch(wp);
        return 0;
    case WM_UPDATE_DONE: {
        g_up_running = 0;
        char head[512], buf[sizeof head + sizeof CUBOLT_RELEASES];   /* head, link, the newline */
        const char *link = "";
        const char *why = (const char *)g_up_why, *tag = (const char *)g_up_tag;
        if (g_up_rc) {
            snprintf(head, sizeof head, "Could not check for updates.\n\n%s\n\n"
                     "You can look for a new release yourself:", why);
            link = CUBOLT_RELEASES;
        } else if (ver_cmp(CUBOLT_VERSION, tag) < 0) {
            snprintf(head, sizeof head, "Cubolt %s is out -- you are running %s.",
                     tag, CUBOLT_VERSION);
            link = CUBOLT_RELEASES;
        } else {
            snprintf(head, sizeof head, CUBOLT_NAME " %s is the latest release.", CUBOLT_VERSION);
        }
        snprintf(buf, sizeof buf, *link ? "%s\n%s" : "%s", head, link);
        log_append(buf);
        log_append("\n");
        ui_msg_link(hw, head, CUBOLT_TITLE,
                    MB_OK | (g_up_rc ? MB_ICONWARNING : MB_ICONINFORMATION), link);
        return 0;
    }
    case WM_TABLES_READY:
        /* a worker does not set this itself: only this handler knows it has been joined */
        g.tables_ready = 1;
        /* the same exit a solve takes */
        finish_worker();
        ShowWindow(g.prog, SW_HIDE);
        {
            int full[1] = { -1 };
            SendMessage(g.status, SB_SETPARTS, 1, (LPARAM)full);
        }
        if (g_build_secs > 0.0) log_appendf("Table build: %.1f s in total.\n", g_build_secs);
        log_append("Tables ready. You can Solve now.\n");
        set_status("Tables ready -- you can Solve now.");
        return 0;
    case WM_TABLES_FAIL:
        /* Solve answers BUSY_TABLES until tables_ready, the log holds why; drained first. */
        finish_worker();
        log_append(g_table_fail);
        ShowWindow(g.prog, SW_HIDE);
        set_status("The tables could not be loaded.");
        ui_msg(g.hwnd, "Cubolt could not load its tables.\n\n"
                        "The reason is in the log below.  Most often the tables need building "
                        "again:\n\n    mktables.exe\n\n"
                        "run in the folder cubolt.exe is in.",
                CUBOLT_NAME, MB_OK | MB_ICONERROR);
        return 0;
    case WM_CTLCOLORSTATIC: {
        /* the two labels that redden when the box they sit under names no cube */
        int *bad = (HWND)lp == g.netstatus ? &g.net_bad
                 : (HWND)lp == g.inphint   ? &g.hint_bad : NULL;
        if (!bad) break;
        SetTextColor((HDC)wp, *bad ? RGB(0xa8, 0x2a, 0x1e) : RGB(0x60, 0x60, 0x60));
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lp;
        draw_owner_button(dis);
        return 0;
    }
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        char path[MAX_PATH];
        /* a drop of several says so rather than let the rest look read */
        if (DragQueryFileA(drop, 0xFFFFFFFF, NULL, 0) > 1)
            ui_msg(hw, "Only the first dropped file is opened.", "Drop", MB_OK);
        if (DragQueryFileA(drop, 0, path, MAX_PATH)) drop_file(path);
        DragFinish(drop);
        return 0;
    }
    /* here, not IDM_EXIT: the X skips the menu; a first-run build is a worker g.busy misses */
    case WM_CLOSE:
        /* lParam 1 is the session ending: it has already asked, and cannot be answered */
        if (g_worker && !lp &&
            ui_msg(hw, "A search or table build is still running. Quit anyway?",
                   CUBOLT_NAME, MB_YESNO | MB_ICONWARNING) != IDYES)
            return 0;
        DestroyWindow(hw);
        return 0;
    /* A shutdown cannot wait for an answer: returning FALSE would hold the machine for hours. */
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        /* the window is the one that saves the ini, so the close has to be asked for here */
        if (wp) { g_ending = 1; PostMessage(hw, WM_CLOSE, 0, 1); }
        return 0;
    case WM_DESTROY:
        /* stopped first: what is freed from here releases objects a worker writes into */
        engine_stop = 1;
        mt_stop     = 1;
        /* engine_stop is what ends it, so a shutdown need not wait for the search */
        if (g_worker) { WaitForSingleObject(g_worker, g_ending ? 2000 : INFINITE); worker_close(); }
        /* finish_worker() is the other remover; a quit mid-search never reaches it */
        remove(g_tmplog);
        /* A check that is out is waited for, but not for as long as the network may take. */
        if (g_up_thread) {
            WaitForSingleObject(g_up_thread, 5000);
            CloseHandle(g_up_thread);
            g_up_thread = NULL;
        }
        if (g_ini_on) ini_save();          /* before the controls go away: the box and checkboxes */
        for (int i = 0; i < 7; i++)
            if (g_face_brush[i]) DeleteObject(g_face_brush[i]);
        if (g_net_pen)  DeleteObject(g_net_pen);
        if (g_chk_pen)  DeleteObject(g_chk_pen);
        if (g_cur_pick)  DestroyIcon((HICON)g_cur_pick);
        if (g_cur_paint) DestroyIcon((HICON)g_cur_paint);
        if (g_net_font) DeleteObject(g_net_font);
        if (g_font)      DeleteObject(g_font);
        if (g_mono_font) DeleteObject(g_mono_font);
        if (g_list_font) DeleteObject(g_list_font);
        for (int i = 0; i < F_N; i++)
            if (g_doc_font[i]) DeleteObject(g_doc_font[i]);
        if (g_doc_rule) DeleteObject(g_doc_rule);
        for (int i = 0; i < BTN_N; i++) {
            if (g_btn_pen[i]) DeleteObject(g_btn_pen[i]);
            if (g_btn_brush[i * 2])     DeleteObject(g_btn_brush[i * 2]);
            if (g_btn_brush[i * 2 + 1]) DeleteObject(g_btn_brush[i * 2 + 1]);
        }
        /* the process's, not the window's; a failed big one leaves the small one in both slots */
        if (g_msg_icon_big) DestroyIcon(g_msg_icon_big);
        if (g_msg_icon_sml && g_msg_icon_sml != g_msg_icon_big) DestroyIcon(g_msg_icon_sml);
        g_msg_icon_big = g_msg_icon_sml = NULL;
        /* the pair the class was registered with: no longer needed once the window is gone */
        if (g_class_icon_big) DestroyIcon(g_class_icon_big);
        if (g_class_icon_sml && g_class_icon_sml != g_class_icon_big)
            DestroyIcon(g_class_icon_sml);
        g_class_icon_big = g_class_icon_sml = NULL;
        clear_solutions();
        free(g.solutions);
        g.solutions = NULL;
        g.capsol = 0;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hw, msg, wp, lp);
}

/* ---- headless self-test: verify the whole solve pipeline without a window ---- */
static int self_test(void)
{
    /* 4 = no self-test to report, 3 = a case failed, 2 = the scramble itself did */
    build_once();
    FILE *out = fopen("_selftest.out", "w");
    if (!out) return 4;
    int miss = missing_table_count(NULL);
    if (miss) {
        /* stderr is NUL by the time this runs, so the report is the only place it can be read */
        fprintf(out, "%d table(s) are missing: build them before --selftest\n", miss);
        fclose(out);
        return 4;
    }
    load_shared_tables();
    Job j = job_default();
    char cs[55];
    const char *e = cube_scramble("R U R'", cs);
    if (e) { fprintf(out, "scramble: %s\n", e); fclose(out); return 2; }
    j.subgroup   = FACE_LET;
    j.dump       = 0;
    sol_std      = 1;

    j.cubestring = cs;
    j.depth      = 6;
    int rc = run_job(&j);
    int bad = rc || rend_on == 0;
    if (rc) {
        fprintf(out, RUN_JOB_FAILED_FMT, rc);
    } else {
        fprintf(out, "self-test: %zu standard solution(s)\n", rend_on);
        int show = (int)rend_on < 5 ? (int)rend_on : 5;
        for (int i = 0; i < show; i++) {
            fprintf(out, "  %s\n", rend_text(rend_nth(i)));
        }
    }

    /* the URF corner and the UR edge wiped ('?'): the known rest alone gives the same R U' R' */
    cs[5] = cs[8] = cs[9] = cs[10] = cs[20] = '?';
    j.cubestring = cs;
    j.depth      = DEFAULT_DEPTH;
    rc = run_job(&j);
    int ic_ok = !rc && rend_on > 0 && strcmp(rend_text(rend_nth(0)), "R U' R'") == 0;
    if (ic_ok) fprintf(out, "incomplete self-test: %s\n", rend_text(rend_nth(0)));
    else fprintf(out, "incomplete self-test failed (rc=%d, %zu solution(s))\n",
                 rc, rend_on);

    /* the Auto complete box on one hidden sticker: the laws leave the solved color */
    char hidden[55], filled[55];
    memcpy(hidden, CUBE_SOLVED, 55);
    hidden[0] = '?';
    int nfill = ic_complete(hidden, filled);
    int ac_ok = nfill == 1 && strcmp(filled, CUBE_SOLVED) == 0;
    if (ac_ok) fprintf(out, "auto complete self-test: %d sticker(s)\n", nfill);
    else fprintf(out, "auto complete self-test failed (%d sticker(s) -> %s)\n", nfill, filled);

    if (fclose(out)) return 4;
    return (bad || !ic_ok || !ac_ok) ? 3 : 0;
}

/* ---- headless layout check ---- */
/* walks every child control: is it inside the client rectangle, does its text fit its box */
typedef struct { HWND hw; FILE *out; int bad; } LD;

static BOOL CALLBACK layout_enum(HWND child, LPARAM lp)
{
    LD *d = (LD *)lp;
    RECT r; GetWindowRect(child, &r);
    MapWindowPoints(NULL, d->hw, (POINT *)&r, 2);
    RECT cr; GetClientRect(d->hw, &cr);

    char cls[64] = ""; GetClassName(child, cls, sizeof cls);
    char txt[128] = ""; GetWindowText(child, txt, sizeof txt);
    int  id = GetDlgCtrlID(child);
    int  w = r.right - r.left, h = r.bottom - r.top;

    /* measure the text with the control's own font, to catch clipped captions */
    int need = 0, fits = 1;
    HDC dc = GetDC(child);
    HFONT f = (HFONT)SendMessage(child, WM_GETFONT, 0, 0);
    HFONT old = f ? (HFONT)SelectObject(dc, f) : NULL;
    SIZE sz = {0, 0};
    if (*txt && strlen(txt) < 60) GetTextExtentPoint32(dc, txt, (int)strlen(txt), &sz);
    need = sz.cx;
    if (old) SelectObject(dc, old);
    ReleaseDC(child, dc);

    int is_btn  = (strcmp(cls, "Button") == 0);
    int is_lbl  = (strcmp(cls, "Static") == 0);
    /* single-line text boxes count too: their content must fit (the multiline log wraps) */
    int is_edit = (strcmp(cls, "Edit") == 0) &&
                  !(GetWindowLongPtr(child, GWL_STYLE) & ES_MULTILINE);
    if (is_btn || is_lbl || is_edit) {
        int avail = w - ((is_btn || is_edit) ? 10 : 4);   /* frame + padding allowance */
        if (*txt && need > avail) fits = 0;
    }
    int inside = (r.left >= 0 && r.top >= 0 &&
                  r.right <= cr.right && r.bottom <= cr.bottom);

    fprintf(d->out, "%-8s id=%-3d %5d,%-4d %4dx%-4d need=%-4d %-14s %s\n",
            cls, id, (int)r.left, (int)r.top, w, h, need,
            *txt && strlen(txt) < 60 ? txt : (is_btn || is_lbl ? txt : "-"),
            !inside ? "OUT-OF-CLIENT" : (fits ? "ok" : "TEXT-CLIPPED"));
    if (!inside || !fits) d->bad++;
    return TRUE;
}

static int layout_check(HWND hw)
{
    FILE *f = fopen("_layout.out", "w");
    if (!f) return 1;
    RECT cr; GetClientRect(hw, &cr);
    RECT wr; GetWindowRect(hw, &wr);
    fprintf(f, "dpi=%d  A=%d  list_h=%d\n", g_dpi, A, g_list_h);
    fprintf(f, "work area : %d,%d %dx%d\n", g_wa[0], g_wa[1],
            g_wa[2] - g_wa[0], g_wa[3] - g_wa[1]);
    fprintf(f, "window    : %d,%d %dx%d  (asked %dx%d)\n",
            (int)wr.left, (int)wr.top, (int)(wr.right - wr.left),
            (int)(wr.bottom - wr.top), g_ow, g_oh);
    fprintf(f, "client    : %dx%d\n\n", (int)(cr.right - cr.left),
            (int)(cr.bottom - cr.top));

    /* Measured, not judged: a work area too small for any layout is not a defect in this one. */
    int wfit = (wr.left >= g_wa[0] && wr.top >= g_wa[1] &&
                wr.right <= g_wa[2] && wr.bottom <= g_wa[3]);
    int centered = 1;
    if (g_ow <= g_wa[2] - g_wa[0]) {
        int l = wr.left - g_wa[0], r = g_wa[2] - wr.right;
        if (l - r > 2 || r - l > 2) centered = 0;
    }
    if (g_oh <= g_wa[3] - g_wa[1]) {
        int t = wr.top - g_wa[1], b = g_wa[3] - wr.bottom;
        if (t - b > 2 || b - t > 2) centered = 0;
    }

    LD d = { hw, f, 0 };
    EnumChildWindows(hw, layout_enum, (LPARAM)&d);
    fprintf(f, "\nwindow vs work area     : %s\n", wfit ? "fits" : "does not fit");
    fprintf(f, "window centered         : %s\n", centered ? "yes" : "NO");
    if (!centered) d.bad++;
    fprintf(f, "\n%s (%d problem(s))\n",
            d.bad ? "PROBLEMS FOUND" : "all controls inside the client area, all captions fit",
            d.bad);
    int lost = fclose(f) != 0;
    DestroyWindow(hw);
    return (d.bad || lost) ? 1 : 0;
}

/* Anywhere on the command line, not just in argv[1]: --layout and friends can come first. */
static int arg_given(const char *flag)
{
    for (int i = 1; i < __argc; i++)
        if (!strcmp(__argv[i], flag)) return 1;
    return 0;
}

static void outer_size(DWORD style, int *ow, int *oh)
{
    RECT r = {0, 0, client_w(), client_h()};
    AdjustWindowRect(&r, style, TRUE);
    *ow = r.right - r.left;
    *oh = r.bottom - r.top;
}

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE prev, LPSTR cmdline, int showcmd)
{
    (void)prev; (void)cmdline; (void)showcmd;

    /* a GUI program touching an unattached console makes the CRT allocate one: a black flash */
    FreeConsole();
    detach_log();

    /* the manifest in cubolt.rc turns the visual style on */
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof icc;
    /* ICC_STANDARD_CLASSES does not cover the status bar or the progress bar */
    icc.dwICC  = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);

    /* Per-monitor awareness is cubolt.rc's to give: the manifest wins over this call. */
    SetProcessDPIAware();

    /* exe_dir() keeps the trailing backslash and says so when the path did not fit. */
    SetCurrentDirectory(exe_dir());

    /* the log mirror is named here, not at the exe: that folder is not always writable */
    char tmpdir[MAX_PATH];
    if (GetTempPathA(MAX_PATH, tmpdir))
        snprintf(g_tmplog, sizeof g_tmplog, "%s_cubolt_gui_%lu.tmp", tmpdir,
                 (unsigned long)GetCurrentProcessId());

    /* load_file() exits silently on a missing table; headless runs must not stop at a dialog. */
    if (!arg_given("--layout") && !arg_given("--selftest")) {
        long long bytes = 0;
        int miss = missing_table_count(&bytes);
        if (miss > 0) {
            if (!ask_build_tables(miss, bytes)) return 1;
            g_need_build = 1;
        }
    }

    if (arg_given("--selftest"))
        return self_test();

    /* Scale to the monitor DPI, then back off if the result would not fit the work area */
    HDC sdc = GetDC(NULL);
    g_dpi = GetDeviceCaps(sdc, LOGPIXELSX);
    ReleaseDC(NULL, sdc);
    if (g_dpi < 96) g_dpi = 96;

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT wa; SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
    int workW = wa.right - wa.left, workH = wa.bottom - wa.top;
    g_wa[0] = wa.left; g_wa[1] = wa.top; g_wa[2] = wa.right; g_wa[3] = wa.bottom;
    int ow, oh, wx, wy;
    for (;;) {
        A = SZ(LOGICAL_A);
        g_list_h = SZ(LIST_H);
        outer_size(style, &ow, &oh);
        /* stopped at 100, not 96: the last step from 101 would otherwise land below 96 */
        if ((ow <= workW && oh <= workH) || g_dpi <= 100) break;
        g_dpi -= 4;
    }
    /* the list is what gives the height back */
    while (oh > workH && g_list_h > SZ(LIST_FLOOR)) {
        int nh = g_list_h - (oh - workH);
        if (nh < SZ(LIST_FLOOR)) nh = SZ(LIST_FLOOR);
        if (nh == g_list_h) break;
        g_list_h = nh;
        outer_size(style, &ow, &oh);
    }
    g_ow = ow; g_oh = oh;

    /* clamped to the work area: letting the system cascade place it puts rows below the screen */
    wx = wa.left + (workW - ow) / 2;
    wy = wa.top  + (workH - oh) / 2;
    if (wx < wa.left)            wx = wa.left;
    if (wy < wa.top)             wy = wa.top;
    if (wx + ow > wa.right)      wx = wa.right - ow;
    if (wy + oh > wa.bottom)     wy = wa.bottom - oh;

    g_font = CreateFont(-MulDiv(9, g_dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, UI_FACE);

    /* the scramble and cube-definition boxes share one monospace face so they line up */
    g_mono_font = CreateFont(-SZ(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, MONO_FACE);

    net_from_cs(CUBE_SOLVED);

    /* two sizes, taskbar and title bar; held past WinMain, freed in WM_DESTROY */
    g_class_icon_big = (HICON)LoadImage(NULL, ICON_FILE, IMAGE_ICON,
                                        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
                                        LR_LOADFROMFILE);
    g_class_icon_sml = (HICON)LoadImage(NULL, ICON_FILE, IMAGE_ICON,
                                        GetSystemMetrics(SM_CXSMICON),
                                        GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE);
    HICON ic_big = g_class_icon_big, ic_sml = g_class_icon_sml;

    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hinst;
    wc.lpszClassName = GUI_CLASS;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor  = LoadCursor(NULL, IDC_ARROW);   /* NULL leaves the "app starting" cursor stuck */
    wc.hIcon   = ic_big;
    wc.hIconSm = ic_sml ? ic_sml : ic_big;
    RegisterClassEx(&wc);

    WNDCLASS cc = {0};
    cc.lpfnWndProc = CanvasProc;
    cc.hInstance = hinst;
    cc.lpszClassName = CANVAS_CLASS;
    cc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    cc.hCursor  = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&cc);

    WNDCLASSEX dc = {0};
    dc.cbSize = sizeof dc;
    dc.lpfnWndProc = DocProc;
    dc.hInstance = hinst;
    dc.lpszClassName = DOC_CLASS;
    dc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    dc.hCursor = LoadCursor(NULL, IDC_ARROW);
    dc.hIcon   = ic_big;
    dc.hIconSm = ic_sml ? ic_sml : ic_big;
    RegisterClassEx(&dc);

    /* read before the controls exist, so build_ui starts from them */
    if (!arg_given("--layout"))
        ini_load();

    if (arg_given("--layout")) {
        g_skip_preload = 1;
        HWND h0 = CreateWindow(GUI_CLASS, CUBOLT_TITLE, style,
                               wx, wy, ow, oh, NULL, build_menu(), hinst, NULL);
        return h0 ? layout_check(h0) : 2;
    }

    /* fixed size: the controls sit at fixed coordinates, so resizing would only clip them */
    HWND hw = CreateWindow(GUI_CLASS, CUBOLT_TITLE, style,
                           wx, wy, ow, oh, NULL, build_menu(), hinst, NULL);
    /* no window: nothing can post the WM_QUIT a loop would need */
    if (!hw) return 2;
    DragAcceptFiles(hw, TRUE);
    ShowWindow(hw, SW_SHOW);
    UpdateWindow(hw);
    /* the OS would leave it set until the first mouse move */
    SetCursor(LoadCursor(NULL, IDC_ARROW));

    /* Ctrl+S and F9, the shortcuts the File and Run menus advertise */
    ACCEL acc[] = { { FCONTROL | FVIRTKEY, 'S',   IDM_SAVE },
                    { FVIRTKEY,            VK_F9, ID_SOLVE } };
    HACCEL hac = CreateAcceleratorTable(acc, (int)(sizeof acc / sizeof acc[0]));

    MSG m;
    while (GetMessage(&m, NULL, 0, 0)) {
        if (hac && TranslateAccelerator(hw, hac, &m)) continue;
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    if (hac) DestroyAcceleratorTable(hac);
    return 0;
}
