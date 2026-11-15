# Game Boy PPU — Behavioural Specification

A complete description of the Game Boy picture processing unit: the LCD timing, the
pixel fetcher, the two FIFOs, the window, objects, the STAT interrupt machinery, the
LCD panel model, and the undocumented edge cases that a cycle-accurate implementation
must reproduce.

The register-level description follows Pan Docs. The timing model refines it with the
behaviour needed to reproduce the measured results of the hardware test suites; where
the nominal documentation and the observable behaviour disagree, the behaviour
described here is the one that reproduces those results.

The text covers the DMG, the CGB, and the CGB running in DMG compatibility mode.
Unless a paragraph says otherwise, "DMG" means "DMG hardware or CGB in DMG
compatibility mode", and "CGB" means "CGB in native CGB mode".

- **Scope:** rendering, timing, memory arbitration, LCD panel, PPU-adjacent DMA.
- **Notation:** one *dot* is the PPU clock period. On DMG and on CGB single speed one
  dot is one T-state; on CGB double speed one dot is two T-states.
- **Code:** all pseudocode is language-agnostic. `←` is assignment, `=` is comparison.

---

## Table of contents

1. [Vocabulary and signal names](#1-vocabulary-and-signal-names)
2. [Top-level structure](#2-top-level-structure)
3. [Registers and memory](#3-registers-and-memory)
4. [The dot grid](#4-the-dot-grid)
5. [Line and frame timeline](#5-line-and-frame-timeline)
6. [Mode 2 — OAM scan](#6-mode-2--oam-scan)
7. [Mode 3 — the pixel pipeline](#7-mode-3--the-pixel-pipeline)
8. [Background and window addressing](#8-background-and-window-addressing)
9. [The window](#9-the-window)
10. [Objects](#10-objects)
11. [Pixel mixing and priority](#11-pixel-mixing-and-priority)
12. [Register visibility and write latency](#12-register-visibility-and-write-latency)
13. [Memory access and bus arbitration](#13-memory-access-and-bus-arbitration)
14. [STAT interrupts](#14-stat-interrupts)
15. [The LCD panel model](#15-the-lcd-panel-model)
16. [LCD enable and disable](#16-lcd-enable-and-disable)
17. [OAM DMA and HDMA](#17-oam-dma-and-hdma)
18. [CGB-specific behaviour](#18-cgb-specific-behaviour)
19. [Required state](#19-required-state)
20. [Reference pseudocode](#20-reference-pseudocode)
21. [Casuistry catalogue](#21-casuistry-catalogue)
22. [Implementation checklist](#22-implementation-checklist)
23. [References](#23-references)

---

## 1. Vocabulary and signal names

| Term | Meaning |
| --- | --- |
| Dot | One PPU clock period. The PPU advances exactly one dot per tick. |
| T-state | CPU clock period. 1 dot = 1 T-state (DMG, CGB single speed), 2 T-states (CGB double speed). |
| Line | One of 154 horizontal periods. Lines 0–143 are visible; 144–153 are VBlank. |
| `LY` | The line register readable by software. Not identical to the internal line counter (see §5.4). |
| `LX` | Internal pipeline coordinate. `LX = screen_x + 8`, so visible pixels are `LX = 8..167` and `LX = 168` marks the end of the line. |
| Mode 0/1/2/3 | HBlank, VBlank, OAM scan, pixel transfer. The numbering is the value of `STAT` bits 1–0. |
| BG | Background layer. |
| WIN | Window layer. It shares the BG tile data and map hardware. |
| OBJ | Sprite layer. |
| FIFO | A fixed 8-entry queue holding pixels. |
| Fetch | One pass of the six-step tile pipeline that produces 8 pixels. |
| Push | The final fetch step, where the produced tile is written into the BG FIFO. |

Two separate clocks exist. The PPU produces a stream of dots and drives `LY`, `STAT`
and the pixel stream. The LCD panel copies one completed row at a time and has its own
frame counter. They run in lock-step while the PPU is enabled and lose synchronisation
when it is disabled (§15, §16).

---

## 2. Top-level structure

The PPU is a per-dot state machine. On every dot it performs, in order:

1. Advance the current line phase (OAM scan, pixel pipeline, HBlank or VBlank).
2. Update the window frame latch.
3. Recompute the `LYC = LY` flag and evaluate the STAT interrupt line.
4. Latch the register values that the PPU sees delayed (`LCDC`, `WX`, `LY`, `LYC`,
   and on DMG `BGP`).
5. Apply register writes whose latency has expired.
6. Refresh the mode bits that software reads from `STAT`.

The pixel path is a three-stage pipeline:

```text
                 +---------------------+
  VRAM  -------> |  tile fetcher       | -----> BG FIFO (8) ---+
                 |  6 dots per tile    |                       |
                 +---------------------+                       +--> mixer --> LCD row buffer
                 +---------------------+                       |
  OAM   -------> |  object fetcher     | -----> OBJ FIFO (8) --+
                 |  6 dots per sprite  |
                 +---------------------+
```

The mixer consumes one BG pixel and one OBJ pixel per dot and writes the resolved color
to the LCD row buffer. The fetcher runs ahead of the mixer; a fetch takes 6 dots and
produces 8 pixels, so under normal conditions it stays ahead. Anything that stops the
mixer (a sprite fetch, a window restart, a FIFO underrun) lengthens Mode 3 by exactly
the number of dots it was stopped.

---

## 3. Registers and memory

### 3.1 Registers used by rendering

| Address | Name | Access | Role |
| --- | --- | --- | --- |
| `$FF40` | `LCDC` | R/W | Layer enables, tile addressing, sprite size, LCD enable. |
| `$FF41` | `STAT` | R/W | Interrupt enables, `LYC = LY` flag, current mode. |
| `$FF42` | `SCY` | R/W | Background/window viewport Y. |
| `$FF43` | `SCX` | R/W | Background/window viewport X. |
| `$FF44` | `LY` | R | Current line. |
| `$FF45` | `LYC` | R/W | Line comparison value. |
| `$FF46` | `DMA` | W | OAM DMA source. |
| `$FF47` | `BGP` | R/W | DMG background palette. |
| `$FF48` | `OBP0` | R/W | DMG sprite palette 0. |
| `$FF49` | `OBP1` | R/W | DMG sprite palette 1. |
| `$FF4A` | `WY` | R/W | Window top. |
| `$FF4B` | `WX` | R/W | Window left, plus 7. |
| `$FF4F` | `VBK` | R/W | CGB VRAM bank. |
| `$FF68` | `BCPS` | R/W | CGB background palette index / auto-increment. |
| `$FF69` | `BCPD` | R/W | CGB background palette data. |
| `$FF6A` | `OCPS` | R/W | CGB object palette index / auto-increment. |
| `$FF6B` | `OCPD` | R/W | CGB object palette data. |
| `$FF6C` | `OPRI` | R/W | CGB object priority mode. |

### 3.2 LCDC

| Bit | Name | Meaning |
| --- | --- | --- |
| 7 | LCD enable | 0 disables the PPU and the panel; 1 enables them. |
| 6 | WIN map | Window tile map: 0 = `$9800`, 1 = `$9C00`. |
| 5 | WIN enable | Enables the window (non-CGB: only if bit 0 is set). |
| 4 | BG/WIN tile data | 0 = signed `$9000` base, 1 = unsigned `$8000` base. |
| 3 | BG map | Background tile map: 0 = `$9800`, 1 = `$9C00`. |
| 2 | OBJ size | 0 = 8×8, 1 = 8×16. |
| 1 | OBJ enable | Enables sprites. |
| 0 | BG/WIN enable / master priority | Non-CGB: BG and window display enable. CGB: master priority; 0 = sprites always on top, 1 = per-pixel priority. |

`LCDC` is never locked by the PPU. Writes can occur on any dot, including inside Mode 3,
and take effect according to the latency table in §12.

### 3.3 STAT

| Bit | Name | Meaning |
| --- | --- | --- |
| 6 | LYC interrupt enable | Enables the `LYC = LY` interrupt source. |
| 5 | Mode 2 interrupt enable | Enables the OAM scan interrupt source. |
| 4 | Mode 1 interrupt enable | Enables the VBlank interrupt source. |
| 3 | Mode 0 interrupt enable | Enables the HBlank interrupt source. |
| 2 | LYC = LY flag | Read-only comparison result. |
| 1–0 | Mode | 0 = HBlank, 1 = VBlank, 2 = OAM scan, 3 = pixel transfer. |

Bits 7 and 2–0 read as their hardware values; writes to them are ignored. On DMG a
write to `STAT` has a one-dot quirk (§14.5).

### 3.4 Memory

| Region | Size | Notes |
| --- | --- | --- |
| VRAM | `$8000–$9FFF` | Two 8 KiB banks on CGB (`VBK`). Bank 0 holds tile data and maps; bank 1 holds CGB map attributes. |
| OAM | `$FE00–$FE9F` | 40 entries × 4 bytes. `$FEA0–$FEFF` is unusable. |
| Palette RAM | 64 bytes BG + 64 bytes OBJ | CGB only, accessed through `BCPD`/`OCPD`. |
| LCD row buffer | 160 pixels | Written by the PPU, copied by the panel. |

An OAM entry is:

| Offset | Field |
| --- | --- |
| +0 | Y position of the top edge, minus 16. |
| +1 | X position of the left edge, minus 8. |
| +2 | Tile number. |
| +3 | Attributes. |

OAM attribute bits:

| Bit | DMG | CGB |
| --- | --- | --- |
| 7 | BG over OBJ | BG over OBJ |
| 6 | Y flip | Y flip |
| 5 | X flip | X flip |
| 4 | OBP1 select | Palette bit 0 |
| 3 | — | VRAM bank |
| 2–0 | — | Palette number (0–7) |

---

## 4. The dot grid

### 4.1 Fundamental constants

| Quantity | Value |
| --- | --- |
| Dots per line | 456 |
| Lines per frame | 154 (0–143 visible, 144–153 VBlank) |
| Dots per frame | 70224 |
| Dot clock | 4194304 Hz |
| Frame rate | 59.7275 Hz |
| Visible pixels per line | 160 |
| BG FIFO depth | 8 |
| OBJ FIFO depth | 8 |
| Maximum OAM entries per line | 10 |
| Maximum OAM entries in memory | 40 |

Mode 2 is 80 dots. Mode 3 is variable. Mode 0 fills the remainder of the line. Modes 0,
2 and 3 exist only on lines 0–143; lines 144–153 are Mode 1.

### 4.2 Dot numbering

Dots are numbered `0..455` inside a line. The phase boundaries are:

| Dots | Phase |
| --- | --- |
| 0–78 | OAM scan (Mode 2) |
| 79 | Mode 3 asserted in `STAT`, pipeline not started |
| 80–82 | First (“dummy”) tile fetch |
| 83 … mode3_end | Pixel pipeline |
| mode3_end … 455 | HBlank (Mode 0) |

`mode3_end` is the dot on which the 160th visible pixel is emitted, plus one; see §7.6.
The line always ends at dot 455 regardless of how long Mode 3 lasted.

Three events are tied to fixed dots near the end of every line:

- **Dot 452:** the LCD panel copies the completed row; the OAM-mode interrupt source is
  evaluated one last time; `LY` is prepared for its increment.
- **Dot 453:** `LY` takes its new value; the `LYC = LY` flag is suppressed on DMG; the
  first OAM read of the next line is issued.
- **Dot 454:** the `STAT` mode field becomes 2 on a normal line. On line 143 it becomes
  1 instead on DMG; on CGB Mode 1 becomes visible at dot 0 of line 144.
- **Dot 455:** the last dot of the line; the next line starts at dot 0.

---

## 5. Line and frame timeline

### 5.1 Normal visible line

```text
dot:   0                          80                             mode3_end      452 453 454 455
       |------------ Mode 2 ------|---------- Mode 3 -------------|---- Mode 0 ----|
       OAM scan                   fetch / pixels                 HBlank         line-end sequence
```

During Mode 2 the PPU builds the sprite list. During Mode 3 it fetches tiles and emits
pixels. During Mode 0 it does nothing to the picture; the CPU may access VRAM and OAM
again as soon as Mode 3 ends.

### 5.2 The line-end sequence

The last four dots of every line are not idle: they contain the state transitions that
make `LY`, `STAT` and the OAM scan line up.

```text
procedure end_of_line():
    # dot 452
    evaluate_oam_interrupt_source()
    begin_ly_increment()

    # dot 453
    oam_acquire()
    reset_sprite_list()
    request_first_oam_word()
    suppress_lyc_flag()          # DMG only
    commit_ly_increment()

    # dot 454
    release_lyc_suppression()
    stat.mode ← 2                # 1 on line 143 (DMG)

    # dot 455
    request_first_oam_word()
    enter_next_line()
```

On DMG the OAM bus is acquired at dot 453. On CGB it is acquired earlier, at dot 452.
On DMG the OAM bus is released again for exactly one dot at dot 75 of the OAM scan,
which allows a CPU write to OAM to slip through in the middle of Mode 2.

### 5.3 VBlank

Line 143 is the last line with pixels. After its Mode 3, the line runs through HBlank
and then the PPU enters VBlank:

- On DMG the Mode 1 flag becomes visible at dot 454 of line 143.
- On CGB the Mode 1 flag becomes visible at dot 0 of line 144.
- The VBlank interrupt (`INT $40`) is requested at dot 0 of line 144 on both.

Lines 144–153 are all Mode 1. `LY` still advances once per line, at dot 453.

### 5.4 The `LY = 153` anomaly

`LY` is updated at dot 453, three dots before the line boundary. Consequently the last
three dots of line *N* already report `LY = N+1`. During line 153 the register reports
153 for only the first few dots, then reads 0:

- On DMG, `LY` becomes 0 at dot 2 of line 153.
- On CGB, `LY` becomes 0 at dot 3 or 4 of line 153 (the exact dot is not stable).
- The `LYC = LY` flag is suppressed on DMG from dot 2 through dot 6 of this line, so a
  comparison against 153 is not visible for the whole line. On CGB the flag instead
  retains its previous value during the suppression window.

The frame still contains 154 full lines; only the *readable value* of `LY` is
compressed. Software that waits for `LY = 153` therefore observes a very short pulse.

### 5.5 VBlank-to-line-0 transition

At the end of line 153 the PPU prepares a new frame: the window line counter and the
window frame latch are reset, the sprite list is cleared, and the first OAM word is
requested. On DMG, during dots 453–455 of line 153, `STAT` reports HBlank instead of
Mode 1; on CGB it reports Mode 1. This is a read-only aliasing of the mode field — the
internal mode is already the OAM-scan side of the transition, and it does not raise an
HBlank interrupt.

### 5.6 `STAT` mode versus internal mode

The mode field read from `STAT` is *usually* the internal mode, but there are deliberate
exceptions:

1. Mode 3 is asserted one dot before the pipeline starts (dot 79).
2. Mode 2 is asserted two dots before the scan starts (dot 454).
3. During the first line after the LCD is enabled, `STAT` reports HBlank for the whole
   OAM scan even though the internal mode is Mode 2.
4. During an OAM DMA transfer, `STAT` reports HBlank for the whole OAM scan.
5. On DMG, during dots 453–455 of line 153, `STAT` reports HBlank (item in §5.5).
6. On CGB double speed, HBlank becomes visible in `STAT` one dot later than the
   internal transition.

Aliases 1–5 are read-only: interrupt generation uses the internal mode. Alias 6
(the CGB double-speed delay) affects only reads of `STAT`; HDMA samples the internal
mode, which is not delayed.

---

## 6. Mode 2 — OAM scan

The scan runs for 80 dots (0–78 plus the first word fetched at dot 453/455 of the
previous line). Its job is to produce the list of sprites that intersect the current
line.

### 6.1 Read schedule

OAM entries are read two bytes at a time (Y and X only; tile number and attributes are
read later, during Mode 3). One entry is processed every two dots:

```text
# Entry 0 was requested at dot 453 of the previous line and is flushed at dot 454.
procedure oam_scan_dot():                     # dots 0-76
    if dots is even:
        word ← flush_oam_read()
        consider_entry(word.y, word.x)
        index ← index + 1
    else:
        request_word(0xFE00 + 4 * index)
```

The first request is issued at dot 453 of the previous line and entry 0 is flushed at
dot 454, before the line boundary. Entry 1 is then processed at dot 0, entry 2 at dot 2,
and so on; entry 39 is processed at dot 76. This is why Mode 3 can start at dot 80.

If the OAM bus is owned by an OAM DMA transfer, the PPU's read is not performed and the
previous OAM word remains in its latch. The scan then operates on stale data. (A CPU
read of OAM while the PPU or DMA owns the bus returns `$FF`; a CPU write is ignored.)

### 6.2 Selection rule

For each entry:

```text
top    ← entry.y - 16
height ← 8 if LCDC.obj_size == 0 else 16

if top <= LY < top + height:
    found ← found + 1
    if found <= 10 and entry.x < 168:
        bucket[entry.x].append(entry)
```

Important consequences:

- The ten-sprite limit counts *every* matching sprite, including sprites whose
  `X >= 168`. A sprite parked off the right edge still consumes a slot.
- Sprites with `X >= 168` are counted but never fetched.
- Sprites with `X = 0` are stored but their pixels fall entirely left of the screen.
  Sprites with `X = 1..7` are partially clipped.
- The list is capped at ten entries. Once ten matches have been recorded, the scan keeps
  reading OAM to the end of the entry table but ignores any further match.

### 6.3 Sprite ordering

Entries are bucketed by their X coordinate. This makes the Mode 3 fetch deterministic:
when the pipeline reaches `LX = X`, it serves that bucket. Buckets are served in
last-in-first-out order; the final per-pixel priority is resolved during the FIFO merge
(§10.3), so the order inside a bucket does not affect the picture.

### 6.4 The glitched first line

The first OAM scan after the LCD is enabled is special:

- No OAM entry is read and no sprite is detected.
- On DMG the OAM bus is not acquired; on CGB it is acquired at dot 78 as on a normal
  line. No entry is read either way.
- The `STAT` mode remains HBlank for the entire scan.
- VRAM and OAM are released one dot before the HBlank mode update.
- The HBlank `STAT` update is delayed by two dots.

The sprite list of that first visible line is therefore empty.

---

## 7. Mode 3 — the pixel pipeline

### 7.1 Pipeline coordinates

The internal coordinate is `LX = screen_x + 8`. It starts at 0 on the first pipeline dot
and ends at 168 when the line is complete. Visible pixels are produced while
`LX = 8..167`; the pixels produced at `LX = 0..7` are used only to align sprites and are
discarded. A sprite with OAM X coordinate `X` is served when `LX = X`, i.e. when the
beam reaches screen column `X - 8`, the position of its leftmost pixel. The pixel is not
emitted on that dot: output stalls while the fetch and merge run, and the pixel appears
after the 6–11 dot pause.

### 7.2 The two FIFOs

**BG FIFO.** Eight entries. Each entry is a color index plus, on CGB, the map
attributes. It is filled one tile at a time (8 pixels). The fetcher may only push when
the FIFO is empty; if the FIFO still has pixels, the fetcher waits in its push stage.
The FIFO is refilled on the dot after it becomes empty, so a normal tile boundary costs
no extra dot: the 6-dot fetch completes well before the 8 pixels are consumed.

**OBJ FIFO.** Eight entries. Each entry is a color index, the OAM attributes, the OAM
entry index and the `LX` at which it was fetched. It is filled by sprite fetches and
consumed in lock-step with the BG FIFO. Entries from multiple sprites can coexist; the
merge in §10.3 decides which one survives for each pixel.

Both FIFOs are cleared when the line is restarted, and the BG FIFO is also cleared when
the window activates.

### 7.3 The six-step fetch

A BG, window or sprite fetch always takes six dots:

| Step | Name | VRAM access | Action |
| --- | --- | --- | --- |
| 1 | Get tile 0 | read tile number (and CGB attributes) | Compute the map address; latch `SCY` on CGB. |
| 2 | Get tile 1 | — | Advance to the data phase. |
| 3 | Data low 0 | read low bitplane | Recompute the tile data address from the current registers. |
| 4 | Data low 1 | — | Handle the CGB tile-data-select glitch. |
| 5 | Data high 0 | read high bitplane | Recompute the tile data address again. |
| 6 | Data high 1 | — | Advance the tile counter (DMG) and enable the push stage. |

The VRAM-access column describes a background or window fetch. A sprite fetch performs
its two bitplane reads on steps 4 and 6 instead of steps 3 and 5 (see §10.2).

The address is recomputed immediately before each of the two bitplane reads. A change
to `SCY`, to `LCDC` bit 4, or (for sprites) to `LCDC` bit 2 between the two reads can
therefore make the low and high bytes come from different tiles or rows. On CGB, `SCY`
is latched once at step 1, so the two planes always use the same row. This is the
“bitplane desync” behaviour that DMG software can exploit.

The tile counter advances at step 6 (DMG), or after two consecutive push dots (CGB).
An aborted fetch therefore does not consume a tile slot on DMG.

### 7.4 The push stage

The push stage is not a seventh dot of the fetch; it is a wait state that runs until the
BG FIFO is empty.

```text
procedure push_stage():
    if not push_enabled or fetching_sprite:
        return

    if BG FIFO is not empty:
        if a sprite is pending at this LX and OBJ is enabled:
            cache the current low/high bitplanes
            start a sprite fetch
        return

    # FIFO empty: the fetched tile can enter it
    if window_frame_latch and LX == WX and not window_just_activated:
        BG FIFO.push(color 0)
        return                      # the real push is postponed one dot

    pending_fill ← true
    push_enabled ← false
    stage ← GetTile0

    if window_just_activated:
        window_just_activated ← false
        return                      # ignore sprites on this push

    if a sprite is pending at this LX and OBJ is enabled:
        start a sprite fetch
```

Two rules in this procedure matter a great deal:

- **Pushing requires an empty FIFO.** The fetcher cannot run ahead by more than one
  tile. This is what makes sprite and window stalls visible as gaps in the pixel
  stream rather than being hidden by buffering.
- **A pending sprite blocks output even before its fetch starts.** The mixer refuses to
  pop while a sprite is pending at the current `LX`. The sprite fetch can only begin at
  a push stage, so the total stall is the fetch itself (6 dots) plus the wait for the
  fetcher to reach a push stage (0–5 dots): 6–11 dots per sprite.

### 7.5 One pixel per dot

```text
procedure output_dot():
    apply_pending_window_activation()
    apply_pending_fill()

    if BG FIFO not empty
       and not fetching_sprite
       and (no sprite pending at LX or OBJ disabled):

        bg ← BG FIFO.pop()
        obj ← OBJ FIFO.pop() if OBJ FIFO not empty

        if LX >= 8:
            color ← mix(bg, obj)
            push_pixel_to_lcd(color)
            if LX == 167:
                LX ← 168
                dots ← dots + 1
                enter_hblank()
                return
            check_window_activation()

        LX ← LX + 1

    fetcher_tick()
    push_stage()
    dots ← dots + 1
```

The pixels popped at `LX = 0..7` are discarded, but they still consume FIFO entries and
advance `LX`. This is the mechanism that clips sprites whose `X < 8` and that aligns the
OBJ FIFO with the BG FIFO.

### 7.6 Duration

Let `D = SCX mod 8`, `W` be the number of window activations on the line, and `S_i` the
cost of the i-th served sprite (6–11). The first HBlank dot is

```text
mode3_end = 251 + D + 6·W + Σ S_i + (1 if WX = 0 and D > 0)
```

which gives the following minimum, for `D = 0`, no window and no sprites:

- Mode 3 begins at dot 80 and the first visible pixel leaves at dot 91.
- The last visible pixel leaves at dot 250.
- HBlank begins at dot 251.

Measured through `STAT`, Mode 3 is asserted at dot 79, so the visible Mode 3 length is
172 dots in this minimum case. This is the figure usually quoted as “Mode 3 is 172 to
289 dots”; the 289 upper figure assumes ten sprites and no window.

Both the `+D` and the `+1` need explanation:

- `D` dots are spent discarding the first `SCX mod 8` pixels of the line, and the first
  visible pixel is delayed by exactly `D`.
- When `WX = 0` and `SCX mod 8 > 0`, the window activation itself is one dot late, so it
  costs one extra dot.

The formula is a *lower bound*. A window may be activated several times on one line if
it is disabled and re-enabled; every activation pays the 6-dot restart even if it
happens where the old BG fetch would already have been ready.

---

## 8. Background and window addressing

### 8.1 Map address

For the background, the map cell is

```text
map_x ← ((tile_x + SCX) / 8) mod 32
map_y ← ((LY + SCY) / 8) mod 32
address ← (LCDC.bg_map ? 0x9C00 : 0x9800) + 32·map_y + map_x
```

`tile_x` is the fetcher's own counter, advanced in steps of 8. `SCX` and `SCY` are
re-read for every fetch. The low three bits of `SCX` are not used here; they were
consumed once at the start of the line (§12.2).

The window uses its own X counter and its own line counter:

```text
map_x ← window_tile_x
map_y ← (window_line / 8) mod 32
address ← (LCDC.win_map ? 0x9C00 : 0x9800) + 32·map_y + map_x
```

The window's map selection is re-read on every fetch, so a mid-fetch change of
`LCDC` bit 6 affects the tile that is being fetched. On DMG the background's map
selection behaves the same way.

### 8.2 Tile data address

```text
if LCDC.bg_win_tile_data == 1:
    base ← 0x8000
    offset ← 16 · tile_number
else:
    base ← 0x9000
    offset ← 16 · signed(tile_number)       # -128..127

row ← (LY + SCY) mod 8
address ← base + offset + 2·row
```

With signed addressing, tile numbers 0–127 map into `$8800–$8FFF` and 128–255 map into
`$8800–$8FFF` as well, but through the negative offsets. The two addressing modes share
the `$8800–$8FFF` block, which is why changing `LCDC` bit 4 mid-fetch sometimes has no
visible effect: tile numbers whose low bit 11 of the address is set read the same byte
in both modes.

For CGB the map attribute byte supplies the bank, palette, flips and priority:

| Bit | Meaning |
| --- | --- |
| 7 | BG-over-OBJ priority |
| 6 | Y flip |
| 5 | X flip |
| 3 | VRAM bank |
| 2–0 | Palette number |

Y flip complements the row within the tile (`row ^ 7`). X flip reverses the 8 pixels
when the tile is expanded into the FIFO.

### 8.3 Sampling points

- `SCX` high five bits: read at each tile-map fetch.
- `SCX` low three bits: read once, three dots into Mode 3 (immediately after the dummy
  fetch). A write after that dot affects only the next line.
- `SCY`: read at each bitplane fetch on DMG (two reads per tile); read once at tile 0 on
  CGB.
- `LCDC` bits 3, 4, 6: read at each fetch, with the address recomputed just before each
  bitplane read.
- `WX`: read with a one-dot delay on DMG and a two-dot delay on CGB.
- `WY`: not used to decide rendering after the frame latch; see §9.1.
- `BGP`: read with a one-dot delay on DMG, with the old and new values OR-ed during the
  transition dot (§12.3).

---

## 9. The window

### 9.1 Frame latch

The window has a frame-level latch. It is set on any dot where

```text
LCDC.win_enable == 1 and LY == WY
```

and it stays set for the rest of the frame, even if `WY` or the enable bit changes
afterwards. The window *line counter* (`window_line`) starts at 255 at the beginning of
the frame and is incremented on every activation, so the first activation uses line 0.

This means:

- `WY` is effectively sampled continuously during the frame, not at a single point.
- Once the latch is set, changing `WY` cannot move the window for the rest of the frame.
- Changing `LCDC` bit 5 to 1 after the latch is set can enable the window on a later
  line of the same frame; this is the classic “WY matched earlier but the window was
  off” case.

### 9.2 Activation

Activation is checked after each displayed pixel, and once immediately after the dummy
fetch when `SCX mod 8 > 0`:

```text
procedure check_window_activation():
    if not window_frame_latch or window_active or not LCDC.win_enable:
        return

    if CGB:
        triggered ← (LX == WX)
    else:                                               # DMG: also the late-trigger case
        triggered ← (LX == last_WX) or
                    (LX == last_WX + 1 and last_LCDC.win_enable == 0)

    if triggered:
        window_activating ← true
        window_line ← window_line + 1
        push_enabled ← false
        restart the window fetch                       # 6 dots in total
```

The comparison is against `LX`, not against the visible X coordinate. Since the pixel at
`LX` is emitted before the check runs, the first window pixel appears at screen
`X = WX - 7`, which is the documented “window top-left is `(WX-7, WY)`”.

`WX = 0` is unreliable on hardware: the window starts at `X = -7`, only the last pixel
of the first tile is visible, and the horizontal stutter depends on `SCX mod 8`.
`WX = 166` places the window at `X = 159` (one pixel wide); `WX >= 167` places it fully
off-screen. A `WX` change that skips over the current `LX` postpones activation to the
next line.

### 9.3 The restart cost

On the dot after `window_activating` is set, the window really activates:

```text
procedure apply_pending_window_activation():
    if not window_activating:
        return

    BG FIFO.clear()
    window_active ← true
    window_just_activated ← true
    window_tile_x ← 0
    window_activating ← false
    fetcher.stage ← GetTile0
```

The BG FIFO is thrown away, so output stops until the first window tile has been
fetched. The complete restart costs 6 dots per activation, including the dot on which
the activation is applied. The window tile counter starts at 0 and is incremented at
each window map fetch.

The window line counter is *not* `LY`: it counts activations. If the window is disabled
and re-enabled on the same line, it advances again, and the window continues from the
next row of its own map. This is why a window that is toggled mid-line shows successive
rows of window content, and why multiple triggers on one line each add 6 dots.

### 9.4 Disabling the window mid-fetch

If `LCDC` bit 5 is cleared while a window fetch is in progress, the fetcher abandons the
window and continues with the background:

- On DMG the current window fetch is replaced by a background fetch; the background
  tile counter continues where it left off (the aborted window fetch does not advance
  it).
- On CGB the behaviour is worse. If the window is disabled on the same dot it was
  activating, the BG FIFO is partially refilled with whatever is currently in the
  bitplane latches, the fetcher restarts from the background tile 0, and the BG FIFO may
  be refilled twice if it was already empty at that moment. The result is a corrupted
  strip of pixels at the window position, followed by a restart of the background
  stream.

### 9.5 The zero-pixel glitch

If the window frame latch is set, the pipeline reaches `LX == WX` while the push stage
is enabled, and the window does *not* actually activate at that position (because it is
disabled, because `WX` was changed, or because the trigger point was already passed),
the push stage inserts a single color-0 pixel and postpones the real tile push by one
dot. The visible effect is one white pixel where the window would have started, and the
following content shifted right by one pixel.

On DMG this can happen on any line after the frame latch is set. On CGB it only happens
while the window is already active, so a disabled window does not produce the ghost
pixel; instead CGB produces the partial-refill glitch of §9.4.

The glitch also reappears on later lines when `WX` is changed to a position that equals
the current `LX` at push time: the condition is `LX == WX`, not “the window was about to
activate here”.

---

## 10. Objects

### 10.1 Fetch trigger

A sprite fetch begins when the pipeline is at `LX = entry.x` and the sprite list for
that `LX` is non-empty. It is served before any further BG pixel is emitted:

```text
procedure start_sprite_fetch():
    fetching_sprite ← true
    current ← bucket[LX].pop()
    stage ← OBJ_GetTile0
```

The fetch reads the tile number and attributes from OAM *now*, not during Mode 2. This
is architecturally important: a sprite's tile number can be changed mid-line, and an
OAM DMA in progress can corrupt the read.

### 10.2 Six steps and the 6–11 dot stall

The object fetch has the same six steps as a BG fetch, but its VRAM reads happen on
steps 4 and 6 rather than 3 and 5. The mixer is blocked for the whole fetch:

| Cause | Dots |
| --- | --- |
| Six fetch steps | 6 |
| Wait for the BG fetcher to reach a push stage | 0–5 |
| **Total** | **6–11** |

The wait is bounded by the alignment between the sprite's X and the background tile.
If `x` is the sprite's on-screen left edge, the cost is equivalently

```text
cost = 11 - min(5, (x + SCX) mod 8)
```

for a sprite over the background, or with `255 - WX` in place of `SCX` for a sprite
over the window. A sprite aligned with the background tile boundary costs 11 dots; one
misaligned by 5 or more costs 6.

If two sprites have the same X, the second fetch starts immediately after the first
merge and costs 6 dots, because the fetcher is already at a push stage.

### 10.3 The OBJ FIFO merge

Each sprite fetch produces 8 pixels. They are merged into the OBJ FIFO as follows:

```text
procedure merge_sprite(pixels[8], attributes, oam_index, x):
    for i in 0 .. 7:
        candidate ← pixel(pixels[i], attributes, oam_index, x)

        if i < OBJ FIFO.size:
            incumbent ← OBJ FIFO[i]

            overwrite ← candidate.opaque and not incumbent.opaque
            if candidate.opaque and incumbent.opaque:
                if DMG:
                    overwrite ← (candidate.x == incumbent.x and
                                 candidate.oam_index < incumbent.oam_index)
                else if OPRI.priority_mode == 1:      # DMG-like
                    overwrite ← (candidate.x == incumbent.x and
                                 candidate.oam_index < incumbent.oam_index)
                else:                                  # CGB default
                    overwrite ← candidate.oam_index < incumbent.oam_index

            if overwrite:
                OBJ FIFO[i] ← candidate
        else:
            OBJ FIFO.push(candidate)
```

After the merge, if another sprite is pending at the same `LX`, another fetch begins.
Otherwise the fetcher restores any BG fetch that was interrupted and returns to normal
operation.

Priority rules in words:

- **DMG:** the opaque sprite with the smaller X wins. For equal X, the smaller OAM index
  wins.
- **CGB, `OPRI` bit 0 = 0 (default):** the smaller OAM index wins, regardless of X.
- **CGB, `OPRI` bit 0 = 1:** the DMG rule (X, then OAM index).
- `OPRI` only takes effect if it was written by the boot ROM. A later write changes the
  value read back but not the priority behaviour.

### 10.4 Sprite geometry

- 8×8: tile row `(LY - (Y - 16)) mod 8`; Y flip complements it.
- 8×16: the sprite occupies two vertically adjacent tiles. The tile number's bit 0 is
  ignored; the row is `(LY - (Y - 16)) mod 16`, Y flip complements it.
- X flip reverses the 8 pixels after expansion.
- Object tile data is always addressed with the unsigned `$8000` base, independent of
  `LCDC` bit 4.
- On CGB the tile's VRAM bank comes from attribute bit 3.

### 10.5 Edge cases

- `Y = 0` or `Y >= 160`: the sprite never intersects a visible line.
- `X = 0`: fully clipped to the left; the fetch still happens but no pixel is displayed.
- `X = 1..7`: partially clipped.
- `X >= 168`: counted against the ten-sprite limit but never fetched.
- A sprite whose first pixel coincides with the first window tile push may be ignored:
  the push stage returns immediately after a window activation and does not start a
  sprite fetch on that dot.
- Sprites are still fetched when `LCDC` bit 1 is set but the BG is disabled; only the
  final mixing changes.

---

## 11. Pixel mixing and priority

For every visible pixel the mixer has a BG entry and, if the OBJ FIFO is non-empty, an
OBJ entry. The OBJ FIFO is popped in lock-step with the BG FIFO even when the sprite is
transparent, so the two stay aligned.

```text
procedure mix(bg, obj):
    if obj exists:
        if show_object(bg, obj):
            return obj_color(obj)

    return bg_color(bg)

procedure show_object(bg, obj):
    if LCDC.obj_enable == 0:            return false
    if obj.color_index == 0:           return false      # transparent

    if LCDC.bg_win_enable == 0:        return true       # master priority off
    if bg.color_index == 0:            return true       # BG color 0 is behind OBJ

    if obj.priority == 1:              return false      # OBJ behind BG colors 1-3
    if CGB and bg.attributes.priority: return false      # map attribute bit 7

    return true
```

Notes:

- On DMG, `LCDC` bit 0 = 0 blanks the BG and window (all pixels become color 0), so
  sprites always show. On CGB, bit 0 = 0 means “sprites always on top” and the BG is
  still rendered underneath.
- On DMG, `LCDC` bit 0 = 1 and a BG color of 1–3 does *not* hide a sprite unless the
  sprite's own priority bit (attribute bit 7) is set.
- On CGB, the BG map attribute's priority bit is consulted only when `LCDC` bit 0 = 1.
- A transparent OBJ pixel (color index 0) never affects the BG, even if its other
  attributes are set.

### 11.1 Color resolution

**DMG.** The 2-bit color index selects 2 bits from the palette register:

```text
shade ← (BGP >> (2 · bg_index)) & 3          # BG and window
shade ← (OBP0 or OBP1 >> (2 · obj_index)) & 3 # OBJ
```

The four shades are the four entries of the DMG palette (white, light gray, dark gray,
black in the usual mapping).

**CGB.** Each layer entry carries a palette number (3 bits) and a color index (2 bits).
The color is a little-endian RGB555 word in palette RAM:

```text
address ← 8 · palette_number + 2 · color_index
color   ← palette[address] | (palette[address + 1] << 8)
```

Blue is in bits 10–14, green in 5–9, red in 0–4.

**CGB in DMG compatibility mode.** The DMG palette registers still remap the color
index first; the result then selects a color from CGB palette 0 (background) or CGB
object palette 0/1 (sprites). This preserves the DMG palette behaviour while boot-ROM
code sets up the actual colors.

---

## 12. Register visibility and write latency

The PPU does not see all register writes immediately. The delays below are what software
can observe by racing writes against the beam.

### 12.1 Latency table

| Register | DMG | CGB |
| --- | --- | --- |
| `LCDC` bit 7 (enable) | immediate | immediate |
| `LCDC` bits 0 and 1 | 1 dot; immediate at `LX = 8` | 2 dots |
| `LCDC` other bits | used at fetch time | 2 dots |
| `STAT` | 1 dot; all four interrupt enables read as 1 during the transition dot | immediate |
| `SCY` | used directly at each fetch | 2 dots |
| `SCX` | used directly at each fetch | 2 dots |
| `WX` | 1 dot | 2 dots |
| `WY` | continuous (frame latch) | continuous (frame latch) |
| `BGP` | 1 dot, with OR of old and new | immediate, no OR |
| `OBP0`/`OBP1` | immediate | immediate |
| `LYC` | 1 dot for the comparison | 1 dot for the comparison |
| `LY` | 1 dot for the comparison | 1 dot for the comparison |
| `BCPD`/`OCPD` | — | blocked during Mode 3; auto-increment still occurs |

Details:

- **`LCDC` on DMG at `LX = 8`.** The BG enable and OBJ enable bits are normally seen one
  dot late, but the pixel at `LX = 8` (the first visible pixel) sees the current value.
  This exception does not exist on CGB.
- **`LY` and `LYC`.** The comparison uses values delayed by one dot. This is why a
  write to `LYC` that is timed against the Mode 2 transition can miss the comparison by
  one dot, and why the `LY = 153` flag behaves oddly around the line boundary.
- **`WX` on DMG and the late-window glitch.** Because the comparison uses the previous
  dot's `WX`, enabling the window on the fly can make it trigger at `LX = WX + 1`
  instead of `LX = WX`. The window then starts one pixel late and all its pixels are
  shifted right by one.
- **`LCDC` on CGB.** Writes to `LCDC`, `SCY`, `SCX` and `WX` are held for two dots.
  The PPU therefore sees a coherent value two dots after the CPU write, which removes
  most of the DMG mid-fetch glitches but creates its own (tile-data-select) glitches.

### 12.2 `SCX` low bits

The low three bits of `SCX` are latched three dots into Mode 3, at the end of the dummy
fetch. A write before that dot affects the current line; a write after it affects only
the next line. The high five bits are read per tile, so a mid-line write changes the
horizontal scroll of the tiles that have not been fetched yet.

Because the low bits are used only for the initial discard, a mid-line `SCX` write that
changes only the low three bits has no effect on the current line.

### 12.3 The DMG `BGP` OR bug

On true DMG hardware, the palette seen by the PPU during the one-dot delay is the
bitwise OR of the previous and the new `BGP` value:

```text
effective_BGP ← BGP | last_BGP
```

The effect is a one-pixel-wide color corruption when `BGP` is written inside Mode 3.
The quirk exists on DMG hardware but not on CGB in DMG compatibility mode. The same
kind of OR does not exist for `OBP0`/`OBP1`.

### 12.4 `STAT` write quirk on DMG

A write to `STAT` on DMG takes effect one dot later. During that transition dot the
register behaves as if `$FF` had been written:

- all four interrupt-enable bits read as 1 during the transition dot;
- the `LYC = LY` flag is not affected;
- the mode field is not affected.

If any source is active at that moment, a spurious STAT interrupt can be requested even
though the value being written disables it. This is the quirk that lets some games
generate an interrupt by writing `$00` to `STAT` during OAM scan, HBlank or VBlank.
CGB (including DMG compatibility mode) applies the write immediately and does not have
the quirk.

### 12.5 `OPRI`

`OPRI` is writable, but the object priority mode used by the mixer is captured when the
boot ROM locks. A write after that point changes the byte that software reads back but
does not change sprite priority. This matters for CGB games that try to switch to
DMG-style priority at runtime.

### 12.6 Palette RAM access

On CGB, palette data (`BCPD`/`OCPD`) cannot be accessed while the PPU is reading palette
RAM (Mode 3). A read returns `$FF`; a write is ignored. The index register (`BCPS`/
`OCPS`) and its auto-increment bit remain accessible. A blocked write to `BCPD` still
advances the index when auto-increment is enabled — the write is discarded but the
address counter moves.

The index registers read back with bit 6 set and bits 5–0 holding the address. Auto-
increment applies to writes only, not to reads.

---

## 13. Memory access and bus arbitration

### 13.1 CPU access by mode

| Mode | VRAM | OAM | CGB palette RAM |
| --- | --- | --- | --- |
| 0 (HBlank) | free | free | free |
| 1 (VBlank) | free | free | free |
| 2 (OAM scan) | free | blocked | free |
| 3 (pixel transfer) | blocked | blocked | blocked |

While a region is blocked:

- CPU reads return `$FF`.
- CPU writes are ignored.
- CGB `BCPD`/`OCPD` writes still advance the auto-increment counter.
- `VBK` and the palette index registers remain accessible.

### 13.2 Exact lock windows

The PPU acquires the buses slightly before the mode change becomes visible in `STAT`:

- **OAM:** acquired at dot 453 (DMG) or dot 452 (CGB) of the previous line, i.e. one or
  two dots before Mode 2 is readable at dot 454.
- **VRAM:** acquired at dot 77 of the OAM scan, two dots before Mode 3 is readable at
  dot 79; the pipeline itself starts at dot 80.
- **Both:** released on the dot Mode 3 ends, which is the first HBlank dot.

On DMG, OAM is released for exactly one dot at dot 75 of the OAM scan and re-acquired at
dot 77. A CPU write timed to that dot succeeds even though the PPU is nominally in
Mode 2.

On the first line after the LCD is enabled, both buses are released one dot before the
delayed HBlank mode update.

### 13.3 What “blocked” means for the fetcher

The fetcher reads VRAM on fetch steps 1, 3 and 5. OAM is read on object steps 1–2. When
the PPU owns a bus it always wins over the CPU; when an OAM DMA owns the OAM bus, the
PPU's OAM read is skipped and its latch keeps the previous word. When an HDMA owns the
VRAM bus, a PPU read and an HDMA write can collide; the PPU sees the byte that wins the
arbitration, which is determined by the relative timing of the two accesses.

---

## 14. STAT interrupts

### 14.1 The interrupt line

Four sources are OR-ed into one internal STAT line when their enable bits are set:

```text
line ← (STAT.lyc_enable  and LYC == LY)
     or (STAT.mode2_enable and mode == 2)
     or (STAT.mode1_enable and mode == 1)
     or (STAT.mode0_enable and mode == 0)
```

The interrupt is requested on a rising edge of this line, one dot after the edge is
detected. A source that rises while the line is already high does not create an edge;
this is **STAT blocking**. Turning sources off can lower the line and re-arm it; turning
a source on while its condition is already true creates an edge only if the line was low.

A consequence: enabling Mode 0 and Mode 1 interrupts together produces no Mode 1
interrupt. On line 143 the HBlank state (`STAT` mode 0) transitions directly into
VBlank (mode 1), so the OR-ed line stays high across the boundary and there is no
rising edge for the VBlank source.

### 14.2 Mode sources are edge-detected at transitions

The Mode 2 source is not evaluated on every dot. It is sampled only when the PPU
transitions into OAM scan, and the result is delivered one dot later. In particular,
writing `STAT` bit 5 while the PPU is already in OAM scan does *not* immediately raise an
interrupt. Mode 0 and Mode 1, by contrast, are evaluated continuously while their mode is
active, so enabling them mid-mode can raise an edge.

### 14.3 `LYC = LY`

The flag is recomputed every dot from the delayed `LY` and `LYC` values, subject to the
suppression windows:

- On DMG, the flag is forced low around the `LY` increment at dot 453 and during dots
  2–6 of line 153.
- On CGB, the flag retains its previous value during those windows.
- The flag is readable at `STAT` bit 2 and is updated even when the interrupt is
  disabled.

Because `LY` updates three dots before the line boundary, a comparison against the
*next* line can become true while the current line is still finishing. Software that
polls `LY` and `LYC` sees the comparison move accordingly.

### 14.4 DMG VBlank source aliasing

On true DMG hardware, the Mode 1 source is also enabled by `STAT` bit 5 (the OAM
interrupt enable):

```text
if true DMG:
    line ← line or (STAT.mode2_enable and mode == 1)
```

CGB does not have this aliasing. It is one of the reasons a program that enables the
OAM interrupt can receive unexpected VBlank STAT interrupts on DMG.

### 14.5 The VBlank transition

At the end of line 143 the PPU evaluates the OAM source one more time and then enters
VBlank. The evaluation uses a variant that does **not** update the internal “last line
state”:

- The result can schedule an interrupt for the OAM source.
- The line state used for edge detection is not cleared, so an `LYC = LY` interrupt for
  line 144 may be blocked if the line was already high.

This is observable as a missing first `LYC` interrupt after VBlank when the OAM and
`LYC` sources are enabled together.

### 14.6 Interaction with `STAT` reads

Reading `STAT` returns:

```text
1, enable bits, LYC flag, mode
```

The top bit is always 1. The mode field follows §5.6, not necessarily the internal mode.
On DMG a read during the one-dot write transition returns 1 in all four
interrupt-enable bits; the `LYC = LY` flag and the mode field keep their normal values.

---

## 15. The LCD panel model

The panel is not a framebuffer that the PPU writes pixel by pixel. It copies one
complete row at a time from a 160-pixel row buffer.

### 15.1 Row copy

- The PPU pushes pixels into the row buffer during Mode 3.
- At dot 452 the panel copies the row buffer into the visible row selected by its own
  row counter, then clears the row buffer to the default color.
- At dot 456 the panel advances its row counter and resets its row tick counter.
- If rendering is disabled (PPU off), the panel clears the row instead of copying it.

The row copy happens four dots before the line ends, so a Mode 3 that runs unusually
long still has its pixels copied as long as it finishes before dot 452. If Mode 3 were
to run past dot 452 the panel would copy an incomplete row; the PPU therefore keeps
Mode 3 inside the line.

### 15.2 The 148-row frame counter

The panel counts 148 rows (67488 dots) and then reaches a synchronisation point. What
happens next depends on the PPU:

- **PPU on:** the panel stalls at the synchronisation point until the PPU starts a new
  frame (the `LY = 153 → 0` transition). It then resumes in lock-step for the new frame.
- **PPU off:** the panel keeps ticking and clears rows, producing a blank frame.

The PPU frame is 154 lines (70224 dots), while the panel frame counter is 148 rows.
This mismatch is intentional: the panel is always waiting at the end of its 148 rows for
the PPU's next frame, and that wait absorbs the six VBlank lines.

Practical consequences:

- Turning the PPU off during VBlank and back on before the panel reaches its
  synchronisation point produces a clean next frame.
- Turning it off after the synchronisation point makes the panel begin clearing the next
  frame, so a blank frame is displayed even if the PPU is re-enabled shortly after.

### 15.3 When the panel loses synchronisation

Disabling the PPU mid-frame:

- Copies the current row to the panel (so the partial row is visible).
- Advances the panel row counter, but only if at least 256 panel ticks have elapsed
  since the last disable. A rapid off/on/off sequence can therefore freeze the panel's
  row selector.
- Disables rendering, resets the panel row ticks, and aborts any pending wait for a new
  frame.
- On DMG, clears the whole panel immediately (passive matrix; pixels decay quickly).
- On CGB, leaves the already-rendered rows visible (active matrix; pixels are held by
  capacitors).

On CGB a carefully timed off/on/off sequence can leave old rows frozen on screen
indefinitely, because the panel never reaches a synchronisation point and the PPU never
rewrites those rows.

---

## 16. LCD enable and disable

### 16.1 Disabling

Writing `LCDC` bit 7 = 0 disables the PPU immediately, on the same dot:

```text
procedure turn_off():
    dots ← 0
    LY ← 0
    clear sprite list
    reset fetcher and both FIFOs
    mode ← 2                      # internal mode, but STAT stays HBlank-like
    release VRAM and OAM
    mark the next line as the glitched first line
    panel.disable()
```

`LY` reads 0 while the PPU is off. The panel behaves as described in §15.3.

### 16.2 Enabling

Writing `LCDC` bit 7 = 1 starts the PPU at dot 0 of line 0:

```text
procedure turn_on():
    LY ← 0
    stat.lyc_flag ← (LYC == LY)   # set, but no interrupt is raised here
    panel.enable()
    mark the first line as glitched
```

The first line after enabling is not identical to a normal line:

- OAM scan reads nothing and finds no sprites.
- The `STAT` mode stays HBlank for the whole OAM scan.
- The HBlank mode update is delayed by two dots.
- VRAM and OAM are released one dot before the delayed mode update.
- The pixel pipeline still runs, so the line displays background/window content with no
  sprites.

On CGB double speed the PPU starts one dot earlier (dot 1 instead of dot 0) after
enabling. This is a small phase offset that affects the first line's timing.

---

## 17. OAM DMA and HDMA

### 17.1 OAM DMA (`$FF46`)

Writing `$FF46` starts a 160-byte copy from `XX00–XX9F` to OAM. The source address is
`value << 8`, with bit 13 cleared if it would point into `$E000–$FFFF` (OAM DMA cannot
read from Echo RAM or I/O).

Timing:

- **Startup:** 4 DMA ticks. In normal speed this is 8 T-states (2 machine cycles); in
  double speed it is 4 T-states (1 machine cycle).
- **Transfer:** one byte per machine cycle. Normal speed: 4 dots per byte, 640 dots
  total. Double speed: 2 dots per byte, 320 dots total.

During the transfer:

- OAM is owned by the DMA controller. CPU reads of OAM return `$FF`; CPU writes are
  ignored.
- The PPU cannot read OAM. Its OAM read is skipped and the previous OAM word remains in
  its latch, so sprite detection can use stale data.
- `STAT` reports HBlank for the whole OAM scan while DMA is active. The internal mode is
  still Mode 2, so the OAM interrupt source still fires normally.
- A PPU OAM read that collides with the DMA's pending write can return the byte being
  transferred instead of the sprite byte. This is an arbitration conflict, not a
  deterministic value; it depends on the exact dot alignment.
- HDMA and OAM DMA can run concurrently on CGB. When they do, the HDMA byte can also be
  written to OAM at the address given by the low byte of the HDMA source, once every two
  bytes.

### 17.2 HDMA (`$FF51–$FF55`)

HDMA copies from ROM/RAM to VRAM in 16-byte chunks.

- **General-purpose mode (`HDMA5` bit 7 = 0):** starts immediately after two internal
  phases and runs as fast as the bus allows. The CPU is stopped for the duration.
- **HBlank mode (`HDMA5` bit 7 = 1):** transfers one 16-byte chunk per HBlank and pauses
  at the end of each chunk. It resumes on a non-HBlank → HBlank transition of the STAT
  mode field.

Resumption details:

- HDMA samples the *unglitched* internal mode, not the mode read from `STAT`.
- If the CPU is halted when the HBlank transition occurs, HDMA does not resume. It also
  does not resume when the CPU wakes up still inside HBlank. It resumes only if it
  observes a fresh transition while not halted.
- HDMA is blocked while the PPU owns VRAM during Mode 3. If a chunk is still in flight
  when Mode 3 begins, the PPU wins the bus; the HDMA write can be lost or delayed
  depending on the exact phase.
- HDMA is blocked during a speed switch on CGB.

HDMA and the PPU share VRAM through an arbitration bus. A PPU read that collides with an
HDMA write returns whichever access won; this affects Mode 3 only in the rare case where
an HDMA chunk is still being written when the PPU starts fetching.

---

## 18. CGB-specific behaviour

### 18.1 Map attributes

CGB VRAM bank 1 holds one attribute byte per map cell. The PPU reads it together with
the tile number. The attributes control palette, VRAM bank, X/Y flip and BG priority.
Bank 0 holds the tile numbers and tile data as on DMG.

### 18.2 Palettes

Background and object palette RAM are separate 64-byte regions. Each color is a
little-endian RGB555 word:

| Bits | Channel |
| --- | --- |
| 0–4 | Red |
| 5–9 | Green |
| 10–14 | Blue |

Palette memory is initialized to white by the boot ROM. It is not initialized on reset
by hardware, so a program that never writes it may show whatever the boot ROM left.

### 18.3 Double speed

In double speed one PPU dot lasts two CPU T-states. Consequences:

- The PPU still produces 70224 dots per frame, so the frame rate is unchanged.
- The CPU has twice as many cycles per dot, which is what makes double-speed
  mid-scanline effects possible.
- HBlank becomes visible in `STAT` one dot later than the internal transition.
- The PPU is stopped for the duration of the speed switch, so the switch costs PPU time.

### 18.4 The `LY` transient

During the dot in which `LY` increments, the CGB can expose a transient value that is
neither the old nor the new line. The value follows a fixed pattern determined by the
line number and the CPU/PPU alignment; it does not occur on DMG, and it occurs in both
single and double speed. Software that reads `LY` on consecutive dots can observe the
glitch.

### 18.5 Tile-data-select glitch

Changing `LCDC` bit 4 between the low and high bitplane fetches can corrupt the fetched
tile:

- **Signed → unsigned:** the byte is taken from the most recent fetch that used unsigned
  addressing. The value can even come from a sprite fetch, because sprite tile data is
  always read unsigned.
- **Unsigned → signed:** the fetch is redone with the new addressing mode, so the low
  (or high) byte is read again immediately, one dot late.

The glitch only affects addresses in the `$9000–$97FF` range; tiles in `$8800–$8FFF`
map to the same bytes in both modes and are unaffected.

### 18.6 `WX` mid-fetch glitch

On CGB, if `LX == WX` is true during a window fetch (because `WX` was changed after the
window activated), the fetcher substitutes data from the previous fetch:

- at the tile-number step, the previous tile number is reused;
- at the low-bitplane step, the previous high bitplane is used as the low byte;
- at the high-bitplane step, the previous low bitplane is used as the high byte.

This produces a corrupted window tile, usually a solid or striped pattern. The condition
is `LX == WX`, so it can even happen on the very first window tile when `WX = 0` and
`SCX mod 8 > 0`.

### 18.7 Window disabled while activating

If the window is disabled on the same dot it was activating, the CGB does not simply
abort the fetch:

- The BG FIFO is partially refilled with the current bitplane latch contents.
- The fetcher restarts from the background tile 0.
- If the FIFO was already empty, it can be refilled twice, producing one pixel from the
  first refill followed by eight pixels from the second.

This is visually a corrupted strip at the window position followed by a restart of the
background. It is closely related to the DMG window abort behaviour, but the FIFO damage
is CGB-specific.

### 18.8 `OPRI`

The object priority mode is captured at boot-ROM lock. Only a boot-ROM write can change
it. See §10.3 and §12.5.

---

## 19. Required state

The following is the minimum state a cycle-accurate implementation needs. Names are
descriptive; the structure is not prescriptive.

### 19.1 Top level

```text
record PpuState:
    dots        : 0..455
    line        : 0..153          # internal line counter
    ly          : 0..153          # value visible to software
    mode        : 0..3            # internal mode
    stat_mode   : 0..3            # value read from STAT
    lx          : 0..168
    lcdc        : Lcdc
    stat        : Stat
    scy, scx    : u8
    wy, wx      : u8
    lyc         : u8
    bgp         : u8
    obp0, obp1  : u8

    last_lcdc   : Lcdc
    last_wx     : u8
    last_bgp    : u8              # true DMG only
    last_ly     : u8
    last_lyc    : u8

    pending_lcdc, pending_scy, pending_scx, pending_wx : PendingWrite   # CGB
    pending_stat : PendingWrite                                          # true DMG

    bg_fifo     : Fifo<BgPixel, 8>
    obj_fifo    : Fifo<ObjPixel, 8>
    sprite_list : array[0..167] of list<OamEntry>

    window      : WindowState
    fetcher     : FetcherState
    irq         : IrqState

    first_line_after_enable : bool
    hblank_delay            : u8    # first line after enable
    vblank_stat_hblank      : bool  # DMG line 153, dots 453-454

    dma_active  : bool
    hdma_active : bool
```

### 19.2 Pixel and sprite records

```text
record BgPixel:
    color_index : 0..3
    attributes  : u8              # CGB only

record ObjPixel:
    color_index : 0..3
    attributes  : u8
    oam_index   : 0..39
    x           : u8              # the LX at which it was fetched

record OamEntry:
    index : 0..39
    y     : u8
    x     : u8
```

### 19.3 Fetch state

```text
record FetcherState:
    stage          : {GetTile0, GetTile1, Low0, Low1, High0, High1, Idle}
    push_enabled   : bool
    pending_fill   : bool

    tile_x         : u16           # BG tile counter, advances by 8
    tile_number    : u8
    attributes     : u8            # CGB
    latched_scy    : u8            # CGB

    tile_data_low  : u8
    tile_data_high : u8
    data_address   : u16           # recomputed before each bitplane read

    interrupted    : {has_data, low, high}
    cached_last    : {data, age}   # CGB glitch support
```

### 19.4 Window state

```text
record WindowState:
    frame_latch      : bool        # WY matched somewhere in this frame
    line_counter     : u8          # starts at 255, increments on activation
    tile_x           : u8          # window map column
    activating       : bool
    active           : bool
    just_activated   : bool
    disabled_during_activation : bool   # CGB
```

### 19.5 Interrupt state

```text
record IrqState:
    line_state   : bool            # previous value of the OR-ed STAT line
    pending      : bool            # edge seen, request next dot
    lyc_suppress : bool            # DMG suppression windows
```

### 19.6 Panel state

```text
record PanelState:
    pixels          : array[160 × 144] of color
    row_buffer      : array[160] of color
    row_cursor      : 0..159
    row             : 0..147
    row_ticks       : u16
    frame_ticks     : u32
    ppu_on          : bool
    rendering       : bool
    row_increment   : bool
    waiting_frame   : bool
```

---

## 20. Reference pseudocode

The following is a complete, if compact, statement of the per-dot behaviour. It is
written for clarity, not for a specific language.

### 20.1 Top-level tick

```text
procedure ppu_tick():
    if not lcdc.enable:
        apply_delayed_writes()
        return

    advance_phase()

    window.frame_latch ← window.frame_latch or (lcdc.win_enable and ly == wy)

    evaluate_stat()

    last_lcdc ← lcdc
    last_wx   ← wx
    last_bgp  ← bgp          # true DMG only
    last_ly   ← ly
    last_lyc  ← lyc

    apply_delayed_writes()
    refresh_stat_mode()
```

### 20.2 Phase machine

```text
procedure advance_phase():
    if mode == OAM_SCAN:
        oam_scan_dot()
    else if mode == PIXEL_TRANSFER:
        pixel_dot()
    else if mode == HBLANK:
        hblank_dot()
    else:
        vblank_dot()
```

### 20.3 OAM scan

```text
procedure oam_scan_dot():
    if line == 0 and first_line_after_enable:
        if dots == 78:
            mode ← PIXEL_TRANSFER
        dots ← dots + 1
        if dots == 80:
            enter_pixel_transfer()
        return

    if dots is even:
        entry ← oam_latch
        consider_entry(entry.y, entry.x)
        index ← index + 1
    else:
        oam_request(0xFE00 + 4 * index)

    dots ← dots + 1

    if dots == 79:
        mode ← PIXEL_TRANSFER
    else if dots == 80:
        enter_pixel_transfer()
```

### 20.4 Entering pixel transfer

```text
procedure enter_pixel_transfer():
    reset_fetcher()
    lx ← 0
    acquired_vram ← true
    acquired_oam  ← true
    panel.reset_row_cursor()
    dots ← 80
    phase ← DUMMY_FETCH
```

### 20.5 Pixel transfer

```text
procedure pixel_dot():
    if phase == DUMMY_FETCH:
        dots ← dots + 1
        if dots == 83:
            bg_fifo.fill_junk()
            discard_target ← scx mod 8
            if discard_target > 0:
                check_window_activation()
                phase ← DISCARD
            else:
                phase ← VISIBLE
        return

    if phase == DISCARD:
        apply_window_activation()
        apply_fill()
        if can_pop():
            bg_fifo.pop()
            discarded ← discarded + 1
            if discarded == discard_target:
                phase ← VISIBLE
        fetcher_tick()
        push_stage()
        dots ← dots + 1
        return

    # VISIBLE
    apply_window_activation()
    apply_fill()

    if can_pop():
        bg ← bg_fifo.pop()
        obj ← obj_fifo.pop() if obj_fifo not empty

        if lx >= 8:
            lcd.push(mix(bg, obj))
            if lx == 167:
                lx ← 168
                dots ← dots + 1
                enter_hblank()
                return
            check_window_activation()

        lx ← lx + 1

    fetcher_tick()

    if CGB:
        handle_window_disabled_glitch()

    push_stage()
    dots ← dots + 1
```

### 20.6 Fetcher

```text
procedure fetcher_tick():
    switch stage:
        GetTile0:
            if window.active and lcdc.win_enable:
                address ← window_map_address()
            else:
                window.active ← false
                address ← bg_map_address()

            tile_number, attributes ← vram[address]
            if CGB: latched_scy ← scy
            stage ← GetTile1

        GetTile1:
            stage ← Low0

        Low0:
            data_address ← tile_data_address()
            tile_data_low ← vram[data_address]
            stage ← Low1

        Low1:
            stage ← High0

        High0:
            data_address ← tile_data_address()
            tile_data_high ← vram[data_address + 1]
            stage ← High1

        High1:
            if DMG and not window.just_activated:
                tile_x ← tile_x + 8
            push_enabled ← true
            stage ← Idle

        Idle:
            no-op
```

### 20.7 Window activation

```text
procedure check_window_activation():
    if not window.frame_latch or window.active or not lcdc.win_enable:
        return

    triggered ← false
    if CGB:
        triggered ← (lx == wx)
    else:
        triggered ← (lx == last_wx) or
                    (lx == last_wx + 1 and not last_lcdc.win_enable)

    if triggered:
        window.activating ← true
        window.line_counter ← window.line_counter + 1
        push_enabled ← false
        stage ← Idle              # the window fetch restarts

procedure apply_window_activation():
    if not window.activating:
        return

    bg_fifo.clear()
    window.active ← true
    window.just_activated ← true
    window.tile_x ← 0
    window.activating ← false
    stage ← GetTile0
```

### 20.8 Sprite merge

```text
procedure merge_object(pixels, attributes, index, x):
    for i in 0..7:
        candidate ← {pixels[i], attributes, index, x}

        if i >= obj_fifo.size:
            obj_fifo.push(candidate)
            continue

        incumbent ← obj_fifo[i]
        take ← false

        if candidate.opaque:
            if not incumbent.opaque:
                take ← true
            else if DMG or OPRI.priority_mode == 1:
                take ← (candidate.x == incumbent.x and
                        candidate.oam_index < incumbent.oam_index)
            else:
                take ← candidate.oam_index < incumbent.oam_index

        if take:
            obj_fifo[i] ← candidate
```

### 20.9 STAT

```text
procedure evaluate_stat():
    eq ← lyc_comparison_enabled() and (last_lyc == last_ly)

    if irq.pending:
        request_interrupt(STAT)
        irq.pending ← false
    else:
        line ← (stat.lyc_enable  and eq)
             or (stat.mode0_enable and mode == HBLANK)
             or (stat.mode1_enable and mode == VBLANK)
             or (stat.mode2_enable and mode == OAM_SCAN)

        if true DMG:
            line ← line or (stat.mode2_enable and mode == VBLANK)

        irq.pending ← (not irq.line_state) and line
        irq.line_state ← line

    stat.lyc_flag ← eq

procedure refresh_stat_mode():
    if mode == OAM_SCAN and (first_line_after_enable or dma_active or dmg_vblank_alias):
        stat_mode ← HBLANK
    else:
        stat_mode ← mode
```

### 20.10 Panel

```text
procedure panel_tick():
    if panel.waiting_frame:
        return

    if panel.row < 148:
        panel.row_ticks ← panel.row_ticks + 1

        if panel.row_ticks == 256:
            panel.row_increment ← true
        else if panel.row_ticks == 452:
            if panel.rendering:
                copy row_buffer to pixels[panel.row]
                clear row_buffer
            else:
                clear pixels[panel.row]
        else if panel.row_ticks == 456:
            panel.row ← panel.row + 1
            panel.row_ticks ← 0

    panel.frame_ticks ← panel.frame_ticks + 1
    if panel.frame_ticks == 67488:
        panel.waiting_frame ← panel.ppu_on
        panel.row ← 0
```

---

## 21. Casuistry catalogue

Each row is an independently observable behaviour. The “effect” column describes what
software or a test sees, not the internal mechanism.

| # | Situation | Effect |
| --- | --- | --- |
| 1 | `SCX mod 8 > 0` | Mode 3 is extended by that many dots; the first visible pixel is delayed by the same amount. |
| 2 | `SCX` low bits written after dot 83 | No effect on the current line. |
| 3 | `SCX` high bits written mid-line | Remaining tiles are fetched with the new scroll; already-fetched tiles are unchanged. |
| 4 | `SCY` written between the two bitplane reads (DMG) | Low and high bytes can come from different rows of the tile. |
| 5 | Same write on CGB | No desync: `SCY` is latched once per tile. |
| 6 | `WX` written mid-line (DMG) | Window triggers one pixel late; all window pixels shift right by one. |
| 7 | Window enabled at `LX = WX + 1` (DMG) | Window triggers even though the comparison point has passed; a BG pixel appears first and the window is shifted. |
| 8 | `WX = 0`, `SCX mod 8 > 0` | Activation costs one extra dot; the first window tile can be corrupted on CGB. |
| 9 | `WX = 0` with changing `SCX` | The window stutters horizontally depending on `SCX mod 8`. |
| 10 | `WX = 166` | One-pixel-wide window at the right edge. |
| 11 | `WY` changed after the frame latch | No effect for the rest of the frame. |
| 12 | Window disabled and re-enabled on one line | Another activation, another 6-dot stall, window line counter advances. |
| 13 | Window disabled just as it activates (CGB) | Partial/double FIFO refill; corrupted strip and restart from background tile 0. |
| 14 | Window enabled but not triggered at `LX = WX` | One color-0 pixel is inserted and the following content shifts right (DMG; CGB only while active). |
| 15 | Sprite `X = 0` | Fully clipped; no pixels displayed. |
| 16 | Sprite `X >= 168` | Counts against the ten-sprite limit but is never drawn. |
| 17 | Eleventh matching sprite | Ignored, even if earlier sprites end up transparent. |
| 18 | Sprite over a tile boundary | Fetch costs 6–11 dots; the line is stretched accordingly. |
| 19 | Second sprite at the same X | Additional fetch costs 6 dots (already at a push stage). |
| 20 | OAM tile number changed mid-line | The sprite uses the new tile number (fetched during Mode 3). |
| 21 | OAM DMA in progress during Mode 3 | Sprite fetches can read DMA data; `STAT` shows HBlank during OAM scan. |
| 22 | `BGP` written during Mode 3 (true DMG) | One pixel uses `old OR new`, producing a wrong color. |
| 23 | `STAT` written during an active source (true DMG) | The transition dot behaves as `$FF`; a spurious STAT interrupt can fire. |
| 24 | `LYC = LY` at dot 453 (DMG) | Flag forced low for the increment window. |
| 25 | `LYC = 153` during line 153 (DMG) | Comparison suppressed from dots 2–6; the flag never becomes visible. |
| 26 | `LYC = 0` during line 153 | `LY` is reset to 0 at dot 2 (DMG) or dot 3–4 (CGB), so the comparison matches from dot 7 onward and remains true across the line boundary. |
| 27 | Mode 0 and Mode 1 interrupts both enabled | No Mode 1 interrupt on line 143: HBlank transitions directly into VBlank, so the STAT line never goes low between them. |
| 28 | Mode 2 interrupt enable written while in Mode 2 | No immediate interrupt: Mode 2 is edge-detected only at the transition. |
| 29 | `STAT` bit 5 set on DMG | Also enables the VBlank STAT source. |
| 30 | OAM interrupt enabled across the VBlank transition | The first `LYC` interrupt of line 144 may be blocked. |
| 31 | First line after LCD enable | No sprites; `STAT` reports HBlank during the scan; HBlank update delayed two dots. |
| 32 | OAM write at dot 75 (DMG) | Succeeds: the OAM bus is released for one dot. |
| 33 | VRAM access at dot 77–79 | Fails: VRAM is already owned by the PPU. |
| 34 | `LCDC` bit 1 or 0 written at `LX = 8` (DMG) | Takes effect immediately for that one pixel, unlike the normal one-dot delay. |
| 35 | CGB double speed, HBlank | `STAT` shows HBlank one dot after the internal transition. |
| 36 | CGB double speed, LCD enabled | PPU starts at dot 1 instead of dot 0. |
| 37 | CGB `LY` increment | A one-dot transient value can be read. |
| 38 | `LCDC` bit 4 changed mid-fetch (CGB) | Stale byte from the last unsigned fetch, or an immediate re-read; only in `$9000–$97FF`. |
| 39 | `WX` changed during an active window fetch (CGB) | Tile number/bitplanes substituted from the previous fetch. |
| 40 | `OPRI` written after boot | Read-back changes, priority does not. |
| 41 | Palette data write during Mode 3 (CGB) | Write discarded, but auto-increment still advances. |
| 42 | PPU disabled mid-frame (DMG) | Panel clears immediately; current row is copied first. |
| 43 | PPU disabled mid-frame (CGB) | Rendered rows stay visible; rapid off/on/off can freeze them. |
| 44 | PPU disabled after the panel sync point | The panel clears a whole frame before the PPU's next frame is shown. |
| 45 | HDMA resumption while halted | HBlank HDMA does not resume if the CPU is halted through the transition. |
| 46 | OAM DMA startup | 4 DMA ticks: 8 T-states normal, 4 T-states double speed. |
| 47 | OAM DMA during OAM scan | The PPU's OAM read is skipped; the scan uses stale data. |
| 48 | Frame length | 154 lines × 456 dots = 70224 dots; the panel's own counter is 148 rows and then waits. |

---

## 22. Implementation checklist

A useful order for bringing up a new implementation:

1. **Dot clock and line counter.** 456 dots, 154 lines, `LY` updated at dot 453, the
   line-end sequence at dots 452–455.
2. **Mode timeline.** 2 → 3 → 0 on visible lines, 1 on lines 144–153, with the early
   `STAT` assertions and the DMG first-line aliases.
3. **OAM scan.** One entry per two dots, Y filter, ten-entry cap including off-screen
   sprites, bucket by X.
4. **Fetcher.** Six steps, address recomputation before each bitplane read, push only
   when the FIFO is empty, tile counter advanced at the right step.
5. **Mixer.** BG FIFO, OBJ FIFO, one pixel per dot, `LX` offset, color resolution.
6. **Window.** Frame latch on `WY`, per-dot `LX == WX`, line counter, 6-dot restart,
   zero-pixel glitch, mid-fetch disable.
7. **Sprites.** 6–11 dot stall, tile-number read in Mode 3, merge priority, 8×16
   geometry, clipping.
8. **Register latencies.** One dot on DMG, two dots on CGB, `BGP` OR, `STAT` quirk,
   `SCX` low-bit latch point.
9. **Memory arbitration.** Bus ownership windows, CPU `$FF`/ignore behaviour, DMA
   conflicts.
10. **STAT.** OR-ed sources, edge detection, blocking, Mode 2 transition sampling, DMG
    VBlank aliasing, `LYC` suppression windows.
11. **Panel.** Row copy at 452, row advance at 456, 148-row counter, stall behaviour,
    disable/clear semantics.
12. **DMA/HDMA.** Startup timing, bus ownership, HBlank resumption rules.

A test strategy that exercises all of the above should include, at minimum:

- steady-state rendering with `SCX mod 8` swept across 0–7;
- `WY`/`WX` changes on every dot of a line, including `WX = 0` and `WX = 166`;
- window enable/disable on consecutive dots;
- sprites at `X = 0, 1, 7, 8, 167, 168`, overlapping sprites, ten-sprite overflow;
- `BGP`, `OBP0`, `OBP1` writes inside Mode 3;
- `STAT` writes during every mode;
- `LYC` changes around the line boundary and around line 153;
- LCD off/on at every dot of a frame;
- OAM DMA and HDMA concurrent with rendering;
- CGB palette, VRAM bank, attribute flips, `OPRI`, double speed.

---

## 23. References

- Pan Docs, *Rendering*, *LCD Status Registers*, *OAM*, *LCD Position and Scrolling*,
  *Interrupts*, and *CGB Registers*.
  <https://gbdev.io/pandocs/>
- Pan Docs, *The Pixel FIFO*.
  <https://gbdev.io/pandocs/pixel_fifo.html>
- Hacktix, *Game Boy: Extended Development Guide — PPU*.
  <https://hacktix.github.io/GBEDG/ppu/>
- Matt Currie, *The Comprehensive Game Boy PPU Documentation*.
  <https://github.com/mattcurrie/mealybug-tearoom-tests/blob/master/the-comprehensive-game-boy-ppu-documentation.md>
- mooneye-gb, PPU and LCD timing tests.
  <https://github.com/Gekkio/mooneye-gb>
- Mealybug Tearoom Tests.
  <https://github.com/mattcurrie/mealybug-tearoom-tests>
