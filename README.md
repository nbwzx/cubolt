<p align="center">
  <img alt="Cubolt" src="res/cubolt-icon.png" width="128" height="128">
</p>

<h1 align="center">Cubolt</h1>

<p align="center">
  <img alt="language: C" src="https://img.shields.io/badge/language-C-555555">
  <img alt="platform: Windows x64" src="https://img.shields.io/badge/platform-Windows%20x64-0078D4">
  <img alt="toolchain: Zig 0.16" src="https://img.shields.io/badge/toolchain-Zig%200.16-F7A41D">
  <img alt="license: GPL-3.0" src="https://img.shields.io/badge/license-GPL--3.0-3DA639">
</p>

<p align="center">
  <strong>A pure-C optimal Rubik's Cube solver.</strong>
</p>

<p align="center">
  If you only want the program, install <code>CuboltSetup-&lt;version&gt;.exe</code> from the project's <a href="../../releases">Releases page</a>.
</p>

<p align="center">
  <a href="#requirements">Requirements</a> &middot;
  <a href="#build">Build</a> &middot;
  <a href="#checks">Checks</a> &middot;
  <a href="#usage">Usage</a> &middot;
  <a href="#search-core">Search core</a> &middot;
  <a href="#license-gpl-30">License</a>
</p>

## Requirements

| Component | Minimum                        | Recommended  |
| --------- | ------------------------------ | ------------ |
| Windows   | 8.1 x64 with the Universal CRT | 10 or 11 x64 |
| CPU       | Any x86-64, 1 core             | 8+ cores     |
| RAM       | 2 GB                           | 4 GB         |
| Free disk | 2 GB                           | 4 GB         |

## Build

Only Zig is required.

```bat
build.bat          # Windows cmd
make               # or Git Bash / MSYS
make installer     # setup program; needs makensis on PATH too
```

Releases are built by `.github/workflows/release.yml`. Pushing a tag `v<version>` publishes `CuboltSetup-<version>.exe`; the workflow refuses a tag that disagrees with `include/version.h`, which is where the version lives.

## Usage

### `mktables.exe`

Run it first to build the solver tables. Later runs skip the ones already on disk.

```text
mktables.exe [<table> ...] [--check] [--force] [--quiet] [--manifest] [--threads N]
mktables.exe --help                    # all flags
```

Naming a table limits the run to that one; with no name every table is built, written next to the exe. `--check` rebuilds in memory and compares against disk byte for byte, writing nothing, and exits 1 if a table is missing or differs. `--force` rebuilds even the ones already there. `--manifest` rewrites `tables.manifest` from what is on disk and builds nothing. `--threads 0` is one worker per logical core.

### `search.exe`

```text
search.exe [<name> [<depth> [<subgroup>]]] (--cubestring <s> | --scramble <alg>)
search.exe --help                    # all other modes and flags
```

`<depth>` defaults to `13`, and `<subgroup>` defaults to `URFDLB`. Any subgroup of `{URFDLB urfdlb EMS xyz}` may be given. Solutions are written to `<out>/<name>.txt` (default: `output/`); with neither a name nor `--out`, the list is printed to stdout. A solved cube yields an empty line.

```text
search.exe demo 6 RUMx --cubestring UUUUUUFFLBRRBRRURRRRUFFUFFFDDRDDDDDDFFDLLLLLLBLLBBBBBB
search.exe Aperm-U-face 15 URFDLB --scramble "R F R2 D' R U R' D R U' R F' R'" --out output
```

A `--cubestring` is the 54 facelets in URFDLB order (nine per face: U, R, F, D, L, B), read in the fixed-center frame.

Example job lines are in `tests/bench/benchmark_jobs.txt`, example case files in `tests/batch/`.

Run `search.exe --help` for the full list of flags and modes.

A `?` facelet hides a sticker, making an _incomplete cube_: the search restores every known cubie and leaves the unknown ones anywhere. All six centers must be provided; they define the frame.

```text
search.exe ic 13 URFDLB --cubestring UUUUU?FF???RBRRURRRR?FFUFFFDDRDDDDDDFFDLLLLLLBLLBBBBBB
```

### `cubolt.exe`

```text
cubolt.exe              # window
cubolt.exe --selftest   # headless solve, writes _selftest.out
cubolt.exe --layout     # headless layout check, writes _layout.out
```

The cube net is on the left and the controls are on the right. Click a center to take the color from it, click any other sticker to paint it in that color, and right-click to hide it (`?`), right-click again to restore its color; you can also paste a cubestring. _Reset_ restores the solved cube and _Clear_ hides every sticker but the six centers. _Auto complete_, under the net, fills in the stickers the laws of the cube force after each color painted; a sticker hidden with a right-click is left alone. The net reports how much of the cube is still known. _Help > Contents_ documents the inputs, the search options, the log and the batch files.

Tables are mmap'd on a background thread. _Solve_ starts only once they are ready. If a table set is missing, it is built there, and the builder output appears in the log. _Help > Check for Updates_ queries GitHub for the latest release on its own thread.

## Checks

`python tests/verify.py` runs every check without rebuilding from `src/`; `python tests/verify.py --build` builds first. `make check`, `make bench`, and `make test` each run one group on its own.

`tests/bench.py` samples `tests/bench/benchmark_jobs.txt` and keeps the answers in `tests/bench/benchmark_baseline.txt`. Timings are specific to this machine and are written to `tests/_out/bench_times.txt`. `python tests/bench.py --update` rewrites both files.

`tests/bench_tables.py` times table building the same way; its rows are end-to-end timings and are also written under `tests/_out/`.

## Search core

The search follows a standard admissible-pruning framework. Its STM metric, table layout, and search logic are adapted from Kociemba's [`RubiksCube-OptimalSolver`](https://github.com/hkociemba/RubiksCube-OptimalSolver) and [`CubeExplorer`](https://github.com/hkociemba/CubeExplorer), with many additional optimizations made during the port to C. Each position carries a lower bound on its remaining moves, evaluated on all three axes under 120°/240° conjugation, stored mod 3 as five base-3 entries a byte, over a coordinate the set's 16-fold symmetry folds into one `u32` holding class and symmetry. A branch is pruned once this bound exhausts the remaining budget.

A depth pass assigns one worker per core. Tables are mapped at start-up, not copied, and shared by every worker. FTM is sliced out of STM by index so the two metrics cannot drift. A case that resolves to a single cube may be searched from its inverse instead: phase 1 opens a different tree from each side, and the solver picks whichever orientation is cheaper to walk. Every answer is reversed back into place.

## License GPL-3.0

    Cubolt (https://github.com/nbwzx/cubolt)
    Copyright (C) 2026 Zixing Wang <zixingwang.cn@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
