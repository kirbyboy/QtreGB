# Mooneye Test Suite runner

Runs the `acceptance/timer` **and `acceptance/interrupts`** ROMs from the
[Mooneye Test Suite](https://github.com/Gekkio/mooneye-test-suite) against the
QtreGB core, without SDL or a graphical interface.

## Setup

The ROMs are **not** compiled from source: they are downloaded already assembled
from the author's server. Neither `wla-gb` nor Rust is needed.

```sh
BUILD=$(curl -sL https://gekkio.fi/files/mooneye-test-suite/ \
        | grep -oE 'mts-[0-9a-z-]+/' | tail -1 | tr -d '/')
mkdir -p vendor/mts
curl -sL -o /tmp/mts.tar.xz \
  "https://gekkio.fi/files/mooneye-test-suite/$BUILD/$BUILD.tar.xz"
tar -xJf /tmp/mts.tar.xz -C vendor/mts
```

`vendor/` is in `.gitignore`, so the ROMs are not versioned. The `Makefile`
resolves the build number with a wildcard over `vendor/mts/`, so it does not
have to be edited when updating the suite.

## Usage

```sh
make run_mts
```

It also accepts individual ROMs:

```sh
build/mts_runner vendor/mts/*/acceptance/timer/tima_reload.gb
```

It exits with code 0 only if all of them pass.

## Optional DMG boot ROM

Set `QTR_BIOS` to a 256-byte DMG boot ROM to run it before each test ROM:

```sh
QTR_BIOS="$PWD/roms/bios/[BIOS] Nintendo Game Boy Boot ROM (World).gb" make run_mts
```

The runner fails with `BIOS ERROR` if the file is missing or is not 256 bytes.
With a boot ROM it uses a 60 M T-cycle budget by default (the DMG boot takes
about 23.4 M T-cycles); `MTS_MAX_T` overrides it.

## How the verdict is read

`common/lib/quit.s` leaves the result in the registers right before the first
`magic_breakpoint` (`LD B, B`, opcode `0x40`), which happens **before** the dump
over the serial port:

- success: B/C/D/E/H/L = `3, 5, 8, 13, 21, 34` (Fibonacci)
- failure: B/C/D/E/H/L = `0x42`

The runner detects it by checking whether the CPU is free (`is_end_opcode()`)
and the opcode about to run is `0x40`, in which case the registers still hold the
verdict.

## Assertion diff

Tests with `setup_assertions` push the registers onto HRAM and leave the
expected value of each assertion there. The map is fixed for the whole suite (it
comes from the `.sym` file shipped with each ROM: `SP = hram.regs_save + 8`, then
`push hl/de/bc/af`):

| Bit | Reg | Actual | Expected |
| --- | --- | ------ | -------- |
| 0 | A | `0xFF81` | `0xFF8A` |
| 1 | F | `0xFF80` | `0xFF89` |
| 2 | B | `0xFF83` | `0xFF8C` |
| 3 | C | `0xFF82` | `0xFF8B` |
| 4 | D | `0xFF85` | `0xFF8E` |
| 5 | E | `0xFF84` | `0xFF8D` |
| 6 | H | `0xFF87` | `0xFF90` |
| 7 | L | `0xFF86` | `0xFF8F` |

Mask of requested assertions at `0xFF88`. Note: the bit is set when the
assertion is **requested** (`set n, (hl)` in `assert_*`), not when it passes.

## Diagnostic trace

`MTS_TRACE=1` prints every TIMA change with its exact cycle. It is useful to
align the emulator's behaviour with the values the test expects:

```sh
MTS_TRACE=1 build/mts_runner vendor/mts/*/acceptance/timer/tim01_div_trigger.gb
```

Mind the volume: the serial dump of `quit_check_asserts` is ~170 bytes at
8192 baud, i.e. ~697000 T-cycles of activity that no longer belong to the test.
The body of the real test is in the first ~330 cycles; the last lines are the
dump's wait loop, not the test.

## PPU frame harness

The PPU acceptance ROMs do not leave a verdict in the registers: they draw a
frame and hit the magic breakpoint. With `MTS_FRAME_OUT` set, the runner dumps
the 160x144 framebuffer as a binary PGM (P5) at that moment and exits as PASS:

```sh
MTS_FRAME_OUT=/tmp/frame.pgm build/mts_runner vendor/dmg-acid2/dmg-acid2.gb
```

The core stores color indices 0..3; the dump maps them to the DMG grayscale
palette (`0 -> 255, 1 -> 170, 2 -> 85, 3 -> 0`).

The breakpoint can fire mid-frame. The PPU updates the framebuffer line by line
and does not clear it between frames, so an immediate dump can mix the current
and the previous frame. `MTS_FRAME_DELAY` advances N extra T-cycles before the
dump (one frame = 70224 T-cycles):

```sh
MTS_FRAME_OUT=/tmp/frame.pgm MTS_FRAME_DELAY=70224 \
    build/mts_runner vendor/dmg-acid2/dmg-acid2.gb
```

To compare every reference ROM against its expected PNG:

```sh
./mts/compare_ppu_frames.sh
```

The script prints one line per ROM (`PASS diff=0` / `FAIL diff=N` / `SKIP`) and
a summary. It accepts individual ROMs too. Useful environment variables:

| Variable | Meaning |
| --- | --- |
| `RUNNER` | Runner binary (default `build/mts_runner`) |
| `VENDOR` | Vendor root (default `vendor`) |
| `OUTDIR` | PGM output directory (default `/tmp/qtrebg_frames`) |
| `FRAME_DELAY` | Extra T-cycles before the dump (default 0) |
| `QUIET=1` | Print only failures and the summary |
| `REPORT=file` | Write the full table as TSV (`name, status, diff, expected, frame`) |

For long runs, `QUIET=1 REPORT=/tmp/ppu.tsv ./mts/compare_ppu_frames.sh` keeps
stdout small and leaves the per-ROM details in the TSV.

