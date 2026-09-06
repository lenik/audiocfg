# SPDX-License-Identifier: AGPL-3.0-or-later
# Portable MinGW-w64 build: <pkg>_mingw-<ver>.exe
#
# Native Windows/MSYS: meson without a cross file.
# Linux: meson --cross-file meson-cross.ini (mingw-w64).
# Skips cleanly when MinGW cross deps (bas-c, libpulse, …) are unavailable.

include ../../lib/common.mk

KIND    := mingw
OUTDIR  := $(CURDIR)/out
STAGE   := $(CURDIR)/stage
BUILD   := $(CURDIR)/build
CROSS   := $(CURDIR)/meson-cross.ini
ARTIFACT := $(OUTDIR)/$(NAME)_mingw-$(VERSION).exe
MINGW_CC := $(shell command -v x86_64-w64-mingw32-gcc 2>/dev/null)

.PHONY: all local stage exe clean

all:
	"$(HOST_SH)" run $(KIND) -- $(MAKE) -C "$(CURDIR)" local

local: stage
	@set -e; \
	if [ -f "$(STAGE)/.skipped" ]; then \
		echo "mingw: skipped (no artifact)"; \
		mkdir -p "$(OUTDIR)"; \
		exit 0; \
	fi; \
	mkdir -p "$(OUTDIR)"; \
	exes=$$(find "$(STAGE)" -type f -name '*.exe' | sort); \
	n=$$(printf '%s\n' "$$exes" | sed '/^$$/d' | wc -l); \
	if [ "$$n" -eq 1 ]; then \
		cp "$$exes" "$(ARTIFACT)"; \
	elif [ -n "$$exes" ] && [ -f "$(STAGE)/bin/$(NAME).exe" ]; then \
		cp "$(STAGE)/bin/$(NAME).exe" "$(ARTIFACT)"; \
	elif command -v 7z >/dev/null 2>&1; then \
		rm -f "$(ARTIFACT)"; \
		( cd "$(STAGE)" && 7z a -sfx "$(ARTIFACT)" . ); \
	elif command -v zip >/dev/null 2>&1; then \
		rm -f "$(ARTIFACT)"; \
		( cd "$(STAGE)" && zip -r -9 "$(ARTIFACT)" . ); \
		echo "mingw: packed stage as zip-format $(ARTIFACT)" >&2; \
	else \
		echo "mingw: no .exe in stage and no 7z/zip to pack" >&2; \
		exit 1; \
	fi; \
	echo "$(ARTIFACT)"

stage:
	rm -rf "$(STAGE)" "$(BUILD)"
	mkdir -p "$(STAGE)" "$(BUILD)"
	@set -e; \
	cd "$(SRCDIR)"; \
	if [ -n "$$OS" ] && [ "$$OS" = Windows_NT ]; then \
		meson setup "$(BUILD)" --prefix=/ --buildtype=release; \
	elif [ -n "$(MINGW_CC)" ]; then \
		if ! meson setup "$(BUILD)" --cross-file "$(CROSS)" --prefix=/ --buildtype=release; then \
			echo "mingw: meson setup failed (cross deps unavailable); skipping" >&2; \
			touch "$(STAGE)/.skipped"; \
			exit 0; \
		fi; \
	else \
		echo "mingw: need MSYS/MinGW or x86_64-w64-mingw32-gcc" >&2; \
		exit 1; \
	fi; \
	meson compile -C "$(BUILD)"; \
	DESTDIR="$(STAGE)" meson install -C "$(BUILD)"

exe: local

clean:
	rm -rf "$(OUTDIR)" "$(STAGE)" "$(BUILD)"
