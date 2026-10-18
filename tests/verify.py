#!/usr/bin/env python3
"""Everything a release is checked against, in one pass.

    python tests/verify.py            check the binaries already in bin/
    python tests/verify.py --build    rebuild from src/ first (zig), then check

ZIG, GCC and MAKENSIS come from the environment, else from PATH; PY defaults to this interpreter.
"""

import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from source_checks import SOURCES, read

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "bin")
SCRATCH = os.path.join(ROOT, "tests", "_out")

COMPILE = ["-std=gnu11", "-c", "-O2", "-D_WIN32_WINNT=0x0A00", "-Iinclude"]
PROJECT_WARN = "-Wall -Wextra -Wpedantic".split()
STRICT_WARN = ("-Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wundef -Wcast-align "
               "-Wstrict-prototypes -Wmissing-prototypes -Wwrite-strings "
               "-Wdouble-promotion -Wswitch-enum -Wlogical-op -Wredundant-decls").split()
# Room enough to follow search()'s recursion, which the default budget cuts short and calls a leak.
ANALYZER_ROOM = ["--param", "analyzer-max-enodes-per-program-point=32",
                 "--param", "analyzer-max-recursion-depth=6"]
# -Wanalyzer-malloc-leak is unsound here: rend_xrealloc frees the old block and the caller stores the new one in a global the analyzer can't track
ANALYZER_NOLEAK = ["-Wno-analyzer-malloc-leak", "-Wno-analyzer-double-free"]

FAIL = []
CHECKED = 0          # one per reported line, so the verdict can say how many passed


# --- reporting ---
def note(label, text):
    global CHECKED
    CHECKED += 1
    print("%-46s %s" % (label, text))


def bad(label, text):
    global CHECKED
    CHECKED += 1
    FAIL.append(label)
    print("%-46s %s" % (label, text))


def die(label, text):
    """Leaving early still has to say which way the gate went."""
    bad(label, text)
    verdict()
    sys.exit(1)


def verdict():
    print()
    if FAIL:
        print("%d of %d checks failed: %s" % (len(FAIL), CHECKED, ", ".join(FAIL)))
    else:
        print("all %d checks pass" % CHECKED)


# --- running one process ---
# A step that hangs or will not start fails as one, not as a traceback that names no step.
STEP_TIMEOUT = 2000


def run(cmd, cwd=ROOT, timeout=STEP_TIMEOUT):
    """Captured output of one process; None when it had to be killed or never started."""
    try:
        return subprocess.run(cmd, cwd=cwd, timeout=timeout,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, errors="replace")
    except subprocess.TimeoutExpired:
        return None
    except OSError as e:
        sys.stderr.write("could not start %s: %s\n" % (cmd[0] if cmd else "", e))
        return None


def out_of(r):
    return r.stdout if r else ""


def status_of(r):
    return "killed" if r is None else r.returncode


def head(text, n):
    return text.splitlines()[:n]


def tail(text, n):
    return text.splitlines()[-n:]


def matching(text, pat, n):
    return [l for l in text.splitlines() if re.search(pat, l)][:n]


def read_file(path):
    """An answer a run wrote to a file, if it is there."""
    if not os.path.exists(path):
        return ""
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


# --- toolchain ---
def tool(name, default=""):
    """A variable wins, then the name on PATH."""
    p = os.environ.get(name)
    if p and (os.path.isfile(p) or shutil.which(p)):
        return p
    return shutil.which(name.lower()) or default


# --- the checks ---
def build(zig):
    if not zig:
        die("build", "zig is not available -- set ZIG=<path>")
    # every step below writes into bin/, and none of them makes it
    os.makedirs(BIN, exist_ok=True)
    steps = [
        [zig, "rc", "/fo", "bin/icon.res", "res/icon.rc"],
        [zig, "rc", "-Iinclude", "/fo", "bin/cubolt.res", "res/cubolt.rc"],
        [zig, "cc", "-target", "x86_64-windows-gnu", "-O3", "-Iinclude",
         "-o", "bin/search.exe", "src/search.c", "bin/icon.res", "-lwininet"],
        [zig, "cc", "-target", "x86_64-windows-gnu", "-O3", "-Iinclude",
         "-o", "bin/mktables.exe", "src/mktables.c", "bin/icon.res"],
        [zig, "cc", "-target", "x86_64-windows-gnu", "-O3", "-Iinclude",
         "-Wl,--subsystem,windows", "-lgdi32", "-luser32", "-lcomdlg32", "-lcomctl32",
         "-lshell32", "-lole32", "-lwininet", "-o", "bin/cubolt.exe", "src/gui.c",
         "bin/icon.res", "bin/cubolt.res"],
    ]
    log = ""
    for cmd in steps:
        r = run(cmd)
        log += out_of(r)
        if r is None or r.returncode != 0:
            with open(os.path.join(SCRATCH, "build.err"), "w", encoding="utf-8") as fh:
                fh.write(log)
            die("build", "failed -- see tests/_out/build.err")
    shutil.copyfile(os.path.join(ROOT, "res", "cubolt.ico"), os.path.join(BIN, "cubolt.ico"))
    note("build", "ok")


