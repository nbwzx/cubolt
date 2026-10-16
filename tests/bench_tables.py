#!/usr/bin/env python3
"""Wall clock per table mktables.exe builds, against this machine's own last run.

    python tests/bench_tables.py             print this run next to the last one made here
    python tests/bench_tables.py --update    make this run the one to compare against
    python tests/bench_tables.py --repeat N  every clock is the fastest (min) of N runs

  tests/_out/bench_tables.txt          the clocks, this machine only

Every column is a clock, so none travels with the repo; --update writes under tests/_out/.
No gate runs this: a run rebuilds every table.
"""

import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "bin")
BUILD = os.path.join(ROOT, "tests", "_out", "tblbuild")
LOG = os.path.join(BUILD, "build.log")
TIMES = os.path.join(BUILD, "times.txt")
BASE = os.path.join(ROOT, "tests", "_out", "bench_tables.txt")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# The noise floor, the shared options, the repeat's count, a missing baseline, an unbuilt exe.
from bench import QUIET, timing_args, check_repeat, no_baseline, exe_or_die

BUILT = re.compile(r"^  ([a-z0-9_]+) +built in ([0-9.]+) s")

# One run rebuilds every table, so the set is not passed over again by default.
REPEAT = 1


def timed(log):
    """Every `  <name> built in <secs> s` line, as (name, the text as printed, the number)."""
    out = []
    for line in log.splitlines():
        m = BUILT.match(line)
        if m:
            out.append((m.group(1), m.group(2), float(m.group(2))))
    return out


def update(path, order, best, repeat):
    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write("# bench_tables.txt -- wall clock in seconds for each table mktables.exe builds.\n")
        fh.write("# written by tests/bench_tables.py --update: this machine's, not the repo's.\n")
        if repeat > 1:
            fh.write("# --repeat %d: each table's clock is the fastest (min) of %d runs.\n"
                     % (repeat, repeat))
        for name in order:
            fh.write("%-22s %s\n" % (name, best[name]))


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("--baseline", default=BASE,
                    help="where the clocks of the last run are kept")
    timing_args(ap, os.path.join(BIN, "mktables.exe"), REPEAT)
    a = ap.parse_args()
    if check_repeat(a.repeat):
        return 1

    exe = exe_or_die(a.exe)
    if not exe:
        return 1
    os.makedirs(BUILD, exist_ok=True)

    order, best, runs = [], {}, []
    for _ in range(a.repeat):
        try:
            r = subprocess.run([exe, "--force"], cwd=BIN, timeout=a.timeout,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, errors="replace")
        except subprocess.TimeoutExpired:
            sys.stderr.write("the build timed out after %d s\n" % a.timeout)
            return 1
        with open(LOG, "w", encoding="utf-8") as fh:
            fh.write(r.stdout)
        if r.returncode != 0:
            sys.stderr.write("the build failed -- see %s\n" % LOG)
            return 1
        this = timed(r.stdout)
        if not this:
            sys.stderr.write("no timings -- see %s\n" % LOG)
            return 1
        for name, raw, val in this:
            if name not in best:
                order.append(name)
                best[name] = raw
            elif val < float(best[name]):
                best[name] = raw
        runs.append(this)

    # A minimum is only over the same work if every run timed the same tables.
    if any(len(r) != len(order) for r in runs):
        sys.stderr.write("not the same tables timed in every run -- see %s\n"
                         % LOG)
        return 1

    with open(TIMES, "w", encoding="utf-8") as fh:
        for name in order:
            fh.write("%-22s %s\n" % (name, best[name]))

    was = {}
    if os.path.exists(a.baseline):
        with open(a.baseline, "r", encoding="utf-8", errors="replace") as fh:
            for line in fh.read().splitlines():
                if line.startswith("#") or not line.strip():
                    continue
                w = line.split()
                if len(w) >= 2:
                    was[w[0]] = w[1]

    print("%-22s %9s %9s %7s  %s" % ("table", "now", "baseline", "change", "notes"))
    missing = 0
    for name in order:
        now = best[name]
        if name not in was:
            print("%-22s %9s %9s %7s  %s" % (name, now, "-", "-",
                                             no_baseline("tests/bench_tables.py")))
            missing += 1
            continue
        old = float(was[name])
        r = (float(now) / old) if old > 0 else 1.0
        notes = []
        if r - 1 > QUIET:
            notes.append("slower")
        elif 1 - r > QUIET:
            notes.append("faster")
        print("%-22s %9s %9s %7s  %s"
              % (name, now, was[name], "%+.0f%%" % ((r - 1) * 100), " ".join(notes)))
    for name in was:
        if name not in best:
            print("%-22s %9s %9s %7s  %s" % (name, "-", was[name], "-", "not built this run"))
            missing += 1

    if a.update:
        update(a.baseline, order, best, a.repeat)
        print("wrote %s" % a.baseline)
    print("timings: %s   log: %s" % (TIMES,
                                     LOG))
    # The clocks are this machine's own, so a slow one is a note; a set no longer matching exits 1.
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
