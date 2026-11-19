# Frame/audio regression harness

Headless runner that executes a fixed set of ROMs for a fixed number of
frames and compares per-frame hashes of:

- the PPU framebuffer (`160x144`, raw DMG color indices `0..3`);
- the APU stereo PCM drained after each frame (48 kHz, `int16` LE).

The goal is to detect behavior changes that unit tests and mts/blargg do not
see: PPU timing/render changes, APU mixer/timing changes, DMA/bus contention,
CPU timing, etc.

## Usage

From the repository root:

```sh
make run_regress          # compare against regress/golden.txt
make update_regress       # rewrite the golden file from the current build
```

With the DMG boot ROM (the boot animation is part of the hashes):

```sh
QTR_BIOS="$PWD/roms/bios/[BIOS] Nintendo Game Boy Boot ROM (World).gb" make run_regress
```

This mode uses `regress/manifest_bios.txt` and `regress/golden_bios.txt`, so
the no-BIOS and BIOS references do not mix. The boot ROM takes ~327 frames to
hand over to the cartridge; the BIOS manifest runs 400 frames per ROM to
cover the boot animation, the handover and the first ROM frames.

The runner can also be called directly:

```sh
./build/regress_runner --manifest regress/manifest.txt --golden regress/golden.txt
./build/regress_runner --manifest ... --golden ... --verbose
./build/regress_runner --manifest ... --golden ... --allow-missing
```

Options:

- `--update`: rewrite the golden file from this run.
- `--input`: apply the fixed input script (see below).
- `--bios PATH`: use a 256-byte DMG boot ROM.
- `--allow-missing`: skip manifest entries whose ROM file is absent.
- `--verbose`: print a per-ROM summary, including nonzero PCM samples.

## Manifest

`regress/manifest.txt` is the default list:

```text
<frames><TAB><path>[<TAB>audio]
```

Paths are resolved relative to the manifest file. `roms/` and `vendor/` are
gitignored, so the default manifest is local to this checkout. Use
`--allow-missing` when a subset is not available.

The optional `audio` flag makes the entry fail if the run produces only
silence. It prevents `--update` from blessing a broken APU with an all-zero
PCM hash.

## Input script

`--input` applies, for every ROM and every 120 frames:

| Frame phase | Button |
|---:|---|
| 20-23 | Right |
| 50-53 | A |
| 70-71 | B |
| 90-93 | Start |

All other buttons are released. The input mode is recorded in the golden
header; a golden generated with `--input` cannot be compared against a run
without it.

## Golden files

The header records the harness version, input mode and BIOS mode:

```text
# qtr-regress v1 input=none bios=0
```

Each data line is:

```text
<rom>\t<frame>\t<framebuffer_hash>\t<pcm_frames>\t<pcm_hash>
```

Hashes are FNV-1a 64 over the raw bytes:

- framebuffer: 23040 bytes;
- PCM: `pcm_frames * 2` samples serialized as little-endian `int16`.

A golden file is a **behavior snapshot**, not a correctness oracle. When a
diff appears, first determine whether the change is intentional. If it is,
regenerate and review the new file. Never update the golden just to make a
failing suite pass.

`--update --allow-missing` drops the lines of ROMs that were skipped. If you
only want to update one entry, keep the full manifest available or restore
the missing lines from version control.

## Frame boundary and audio

Each frame is exactly `70224` T-cycles, matching the DMG frame length and the
mts frame dumper. Audio is drained after every frame; the ring is 4096 stereo
frames, so a normal frame (~803 samples) never overflows.

The runner does not use SDL and does not open a window.
