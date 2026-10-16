#!/usr/bin/env python3
# Input validation of search.exe, table driven: each case states the status and the message.
# Positives stop the suite passing by rejecting everything; runs in bin/, where the tables are.
import os, re, shutil, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BIN = os.path.join(ROOT, "bin")
SCRATCH = os.path.join(BIN, "_clivalidation")
# A case that has to be killed fails on its own, rc=124; the suite goes on without it.
RUN_TIMEOUT = 300

# a printed maneuver: any letter a report can spell with, or a face turn alone
MANEUVER      = re.compile(r"^[URFDLBEMSxyz][2']?( [URFDLBEMSxyz][2']?)*$")
FACE_MANEUVER = re.compile(r"^[URFDLB][2']?( [URFDLB][2']?)*$")


def run_exe(exe, argv, cwd=BIN):
    """Run the solver, answering rather than raising: a hung case ends the suite, rc=124."""
    argv = list(argv)
    head = argv[0] if argv else ""
    if not head.startswith("-") and not head.isdigit() and "--out" not in argv:
        argv = argv + ["--out", "_clivalidation"]
    try:
        return subprocess.run([exe] + argv, cwd=cwd, capture_output=True, text=True,
                              timeout=RUN_TIMEOUT)
    except subprocess.TimeoutExpired:
        print("%-30s TIMEOUT after %ds" % (" ".join(argv)[:30], RUN_TIMEOUT))
        return subprocess.CompletedProcess(argv, 124, "", "")


def field(out, pat):
    """The first group of `pat` in `out`, or "?" for a line the run never printed."""
    m = re.search(pat, out)
    return m.group(1) if m else "?"


def fresh_dir(path):
    """`path` as an empty directory, whatever an earlier run left in it."""
    shutil.rmtree(path, ignore_errors=True)
    os.makedirs(path, exist_ok=True)


def have_exe(name, label):
    """The exe to run, or None having said the check skips: a missing build is not a failure."""
    exe = os.path.join(BIN, name)
    if not os.path.exists(exe):
        print("%-30s %s" % (label, "skipped -- not built"))
        return None
    return exe


SOLVED = "UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB"
# the cube "R" builds, to aim a --goal at: one turn away from solved, so the answer is R
ONE_R = "UUFUUFUUFRRRRRRRRRFFDFFDFFDDDBDDBDDBLLLLLLLLLUBBUBBUBB"
# the cube "x" builds: its centers stand a quarter turn off, so reaching it takes a rotation
TURNED_X = "FFFFFFFFFRRRRRRRRRDDDDDDDDDBBBBBBBBBLLLLLLLLLUUUUUUUUU"
# the cube "z2" builds: a half turn is an even permutation of the centers, so it is still a goal
TURNED_Z2 = "DDDDDDDDDLLLLLLLLLFFFFFFFFFUUUUUUUUURRRRRRRRRBBBBBBBBB"
# the cube "x2 R U" builds: its centers stand a half turn off, and it has to be its own goal
TURNED_X2_RU = "DDDDDDBBBDFFRRRRRRRRRBBUBBUUUFUUFUUFBBULLLLLLLLLDFFDFF"
WIPED = "UUUUU?FF???RBRRURRRR?FFUFFFDDRDDDDDDFFDLLLLLLBLLBBBBBB"
# a "?" on a center: the six centers name the frame, so they may not be unknown
NOCENTER = "UUUU?UUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB"
# one sticker hidden on each of three cubies: three ways left, no corner unknown
PARTIAL_E = "UUUUUUUUUR?RRRRRRRF?FFFFFFFDDDDDDDDDL?LLLLLLLBBBBBBBBB"
PARTIAL_C = "UU?UUU?FL?R?BRRURR?R?FFUFFFDDRDDDDDDFF?LLLLLL?LLBBBBBB"
# four whole cubies wiped at once: 96 completions, more than the engine walks one by one
MANY_E = "U?U?U?L?DF?RFRRLRRF?RFFRFUUUBBDDDDDDF?DLLDLLRB?LBBBBBB"
# one face uncovered: every visible sticker sits on a partial cubie, so no known count moves
ONE_FACE = "UUBUUBUDD????R????????F????????D????????L????????B????"
# URF, its two side stickers swapped: the sides alone name the DFR cubie, the U facelet beside
# them does not; DFR is hidden, so no other corner says the two are wrong
MIRROR_C = "UUUUUUUUUFRRRRR?RRFFRFFFFF?DD?DDDDDDLLLLLLLLLBBBBBBBBB"
# the cube "R L'" builds, with all twelve edges hidden: the eight corners are the whole question
CORNERS_ONLY = "F?F?U?F?FR?R?R?R?RD?D?F?D?DB?B?D?B?BL?L?L?L?LU?U?B?U?U"
# URF's U facelet traded for DRB's D one: either corner's two sides still name a cubie, so only
# the U or D facelet says the cube cannot be stickered that way
UD_TRADED = "UUUUUUUUDRRRRRRRRRFFFFFFFFFDDDDDDDDULLLLLLLLLBBBBBBBBB"

# scratch files some cases need; inside bin/, so a relative path still resolves for search.exe
WRITTEN_FILES = {
    "unclosed.json": "[1,",
    "lonebracket.json": "[",
    "notarray.json": '{"name":"x"}',
    # a plain file where --out expects a directory
    "blocker": "not a directory",
    "one.json": '[{"name":"jsolved","subgroup":"URFDLB","depth":0,'
                '"cubestring":"%s"}]' % SOLVED,
    # 0 is a search depth of its own: the empty maneuver and nothing longer, not "unset"
    "zero.json": '[{"name":"zero","subgroup":"URFDLB","depth":0,'
                 '"scramble":"R U R\'"}]',
    # an empty cubestring is no more of a cube than a missing one: the scramble is the case
    "emptycs.json": '[{"name":"ecs","subgroup":"URFDLB","depth":0,"cubestring":"",'
                    '"scramble":"R U"}]',
    "slash.json": '[{"name":"a/b","subgroup":"URFDLB","depth":6,'
                  '"cubestring":"%s"}]' % SOLVED,
    "deep.json": '[{"name":"deep","subgroup":"URFDLB","depth":99,'
                 '"cubestring":"%s"}]' % SOLVED,
    # one over the 63 the overlay lets through: clamped, this would run at 63 and pass
    "deep64.json": '[{"name":"deep64","subgroup":"URFDLB","depth":64,'
                   '"cubestring":"%s"}]' % SOLVED,
    # a misspelled key is not an absent one: the case is refused, not run at the default depth
    "typo.json": '[{"name":"typo","subgroup":"URFDLB","depht":0,'
                 '"cubestring":"%s"}]' % SOLVED,
    # a key written twice: the case is refused rather than read past the first
    "dupkey.json": '[{"name":"dup","subgroup":"URFDLB","depth":3,"depth":64,'
                   '"cubestring":"%s"}]' % SOLVED,
    "badgroup.json": '[{"name":"bg","subgroup":"RUZ","depth":6,'
                     '"cubestring":"%s"}]' % SOLVED,
    # not a JSON number at all, so it is refused by the parser rather than by the depth check
    "nan.json": '[{"name":"nan","subgroup":"URFDLB","depth":-nan,'
                '"cubestring":"%s"}]' % SOLVED,
    "negdep.json": '[{"name":"nd","subgroup":"URFDLB","depth":-1,'
                   '"cubestring":"%s"}]' % SOLVED,
    # both counts are unusable rather than absent: neither may fall back to the run's default
    "badthr.json": '[{"name":"tn","subgroup":"URFDLB","depth":0,"threads":-2,'
                   '"cubestring":"%s"},'
                   '{"name":"tf","subgroup":"URFDLB","depth":0,"threads":1.5,'
                   '"cubestring":"%s"}]' % (SOLVED, SOLVED),
    # 65 is past the pool, and --threads on the command line refuses it: so does a case
    "bigthr.json": '[{"name":"tb","subgroup":"URFDLB","depth":0,"threads":65,'
                   '"cubestring":"%s"}]' % SOLVED,
    "optonly.json": '[{"name":"oo","only_optimal":true,"subgroup":"URFDLB","depth":8,'
                    '"scramble":"R U R\'"}]',
    # only_optimal with sort off: it reports the first entry of the sorted order anyway
    "optsort.json": '[{"name":"os","subgroup":"URFDLBEMS","depth":10,"sort":false,'
                    '"only_optimal":true,"scramble":"S U R U\' R\' S\'"}]',
    # the goal as a case's own key, aimed at the same cube CASES gives the --goal command line
    "goaly.json": '[{"name":"gl","goal":"%s","subgroup":"URFDLB","depth":4,'
                  '"cubestring":"%s"}]' % (ONE_R, SOLVED),
    "notobject.json": "[1]",
    # \u0000 would put a NUL inside the name, so what follows it is unreachable and never missed
    "nulname.json": '[{"name":"a\\u0000b","subgroup":"URFDLB","depth":0,'
                    '"cubestring":"%s"}]' % SOLVED,
    # half a surrogate pair encodes three bytes no UTF-8 reader takes
    "surr.json": '[{"name":"\\ud800","subgroup":"URFDLB","depth":0,'
                 '"cubestring":"%s"}]' % SOLVED,
    "badesc.json": '[{"name":"a\\qb","subgroup":"URFDLB","depth":0,'
                   '"cubestring":"%s"}]' % SOLVED,
    "trail.json": '[{"name":"t","subgroup":"URFDLB","depth":0,'
                  '"cubestring":"%s"}] x' % SOLVED,
    # 40 deep, against a limit of 32: recursion into a file that will not parse costs nothing here
    "deepnest.json": "[" * 40 + "]" * 40,
    "one.txt": "bok 6 URFDLB --scramble R U R'\n",
    # a # comment and a blank line, either side of the one case: neither is a case
    "comment.txt": "# no case here\n\nc 0 URFDLB --cubestring %s\n\n" % SOLVED,
    "algos.txt": "R U R'\nZ\n",
    # a blank line answers a blank line and the list goes on
    "blankline.txt": "R\n\nF\n",
    # 4096 is the line buffer: a longer maneuver is named rather than answered for what fit
    "longmove.txt": "R " * 2600 + "\n",
    # the same case as agreement() runs from the command line
    "agree.txt": "agreed 6 URFDLB --scramble R U R'\n",
    "agree.json": '[{"name":"agreed","subgroup":"URFDLB","depth":6,"scramble":"R U R\'"}]',
    "dup.json": '[{"name":"alpha","scramble":"R U","depth":4},'
                '{"name":"alpha","scramble":"F","depth":4}]',
    "emptyscr.json": '[{"name":"same","scramble":"","depth":0}]',
    "noscr.json": '[{"name":"dflt","subgroup":"URFDLB","depth":1}]',
    # no "name" key at all: the file falls back to case_NNN
    "noname.json": '[{"subgroup":"URFDLB","depth":0,"cubestring":"%s"}]' % SOLVED,
}