def bench(py):
    r = run([py, "tests/bench.py", "--check", "--repeat", "1"])
    if r is None:
        bad("bench", "timed out after %ds -- bench.py times each child, this was the backstop"
                     % STEP_TIMEOUT)
    elif r.returncode != 0:
        bad("bench", "answers differ -- rc=%s" % status_of(r))
    else:
        note("bench vs tests/bench/benchmark_baseline.txt", "answers match")
    print(out_of(r), end="")


def tables():
    # Rebuilt byte for byte, not against the manifest the build wrote: fs24_rep is answered too.
    label = "mktables --check"
    want = len(re.findall(r'^\s*\{"([A-Za-z0-9_]+)"\s*,', read("include", "tablesum.h"), re.M))
    r = run([os.path.join(BIN, "mktables.exe"), "--check", "--quiet"], cwd=BIN)
    out = out_of(r)
    same = out.count("IDENTICAL")
    if r is None or r.returncode != 0:
        bad(label, "rc=%s" % status_of(r))
        print("\n".join(matching(out, "MISSING|DIFFERS|NOT CHECKED", 6)))
    elif same != want:
        bad(label, "%d of %d tables rebuilt identical" % (same, want))
        print("\n".join(matching(out, "MISSING|DIFFERS|NOT CHECKED", 6)))
    else:
        note(label, "%d tables rebuilt and identical" % same)


def gui():
    cubolt = os.path.join(BIN, "cubolt.exe")
    # Both passes answer by writing a file into bin/: a stale one would read as this run's.
    for stale in ("_selftest.out", "_layout.out"):
        if os.path.exists(os.path.join(BIN, stale)):
            os.remove(os.path.join(BIN, stale))

    r = run([cubolt, "--selftest"], cwd=BIN)
    if r is None or r.returncode != 0:
        bad("gui --selftest", "rc=%s" % status_of(r))
        print("\n".join(tail(out_of(r), 4)))
    else:
        note("gui --selftest", "rc=0")
    answer = read_file(os.path.join(BIN, "_selftest.out"))
    want = ["self-test: 1 standard solution(s)", "  R U' R'", "incomplete self-test: R U' R'",
            "auto complete self-test: 1 sticker(s)"]
    if answer.strip().splitlines() != want:
        bad("gui --selftest answer", "the file does not hold the three lines it should")
        print("\n".join(head(answer, 4)) if answer.strip() else "(empty)")
    else:
        note("gui --selftest answer", want[-1].strip())

    r = run([cubolt, "--layout"], cwd=BIN)
    if r is None or r.returncode != 0:
        bad("gui --layout", "rc=%s" % status_of(r))
        print("\n".join(matching(out_of(r), r"OUT-OF-CLIENT|TEXT-CLIPPED|centered *: NO", 8)))
        print("\n".join(tail(out_of(r), 1)))
    else:
        note("gui --layout", "rc=0")
    report = os.path.join(BIN, "_layout.out")
    if os.path.exists(report) and os.path.getsize(report):
        note("gui --layout size", "%d bytes" % os.path.getsize(report))
    else:
        bad("gui --layout output", "missing")


def cli(py):
    label = "tests/cli_validation_test.py"
    r = run([py, label])
    out = out_of(r)
    if r is None:
        bad(label, "timed out after %d s" % STEP_TIMEOUT)
    elif r.returncode != 0:
        bad(label, "failed")
        print("\n".join(matching(out, "fail", 8)))
        print("\n".join(tail(out, 4)))
    elif "skipped -- not built" in out:
        bad(label, "checks were skipped (a target was not built)")
    else:
        note(label, tail(out, 1)[0] if out.strip() else "")


