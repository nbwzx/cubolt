#!/usr/bin/env python3
"""Source invariants gcc cannot see: silent at run time rather than a compile error."""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FAILURES = []
CHECKED = 0          # every check() run, so the verdict can say how many passed

# The TUs gcc compiles: src/ pins the list, so a fourth .c fails here instead of being skipped.
SOURCES = ["gui", "mktables", "search"]


def read(*parts):
    with open(os.path.join(ROOT, *parts), "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def spans(text):
    """("s", start, end) for each string literal and ("c", start, end) for each comment, in order.

    A // or /* inside a string opens nothing, so a string is yielded whole.
    """
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            yield "s", i, min(j + 1, n)
            i = min(j + 1, n)
            continue
        if c == "/" and i + 1 < n and text[i + 1] in "/*":
            if text[i + 1] == "/":
                j = text.find("\n", i)
                j = n if j < 0 else j
            else:
                j = text.find("*/", i + 2)
                j = n if j < 0 else j + 2
            yield "c", i, j
            i = j
            continue
        i += 1


def strip_comments(text):
    """Comments blanked, newlines kept, strings stepped over: a line number stays the file's own."""
    out, last = [], 0
    for kind, a, b in spans(text):
        if kind == "c":
            out.append(text[last:a])
            out.append(re.sub(r"[^\n]", " ", text[a:b]))
            last = b
    out.append(text[last:])
    return "".join(out)


def hits(text, pat):
    """Every match as (line number, that line): a call can be written across two lines."""
    found = []
    for m in pat.finditer(text):
        n = text.count("\n", 0, m.start()) + 1
        a = text.rfind("\n", 0, m.start()) + 1
        b = text.find("\n", m.start())
        found.append((n, text[a:b if b >= 0 else len(text)].strip()))
    return found


def check(name, problems, how=""):
    global CHECKED
    CHECKED += 1
    if problems:
        FAILURES.append(name)
        print("%-42s %d problem(s)" % (name, len(problems)))
        for p in problems[:12]:
            print("    %s" % p)
    else:
        print("%-42s ok%s" % (name, " -- " + how if how else ""))


def block(text, start):
    """The {...} initializer that follows `start`, up to the line that closes it."""
    i = text.find(start)
    if i < 0:
        return ""
    j = text.find("{", i)
    depth, k = 0, j
    while k < len(text):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                return text[j:k]
        k += 1
    return text[j:]


def table_files():
    return re.findall(r'"([a-z_0-9]+)"', block(read("include", "tablesum.h"), "TABLE_FILES[] ="))


# Every size is a macro over core.h's counts: a clone with no tables evaluates them instead.
CASTS = re.compile(r"\(\s*(?:unsigned\s+|signed\s+)?(?:long\s+long|long|int|size_t|u\d+)\s*\)")
IDENT = re.compile(r"\b[A-Za-z_][A-Za-z_0-9]*\b")


def table_bytes():
    pat = r"^#define\s+([A-Z_][A-Z_0-9]*)\s+(\S.*)$"
    defs = dict(re.findall(pat, read("include", "core.h"), re.M))

    def number(expr):
        expr = CASTS.sub("", expr)

        def macro(m):
            return str(number(defs[m.group(0)])) if m.group(0) in defs else m.group(0)
        try:
            # C's integer divide; an empty builtins is all an expression of macros needs
            return int(eval(IDENT.sub(macro, expr).replace("/", "//"),
                            {"__builtins__": {}}, {}))
        except Exception as e:
            raise ValueError("a TABLE_FILES[] size cannot be read: %s (%s)" % (expr, e))

    sizes = block(read("include", "tablesum.h"), "TABLE_FILES[] =")
    return sum(number(sz) for sz in re.findall(r'\{\s*"[a-z_0-9]+"\s*,\s*(.+?)\s*\}', sizes))


def builder_files():
    """The filenames in mktables' BUILDERS[]: the first brace group of each entry."""
    names = []
    for line in block(read("src", "mktables.c"), "BUILDERS[] =").splitlines():
        entry = re.search(r"\{\{(.*?)\}", line)
        if entry:
            names += re.findall(r'"([a-z_0-9]+)"', entry.group(1))
    return names


# --- the lists of tables ---
# Not in TABLE_FILES: ts_size() is -1, so nothing loads it.  Not in BUILDERS: built tables only.
def check_table_lists():
    ts = table_files()
    bl = builder_files()
    bad = []
    if set(bl) - set(ts):
        bad.append("in BUILDERS but not TABLE_FILES: %s" % ", ".join(sorted(set(bl) - set(ts))))
    if set(ts) - set(bl):
        bad.append("in TABLE_FILES but not BUILDERS: %s" % ", ".join(sorted(set(ts) - set(bl))))
    check("tables: BUILDERS vs TABLE_FILES", bad, "%d tables" % len(ts))

    nsi = read("installer", "cubolt.nsi")
    i = nsi.find('Delete "$INSTDIR\\tables.manifest"')
    deletes = ["tables.manifest"]
    if i >= 0:
        for line in nsi[i:].splitlines()[1:]:
            m = re.match(r'\s*Delete "\$INSTDIR\\([^"\\]+)"', line)
            if not m:
                break
            deletes.append(m.group(1))
    want = set(ts) | {"tables.manifest"}
    got = set(deletes)
    bad = []
    if want - got:
        bad.append("not removed by the uninstaller: %s" % ", ".join(sorted(want - got)))
    if got - want:
        bad.append("removed but is not a table: %s" % ", ".join(sorted(got - want)))
    check("tables: uninstaller vs TABLE_FILES", bad, "%d names" % len(got))


def check_installer_size():
    nsi = read("installer", "cubolt.nsi")
    m = re.search(r"SectionSetSize\s+\$\{SecBuild\}\s+(\d+)", nsi)
    bad = [] if m else ["cubolt.nsi does not give SecBuild a size"]
    want = (table_bytes() + 1023) // 1024
    if m and int(m.group(1)) != want:
        bad.append("cubolt.nsi asks for %s KB, the tables come to %d KB" % (m.group(1), want))
    check("installer: SecBuild size == the tables", bad, "%d KB" % want)


def check_version():
    vh = read("include", "version.h")
    nums = re.findall(r"#define CUBOLT_VERSION_(MAJOR|MINOR|PATCH)\s+(\d+)", vh)
    have = ".".join(n for _, n in sorted(nums, key=lambda p: "MAJOR MINOR PATCH".split().index(p[0])))
    # the Makefile has no number of its own: what it can get wrong is where it reads from
    mk = re.search(r"^VERSION \?= \$\(shell .*version\.h", read("Makefile"), re.M)
    bad = []
    if len(nums) != 3:
        bad.append("version.h does not define MAJOR, MINOR and PATCH")
    if not mk:
        bad.append("Makefile does not read VERSION out of version.h")

    # res/cubolt.rc takes the same four out of version.h; the installer keeps its own copies
    nsi = read("installer", "cubolt.nsi")
    for nsi_name, vh_name in (("APPNAME", "CUBOLT_NAME"), ("PUBLISHER", "CUBOLT_AUTHOR"),
                              ("AUTHORMAIL", "CUBOLT_EMAIL"), ("COPYYEAR", "CUBOLT_YEAR")):
        there = re.search(r'^!define\s+%s\s+"([^"]*)"' % nsi_name, nsi, re.M)
        here = re.search(r'#define\s+%s\s+"([^"]*)"' % vh_name, vh)
        if not there or not here:
            bad.append("%s is not defined in both places" % nsi_name)
        elif there.group(1) != here.group(1):
            bad.append("%s: cubolt.nsi %s vs version.h %s"
                       % (nsi_name, there.group(1), here.group(1)))
    check("version: version.h / Makefile / cubolt.nsi", bad, have)


# --- strchr / memchr results used as pointers ---
# NULL - base is undefined, and both answer NULL when the character is absent: test first.
def check_lookup_arithmetic():
    bad = []
    # strrchr and strstr too; the pattern asks for the subtraction, so a cast still counts.
    pat = re.compile(r"\b(strchr|strrchr|memchr|strstr)\s*\([^;]*\)\s*-\s*")
    for d in ("include", "src"):
        for fn in sorted(os.listdir(os.path.join(ROOT, d))):
            if not fn.endswith((".h", ".c")):
                continue
            text = strip_comments(read(d, fn))
            for n, line in hits(text, pat):
                bad.append("%s/%s:%d: %s" % (d, fn, n, line))
    # What was found, not what is guaranteed: no pattern shows that every result is tested.
    check("lookups: no subtraction on a fresh lookup", bad, "no result is subtracted from inline")


def check_turn_alphabet():
    src = read("include", "render.h")
    # REND_LET is FACE_LET + SLICE_LET + ROT_LET + WIDE_LET, so those macros are expanded first.
    for name, val in re.findall(r'#define\s+([A-Z_]\w*)\s+"([^"]*)"', src):
        src = re.sub(r'\b%s\b' % name, lambda m: '"%s"' % val, src)
    init = re.search(r'REND_LET\[\d+\]\s*=\s*([^;]+);', src)
    rend = "".join(re.findall(r'"([^"]*)"', init.group(1))) if init else ""
    if not rend:
        check("turns: turn_read's letters are in REND_LET", ["REND_LET not found in render.h"])
        return
    letters = set(rend)
    body = read("include", "cube.h")
    i = body.find("static int turn_read")
    body = body[i:body.find("\n}", i)] if i >= 0 else ""
    cases = re.findall(r"case '(\w)':", body)
    # No case labels means the anchor missed turn_read: 0 letters would all be in REND_LET.
    if not cases:
        check("turns: turn_read's letters are in REND_LET",
              ["turn_read not found in cube.h -- 0 letters means nothing was checked"])
        return
    # turn_read uppercases its aliases, so a letter may be either case; REND_LET must carry it.
    bad = ["'%s'" % c for c in sorted(set(cases))
           if c not in letters and c.upper() not in letters]
    check("turns: turn_read's letters are in REND_LET",
          ["accepted by turn_read but not in REND_LET: %s" % ", ".join(bad)] if bad else [],
          "%d letters" % len(set(cases)))


def check_ini_keys():
    gui = read("src", "gui.c")
    i = gui.find("static void ini_save")
    body = gui[i:gui.find("\n}", i)] if i >= 0 else ""
    saved = set(re.findall(r'fprintf\(f,\s*"([a-z_0-9]+)=', body))
    # Same scope both sides: a strcmp anywhere in gui.c would answer for a key ini_load skips.
    j = gui.find("static void ini_load")
    lbody = gui[j:gui.find("\n}", j)] if j >= 0 else ""
    loaded = set(re.findall(r'strcmp\(line,\s*"([a-z_0-9]+)"\)', lbody))
    if not saved or not loaded:
        check("cubolt.ini: saved keys == loaded keys",
              ["%s not found in gui.c" % ("ini_save" if not saved else "ini_load")])
        return
    bad = []
    if saved - loaded:
        bad.append("saved but never read back: %s" % ", ".join(sorted(saved - loaded)))
    if loaded - saved:
        bad.append("read but never saved: %s" % ", ".join(sorted(loaded - saved)))
    check("cubolt.ini: saved keys == loaded keys", bad, "%d keys" % len(saved))


# A line past 100 does not fit a side-by-side review.
def check_line_width():
    bad = []
    scanned = 0
    for d in ("include", "src"):
        for fn in sorted(os.listdir(os.path.join(ROOT, d))):
            if not fn.endswith((".h", ".c")):
                continue
            scanned += 1
            for n, line in enumerate(read(d, fn).splitlines(), 1):
                if len(line) > 100:
                    bad.append("%s/%s:%d: %d columns" % (d, fn, n, len(line)))
    check("lines: none past 100 columns", bad, "%d files" % scanned)


def check_tracked_files():
    try:
        out = subprocess.run(["git", "ls-files"], cwd=ROOT, stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, text=True, check=True).stdout
    except Exception as e:
        # Not available is not clean: "skipped" here would read like a pass.
        check("git: no build output tracked", ["git ls-files did not run: %s" % e], "not run")
        return
    bad = [f for f in out.splitlines() if re.search(r"\.(exe|o|pdb|bak|res|obj|lib|a)$", f)]
    check("git: no build output tracked", ["tracked: %s" % ", ".join(bad[:12])] if bad else [],
          "%d files" % len(out.splitlines()))


def check_readme_flags():
    code = set()
    for d in ("include", "src"):
        for fn in sorted(os.listdir(os.path.join(ROOT, d))):
            if fn.endswith((".h", ".c")):
                code |= set(re.findall(r'"(--[a-z][a-z0-9-]+)"', read(d, fn)))
    scripts = {}
    for fn in sorted(os.listdir(os.path.join(ROOT, "tests"))):
        if fn.endswith(".py"):
            scripts[fn] = set(re.findall(r"(--[a-z][a-z0-9-]+)", read("tests", fn)))
    # A flag on a line that names one of the scripts is that script's, not a binary's.
    bad = []
    for line in read("README.md").splitlines():
        named = re.findall(r"tests/([A-Za-z0-9_]+\.py)", line)
        for flag in re.findall(r"(--[a-z][a-z0-9-]+)", line):
            if named:
                if not any(flag in scripts.get(n, ()) for n in named):
                    bad.append("README.md: %s is not an option of %s"
                               % (flag, ", ".join(named)))
            elif flag not in code:
                bad.append("README.md: %s is not an option any source file names" % flag)
    check("docs: README options exist in the code", sorted(set(bad)),
          "%d options in the sources, %d scripts" % (len(code), len(scripts)))


def _mk_var(text, name):
    """A Makefile variable, continued lines joined: a value can be spelled over more than one."""
    m = re.search(r"^%s\s*\??=(.*)$" % re.escape(name), text, re.M)
    if not m:
        return ""
    out, lines = m.group(1), text[m.end():].split("\n")[1:]
    for line in lines:
        if not out.rstrip().endswith("\\"):
            break
        out = out.rstrip()[:-1] + " " + line
    return out


# cubolt.exe's line carries every flag, so compare it; verify.py's is a list, so strip that first.
def _build_parts(cmd):
    cmd = re.sub(r'["\']', "", cmd).replace(",", " ")
    return (sorted(set(re.findall(r"-target\s+([^\s]+)", cmd))),
            sorted(set(re.findall(r"-O(\d)", cmd))),
            sorted(set(re.findall(r"-I([^\s]+)", cmd))),
            sorted(set(re.findall(r"-(l[a-z0-9]+)", cmd))))


# verify.py builds from src/ too: not what CI runs, but check_build_consistency compares it.
def _verify_gui_cmd():
    text = read("tests", "verify.py")
    for m in re.finditer(r'\[zig, "cc".*?\]', text, re.S):
        if "src/gui.c" in m.group(0):
            return m.group(0)
    return ""


def check_build_consistency():
    mk = read("Makefile")
    gui = _mk_var(mk, "GUI_LIBS").replace("$(NET_LIBS)", _mk_var(mk, "NET_LIBS"))
    mk_cmd = _mk_var(mk, "CFLAGS").replace("$(TARGET)", _mk_var(mk, "TARGET").strip()) + " " + gui

    entrances = [("build.bat", "")]
    for line in read("build.bat").splitlines():
        if " cc " in line and "gui.c" in line:
            entrances[0] = ("build.bat", line)
            break
    entrances.append(("verify.py", _verify_gui_cmd()))

    mk_p = _build_parts(mk_cmd)
    bad = []
    for name, cmd in entrances:
        p = _build_parts(cmd)
        for i, label in enumerate(("target", "optimization", "include", "libraries")):
            if p[i] != mk_p[i]:
                bad.append("%s %s: Makefile %s vs %s %s"
                           % (name, label, mk_p[i] or "-", name, p[i] or "-"))
        if not p[3]:
            bad.append("%s: no command compiling gui.c was found" % name)
    check("build: Makefile vs both entrances", bad, "%d libraries" % len(mk_p[3]))


# The manifest copies what mktables.c includes, and a copy drifts: building tables takes minutes.
def check_table_headers():
    listed = sorted(set(re.findall(r"\$\(INC\)/(\w+\.h)", _mk_var(read("Makefile"), "MKT_HEADERS"))))
    want, stack = set(), re.findall(r'#include\s+"([^"]+)"', read("src", "mktables.c"))
    while stack:
        h = stack.pop()
        if h in want or not os.path.isfile(os.path.join(ROOT, "include", h)):
            continue
        want.add(h)
        stack += re.findall(r'#include\s+"([^"]+)"', read("include", h))
    want = sorted(want)
    bad = []
    if listed != want:
        bad.append("MKT_HEADERS names %s, mktables.c reads %s" % (listed, want))
    check("tables: only the builder's own headers", bad, "%d headers" % len(want))


# `make syntax` globs src/, so the Makefile must: a fourth .c would be parsed there, not here.
def check_syntax_sources():
    have = sorted(f[:-2] for f in os.listdir(os.path.join(ROOT, "src")) if f.endswith(".c"))
    bad = []
    if sorted(SOURCES) != have:
        bad.append("SOURCES names %s, src/ holds %s" % (sorted(SOURCES), have))
    if not re.search(r"^SYNTAX_SRC\s*=.*wildcard", read("Makefile"), re.M):
        bad.append("Makefile: SYNTAX_SRC names the sources instead of taking them from src/")
    check("gcc: every src/*.c is compiled", bad, "%d sources" % len(have))


def main():
    check_table_lists()
    check_installer_size()
    check_version()
    check_build_consistency()
    check_table_headers()
    check_lookup_arithmetic()
    check_turn_alphabet()
    check_ini_keys()
    check_line_width()
    check_tracked_files()
    check_readme_flags()
    check_syntax_sources()
    print()
    if FAILURES:
        print("%d of %d source checks failed: %s"
              % (len(FAILURES), CHECKED, ", ".join(FAILURES)))
        return 1
    print("all %d source checks pass" % CHECKED)
    return 0


if __name__ == "__main__":
    sys.exit(main())
