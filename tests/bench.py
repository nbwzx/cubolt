#!/usr/bin/env python3
"""Wall clock per case in tests/bench/benchmark_jobs.txt, against two baselines.

    python tests/bench.py                    print this run beside the clock baseline
    python tests/bench.py --update           make this run both baselines
    python tests/bench.py --repeat N         every clock is the fastest (min) of N runs

`search.exe --bench` only samples: one row per case.  How many samples to take, which clock
counts and what counts as a regression belong here.  The answers are compared even when no
clock is, so a machine that has never timed a run here still gets the check.
"""

import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "bin")

# Below this ratio a change is this machine's noise, not the code's.
QUIET = 0.15

REPEAT = 3
TIMEOUT = 2000

# The nine answer columns in order: written as the baseline file's header line, and counted.
ANSWER_HEADER = "case\tdepth\tsubgroup\tnmoves\tnsol\tnstd\tlengths\tnodes\tsolhash"
ANSWER_COLUMNS = len(ANSWER_HEADER.split("\t"))
WALL, SOL = ANSWER_COLUMNS, ANSWER_COLUMNS + 1   # the two clocks, in seconds


def timing_args(ap, exe, repeat=REPEAT):
    """What both bench scripts are run with; repeat is 1 where one pass is all a set is worth."""
    ap.add_argument("--exe", default=exe, help="the binary to time")
    ap.add_argument("--repeat", type=int, default=repeat,
                    help="runs of the whole set; the fastest clock of them is the one reported")
    ap.add_argument("--update", action="store_true",
                    help="make what was measured the baseline to compare against")
    ap.add_argument("--timeout", type=int, default=TIMEOUT,
                    help="seconds one run of the whole set is given")


def check_repeat(n):
    """A count of runs: fewer than one is a mistake, not a request for one."""
    if n < 1:
        sys.stderr.write("--repeat must be 1 or more, not %d\n" % n)
        return 1
    return 0


def no_baseline(script):
    return "no baseline -- run %s --update" % script


def exe_or_die(exe):
    """The exe it can run, having said why not when the build never made one."""
    if not os.path.exists(exe):
        sys.stderr.write("no %s -- build it first\n" % exe)
        return None
    return exe


def read_rows(text):
    return [line.split("\t") for line in text.splitlines()
            if line.strip() and not line.startswith("#")]


def by_name(rows):
    return {r[0]: r for r in rows}