# name, argv after the exe, expected status, substring that must be in the merged output
CASES = [
    ("depth not a number",        ["x", "abc", "URFDLB"], 1, "depth must be a non-negative number"),
    ("depth too many digits",     ["x", "1000000000", "URFDLB"], 1,
                                  "depth must be a non-negative number"),
    ("depth negative",            ["x", "-1", "URFDLB"], 1, "unknown or incomplete flag -1"),
    ("subgroup letter not a move",["x", "6", "RUZ"], 1, "subgroup letter 'Z' is not a move"),
    ("depth over the buffer",     ["x", "99", "URFDLB"], 1,
                                  "search depth 99 exceeds the 63 move solution buffer"),
    ("cube string too short",     ["x", "6", "URFDLB", "--cubestring", "XYZ"], 1,
                                  "Cube definition string XYZ contains less than 54 facelets."),
    # two corners whose U and D facelets are traded: the sides of each still name a cubie, so the
    # U or D sticker is the only thing that says no stickering looks like this
    ("corner with a traded U/D",  ["x", "6", "URFDLB", "--cubestring", UD_TRADED], 1,
                                  "Some corners are undefined."),
    ("cube named twice",          ["x", "6", "URFDLB", "--cubestring", SOLVED,
                                   "--scramble", "R"], 1,
                                  "give --cubestring or --scramble, not both"),
    # a held cube needs a turn that carries its rotation back; face turns never move the centers
    ("turned cube, face turns",   ["x", "6", "URFDLB", "--scramble", "R U x"], 1,
                                  "the cube is held turned and URFDLB cannot write that rotation"),
    ("turned cube, other axis",   ["x", "6", "URFDLBx", "--scramble", "R U y"], 1,
                                  "the cube is held turned and URFDLBx cannot write that rotation"),
    # the same from the goal's end: it is the two frames' difference that the answer has to carry
    ("turned goal, face turns",   ["x", "4", "URFDLB", "--cubestring", SOLVED,
                                   "--goal", TURNED_Z2], 1,
                                  "the cube is held turned and URFDLB cannot write that rotation"),
    ("case name escapes --out",   ["../ESCAPED", "6", "URFDLB", "--scramble", "R"], 1,
                                  "case name '../ESCAPED' is not usable as a file name"),
    # Windows takes trailing dots and spaces off a file name, so "trail." would land on "trail"
    ("case name ends in a dot",   ["trail.", "0", "URFDLB", "--cubestring", SOLVED], 1,
                                  "case name 'trail.' is not usable as a file name"),
    ("case name ends in a space", ["trail ", "0", "URFDLB", "--cubestring", SOLVED], 1,
                                  "case name 'trail ' is not usable as a file name"),

    # --- positives: these must still be accepted ---
    ("solved cube, depth 0",      ["solved", "0", "URFDLB", "--cubestring", SOLVED], 0, "standard   : 1"),
    ("ordinary search",           ["plain", "6", "URFDLB", "--scramble", "R U R'"], 0, "standard   : 1"),
    # CreateDirectory takes one level, so a path two deep has to be made one level at a time
    ("out two levels deep",       ["nest", "0", "URFDLB", "--cubestring", SOLVED,
                                   "--out", "_clivalidation/a/b"], 0, "solutions  : 1"),
    # a folder picked with a dialog arrives with its separator on the end, or spelled Windows' way
    ("out with a trailing slash", ["slashy", "0", "URFDLB", "--cubestring", SOLVED,
                                   "--out", "_clivalidation/c/"], 0, "solutions  : 1"),
    ("out spelled with backslashes", ["backy", "0", "URFDLB", "--cubestring", SOLVED,
                                      "--out", "_clivalidation\\d"], 0, "solutions  : 1"),
    # --goal aims one turn away, so the answer is R; ignored, it answers the empty maneuver
    ("goal is another cube",       ["goal", "4", "URFDLB", "--cubestring", SOLVED,
                                    "--goal", ONE_R, "--solhash"], 0,
                                   "5d5a8e2a785df7ba"),
    # a quarter turn off is a rotation away, and face turns alone spell no rotation at all
    ("goal a quarter turn off",     ["goalx", "4", "URFDLB", "--cubestring", SOLVED,
                                     "--goal", TURNED_X], 1,
                                    "cannot write that rotation"),
    # the same pair with x y z in hand: the answer is the turn, and either direction gives it
    ("goal a quarter turn, spelled", ["goalturn", "2", "URFDLBxyz", "--cubestring", SOLVED,
                                      "--goal", TURNED_X, "--solhash"], 0, "14650fb0739d0383"),
    ("start a quarter turn off",    ["goalturn2", "2", "URFDLBxyz", "--cubestring", TURNED_X,
                                     "--goal", SOLVED, "--solhash"], 0, "14650fb0739d0383"),
    # a half turn off is the same cube held turned: the goal is renamed as the start is
    ("goal a half turn off",        ["goalz", "6", "URFDLBEMSxyz", "--cubestring", SOLVED,
                                     "--goal", TURNED_Z2], 0,
                                    "solutions  : 229"),
    ("goal is the start itself",    ["goalself", "6", "URFDLB", "--scramble", "x2 R U",
                                     "--goal", TURNED_X2_RU], 0,
                                    "solutions  : 1"),

    # --- the --json front end ---
    ("json file that is not there", ["--json", "_clivalidation/absent.json"], 1,
                                    "cannot open"),
    ("json array left open",      ["--json", "_clivalidation/unclosed.json"], 1,
                                  "unexpected end of input"),
    ("json nothing but a bracket",["--json", "_clivalidation/lonebracket.json"], 1,
                                  "unexpected end of input"),
    ("json that is not an array", ["--json", "_clivalidation/notarray.json"], 1,
                                  "JSON input must be a [ ... ] array of cases"),
    ("json one case",             ["--json", "_clivalidation/one.json",
                                   "--out", "_clivalidation"], 0, "json batch done: 1 solved"),
    ("json empty cubestring",     ["--json", "_clivalidation/emptycs.json",
                                   "--out", "_clivalidation"], 0, "json batch done: 1 solved"),
    # a per-case key reaches the same checks, and the refusal quotes the number the case wrote
    ("json depth over the buffer",["--json", "_clivalidation/deep.json"], 1,
                                  "depth 99 is past the 63 this front end accepts"),
    ("json depth on the buffer",  ["--json", "_clivalidation/deep64.json"], 1,
                                  "depth 64 is past the 63 this front end accepts"),
    ("json unknown key",         ["--json", "_clivalidation/typo.json"], 1,
                                  'unknown key "depht"'),
    ("json key written twice",   ["--json", "_clivalidation/dupkey.json"], 1,
                                  'key "depth" appears twice'),
    ("json subgroup not moves",   ["--json", "_clivalidation/badgroup.json"], 1,
                                  "subgroup letter 'Z' is not a move"),
    ("json depth 0 is a depth",   ["--json", "_clivalidation/zero.json", "--out", "_clivalidation"], 0,
                                  "searchdepth 0"),
    # a byte order mark is skipped rather than read as part of the first token
    ("json file with a BOM",      ["--json", "_clivalidation/bom.json",
                                   "--out", "_clivalidation"], 0, "json batch done: 1 solved"),
    ("json depth -nan refused",   ["--json", "_clivalidation/nan.json"], 1,
                                  "bad number"),
    # the command line refuses a negative depth before it is a number; this one is a number first
    ("json depth negative",       ["--json", "_clivalidation/negdep.json"], 1,
                                  "depth must not be negative"),
    # a negative and a fractional count: --threads on the command line refuses both
    ("json threads not a count",  ["--json", "_clivalidation/badthr.json",
                                   "--out", "_clivalidation"], 1,
                                  "threads is not a whole number from 0 up"),
    ("json threads past the pool",["--json", "_clivalidation/bigthr.json",
                                   "--out", "_clivalidation"], 1,
                                  "threads is 65, outside 0 to 64"),
    ("json name is refused",      ["--json", "_clivalidation/slash.json",
                                   "--out", "_clivalidation"], 1,
                                  'name "a/b" is not usable as a file name'),
    ("json case without a name",  ["--json", "_clivalidation/noname.json",
                                   "--out", "_clivalidation"], 0, "JOB case_001"),

    # --- the two list modes ---
    # --dump writes the maneuvers themselves; --optimal keeps the shortest of the three
    ("dump the maneuvers",        ["8", "URFDLB", "--scramble", "R U R'", "--dump"], 0,
                                  "R1L0 U3D0 R3L0"),
    ("only optimal",              ["opt", "8", "URFDLB", "--scramble", "R U R'", "--optimal"], 0, "solutions  : 1"),

    # three rotation letters needed here, so a search stopping at pairs renders nothing.
    ("rotation three turns away",  ["4", "RUxy", "--scramble", "z"], 0, "x y' x'"),
    ("json entry not an object",  ["--json", "_clivalidation/notobject.json"], 1,
                                  "case entry 1 skipped: not a JSON object"),
    ("json NUL in a name",        ["--json", "_clivalidation/nulname.json"], 1,
                                  "bad \\u escape: a NUL would cut the value short"),
    ("json half a surrogate",     ["--json", "_clivalidation/surr.json"], 1,
                                  "bad \\u escape: half a surrogate pair"),
    # the case's own only_optimal has to reach the header, with no --optimal on the command line
    ("json only_optimal",         ["--json", "_clivalidation/optonly.json",
                                   "--out", "_clivalidation"], 0, "only_optimal"),
    # sort:false with only_optimal: the shortest spelling has to come out all the same
    ("json only_optimal unsorted", ["--json", "_clivalidation/optsort.json",
                                    "--out", "_clivalidation"], 0, "standard   : 1"),
    ("json goal",                 ["--json", "_clivalidation/goaly.json",
                                   "--out", "_clivalidation", "--solhash"], 0, "5d5a8e2a785df7ba"),
    ("json bad escape",           ["--json", "_clivalidation/badesc.json"], 1,
                                  "bad escape \\q"),
    ("json trailing characters",  ["--json", "_clivalidation/trail.json"], 1,
                                  "unexpected trailing characters"),
    ("json nested past the limit",["--json", "_clivalidation/deepnest.json"], 1,
                                  "JSON nested too deep"),
    # a flag this mode does not take must be refused: ignored, a mistyped one runs the batch
    ("bench flag it does not take", ["--bench", "_clivalidation/one.txt", "--bogus"], 1,
                                    "--bogus is not an option --bench takes"),
    ("bench flag with no value",  ["--bench", "_clivalidation/one.txt", "--threads"], 1,
                                  "--threads needs a count"),
    # one row is one walk: the hash column is "-" until --solhash asks, both clocks plain seconds
    ("bench samples once",        ["--bench", "_clivalidation/one.txt"], 0, "\t-\t"),
    ("bench asked for a hash",    ["--bench", "_clivalidation/one.txt", "--solhash"], 0,
                                  "be5256941ed65d99"),
    # how many samples the harness takes is its own business, not a flag's
    ("bench flag it dropped",     ["--bench", "_clivalidation/one.txt", "--repeat", "3"], 1,
                                  "--repeat is not an option --bench takes"),

    # --- the --batch front end, which has its own line splitter ---
    ("batch past comments",       ["--batch", "_clivalidation/comment.txt", "--out", "_clivalidation"], 0,
                                  "solutions  : 1"),
    ("batch file not there",      ["--batch", "_clivalidation/absent.txt", "--out", "_clivalidation"], 1,
                                  "cannot open"),
    # --out names the folder the per-case files go to; with none they land in output/
    ("batch honors --out",        ["--batch", "_clivalidation/one.txt", "--out",
                                   "_clivalidation"], 0,
                                  "standard   :"),
    # the notation layer is on for a batch line as it is for any other run
    ("batch lists the notation",  ["--batch", "_clivalidation/one.txt", "--out", "_clivalidation"], 0,
                                  "standard   :"),

    # --- the translation front ends, which need no table at all ---
    ("scramble to cubestring",    ["--print", "--scramble", "R U R'"], 0,
                                  "UUUUUUFFLBRRBRRURRRRUFFUFFFDDRDDDDDDFFDLLLLLLBLLBBBBBB"),
    ("untranslatable scramble",   ["--print", "--scramble", "R Q"], 1,
                                  "unknown turn 'Q'"),
    ("scramble list, one bad",    ["--scramble-list", "_clivalidation/algos.txt"], 1,
                                  "unknown turn 'Z'"),
    # one cubestring per line in, so the line after a blank one answers for that line
    ("scramble list, blank line", ["--scramble-list", "_clivalidation/blankline.txt"], 0,
                                  "UUUUUULLLURRURRURRFFFFFFFFFRRRDDDDDDLLDLLDLLDBBBBBBBBB"),
    ("scramble list, line too long", ["--scramble-list", "_clivalidation/longmove.txt"], 1,
                                     "line 1 is too long"),
    # neither mode loads a table, so the two answers read off one have nothing to be answers to
    ("print takes no --solhash",  ["--print", "--solhash", "--scramble", "R U R'"], 1,
                                  "reads no table"),
    ("list takes no --solhash",   ["--scramble-list", "_clivalidation/algos.txt",
                                   "--solhash"], 1, "reads no table"),
    # a NUL ends the string every reader below takes, so what followed it would vanish silently
    ("scramble list, NUL in a line", ["--scramble-list", "_clivalidation/nulbyte.txt"], 1,
                                     "line 1 contains a NUL"),
    # no table needed, so it answers where a case would not; the number itself is version.h's
    ("version",                   ["--version"], 0, "Cubolt "),
    ("version, short",            ["-v"], 0, "Cubolt "),

    # --- the incomplete-cube path: ? covers whole cubies, and the centers name the frame ---
    ("incomplete cube",           ["icc", "8", "URFDLB", "--cubestring", WIPED,
                                   "--solhash"], 0, "solhash    : 22909332d6bd17c9"),
    ("partly known edges",        ["icp", "8", "UM", "--cubestring", PARTIAL_E,
                                   "--solhash"], 0, "solhash    : 587cdec12dd3cd5b"),
    ("partly known corners",      ["icq", "9", "URFDLB", "--cubestring", PARTIAL_C,
                                   "--solhash"], 0, "solhash    : 38c7dc9fbfbe8cc9"),
    ("one face uncovered",        ["icf", "4", "URFDLB", "--cubestring", ONE_FACE,
                                   "--solhash"], 0, "solhash    : ba6da45dd5dd7ae7"),
    ("relaxed search",            ["icm", "8", "URFDLB", "--cubestring", MANY_E,
                                   "--solhash"], 0, "solhash    : 2695fb0ad71833d3"),
    ("unknown center",            ["noc", "6", "URFDLB", "--cubestring", NOCENTER], 1,
                                  "a center is unknown"),
    # a mirrored corner is no corner: two sides alone do not name one
    ("mirrored corner",           ["icr", "8", "URFDLB", "--cubestring", MIRROR_C], 1,
                                  "a corner's colors name no cubie"),
    # hiding the edges leaves a question with fewer demands, so RL owes the two turns it takes:
    # a move set the ct table's reduction does not hold for, so a wrong one answers nothing
    ("corners only, narrow group", ["20", "RL", "--cubestring", CORNERS_ONLY, "--optimal"], 0,
                                   "R' L"),
    # a directory where the solutions file goes: the run names the write that failed
    ("solutions file unwritable", ["willfail", "6", "URFDLB", "--cubestring", SOLVED], 1,
                                  "not written in full"),
    # same plain file, and the run has to name the end of the path that is at fault
    ("outdir is a file",          ["isafile", "6", "URFDLB", "--cubestring", SOLVED,
                                   "--out", "_clivalidation/blocker"], 1,
                                  "exists and is not a directory"),
]


