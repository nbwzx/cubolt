# Cubolt -- Makefile wrapping `zig cc` (clang + mingw-w64); POSIX shell recipes: Git Bash or MSYS.

ZIG     ?= zig
TARGET  ?= x86_64-windows-gnu

CC       = $(ZIG) cc
RC       = $(ZIG) rc

CFLAGS   = -target $(TARGET) -O3 -Iinclude -Wall -Wextra -Wpedantic -Werror
# the update check (include/update.h) makes one HTTPS request through WinINet
NET_LIBS = -lwininet
GUI_LIBS = -lgdi32 -luser32 -lcomdlg32 -lcomctl32 -lshell32 -lole32 $(NET_LIBS) -Wl,--subsystem,windows

# `make syntax` needs a C compiler on PATH (override SYNTAX_CC= / SYNTAX_WARN=).
PYTHON    ?= python

SRC = src
INC = include
RES = res
BIN = bin

SYNTAX_CC    ?= gcc
# -c, not -fsyntax-only: the middle-end warnings need the code written out.
SYNTAX_CFLAGS = -std=gnu11 -c -O2 -D_WIN32_WINNT=0x0A00 -I$(INC)
SYNTAX_WARN  ?= -Wall -Wextra -Wpedantic -Werror
SYNTAX_SRC    = $(wildcard $(SRC)/*.c)

BINS      = $(BIN)/search.exe $(BIN)/mktables.exe $(BIN)/cubolt.exe
HEADERS   = $(wildcard $(INC)/*.h)
# What mktables.c reads: a GUI header is no reason to rebuild the table builder.
MKT_HEADERS = $(INC)/cube.h $(INC)/core.h $(INC)/host.h $(INC)/render.h \
              $(INC)/tablesum.h $(INC)/version.h

.PHONY: all tables check layout bench test installer syntax clean distclean

# No half-written target survives a failed recipe: make would call it up to date next time.
.DELETE_ON_ERROR:

all: $(BINS) $(BIN)/cubolt.ico
	@echo "Build succeeded -> $(BIN)/  (search, mktables, cubolt)"

# --- compiled resources ---
# cubolt.exe finds the icon in its own folder at run time, so bin/ keeps a copy of it.

$(BIN)/icon.res: $(RES)/icon.rc $(RES)/cubolt.ico
	@mkdir -p $(BIN)
	$(RC) -fo $@ $<

$(BIN)/cubolt.res: $(RES)/cubolt.rc $(RES)/cubolt.exe.manifest $(INC)/version.h
	@mkdir -p $(BIN)
	$(RC) -I$(INC) -fo $@ $<

$(BIN)/cubolt.ico: $(RES)/cubolt.ico
	@mkdir -p $(BIN)
	cp -f $< $@

# --- binaries ---

$(BIN)/search.exe: $(SRC)/search.c $(HEADERS) $(BIN)/icon.res
	$(CC) $(CFLAGS) -o $@ $< $(BIN)/icon.res $(NET_LIBS)

$(BIN)/mktables.exe: $(SRC)/mktables.c $(MKT_HEADERS) $(BIN)/icon.res
	$(CC) $(CFLAGS) -o $@ $< $(BIN)/icon.res

$(BIN)/cubolt.exe: $(SRC)/gui.c $(SRC)/mktables.c $(HEADERS) $(BIN)/icon.res $(BIN)/cubolt.res
	$(CC) $(CFLAGS) $(GUI_LIBS) -o $@ $(SRC)/gui.c $(BIN)/icon.res $(BIN)/cubolt.res

# --- helper targets ---

tables: $(BIN)/mktables.exe
	cd $(BIN) && ./mktables.exe

check: $(BIN)/cubolt.exe $(BIN)/tables.manifest
	cd $(BIN) && ./cubolt.exe --selftest

layout: $(BIN)/cubolt.exe
	cd $(BIN) && ./cubolt.exe --layout

bench: $(BIN)/search.exe $(BIN)/tables.manifest
	$(PYTHON) tests/bench.py --check

# Stands for the tables: rebuilds on mktables.c's headers; | mktables.exe, whose clock make skips.
$(BIN)/tables.manifest: $(SRC)/mktables.c $(MKT_HEADERS) | $(BIN)/mktables.exe
	cd $(BIN) && ./mktables.exe

test: $(BIN)/search.exe $(BIN)/mktables.exe $(BIN)/tables.manifest
	$(PYTHON) tests/cli_validation_test.py

# Version from version.h, not a copy here to bump;  make installer VERSION=1.2.3  overrides.
VERSION ?= $(shell awk '$$2 ~ /^CUBOLT_VERSION_(MAJOR|MINOR|PATCH)$$/ {printf "%s%s", (n++ ? "." : ""), $$3}' $(INC)/version.h)
installer: $(BINS) $(BIN)/cubolt.ico
	cd installer && makensis -DVERSION=$(VERSION) cubolt.nsi

syntax:
	@for f in $(SYNTAX_SRC); do $(SYNTAX_CC) $(SYNTAX_CFLAGS) $(SYNTAX_WARN) $$f -o /dev/null || exit 1; done

clean:
	rm -f $(BIN)/*.exe $(BIN)/*.res $(BIN)/*.pdb $(BIN)/cubolt.ico

distclean: clean
	rm -rf $(BIN)/*
