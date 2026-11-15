#!/usr/bin/env bash
#
# PPU frame harness for QtreGB.
#
# Runs the Mealybug PPU acceptance ROMs and dmg-acid2, dumps the 160x144
# framebuffer as a binary PGM (P5) and compares it against the expected
# screenshot. The runner writes the dump when it reaches the magic breakpoint
# (opcode 0x40); see mts/README.md.
#
# Usage:
#   mts/compare_ppu_frames.sh [rom.gb ...]
#
# With no arguments it compares dmg-acid2 plus every vendor/mealybug/ppu/*.gb
# that has a matching expected PNG.
#
# Environment:
#   RUNNER=...       runner binary (default: <root>/build/mts_runner)
#   VENDOR=...       vendor root (default: <root>/vendor)
#   OUTDIR=...       PGM output directory (default: /tmp/qtrebg_frames)
#   FRAME_DELAY=N    extra T-cycles before the dump (70224 = one frame)
#   QUIET=1          print only failures and the summary
#   REPORT=file      write a TSV report (name, status, diff, expected, frame)
#
# Exit status: 0 if every compared ROM has diff=0, 1 otherwise.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNNER="${RUNNER:-$ROOT/build/mts_runner}"
VENDOR="${VENDOR:-$ROOT/vendor}"
OUTDIR="${OUTDIR:-/tmp/qtrebg_frames}"
FRAME_DELAY="${FRAME_DELAY:-0}"
QUIET="${QUIET:-0}"
REPORT="${REPORT:-}"

mkdir -p "$OUTDIR"

if [ ! -x "$RUNNER" ]; then
	echo "runner not found: $RUNNER (build it with: make -C mts)" >&2
	exit 2
fi
if ! command -v magick >/dev/null 2>&1; then
	echo "ImageMagick 'magick' not found in PATH" >&2
	exit 2
fi

if [ -n "$REPORT" ]; then
	printf 'name\tstatus\tdiff\texpected\tframe\n' > "$REPORT"
fi

passed=0
failed=0
skipped=0

report_line()
{
	local name="$1"
	local status="$2"
	local diff="$3"
	local expected="$4"
	local frame="$5"

	if [ -n "$REPORT" ]; then
		printf '%s\t%s\t%s\t%s\t%s\n' "$name" "$status" "$diff" "$expected" "$frame" >> "$REPORT"
	fi
}

expected_for()
{
	local rom="$1"
	local base
	local dir
	base="$(basename "$rom" .gb)"
	dir="$(dirname "$rom")"

	for candidate in \
		"$dir/${base}_dmg_blob.png" \
		"$dir/${base}-dmg.png" \
		"$dir/${base}.png"
	do
		if [ -f "$candidate" ]; then
			printf '%s\n' "$candidate"
			return
		fi
	done

	printf '%s\n' "$dir/${base}_dmg_blob.png"
}

compare_one()
{
	local name="$1"
	local rom="$2"
	local expected="$3"
	local frame="$OUTDIR/$name.pgm"

	if [ ! -f "$expected" ]; then
		[ "$QUIET" = "1" ] || printf '%-38s SKIP   (no expected PNG)\n' "$name"
		skipped=$((skipped + 1))
		report_line "$name" "SKIP" "-" "$expected" "$frame"
		return
	fi

	rm -f "$frame"
	MTS_FRAME_OUT="$frame" MTS_FRAME_DELAY="$FRAME_DELAY" \
		"$RUNNER" "$rom" >/dev/null 2>&1 || true

	if [ ! -f "$frame" ]; then
		printf '%-38s FAIL   (no frame: breakpoint 0x40 not reached)\n' "$name"
		failed=$((failed + 1))
		report_line "$name" "FAIL" "no-frame" "$expected" "$frame"
		return
	fi

	local metric
	metric="$(magick compare -metric AE "$frame" "$expected" null: 2>&1 || true)"
	local diff="${metric%% *}"

	if ! [[ "$diff" =~ ^[0-9]+$ ]]; then
		printf '%-38s ERROR  (%s)\n' "$name" "$metric"
		failed=$((failed + 1))
		report_line "$name" "ERROR" "compare-failed" "$expected" "$frame"
		return
	fi

	if [ "$diff" = "0" ]; then
		[ "$QUIET" = "1" ] || printf '%-38s PASS   diff=0\n' "$name"
		passed=$((passed + 1))
		report_line "$name" "PASS" "0" "$expected" "$frame"
	else
		printf '%-38s FAIL   diff=%s\n' "$name" "$diff"
		failed=$((failed + 1))
		report_line "$name" "FAIL" "$diff" "$expected" "$frame"
	fi
}

if [ "$#" -gt 0 ]; then
	for rom in "$@"; do
		if [ ! -f "$rom" ]; then
			echo "missing ROM: $rom" >&2
			failed=$((failed + 1))
			continue
		fi
		name="$(basename "$rom" .gb)"
		compare_one "$name" "$rom" "$(expected_for "$rom")"
	done
else
	compare_one "dmg-acid2" \
		"$VENDOR/dmg-acid2/dmg-acid2.gb" \
		"$VENDOR/dmg-acid2/dmg-acid2-dmg.png"

	for rom in "$VENDOR"/mealybug/ppu/*.gb; do
		[ -e "$rom" ] || continue
		name="$(basename "$rom" .gb)"
		compare_one "$name" "$rom" "$(expected_for "$rom")"
	done
fi

echo
printf '%d passed, %d failed, %d skipped\n' "$passed" "$failed" "$skipped"

[ "$failed" -eq 0 ]