def agreement(exe):
    """One case through all three front ends: each splits it its own way, so only the answer tells."""
    def solhash(argv):
        r = run_exe(exe, argv)
        m = re.search(r"solhash\s*:\s*([0-9a-f]+)", r.stdout + r.stderr)
        return r.returncode, (m.group(1) if m else None)

    ways = [
        ("command line", ["agreed", "6", "URFDLB", "--scramble", "R U R'",
                          "--solhash"]),
        ("--batch",      ["--batch", "_clivalidation/agree.txt", "--out", "_clivalidation", "--solhash"]),
        ("--json",       ["--json", "_clivalidation/agree.json",
                          "--out", "_clivalidation", "--solhash"]),
    ]
    seen = {}
    failures = []
    for name, argv in ways:
        rc, h = solhash(argv)
        print("%-30s %-6s %s" % ("agreement: " + name, "rc=%d" % rc, h or "(no hash)"))
        if rc != 0 or not h:
            failures.append("%s: rc=%d hash=%s" % (name, rc, h))
            continue
        seen.setdefault(h, []).append(name)
    if len(seen) > 1:
        failures.append("solutions differ: %s" %
                        "; ".join("%s=%s" % (h, "+".join(n)) for h, n in seen.items()))
    return failures


def solhash_is_asked_for(exe):
    """The hash costs a long run time, so a run that does not ask for it gets no solhash."""
    base = ["shd", "6", "URFDLB", "--cubestring", SOLVED, "--quiet"]
    off = run_exe(exe, base)
    on = run_exe(exe, base + ["--solhash"])
    off_has = "solhash" in (off.stdout + off.stderr)
    on_has = re.search(r"solhash\s*:\s*[0-9a-f]+", on.stdout + on.stderr)
    print("%-30s %s" % ("solhash is off by default",
                        "no line, and --solhash brings it back" if not off_has and on_has else
                        "without=%s with=%s" % (off_has, bool(on_has))))
    if off_has or not on_has:
        return ["solhash: without the flag %s, with it %s" % (off_has, bool(on_has))]
    return []