def sample(exe, jobs, extra, timeout):
    try:
        r = subprocess.run([exe, "--bench", jobs] + extra, cwd=BIN,
                           capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return 1, "the bench timed out after %d s" % timeout, []
    return r.returncode, r.stderr.strip(), read_rows(r.stdout)


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("--check", action="store_true",
                    help="exit 1 on any answer that differs, is missing or moved (verify.py asks)")
    ap.add_argument("--jobs", default=os.path.join(ROOT, "tests", "bench", "benchmark_jobs.txt"),
                    help="the job file search.exe --bench reads")
    ap.add_argument("--baseline", default=os.path.join(ROOT, "tests", "bench",
                                                       "benchmark_baseline.txt"),
                    help="the answers, which travel with the repo")
    ap.add_argument("--times", default=os.path.join(ROOT, "tests", "_out", "bench_times.txt"),
                    help="the clocks of this machine's last run")
    timing_args(ap, os.path.join(BIN, "search.exe"))
    a = ap.parse_args()
    if check_repeat(a.repeat):
        return 1

    if not exe_or_die(a.exe):
        return 1
    jobs = os.path.relpath(a.jobs, BIN)

    runs, errs = [], []
    for _ in range(a.repeat):
        rc, err, rows = sample(a.exe, jobs, [], a.timeout)
        if rc != 0:
            sys.stderr.write("search.exe --bench exited %d\n%s\n" % (rc, err))
            return 1
        if err:
            errs.append(err)
        runs.append(rows)
    rc, herr, hash_rows = sample(a.exe, jobs, ["--solhash"], a.timeout)
    if rc != 0:
        sys.stderr.write("the --solhash pass exited %d\n%s\n" % (rc, herr))
        return 1
    if herr:
        errs.append(herr)

    first = runs[0]
    order = [r[0] for r in first]
    maps = [by_name(r) for r in runs]
    hashmap = by_name(hash_rows)
    answers, clocks, spread = {}, {}, {}
    odd = []
    for name in order:
        cols = [m.get(name) for m in maps]
        if any(c is None or len(c) <= SOL for c in cols):
            odd.append(name)
            continue
        stem = "\t".join(cols[0][:8])
        if any("\t".join(c[:8]) != stem for c in cols):
            odd.append(name)
            continue
        wall = [float(c[WALL]) for c in cols]
        sol = [float(c[SOL]) for c in cols]
        hashes = hashmap.get(name)
        answers[name] = stem + "\t" + (hashes[8] if hashes and len(hashes) > 8 else "-")
        clocks[name] = (min(wall), min(sol))
        spread[name] = max(wall) / min(wall) if min(wall) > 0 else 1.0

    def baseline_of(path):
        if not os.path.exists(path):
            return {}
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            return by_name(read_rows(fh.read()))

    base = baseline_of(a.baseline)
    times = baseline_of(a.times)

    bad = bool(odd) or bool(errs)
    print("%-12s %9s %9s %9s %7s  %s"
          % ("case", "wall", "sol", "baseline", "change", "notes"))
    if a.repeat < 2:
        print("one pass only: these clocks are that one cold sample"
              + (" -- writing them anyway; --repeat 3 first would make them worth keeping"
                 if a.update else " -- do not --update from them"))
    for name in order:
        if name not in answers:
            print("%-12s %9s %9s %9s %7s  %s" % (name, "-", "-", "-", "-", "no row"))
            bad = True
            continue
        wall, sol = clocks[name]
        notes = []
        if name in base:
            if "\t".join(base[name][:ANSWER_COLUMNS]) != answers[name]:
                notes.append("answers differ")
                bad = True
        else:
            notes.append(no_baseline("tests/bench.py"))
            bad = True
        if name in times and len(times[name]) > 2 and float(times[name][1]) > 0:
            was = times[name][1]
            r = wall / float(was)
            pct = (r - 1) * 100
            if r - 1 > QUIET:
                notes.append("slower")
            elif 1 - r > QUIET:
                notes.append("faster")
        else:
            was, pct = "-", None
            notes.append("no clock yet")
        if spread[name] > 1.5:
            notes.append("noisy %.1fx" % spread[name])
        print("%-12s %9.2f %9.2f %9s %7s  %s"
              % (name, wall, sol, was, "-" if pct is None else "%+.0f%%" % pct,
                 " ".join(notes)))

    for e in errs:
        print("stderr: %s" % e)
    gone = [n for n in base if n not in answers]
    if gone:
        print("in the baseline, not in the job file: %s" % ", ".join(sorted(gone)))
        bad = True

    if a.update:
        write_baseline(a.baseline, order, answers)
        write_times(a.times, order, clocks)
        print("wrote %s" % a.baseline)
        print("wrote %s" % a.times)

    if odd:
        print("%d case(s) came out different on two samples" % len(odd))
    if a.check and bad:
        return 1
    return 0


def line_ending_of(path):
    if not os.path.exists(path):
        return "\n"
    with open(path, "rb") as fh:
        return "\r\n" if b"\r\n" in fh.read(4096) else "\n"


def write_baseline(path, order, answers):
    eol = line_ending_of(path)
    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write("# benchmark_baseline.txt -- the answers of tests/bench/benchmark_jobs.txt," + eol)
        fh.write("# written by tests/bench.py --update.  The clocks are this machine's and live" + eol)
        fh.write("# in tests/_out/bench_times.txt instead." + eol)
        fh.write("# " + ANSWER_HEADER + eol)
        for name in order:
            if name in answers:
                fh.write(answers[name] + eol)


def write_times(path, order, clocks):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write("# bench_times.txt -- this machine's clocks, the fastest of one run's samples.\n")
        fh.write("# case\twall\tsol_time\n")
        for name in order:
            if name in clocks:
                fh.write("%s\t%.4f\t%.4f\n" % (name, clocks[name][0], clocks[name][1]))


if __name__ == "__main__":
    sys.exit(main())