def gcc_passes(gcc):
    def diagnostics(r):
        # Never reaching a diagnostic leaves no "warning:" behind, so the return code counts as one.
        n = len(re.findall("warning:|error:", out_of(r)))
        if r is None or r.returncode != 0:
            n += 1
            print("  gcc ended %s: %s" % (status_of(r), tail(out_of(r), 2)))
        return n

    if not gcc:
        bad("gcc (warnings, strict, -fanalyzer)", "gcc is missing -- not run")
        return
    for f in SOURCES:
        r = run([gcc] + COMPILE + PROJECT_WARN +
                ["-o", os.path.join(SCRATCH, "w_%s.o" % f), "src/%s.c" % f])
        n = diagnostics(r)
        if n:
            bad("gcc project-warnings %s.c" % f, str(n))
            print("\n".join(head(out_of(r), 6)))
        else:
            note("gcc project-warnings %s.c" % f, "0")

    strict = 0
    for f in SOURCES:
        r = run([gcc] + COMPILE + STRICT_WARN +
                ["-o", os.path.join(SCRATCH, "s_%s.o" % f), "src/%s.c" % f])
        n = diagnostics(r)
        strict += n
        if n:
            print("\n".join(head(out_of(r), 6)))
    if strict:
        bad("gcc strict-warnings (%d TU)" % len(SOURCES), str(strict))
    else:
        note("gcc strict-warnings (%d TU)" % len(SOURCES), "0")

    analyzed = 0
    for f in SOURCES:
        # -c, not -fsyntax-only: the analyzer needs the object written out.
        r = run([gcc, "-std=gnu11", "-c", "-O2", "-fanalyzer"] + ANALYZER_ROOM + ANALYZER_NOLEAK +
                ["-D_WIN32_WINNT=0x0A00", "-Iinclude",
                 "-o", os.path.join(SCRATCH, "an_%s.o" % f), "src/%s.c" % f])
        n = diagnostics(r)
        analyzed += n
        if n:
            print("\n".join(head(out_of(r), 8)))
    if analyzed:
        bad("gcc -fanalyzer (%d TU)" % len(SOURCES), str(analyzed))
    else:
        note("gcc -fanalyzer (%d TU)" % len(SOURCES), "0")


def installer(makensis):
    if not makensis:
        bad("makensis cubolt.nsi", "makensis is missing -- not run")
        return
    # version.h is the one place the version lives; `make installer` reads it the same way.
    ver = ".".join(re.findall(r"^#define CUBOLT_VERSION_(?:MAJOR|MINOR|PATCH)\s+(\d+)",
                              read("include", "version.h"), re.M))
    r = run([makensis, "-DVERSION=" + ver, "cubolt.nsi"],
            cwd=os.path.join(ROOT, "installer"), timeout=300)
    out = out_of(r)
    # Status first: a script that fails without printing either word does not compile.
    if r is None:
        bad("makensis cubolt.nsi", "timed out after 300 s")
    elif r.returncode != 0:
        bad("makensis cubolt.nsi", "rc=%d" % r.returncode)
        print("\n".join(tail(out, 6)))
    elif re.search("warning|error", out, re.I):
        bad("makensis cubolt.nsi", "%d diagnostic(s)"
            % len(re.findall("warning|error", out, re.I)))
        print("\n".join(head(out, 6)))
    else:
        note("makensis cubolt.nsi", "clean")


def source_checks(py):
    script = "tests/source_checks.py"
    r = run([py, script])
    out = out_of(r)
    if r is None or r.returncode != 0:
        bad(script, "failed")
        print("\n".join([l for l in out.splitlines() if " ok" not in l][:14]))
    else:
        note(script, "%d checks ok" % out.count(" ok"))


def main():
    args = sys.argv[1:]
    strange = [a for a in args if a != "--build"]
    if strange:
        sys.stderr.write("%s is not an option verify.py takes\n" % strange[0])
        return 1

    # A missing tool must not read as a pass.
    zig = tool("ZIG")
    gcc = tool("GCC")
    makensis = tool("MAKENSIS")
    py = os.environ.get("PY") or sys.executable
    # ZIG is left out: only --build uses it, and build() already says when it is missing
    for name, p in (("GCC", gcc), ("PY", py), ("MAKENSIS", makensis)):
        if not p or not (os.path.isfile(p) or shutil.which(p)):
            bad("toolchain", "%s not found -- set %s=<path>" % (name, name))

    os.makedirs(SCRATCH, exist_ok=True)
    if "--build" in args:
        build(zig)

    bench(py)
    tables()
    gui()
    cli(py)
    gcc_passes(gcc)
    installer(makensis)
    source_checks(py)

    verdict()
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