def threads_auto(exe):
    """--threads 0 is the auto walk: the same header as saying nothing."""
    def header(argv):
        r = run_exe(exe, argv)
        for line in (r.stdout + r.stderr).splitlines():
            if line.startswith("subgroup  :"):
                return line.strip()
        return None

    base = ["t0", "0", "URFDLB", "--cubestring", SOLVED]
    want, got = header(base), header(base + ["--threads", "0"])
    print("%-30s %s" % ("threads: --threads 0 is auto", got or "(no header)"))
    if want is None or got != want:
        return ["--threads 0 is not the auto walk: %r != %r" % (got, want)]
    return []


def quiet_suppresses(exe):
    """--quiet is the list, not the report: with no name the list is on stdout, and --quiet takes it off."""
    base = ["2", "URFDLB", "--scramble", "R U"]

    def maneuvers(argv):
        r = run_exe(exe, argv)
        found = [l for l in (r.stdout + r.stderr).splitlines() if MANEUVER.match(l)]
        return r.returncode, found

    rc_loud, loud = maneuvers(base)
    rc_quiet, quiet = maneuvers(base + ["--quiet"])
    ok = rc_loud == rc_quiet == 0 and len(loud) == 1 and not quiet
    print("%-30s %s" % ("--quiet takes the list off",
                        "%s printed, %s with --quiet" % (loud, quiet) if ok
                        else "rc=%d/%d loud=%s quiet=%s" % (rc_loud, rc_quiet, loud, quiet)))
    if not ok:
        return ["--quiet did not take the maneuvers off stdout: %r then %r" % (loud, quiet)]
    return []


def cube_of(exe, alg):
    """The cube an algorithm builds, in the only words the solver has for a cube."""
    r = run_exe(exe, ["--print", "--scramble", alg])
    lines = [l.strip() for l in (r.stdout + r.stderr).splitlines() if l.strip()]
    return lines[-1] if lines else ""


def invert_maneuver(alg):
    """The same walk read backwards: the last move undone first, and every move undone."""
    undo = lambda t: t[0] + ("" if t.endswith("'") else "2" if t.endswith("2") else "'")
    return " ".join(undo(t) for t in reversed(alg.split()))


def solutions_really_solve(exe):
    """A printed maneuver has to undo its scramble: --print names the cube they build."""
    bad = []
    checked = 0

    def walk(name, argv, start, want):
        """`want` is the cube the maneuver has to land on; `start` the algorithm it starts from."""
        r = run_exe(exe, argv)
        found = [l.strip() for l in (r.stdout + r.stderr).splitlines() if MANEUVER.match(l.strip())]
        if r.returncode != 0 or not found:
            bad.append("%s: rc=%d, maneuvers %r" % (name, r.returncode, found))
            return 0
        for m in found:
            landed = cube_of(exe, start + " " + m)
            if landed != want:
                bad.append("%s: %s + %s lands on %s" % (name, start, m, landed))
        return len(found)

    for scramble, depth in (("R U", 4), ("R U R' F2", 6), ("R2 U2", 6)):
        checked += walk(scramble, [str(depth), "URFDLB", "--scramble", scramble],
                        scramble, SOLVED)
    # --goal: the same check, only the cube it has to land on is another one
    goal = cube_of(exe, "R2")
    checked += walk("--goal R2", ["6", "URFDLB", "--scramble", "R U",
                                  "--goal", goal], "R U", goal)
    # a goal held turned needs the turn on the end: without it the answer lands on the goal's rename
    for alg in ("z2", "x y"):
        goal = cube_of(exe, alg)
        checked += walk("--goal " + alg, ["3", "URFDLBEMSxyz", "--scramble", "R U",
                                          "--goal", goal], "R U", goal)
    print("%-30s %s" % ("maneuvers land where asked",
                        "%d checked" % checked if not bad else bad[0]))
    return bad


def optimal_under_a_rotation(exe):
    """--optimal where the subgroup's moves carry a rotation: a ban read without the frame drops
    the shortest spelling, and the walk answers with a longer one it calls optimal.

    The length comes from --dump, one token per move, and not from the standard-notation list:
    a move can render as two tokens (U3D1 prints as `E2 D U'` in UDES), so the same maneuver is
    a different number of tokens there. The standard list is still read, to check where it lands.

    Each case is asked twice, the second time for one move less than it answers: a ban that drops
    a walk drops a shorter one, so a search that has lost nothing finds nothing there.
    """
    SG_MANEUVER = re.compile(r"^[URFDLBEMSxyzurfdlb][2']?( [URFDLBEMSxyzurfdlb][2']?)*$")
    DUMP = re.compile(r"^([URFDLB]\d[URFDLB]\d)( [URFDLB]\d[URFDLB]\d)*$")
    # (subgroup, scramble, depth, fewest moves): every one of these used to come back longer
    OPTIMA = (
        ("EMu",  "u E M' u",      8, 4),
        ("EMu",  "u2 M M' E' M",  8, 3),
        ("EMu",  "u M' E u u",    8, 4),
        ("EMu",  "u2 u2 u E' M2", 8, 3),
        ("ESu",  "S E u2 S2 E2",  8, 4),
        ("ESu",  "u S E u2 u'",   8, 4),
        ("EMSu", "M E u2 S2 S2",  8, 3),
        ("EMSu", "u2 E u' M M'",  8, 2),
        # no wide turn, but a column the subgroup renders on a carrier: the frame moves, and a
        # ban that folds without asking the frame loses the shorter spelling here too
        ("URDE", "U' U' U' R R2 U D' E'", 10, 4),
        ("URDE", "E' E2 E2 D R2 E' D' U", 10, 5),
        ("UDES", "S U' E2 D2 D' S2 S2 E'", 10, 3),
        # subgroups whose frames all carry one language, where one frame answers the ban for
        # all twenty-four of them: what is pinned is that writing it everywhere drops no walk
        ("URFDLBE",   "E' U' F2 E D2 D E2 E",    10, 4),
        ("urfdlbEMS", "S f r r' f' r' M2 u2",    10, 4),
        ("urfdlbEMS", "u d2 E2 E' u' f' f2 M",   10, 4),
        ("urfdlbxyz", "l2 b2 x2 u b' d2 y' u",   10, 6),
        ("UR",        "U R' U2 U' R2 U2 R' U'",  10, 7),
        # and subgroups where they do not: claiming one language all the same answers these six,
        # six, six, six, seven and six moves, where three, three, four, four, four and four are
        # what they are. The last prints as six tokens, not five: a move can render as two.
        ("UDEM",  "D2 D' U M2 D2 E2 E2 U2 M",    10, 3),
        ("URDEM", "M' M D' D' E' E2 E' E' M",    10, 3),
        ("URDES", "E U2 S' D E E U' D2 E",       10, 4),
        ("URLEM", "R' M R2 R E L L2 R L",        10, 4),
        ("ESu",   "S E u' S2 E2 S' S' S2 S",     10, 4),
        ("UDES",  "U' D D2 S' U2 S D2 U2 S",     10, 4),
        ("UDES",  "D' U' U' U' S U' S E",        10, 5),
        # subgroups where the ban grew: section 14 lets a frame of the replacement stand in for
        # a frame of the pair when its language holds that frame's. Six of the eight have teeth:
        # a build claiming every frame carries every frame's language answers these longer.
        ("urfdlbS",  "u2 r' S' S' l2 r' r' u u",  10, 5),
        ("udEMS",    "u2 M2 u u' M E2 S2 E u",    10, 4),
        ("urdlMS",   "u' d u d' u2 M l2 l r2",    10, 3),
        ("urdlMS",   "u' r r d' r r' l M2 r'",    10, 5),
        ("urfdlbMS", "S r2 b2 b b M' u r r'",     10, 4),
        ("EMurdl",   "M2 r' M' r u d' E2 r2 u",   10, 5),
        ("EMurdl",   "u2 E2 r' M2 d' E M' M r",   10, 6),
        ("ufdbES",   "E2 d' E E2 S b' f S' u",    10, 4),
    )
    bad = []
    for sg, scramble, depth, want in OPTIMA:
        r = run_exe(exe, [str(depth), sg, "--scramble", scramble, "--optimal"])
        found = [l.strip() for l in (r.stdout + r.stderr).splitlines()
                 if SG_MANEUVER.match(l.strip())]
        if r.returncode != 0 or not found:
            bad.append("%s %s: rc=%d, maneuvers %r" % (sg, scramble, r.returncode, found))
            continue
        landed = cube_of(exe, scramble + " " + found[0])
        d = run_exe(exe, [str(depth), sg, "--scramble", scramble, "--optimal", "--dump"])
        walked = [l.strip() for l in (d.stdout + d.stderr).splitlines()
                  if DUMP.match(l.strip())]
        got = min(len(m.split()) for m in walked) if walked else None
        if got != want or landed != SOLVED:
            bad.append("%s %s: %s moves, want %d (%s lands on %s)" %
                       (sg, scramble, got, want, found[0], landed))
            continue
        # The ban is the only thing here that can drop a walk, and a walk dropped is a shorter
        # one: asked for one move less than the fewest, a complete search answers nothing.
        below = run_exe(exe, [str(want - 1), sg, "--scramble", scramble, "--optimal"])
        short = field(below.stdout + below.stderr, r"solutions\s*:\s*(\d+)")
        if short != "0":
            bad.append("%s %s: %d moves, yet %s answers at depth %d" %
                       (sg, scramble, want, short, want - 1))
    print("%-30s %s" % ("optimal under a rotation",
                        "%d cases at their fewest, none shorter" % len(OPTIMA)
                        if not bad else bad[0]))
    return bad


def carriers_really_solve(exe):
    """The same check for the letters that carry more than one face turn: a wrong sign still spells
    a maneuver the subgroup admits, so only landing on the right cube tells."""
    CARRIER = re.compile(r"^[URFDLBEMSxyzurfdlb][2']?$")
    bad = []
    checked = 0

    def listed(sg, scramble, depth):
        r = run_exe(exe, [str(depth), sg, "--scramble", scramble])
        found = []
        for line in (r.stdout + r.stderr).splitlines():
            s = line.strip()
            toks = s.split()
            if toks and all(CARRIER.match(t) and t[0] in sg for t in toks):
                found.append(s)
        return r.returncode, found

    for sg, scramble, depth in (("URFDLBEMSxyz",   "R U",       4),
                                ("URFDLBurfdlb",    "R U R' F2", 6),
                                ("URFDLBurfdlbxyz", "R U R' F2", 6)):
        rc, found = listed(sg, scramble, depth)
        carried = [m for m in found if any(t[0] not in "URFDLB" for t in m.split())]
        if rc != 0 or not carried:
            bad.append("%s: rc=%d, %d listed, %d carrying" % (sg, rc, len(found), len(carried)))
            continue
        for m in (carried + found)[:4]:
            landed = cube_of(exe, scramble + " " + m)
            checked += 1
            if landed != SOLVED:
                bad.append("%s: %s + %s lands on %s" % (sg, scramble, m, landed))
    print("%-30s %s" % ("carriers land where asked",
                        "%d checked" % checked if not bad else bad[0]))
    return bad


def own_letters_reach(exe):
    """A scramble written in a subgroup's own letters is itself a maneuver in it, so a search given
    that many moves has to report one; a move the subgroup does not really carry answers zero."""
    bad = []
    checked = 0
    for sg, alg in (("URFDLB", "R U"), ("URFDLB", "R U F2 D'"), ("URFDLBEMS", "R U M E'"),
                    ("URFDLBEMS", "M2 E2 S2"), ("URFDLBEMSxyz", "R U x"), ("urfdlb", "r u r'"),
                    ("RUF", "R U F"), ("UD", "U2 D"), ("EMS", "E M S")):
        d = str(len(alg.split()))
        r = run_exe(exe, ["own", d, sg, "--scramble", alg])
        m = re.search(r"solutions\s*:\s*(\d+)", r.stdout + r.stderr)
        got = m.group(1) if m else "?"
        checked += 1
        if r.returncode != 0 or got == "?" or got == "0":
            bad.append("%s %s at %s: rc=%d solutions=%s" % (sg, alg, d, r.returncode, got))
    print("%-30s %s" % ("own letters are reachable",
                        "%d checked" % checked if not bad else bad[0]))
    return bad


def incomplete_path_repeats(exe):
    """A '?' cube walks one of two ways, named by the case's own numbers: the ways it leaves, and
    whether a corner is left unknown.  Asked again it has to answer the same walk: a decision that
    read the clock would leave no node count to hold the solver to."""
    def answer(cs, depth):
        r = run_exe(exe, ["ic", depth, "URFDLB", "--cubestring", cs, "--quiet",
                          "--solhash"])
        out = r.stdout + r.stderr
        return (r.returncode, field(out, r"solutions\s*:\s*(\d+)"),
                field(out, r"solhash\s*:\s*([0-9a-f]+)"), field(out, r"nodes\s*:\s*(\d+)"))

    bad = []
    for what, cs, depth in (("four cubies wiped", MANY_E, "6"),
                            ("three cubies one sticker", PARTIAL_E, "8")):
        runs = [answer(cs, depth) for _ in range(3)]
        # a "?" is a line the run never printed, and three of them compare equal: without this
        # check a report whose labels moved passes having observed nothing
        ok = (all(r == runs[0] for r in runs) and runs[0][0] == 0
              and "?" not in runs[0] and runs[0][1] != "0")
        print("%-30s %s" % ("? cube: " + what,
                            "%s solutions, %s nodes, three times" % (runs[0][1], runs[0][3])
                            if ok else "%s" % (runs,)))
        if not ok:
            bad.append("the same ? case answered three ways on %s: %s" % (what, runs))
    return bad


def incomplete_solutions_fit(exe):
    """An answer to a '?' cube is judged on the stickers it shows: each has to land on the face it
    names, whichever way the hidden ones went.  Which walk answered is not this check's business."""

    def hidden(cs, which):
        return "".join("?" if i in which else c for i, c in enumerate(cs))

    bad = []
    checked = 0
    for scramble, which in (("R U", (8, 9, 20)), ("R U F2 L'", (0, 1, 2, 9, 18, 38, 45))):
        pat = hidden(cube_of(exe, scramble), which)
        r = run_exe(exe, ["6", "URFDLB", "--cubestring", pat, "--optimal"])
        found = [l.strip() for l in (r.stdout + r.stderr).splitlines()
                 if FACE_MANEUVER.match(l.strip())]
        if r.returncode != 0 or not found:
            bad.append("%s: rc=%d, %r" % (scramble, r.returncode, found[:2]))
            continue
        for m in found[:3]:
            landed = cube_of(exe, scramble + " " + m)
            checked += 1
            wrong = [i for i, c in enumerate(pat)
                     if c != "?" and (i >= len(landed) or landed[i] != SOLVED[i])]
            if wrong:
                bad.append("%s + %s leaves sticker %d on %s, not %s"
                           % (scramble, m, wrong[0],
                              landed[wrong[0]:wrong[0] + 1], SOLVED[wrong[0]]))
    print("%-30s %s" % ("? cube: answers fit the pattern",
                        "%d checked" % checked if not bad else bad[0]))
    return bad


# One rule per check, on what a run answers rather than a number: no load case, no clock.
def block_report(exe, argv):
    """One run's block report: rc, maneuvers, nodes, how many of them the subgroup can spell out,
    and the report itself to read a line in.  A line the run never printed reads "?"."""
    r = run_exe(exe, argv)
    out = r.stdout + r.stderr
    return (r.returncode, field(out, r"solutions\s*:\s*(\d+)"), field(out, r"nodes\s*:\s*(\d+)"),
            field(out, r"standard\s*:\s*(\d+)"), out)


def one_cube_two_inputs(exe):
    """Two inputs naming one cube are one question: a --scramble and the cubestring --print prints
    for it, and two spellings of one whole-cube turn.  The inputs differ, the answer must not."""
    pairs = (("M E and the cube it prints", ["scr", "4", "ME", "--scramble", "M E"],
              ["cs", "4", "ME", "--cubestring", cube_of(exe, "M E")]),
             ("L' M' R and x", ["one", "3", "ME", "--scramble", "L' M' R"],
              ["two", "3", "ME", "--scramble", "x"]))
    bad = []
    for what, left, right in pairs:
        a, b = block_report(exe, left)[:4], block_report(exe, right)[:4]
        if a[0] or b[0] or a[1:] != b[1:]:
            bad.append("%s: %s vs %s" % (what, a, b))
    print("%-30s %s" % ("one cube, two inputs",
                        "%d pairs, one answer each" % len(pairs) if not bad else bad[0]))
    return bad


def wide_tail_needs_a_rotation(exe):
    """A wide subgroup spells its tail with x/y/z: without them the answers are the same, but not
    one of them can be written out, which the report says outright."""
    plain = block_report(exe, ["aw", "3", "urfdlb", "--scramble", "R U"])
    turned = block_report(exe, ["awx", "3", "urfdlbxyz", "--scramble", "R U"])
    ok = (plain[0] == turned[0] == 0 and plain[1:3] == turned[1:3] and plain[3] == "0"
          and turned[3] != "0" and "no standard spelling" in plain[4]
          and "no standard spelling" not in turned[4])
    print("%-30s %s" % ("wide tail: spelled or not",
                        "%s of %s solutions spelled" % (turned[3], plain[1]) if ok
                        else "%s vs %s" % (plain[:4], turned[:4])))
    return [] if ok else ["wide tail: %s vs %s" % (plain[:4], turned[:4])]


def tail_spelled_through_the_twin(exe):
    """A z2 tail is spelled as its x2 y2 twin, so a subgroup naming no z still reaches it: which of
    the three axes it spells makes no difference to the answer."""
    groups = ("URFDLBEMSyz", "URFDLBEMSxz", "URFDLBEMSxy", "URFDLBEMSxyz")
    answers = [block_report(exe, ["zt%d" % i, "7", sg, "--scramble", "M2 E2"])[:4]
               for i, sg in enumerate(groups)]
    ok = all(a == answers[0] and a[0] == 0 for a in answers)
    print("%-30s %s" % ("z2 tail: four spellings",
                        "%s solutions, %s spelled" % (answers[0][1], answers[0][3]) if ok
                        else "%s" % (answers,)))
    return [] if ok else ["z2 tail: %s" % (answers,)]


def goal_is_the_other_end(exe):
    """--goal names the cube to end on.  M2 E2 is its own inverse, so walking to it from solved is
    the same question as walking back, and both owe one answer."""
    to_it = block_report(exe, ["g1", "7", "URFDLBEMSxyz", "--cubestring", SOLVED,
                               "--goal", cube_of(exe, "M2 E2")])[:4]
    from_it = block_report(exe, ["g2", "7", "URFDLBEMSxyz", "--scramble", "M2 E2"])[:4]
    ok = to_it == from_it and to_it[0] == 0
    print("%-30s %s" % ("--goal: walked backwards",
                        "%s solutions either way" % to_it[1] if ok
                        else "to it %s, from it %s" % (to_it, from_it)))
    return [] if ok else ["--goal: to it %s, from it %s" % (to_it, from_it)]


def inverses_are_reverses(exe):
    """A cube and its inverse are two cubes, so the two searches differ, yet each side's answers,
    read backwards, have to solve the other side.  Four cubes, both ways."""

    def answers(scramble):
        # no name, so there is no file to write to and the maneuvers come out on stdout
        r = run_exe(exe, ["12", "URFDLB", "--optimal", "--scramble", scramble])
        return [l.strip() for l in (r.stdout + r.stderr).splitlines()
                if FACE_MANEUVER.match(l.strip())]

    bad = []
    checked = 0
    for alg in ("L2 F2 L2 B2 R2 F2 D", "R L D' U' B2 U' D R2 F' L'",
                "F' U2 D L B' D2 U2 B F' D2 B2 D'", "R D F2 B L U' F2 D' U R2 F2 R'"):
        mine, theirs = answers(alg), answers(invert_maneuver(alg))
        if not mine or len(mine) != len(theirs):
            bad.append("%s: %d answers, its inverse %d" % (alg, len(mine), len(theirs)))
            continue
        for m in mine:
            checked += 1
            landed = cube_of(exe, invert_maneuver(alg) + " " + invert_maneuver(m))
            if landed != SOLVED:
                bad.append("%s: %s read backwards leaves %s" % (alg, m, landed))
    print("%-30s %s" % ("a cube and its inverse",
                        "%d answers read backwards" % checked if not bad else bad[0]))
    return bad


def solved_is_already_there(exe):
    """A cube already where it is asked to be is answered by the empty maneuver alone.  A depth of 0
    asks for that, so not one node is expanded to find it."""
    asked = block_report(exe, ["sv", "3", "URFDLB", "--cubestring", SOLVED])
    zero = block_report(exe, ["z0", "0", "URFDLB", "--cubestring", SOLVED])
    ok = asked[0] == zero[0] == 0 and asked[1] == zero[1] == "1" and zero[2] == "0"
    print("%-30s %s" % ("solved: the empty maneuver",
                        "%s maneuver, %s nodes at depth 0" % (asked[1], zero[2]) if ok
                        else "depth 3 %s, depth 0 %s" % (asked[:4], zero[:4])))
    return [] if ok else ["solved cube: depth 3 %s, depth 0 %s" % (asked[:4], zero[:4])]


def more_frames_answer_no_less(exe):
    """A subgroup with more frames measures a cube against the cheapest of them, so adding letters
    to it can only add answers: it may not prune harder than the smaller one."""
    few = block_report(exe, ["mf1", "6", "UEM", "--scramble", "U' M E"])
    many = block_report(exe, ["mf2", "6", "UDLEM", "--scramble", "U' M E"])
    ok = (few[0] == many[0] == 0 and few[1].isdigit() and many[1].isdigit()
          and int(few[1]) > 0 and int(many[1]) >= int(few[1]))
    print("%-30s %s" % ("more frames: no fewer answers",
                        "%s in UEM, %s in UDLEM" % (few[1], many[1]) if ok
                        else "UEM %s, UDLEM %s" % (few[:4], many[:4])))
    return [] if ok else ["more frames: UEM %s, UDLEM %s" % (few[:4], many[:4])]


def hidden_faces_still_answered(exe):
    """Four faces hidden leave more completions than the engine walks one at a time, so the relaxed
    walk answers on its own bounds: a bound read at the wrong slot would cost every answer."""
    bad = []
    for i, cs in enumerate(("????U????FBBFRRFRR????F????????D????????L????LLLUBBUBB",
                            "????U????RFFLRRBLL????F????????D????????L????RRRBBBFFF",
                            "????U????LLLRRRBBB????F????????D????????L????FFFBBFRRR",
                            "????U????DRRDRRDRR????F????????D????????L????BBDBBDBBL",
                            "????U????BLLBRRBRR????F????????D????????L????ULLDBBDBB")):
        r = block_report(exe, ["hf%d" % i, "4", "URFDLB", "--cubestring", cs])
        if r[0] != 0 or not r[1].isdigit() or int(r[1]) == 0:
            bad.append("%s: rc=%d, %s solutions" % (cs[:12], r[0], r[1]))
    print("%-30s %s" % ("? cube: four faces hidden",
                        "all five answered" if not bad else bad[0]))
    return bad


def masked_keeps_every_answer(exe):
    """Hiding a sticker takes a demand away and never adds one, so a cube with its twelve edges
    hidden owes every answer the whole cube has, in any subgroup.  A move set the ct table's
    reduction does not hold for read a distance too high there and answered none at all."""

    def edges_hidden(cs):
        out = list(cs)
        for face in range(6):
            for local in (1, 3, 5, 7):          # the four edge facelets of a face, the rest corners
                out[9 * face + local] = "?"
        return "".join(out)

    bad = []
    for sg, alg in (("RL", "R L'"), ("RL", "R L"), ("RUF", "R U F"), ("RUFL", "R U F L"),
                    ("RUL", "R U L"), ("URFDLB", "R U")):
        depth = str(len(alg.split()))
        cs = cube_of(exe, alg)
        whole = block_report(exe, [depth, sg, "--cubestring", cs, "--optimal"])
        part = block_report(exe, [depth, sg, "--cubestring", edges_hidden(cs), "--optimal"])
        counted = whole[1].isdigit() and part[1].isdigit()
        if not (whole[0] == part[0] == 0 and counted and 0 < int(whole[1]) <= int(part[1])):
            bad.append("%s %s at %s: whole %s, edges hidden %s"
                       % (sg, alg, depth, whole[1], part[1]))
    print("%-30s %s" % ("? cube: hiding edges loses none",
                        "every subgroup answers at least as much" if not bad else bad[0]))
    return bad


def optimal_threads_agree(exe):
    """--optimal over more than one worker: a worker's first maneuver can be one the notation layer
    drops, so the pass has to run it out.  Which maneuver it is may differ; the length may not."""
    TOKEN = re.compile(r"^[URFDLBEMSxyzurfdlb][0-9']?( [URFDLBEMSxyzurfdlb][0-9']?)*$")

    def answer(threads):
        r = run_exe(exe, ["10", "urfdlbz", "--scramble", "E2 u' u' l2 z' r2",
                          "--optimal", "--threads", threads])
        found = [l.strip() for l in (r.stdout + r.stderr).splitlines() if TOKEN.match(l.strip())]
        return r.returncode, [len(m.split()) for m in found]

    one, many = answer("1"), answer("20")
    ok = one[0] == many[0] == 0 and one[1] and one[1] == many[1]
    print("%-30s %s" % ("--optimal: 1 vs 20",
                        "%d moves both ways" % one[1][0] if ok
                        else "1 thread %s, 20 threads %s" % (one, many)))
    if not ok:
        return ["--optimal across thread counts: %s vs %s" % (one, many)]
    return []


def same_name_cases(exe):
    """Two cases called the same: both solutions and both file names have to survive."""
    out = os.path.join(SCRATCH, "dup")
    shutil.rmtree(out, ignore_errors=True)
    r = run_exe(exe, ["--json", "_clivalidation/dup.json", "--out", "_clivalidation/dup"])
    names = sorted(os.listdir(out)) if os.path.isdir(out) else []
    print("%-30s %s" % ("json: two cases, one name", ", ".join(names) or "(no files)"))
    if names != ["alpha.txt", "alpha_2.txt"] or "2 solved" not in r.stdout:
        return ["two cases with one name: %s -- %s" % (names, re.sub(r"\s+", " ", r.stdout)[:90])]
    if any(os.path.getsize(os.path.join(out, n)) == 0 for n in names):
        return ["a same-named case's file came out empty"]
    return []


def empty_scramble_case(exe):
    """An empty scramble is skipped, not failed: presence is not emptiness."""
    r = run_exe(exe, ["--json", "_clivalidation/emptyscr.json", "--out", "_clivalidation"])
    out = r.stdout + r.stderr
    # rc=1: a batch that solved nothing and skipped something has to say so on exit
    ok = r.returncode == 1 and "1 skipped" in out and "give exactly one of" in out
    print("%-30s %s" % ("json: empty scramble", "skipped, rc=1" if ok else "not skipped as it should be"))
    if not ok:
        return ["empty scramble: %s" % re.sub(r"\s+", " ", out)[:90]]
    return []


def other_drive_out(exe):
    """--out onto another drive: the nested folders have to be made. Skipped where temp is on this drive."""
    here = os.path.splitdrive(os.path.abspath(BIN))[0].upper()
    temp = os.path.abspath(tempfile.gettempdir())
    if os.path.splitdrive(temp)[0].upper() == here:
        print("%-30s %s" % ("--out on another drive", "skipped -- temp is on this drive"))
        return None
    base = os.path.join(temp, "_cubolt_outdrive", "x", "y")
    name = "od%d" % int(time.time())          # a fresh name, so an earlier run's file cannot pass
    r = run_exe(exe, [name, "4", "URFDLB", "--scramble", "R U R' U'",
                      "--out", base.replace("\\", "/")])
    out = r.stdout + r.stderr
    written = os.path.isfile(os.path.join(base, name + ".txt"))
    ok = r.returncode == 0 and written and "cannot create" not in out
    print("%-30s %s" % ("--out on another drive", base if ok else "nothing written"))
    if not ok:
        return ["cross-drive --out: %s" % re.sub(r"\s+", " ", out)[:90]]
    return []


def unc_out_blames_the_directory(exe):
    """A UNC path roots past the share: what fails is the directory, not the share above it."""
    unc = "\\\\127.0.0.1\\cubolt-noshare\\deep"    # an absent share refuses, so nothing waits
    r = run_exe(exe, ["uo", "4", "URFDLB", "--scramble", "R U R'", "--out", unc])
    out = r.stdout + r.stderr
    ok = r.returncode == 1 and ("cannot create %s" % unc) in out
    print("%-30s %s" % ("--out to a UNC share", "reports %s" % unc if ok else "reports less"))
    if not ok:
        return ["UNC --out: %s" % re.sub(r"\s+", " ", out)[:90]]
    return []


def json_scramble_default(exe):
    """`--scramble` on the command line is the default a case with no cube of its own runs on."""
    out_dir = os.path.join(SCRATCH, "dflt.txt")
    if os.path.exists(out_dir):
        os.remove(out_dir)
    r = run_exe(exe, ["--json", "_clivalidation/noscr.json", "--out", "_clivalidation",
                      "--scramble", "R"])
    out = r.stdout + r.stderr
    body = ""
    if os.path.isfile(out_dir):
        with open(out_dir) as fh:
            body = fh.read()
    ok = r.returncode == 0 and "1 solved" in out and "0 skipped" in out and "R'" in body
    print("%-30s %s" % ("json: --scramble default",
                        "solved on the command line's R" if ok else "not solved on it"))
    if not ok:
        return ["--scramble did not reach the case: %s" % re.sub(r"\s+", " ", out)[:90]]
    return []


def json_cubestring_default(exe):
    """`--cubestring` on the command line is the default a case with no cube of its own runs on."""
    out_dir = os.path.join(SCRATCH, "dflt.txt")
    if os.path.exists(out_dir):
        os.remove(out_dir)
    r = run_exe(exe, ["--json", "_clivalidation/noscr.json", "--out", "_clivalidation",
                      "--cubestring", ONE_R])
    out = r.stdout + r.stderr
    body = ""
    if os.path.isfile(out_dir):
        with open(out_dir) as fh:
            body = fh.read()
    ok = r.returncode == 0 and "1 solved" in out and "0 skipped" in out and "R'" in body
    print("%-30s %s" % ("json: --cubestring default",
                        "solved on the command line's cube" if ok else "not solved on it"))
    if not ok:
        return ["--cubestring did not reach the case: %s" % re.sub(r"\s+", " ", out)[:90]]
    return []


def run_report(exe, argv):
    """rc, the run's three counts, its solution hash and its whole output: two runs of one question
    have to be the same run, so a check asks for more than the numbers."""
    rc, solutions, nodes, standard, out = block_report(exe, argv)
    m = re.search(r"solhash\s*:\s*([0-9a-f]+)", out)
    return rc, solutions, nodes, standard, (m.group(1) if m else "?"), out


def threads_one_and_many(exe):
    """One worker and twenty walk the same tree, so a case owes the same answer both ways: the node
    count is what a split that lost a subtree, or walked one twice, would move."""
    runs = (("a plain cube",       ["th1", "6", "URFDLB", "--scramble", "R U R'"]),
            ("four cubies wiped",  ["th2", "6", "URFDLB", "--cubestring", MANY_E]),
            ("three stickers hidden", ["th3", "8", "UM", "--cubestring", PARTIAL_E]))
    bad = []
    for what, argv in runs:
        argv = argv + ["--solhash"]
        one, many = run_report(exe, argv + ["--threads", "1"])[:5], \
                    run_report(exe, argv + ["--threads", "20"])[:5]
        if one[0] or many[0] or one != many:
            bad.append("%s: 1 thread %s, 20 threads %s" % (what, one, many))
    print("%-30s %s" % ("--threads 1 vs 20",
                        "%d cases, one answer each" % len(runs) if not bad else bad[0]))
    return bad


def both_directions_one_set(exe):
    """--scramble A and --goal <the cube A builds> ask one question from either end, and the walk
    opens on the deeper end: so one of these two runs is the reversed one.  Compared by the cube an
    answer builds, because the two ends spell the same walks in their own order."""

    def answers(argv):
        r = run_exe(exe, argv)
        return [l.strip() for l in (r.stdout + r.stderr).splitlines()
                if FACE_MANEUVER.match(l.strip())]

    def cubes_built(algs):
        path = os.path.join(SCRATCH, "bothdirs.txt")
        with open(path, "w", newline="\n") as fh:
            fh.write("\n".join(algs) + "\n")
        r = run_exe(exe, ["--scramble-list", path])
        return [l.strip() for l in (r.stdout + r.stderr).splitlines() if l.strip()]

    # These four each open on the other end from the one the goal-run opens on: a short scramble
    # reads deeper from the solved side in both, and would hold the two runs to one direction.
    cases = (("L2 F2 L2 B2 R2 F2 D", "8", []),
             ("R L D' U' B2 U' D R2 F' L'", "12", ["--optimal"]),
             ("F' U2 D L B' D2 U2 B F' D2 B2 D'", "12", ["--optimal"]),
             ("R D F2 B L U' F2 D' U R2 F2 R'", "12", ["--optimal"]))
    bad = []
    checked = 0
    for alg, depth, opt in cases:
        mine = answers([depth, "URFDLB"] + opt + ["--scramble", alg])
        theirs = answers([depth, "URFDLB"] + opt + ["--cubestring", SOLVED,
                                                    "--goal", cube_of(exe, alg)])
        if not mine or not theirs:
            bad.append("%s: %d answers one way, %d the other" % (alg, len(mine), len(theirs)))
            continue
        built = cubes_built(mine + [invert_maneuver(m) for m in theirs])
        if len(built) != len(mine) + len(theirs):
            bad.append("%s: %d maneuvers, %d cubes back" % (alg, len(mine) + len(theirs),
                                                            len(built)))
            continue
        checked += len(mine)
        if sorted(built[:len(mine)]) != sorted(built[len(mine):]):
            bad.append("%s: the two ends build different cubes" % alg)
    print("%-30s %s" % ("--goal: the walk's other end",
                        "%d answers, either way" % checked if not bad else bad[0]))
    return bad


def the_two_metrics(exe):
    """A subgroup naming a slice runs the STM engine, one without runs the FTM one.  STM counts a
    slice as one move and carries the face turns too, so the shortest answer it owes is never the
    longer of the two -- and the two table sets are built apart from each other."""
    cases = ("M2 E2", "R U M E S", "R U R'", "R2 U2 F2")
    rows = []
    for i, alg in enumerate(cases):
        for tag, sg in (("ftm", "URFDLBxyz"), ("stm", "URFDLBEMSxyz")):
            rows.append(("%s%d" % (tag, i), sg, alg))
    path = os.path.join(SCRATCH, "metrics.txt")
    with open(path, "w", newline="\n") as fh:
        for name, sg, alg in rows:
            fh.write("%s 8 %s --scramble %s --optimal\n" % (name, sg, alg))
    r = run_exe(exe, ["--bench", path])
    got = {}
    for line in (r.stdout + r.stderr).splitlines():
        f = line.split("\t")
        if len(f) >= 7:
            got[f[0]] = f[6]
    bad = []
    for i, alg in enumerate(cases):
        ftm, stm = got.get("ftm%d" % i, "?"), got.get("stm%d" % i, "?")
        if not (ftm.isdigit() and stm.isdigit()) or int(stm) > int(ftm):
            bad.append("%s: %s moves in FTM, %s in STM" % (alg, ftm, stm))
    print("%-30s %s" % ("FTM vs STM: the shortest",
                        ", ".join("%s/%s" % (got.get("ftm%d" % i, "?"), got.get("stm%d" % i, "?"))
                                  for i in range(len(cases))) if not bad else bad[0]))
    return bad


def one_completion_answers_once(exe):
    """A sticker the rest still determine leaves one completion behind, and the enumeration has to
    come back with that one alone: then the run is the run the whole cube gets, node for node."""
    bad = []
    for alg, spot in (("R U", 8), ("R U F2 L'", 5), ("R2 U2", 20)):
        cube = cube_of(exe, alg)
        depth = str(len(alg.split()) + 4)
        whole = run_report(exe, [depth, "URFDLB", "--cubestring", cube,
                                 "--out", "_clivalidation", "--solhash"])[:5]
        hidden = run_report(exe, [depth, "URFDLB", "--cubestring",
                                  cube[:spot] + "?" + cube[spot + 1:],
                                  "--out", "_clivalidation", "--solhash"])[:5]
        if whole[0] or whole != hidden:
            bad.append("%s hiding sticker %d: %s vs %s" % (alg, spot, whole, hidden))
    print("%-30s %s" % ("? cube: one completion",
                        "the whole cube's run, node for node" if not bad else bad[0]))
    return bad


def the_bound_never_overestimates(exe):
    """`from` is the bound the walk opens on, read off the flip, twist and slice-sorted
    coordinates: were one of them read from the wrong slot, the walk would prune its own shortest
    answers away, and the optimum would come out under the bound it was pruned by."""
    cases = (("R U R'", "6"), ("R U F2 L'", "8"), ("R2 U2 F2 D2", "8"),
             ("R U' R' U R U2 R'", "10"), ("F' U2 D L B' D2", "9"))
    path = os.path.join(SCRATCH, "bound.txt")
    with open(path, "w", newline="\n") as fh:
        for i, (alg, depth) in enumerate(cases):
            fh.write("b%d %s URFDLB --scramble %s --optimal\n" % (i, depth, alg))
    r = run_exe(exe, ["--bench", path])
    best = {}
    for line in (r.stdout + r.stderr).splitlines():
        f = line.split("\t")
        if len(f) >= 7 and f[0].startswith("b"):
            best[f[0]] = f[6]
    bad = []
    for i, (alg, depth) in enumerate(cases):
        head = run_exe(exe, [depth, "URFDLB", "--scramble", alg, "--optimal",
                             "--out", "_clivalidation"])
        m = re.search(r"from (\d+)", head.stdout + head.stderr)
        lo, hi = (m.group(1) if m else "?"), best.get("b%d" % i, "?")
        if not (lo.isdigit() and hi.isdigit()) or int(lo) > int(hi):
            bad.append("%s: bound %s, shortest %s" % (alg, lo, hi))
    print("%-30s %s" % ("from: the walk's own bound",
                        "never past the shortest answer" if not bad else bad[0]))
    return bad


def dump_carries_the_standard_list(exe):
    """--dump names a solution by the columns the walk went through, and the standard list spells
    those same walks.  One way only: a walk this subgroup cannot write is in the dump and not in
    the list."""
    AXES = (("U", "D"), ("R", "L"), ("F", "B"))

    def columns_of(m):
        """One column a move: a walk keeps two turns of one axis apart, as it walked them."""
        out = []
        for t in m.split():
            ax = "URFDLB".index(t[0]) % 3
            amt = 3 if t.endswith("'") else 2 if t.endswith("2") else 1
            a, b = AXES[ax]
            out.append("%s%d%s%d" % (a, amt if t[0] in "URF" else 0,
                                     b, 0 if t[0] in "URF" else amt))
        return " ".join(out)

    bad = []
    checked = 0
    for alg, depth in (("R U R'", "8"), ("R U", "6"), ("R2 U2", "6")):
        listed = run_exe(exe, [depth, "URFDLB", "--scramble", alg])
        std = [l.strip() for l in (listed.stdout + listed.stderr).splitlines()
               if MANEUVER.match(l.strip())]
        dumped = set()
        for line in (run_exe(exe, [depth, "URFDLB", "--scramble", alg, "--dump"]).stdout
                     ).splitlines():
            s = line.strip()
            if s and all(re.match(r"^[URF][0-9][DLB][0-9]$", t) for t in s.split()):
                dumped.add(s)
        if listed.returncode or not std or not dumped:
            bad.append("%s: %d listed, %d dumped" % (alg, len(std), len(dumped)))
            continue
        for m in std:
            checked += 1
            if columns_of(m) not in dumped:
                bad.append("%s: %s walks %s, which the dump does not carry"
                           % (alg, m, columns_of(m)))
    print("%-30s %s" % ("--dump: the standard list's",
                        "%d spellings found in it" % checked if not bad else bad[0]))
    return bad


def check_builds_nothing():
    """`mktables --check` against a missing table: a verdict, never a build."""
    exe = have_exe("mktables.exe", "mktables --check")
    if not exe:
        return None
    empty = os.path.join(SCRATCH, "checkempty")
    fresh_dir(empty)
    # a table is sought beside the exe, so a copy with none beside it is what makes one missing
    lone = os.path.join(SCRATCH, "lone")
    fresh_dir(lone)
    lone_exe = os.path.join(lone, "mktables.exe")
    shutil.copy2(exe, lone_exe)
    r = run_exe(lone_exe, ["--check", "ct_clssym"], cwd=empty)
    # the exe's folder, not the cwd: a builder names its tables beside itself
    left = sorted(set(os.listdir(lone)) - {os.path.basename(lone_exe)})
    ok = "MISSING" in r.stdout and not left
    print("%-30s %s" % ("mktables --check missing",
                        "MISSING, wrote nothing" if ok
                        else "wrote %s" % (", ".join(left) or "(no MISSING verdict)")))
    if not ok:
        return ["mktables --check built what it was asked to check: %s"
                % (", ".join(left) or re.sub(r"\s+", " ", r.stdout)[:90])]
    return []


def mktables_flags():
    """mktables' own arguments; --check rides along so a refused case cannot become a build."""
    exe = have_exe("mktables.exe", "mktables flags")
    if not exe:
        return None
    empty = os.path.join(SCRATCH, "mtflags")
    fresh_dir(empty)
    REFUSED = [
        ("--threads needs a count", ["--threads"], "--threads needs a count"),
        ("--threads negative",      ["--threads", "-4"], "--threads is not a number"),
        ("--threads not a number",  ["--threads", "abc"], "--threads is not a number"),
        ("--threads past the pool", ["--threads", "65"], "--threads is 65, outside 0 to 64"),
        ("table name too long",     ["x" * 32], "table name is too long"),
        ("too many table names",    [chr(ord("a") + i) for i in range(17)],
                                    "too many table names"),
    ]
    failures = []
    for name, argv, want in REFUSED:
        r = run_exe(exe, ["--check"] + argv, cwd=empty)
        said = re.sub(r"\s+", " ", r.stdout + r.stderr)
        ok = r.returncode == 1 and want in said
        print("%-30s %s" % (name, "refused" if ok else "accepted -- %s" % said[:70]))
        if not ok:
            failures.append("mktables did not refuse %s" % name)
    r = run_exe(exe, ["--help"], cwd=empty)
    said = re.sub(r"\s+", " ", r.stdout + r.stderr)
    ok = r.returncode == 0 and "usage:" in said
    print("%-30s %s" % ("mktables --help", "answered" if ok else "not -- %s" % said[:70]))
    if not ok:
        failures.append("mktables did not answer --help")
    return failures


def main():
    exe = os.path.join(BIN, "search.exe")
    if not os.path.exists(exe):
        # not built is not passing: a green gate over a missing binary says nothing ran
        print("fail: %s is not built -- no case ran" % exe)
        return 1

    fresh_dir(SCRATCH)
    for fname, body in WRITTEN_FILES.items():
        with open(os.path.join(SCRATCH, fname), "w", newline="\n") as fh:
            fh.write(body)
    # Written its own way: the byte order mark is not something the text line above can spell.
    with open(os.path.join(SCRATCH, "bom.json"), "wb") as fh:
        fh.write(b"\xef\xbb\xbf" +
                 ('[{"name":"bom","subgroup":"URFDLB","depth":0,'
                  '"cubestring":"%s"}]' % SOLVED).encode("utf-8"))
    with open(os.path.join(SCRATCH, "nulbyte.txt"), "wb") as fh:
        fh.write(b"R U\0 R'\n")
    # a directory where one case's solutions file wants to be, the outdir itself in order
    os.mkdir(os.path.join(SCRATCH, "willfail.txt"))

    # A stray left by an earlier run would fail this pass for something that did not happen now.
    for stale in ("ESCAPED.txt",):
        try:
            os.remove(os.path.join(BIN, stale))
        except OSError:
            pass
    in_bin_before = set(os.listdir(BIN))

    ran = 0
    failed = 0
    skipped = 0
    failures = []

    print("%-30s %-6s %s" % ("case", "want", "result"))
    print("-" * 78)
    for name, argv, want_rc, want_text in CASES:
        ran += 1
        r = run_exe(exe, argv)
        out = r.stdout + r.stderr
        ok_rc = (r.returncode == want_rc)
        ok_txt = (want_text in out)
        if ok_rc and ok_txt:
            print("%-30s rc=%-4d ok" % (name, want_rc))
        else:
            failed += 1
            detail = []
            if not ok_rc:
                detail.append("got rc=%d" % r.returncode)
            if not ok_txt:
                detail.append("missing %r" % want_text)
            snippet = re.sub(r"\s+", " ", out.strip())[:110]
            print("%-30s rc=%-4d fail (%s)  output: %s" % (name, want_rc, ", ".join(detail), snippet))
            failures.append(name)

    # the rest are checks too; one answers None where it cannot run, leaving both counts alone.
    extra = (agreement, solhash_is_asked_for, threads_auto, quiet_suppresses,
             solutions_really_solve, optimal_under_a_rotation,
             carriers_really_solve, own_letters_reach, incomplete_path_repeats,
             incomplete_solutions_fit, one_cube_two_inputs, wide_tail_needs_a_rotation,
             tail_spelled_through_the_twin, goal_is_the_other_end,
             inverses_are_reverses, solved_is_already_there, more_frames_answer_no_less,
             hidden_faces_still_answered, masked_keeps_every_answer, optimal_threads_agree,
             same_name_cases, empty_scramble_case,
             json_scramble_default, json_cubestring_default, other_drive_out,
             unc_out_blames_the_directory,
             threads_one_and_many, both_directions_one_set, the_two_metrics,
             one_completion_answers_once, the_bound_never_overestimates,
             dump_carries_the_standard_list,
             check_builds_nothing, mktables_flags)
    no_exe = (check_builds_nothing, mktables_flags)
    print()
    for check in extra:
        got = check() if check in no_exe else check(exe)
        if got is None:
            skipped += 1
            continue
        ran += 1
        if got:
            failed += 1
            failures.extend(got)
    print()

    # taken from the whole directory: a file written under another name was never looked for
    ran += 1
    strays = sorted(set(os.listdir(BIN)) - in_bin_before)
    if strays:
        print("\nfail: written outside the output directory: %s" % ", ".join(strays))
        failures.append("no stray file")
        failed += 1
    else:
        print("\nno file written outside the output directory   ok")

    written = sorted(os.listdir(SCRATCH)) if os.path.isdir(SCRATCH) else []
    print("files in --out: %s" % (", ".join(written) if written else "(none expected of the failures)"))
    # main() empties it before writing; a cleanup here would come after the verdict.

    print("-" * 78)
    if failures:
        print("%d of %d checks failed: %s" % (failed, ran, ", ".join(failures)))
        return 1
    print("all %d checks pass%s" % (ran, ", %d skipped" % skipped if skipped else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
