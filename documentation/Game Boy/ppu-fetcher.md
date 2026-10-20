# The DMG PPU Pixel Fetcher

A complete, from-scratch description of the DMG pixel fetcher: the state machine that
turns VRAM into a stream of pixels, the two FIFOs it feeds, and every obscure timing
behaviour that a cycle-accurate implementation has to reproduce. Someone reading this
document should be able to build a cycle-accurate fetcher from nothing and understand
*why* each rule exists.

The behaviour described here is the **real DMG hardware behaviour**, derived from the
primary hardware research listed in [§14](#14-references): Pan Docs, GBEDG (Hacktix's
*Game Boy: Extended Development Guide*), Matt Currie's *Comprehensive Game Boy PPU
Documentation*, and the mooneye-gb PPU timing tests. Where the research is silent on a
point, the point is omitted rather than guessed.

- **Source:** [`core/src/fetcher.cpp`](../../core/src/fetcher.cpp), [`core/inc/fetcher.h`](../../core/inc/fetcher.h), [`core/src/ppu.cpp`](../../core/src/ppu.cpp)
- **Tests:** Mealybug `m3_*` ROMs ([`vendor/mealybug/ppu/`](../../vendor/mealybug/ppu/)) and `dmg-acid2` ([`vendor/dmg-acid2/`](../../vendor/dmg-acid2/))
- **Primary reference:** [Pan Docs, *Rendering* / `pixel_fifo`](https://gbdev.io/pandocs/pixel_fifo.html), [GBEDG, *PPU*](https://hacktix.github.io/GBEDG/ppu/), [Matt Currie, *The Comprehensive Game Boy PPU Documentation*](https://github.com/mattcurrie/mealybug-tearoom-tests/blob/master/the-comprehensive-game-boy-ppu-documentation.md)

---

## Table of contents

1. [What the hardware actually is](#1-what-the-hardware-actually-is)
2. [The cycle model](#2-the-cycle-model)
3. [The 7-state fetch state machine](#3-the-7-state-fetch-state-machine)
4. [When and how SCX, SCY, WX, LY and LCDC bits 3/4/6 are sampled](#4-when-and-how-scx-scy-wx-ly-and-lcdc-bits-346-are-sampled)
5. [How mid-scanline writes are honored](#5-how-mid-scanline-writes-are-honored)
6. [The window](#6-the-window)
7. [The window reactivation zero pixel](#7-the-window-reactivation-zero-pixel)
8. [Hardware behaviour catalogue](#8-hardware-behaviour-catalogue)
9. [The object fetch state machine](#9-the-object-fetch-state-machine)
10. [BG/OBJ FIFO merge and priority](#10-bgobj-fifo-merge-and-priority)
11. [Scanline start](#11-scanline-start)
12. [How this maps to QtreGB today](#12-how-this-maps-to-qtregb-today)
13. [Reimplementation recipe](#13-reimplementation-recipe)
14. [References](#14-references)

---

## 1. What the hardware actually is

The DMG PPU does not render a scanline in one pass. It runs a small **pixel fetcher**: a
state machine that repeatedly reads one 8-pixel tile row out of VRAM and pushes it into a
**background FIFO**. A second, parallel path fetches sprite rows into an **object FIFO**.
A mixer pops one pixel from each FIFO per output pixel and resolves priority.

The structural idea, and the one everything else follows from:

> The fetcher is a **7-state machine**. Each state lasts exactly **one T-cycle**. It feeds
> two **8-entry FIFOs**. The mixer consumes one pixel per T-cycle once the background FIFO
> has been primed.

Each FIFO holds **8** items. An item carries four fields:

| Field | Meaning |
|---|---|
| `pixel` | Color index, 0 to 3 |
| `palette` | Palette selector (0 to 7 on CGB; 0 or 1 on DMG) |
| `priority` | Object priority (0 on DMG; OAM index on CGB) |
| `bg_priority` | For the object FIFO, the sprite's BG-priority bit; for the BG FIFO, the CGB attribute priority bit |

The background FIFO is filled a whole tile row at a time; the object FIFO is filled a whole
sprite row at a time. The mixer drains them one item per pixel.

### 1.1 The PPU clock: T-cycles and dots

The Game Boy CPU and PPU share one clock. The smallest unit is a **T-cycle** (one clock
tick). A **dot** is the PPU's own unit and equals **2 T-cycles**. The PPU is counted in
T-cycles internally, and a state-machine divisor of 2 converts to dots.

A scanline is **456 dots** = 912 T-cycles. Mode 3 (pixel transfer) is nominally **172
dots**, but it stretches when the window or objects are fetched. The nominal split is:

| Mode | Dots | T-cycles | Role |
|---|---|---|---|
| Mode 2 | 80 | 160 | OAM scan |
| Mode 3 | 172 | 344 | Rendering |
| Mode 0 | 204 | 408 | HBlank |
| Mode 1 | 456 | 912 | VBlank (whole line) |

The fetcher only runs during Mode 3. Mode 2 (OAM scan) fills the visible-object list;
Mode 0 (HBlank) and Mode 1 (VBlank) do not fetch.

---

## 2. The cycle model

Three facts define the model. Get them wrong and nothing else lines up.

1. **One fetcher state = one T-cycle.** The main Mode 3 loop advances the fetcher by
   exactly one state per iteration, and each iteration costs one T-cycle.
2. **The PPU clock is 2 T-cycles per dot.** A scanline is 456 dots = 912 T-cycles.
3. **`position_in_line` starts at -16.** It is a signed counter that begins negative to
   model the fine-scroll pre-fetch, and reaches 160 at the end of the visible line.

### 2.1 The main per-cycle loop

The heart of Mode 3 is an infinite loop. Each pass renders at most one pixel and advances
the fetcher one state:

```
loop forever:
    render_pixel_if_possible()
    advance_fetcher_state_machine()
    if position_in_line == 160:
        break
    cycles_for_line += 1
    consume_one_T_cycle()
```

One loop iteration is one T-cycle, and one fetcher state is one T-cycle.
`advance_fetcher_state_machine` is the only thing that moves the fetcher forward.

### 2.2 T1 and T2: address then data

Every VRAM access is split across two states. The hardware determines the read address in
the first cycle and the read actually completes in the second cycle:

```
state GET_TILE_T1:            compute tile-map address, sample selectors
state GET_TILE_T2:            read tile index from that address
state GET_TILE_DATA_LOW_T1:   compute tile-data address, sample selectors
state GET_TILE_DATA_LOW_T2:   read bitplane 0
state GET_TILE_DATA_HIGH_T1:  compute tile-data address + 1, sample selectors
state GET_TILE_DATA_HIGH_T2:  read bitplane 1
state PUSH:                   wait for the BG FIFO to empty, then push 8 pixels
```

The `_T1` state computes the address (and samples the registers that select it); the `_T2`
state performs the actual VRAM read. This split is what makes mid-fetch register writes
observable: a write landing between T1 and T2 changes the address that T2 uses.

### 2.3 `position_in_line`

`position_in_line` is initialised to `-16` at the start of every line. It is incremented by
one in `render_pixel_if_possible` each time a pixel is emitted. The negative range models
the fine-scroll discard: the first `SCX & 7` pixels are fetched but thrown away, and the
counter is nudged forward to skip them (see [§11](#11-scanline-start)). The window and
object triggers are all expressed relative to this counter.

---

## 3. The 7-state fetch state machine

`advance_fetcher_state_machine` advances exactly one state per call. The seven states group
into four logical steps:

| Step | States | T-cycles | What it does |
|---|---|---|---|
| 1. Get tile | `GET_TILE_T1`, `GET_TILE_T2` | 2 | Compute the tile-map address, then read the tile index |
| 2. Get data low | `GET_TILE_DATA_LOW_T1`, `_T2` | 2 | Compute the tile-data address, then read bitplane 0 |
| 3. Get data high | `GET_TILE_DATA_HIGH_T1`, `_T2` | 2 | Compute the address + 1, then read bitplane 1 |
| 4. Push | `PUSH` | variable | Wait for the BG FIFO to empty, then push 8 pixels |

Steps 1 to 3 are fixed at 6 T-cycles. Step 4 is **not** fixed: `PUSH` is a waiting state
that repeats every T-cycle until the FIFO drains.

### 3.1 Step 1: Get tile

```
on GET_TILE_T1:
    map = 0x1800
    if LCDC.win_enable == 0:
        wx_triggered = false
    if LCDC.bg_map == 1 and wx_triggered == false:
        map = 0x1C00
    else if LCDC.win_map == 1 and wx_triggered == true:
        map = 0x1C00

    y = fetcher_y()
    if wx_triggered:
        x = window_tile_x
    else if (position_in_line + 16) < 8:        # unsigned comparison
        x = SCX >> 3
    else:
        x = ((SCX + position_in_line + 8) / 8) & 0x1F

    last_tile_index_address = map + x + (y / 8) * 32

on GET_TILE_T2:
    current_tile = vram_read(last_tile_index_address)
```

`GET_TILE_T1` samples the map-select bits (LCDC 3 and 6), the window state, `SCX` and
`SCY` (through `fetcher_y`), and computes `last_tile_index_address`. `GET_TILE_T2` reads
the tile index from that address.

### 3.2 Step 2: Get tile data, low bitplane

```
on GET_TILE_DATA_LOW_T1:
    y = fetcher_y()
    last_tileset = LCDC.tile_sel
    if last_tileset:
        tile_address = current_tile * 0x10
    else:
        tile_address = (signed)current_tile * 0x10 + 0x1000
    last_tile_data_address = tile_address + (y & 7) * 2

on GET_TILE_DATA_LOW_T2:
    current_tile_data[0] = vram_read(last_tile_data_address)
```

`LOW_T1` samples LCDC bit 4 (`tile_sel`) into `last_tileset`, computes the tile data base
address (signed tile index when bit 4 is clear), and stores `last_tile_data_address`.
`LOW_T2` reads bitplane 0.

### 3.3 Step 3: Get tile data, high bitplane

```
on GET_TILE_DATA_HIGH_T1:
    y = fetcher_y()
    last_tileset = LCDC.tile_sel
    if last_tileset:
        tile_address = current_tile * 0x10
    else:
        tile_address = (signed)current_tile * 0x10 + 0x1000
    last_tile_data_address = tile_address + (y & 7) * 2 + 1

on GET_TILE_DATA_HIGH_T2:
    current_tile_data[1] = vram_read(last_tile_data_address)
    if wx_triggered:
        window_tile_x = (window_tile_x + 1) & 0x1F
```

`HIGH_T1` recomputes the address with `+ 1` for the second bitplane. Note that it samples
`last_tileset` and `fetcher_y` **again**, independently of step 2. This is deliberate: it
is what lets a mid-fetch write to LCDC bit 4 or SCY mix the two bitplanes from different
tiles or rows (see [§8.3](#83-tile_sel-bitplane-mixing-glitch) and
[§8.4](#84-scy-is-re-read-at-every-t1)). `HIGH_T2` reads bitplane 1 and, when the window
is active, advances `window_tile_x`.

### 3.4 Step 4: Push (a waiting state)

```
on PUSH:
    state = PUSH
    if bg_fifo.size > 0:
        return                          # wait another T-cycle

    if window_disabled_insertion_condition():
        insert_single_zero_pixel()
        return

    push_bg_row(current_tile_data[0], current_tile_data[1])
    state = GET_TILE_T1
```

The first line pins the state to `PUSH`. The second line is the wait: **if the BG FIFO is
not empty, the state does nothing and stays in `PUSH` for another T-cycle.** Only when the
FIFO is empty does it push the 8 pixels and return to `GET_TILE_T1`. Because the mixer
drains one pixel per T-cycle and the fetch takes 6 T-cycles, `PUSH` normally waits about
2 T-cycles, but the wait is data-dependent, not fixed.

`push_bg_row` expands the two bitplanes into 8 FIFO items:

```
function push_bg_row(lower, upper, palette, bg_priority):
    for i in 0 .. 7:
        bit = 7 - i
        pixel = ((lower >> bit) & 1) | (((upper >> bit) & 1) << 1)
        fifo[i] = { pixel, palette, priority = 0, bg_priority }
    fifo.size = 8
```

The color index is `(bit of lower) | (bit of upper << 1)`, MSB first. On CGB, attribute bit
5 reverses the bit order.

---

## 4. When and how SCX, SCY, WX, LY and LCDC bits 3/4/6 are sampled

The single most important property of the fetcher: **it does not latch these registers
into a per-line snapshot.** It reads them live at the exact state that needs them. A write
that lands between two reads therefore changes the second read. This is the mechanism
behind every "mid-scanline register write" test.

| Register / bit | Sampled in |
|---|---|
| `SCX` (integer part) | `GET_TILE_T1`, when computing `x` |
| `SCX & 7` (fine scroll) | `render_pixel_if_possible`, `position_in_line` adjustment |
| `SCY` | `fetcher_y()`, called from `GET_TILE_T1`, `LOW_T1`, `HIGH_T1` |
| `WX` | main loop (window activation) and the WX-glitch update |
| `LY` | internal line counter, exposed as `io_registers[LY]` |
| `LCDC` bit 3 (BG map) | `GET_TILE_T1` |
| `LCDC` bit 6 (WIN map) | `GET_TILE_T1` |
| `LCDC` bit 4 (TILE_SEL) | `LOW_T1` and `HIGH_T1`, stored in `last_tileset` |

`fetcher_y` is the function that turns `SCY` (or the window row) into the tile row:

```
function fetcher_y():
    if wx_triggered:
        return window_y
    return current_line + SCY
```

Two consequences worth stating explicitly:

- **`SCY` is re-read at all three T1 steps.** A write to `SCY` between the low-bitplane
  fetch and the high-bitplane fetch makes the two bitplanes come from different tile rows.
  This is the "tile bitplane mixing" behaviour the Mealybug tests exercise.
- **`LCDC` bit 4 is re-read at both data steps.** A write between them mixes bitplanes
  from the `$8000` and `$8800` tilesets. See [§8.3](#83-tile_sel-bitplane-mixing-glitch).

---

## 5. How mid-scanline writes are honored

A CPU write to a PPU register and the PPU's own progress are two independent timelines.
The write takes effect at the exact T-cycle it lands, so the fetcher must be advanced to
that cycle before the write is applied. The rule is: **before applying any PPU register
write, advance the PPU to the exact cycle of the write.**

The per-register model, in one table:

| Register | What the write does | Why |
|---|---|---|
| `SCX`, `SCY` | Store only | The fetcher re-reads them live at the next T1, so the effect is immediate and cycle-exact |
| `WX` | Store, then recompute the WX-glitch flag | The glitch flag depends on the current fetch position |
| `WY` | Store, then schedule a deferred `WY == LY` check | The comparison is deferred to a specific cycle, not done at write time |
| `LCDC` | Store, then schedule the same check; abort an in-flight object fetch if `OBJ_EN` clears | LCDC bits are read live by the fetcher; the object-fetch abort is a DMG-only side effect |
| `LY` | Written by the PPU, not the CPU | The internal line counter drives it |

The `WY` deferral is the subtle one. The `WY == LY` comparison is not performed at the
write instant; it is scheduled and runs at a cycle offset that depends on the model, so a
`WY` write mid-line is honored at the hardware's own comparison instant. When the
comparison matches and the window is enabled, `wy_triggered` is set.

---

## 6. The window

The window is a second background layer that starts at a fixed screen position and has
its own tile row counter. Three pieces of state drive it:

| State | Meaning |
|---|---|
| `window_y` | The window's internal line counter. Starts at `-1` and increments once per line on which the window is actually shown. Used as the window's tile row. |
| `window_tile_x` | The window's tile column, advanced as window tiles are fetched. |
| `wx_triggered` | True once the window has started on this line. Cleared at line start. |

`window_y` is initialised to `-1` at the top of the frame.

### 6.1 The activation trigger

The window activates when `WX == position_in_line + 7`, an **exact equality**, not a `>=`.
Because `position_in_line` advances by one per pixel and the check runs every T-cycle,
equality is the crossing point. The activation block:

```
if wx_triggered == false and wy_triggered == true and LCDC.win_enable == 1:
    should_activate = false
    if WX == 0:
        if position_in_line == -7:
            should_activate = true
        else if position_in_line == -16 and (SCX & 7) != 0:
            should_activate = true
        else if -15 <= position_in_line <= -8:
            should_activate = true
    else if WX < 166:
        if WX == position_in_line + 7:
            should_activate = true
        else if WX == position_in_line + 6 and not wx_just_changed:
            should_activate = true
            # DMG horizontal desync: lcd_x -= 1

    if should_activate:
        window_y += 1
        window_tile_x = 0
        clear(bg_fifo)
        wx_triggered = true
        state = GET_TILE_T1
        window_is_being_fetched = true
```

Read the activation body carefully. On a real activation the fetcher does **not** insert a
blank pixel. It:

1. increments `window_y` (advance the window's tile row),
2. resets `window_tile_x` to 0,
3. **clears the BG FIFO**,
4. resets the fetcher to `GET_TILE_T1`,
5. sets `wx_triggered = true` and `window_is_being_fetched = true`.

The cleared FIFO is refilled by the next fetch, which now reads window tiles. There is no
phantom pixel on a genuine activation.

### 6.2 The special cases

- **`WX == 0`**: the window activates during the fine-scroll region, with three sub-cases
  keyed on `position_in_line` (`-7`, `-16` with `SCX & 7`, or the range `-15..-8`). On DMG
  with `SCX & 7` set, one extra T-cycle is inserted.
- **`WX == 166`**: sets the `WX == 166` interrupt glitch and, on DMG, bumps `window_y`
  without a full activation. `WX == 167` never activates because the guard is `WX < 166`.
- **DMG horizontal desync**: on DMG, `WX == position_in_line + 6` also activates, and
  `lcd_x` is decremented by one to model the LCD/PPU desync. This is suppressed for one
  cycle after a `WX` write by `wx_just_changed`.

### 6.3 `window_y` at the end of the line

At the end of Mode 3, the `WX == 166` carry-over is handled and `window_y` is reset at the
last visible line:

```
if current_line == 143:
    window_y = -1
if wy_triggered and LCDC.win_enable == 1 and WX == 166:
    wx_triggered = true
    window_tile_x = 1
    window_y += 1
else:
    wx_triggered = false
```

---

## 7. The window reactivation zero pixel

This is the quirk that makes `m3_wx_4_change_sprites` fail by a handful of pixels. It is a
**separate mechanism** from activation, and it is easy to conflate with the
window-disabled glitch of [§8.2](#82-window-disabled-pixel-insertion-glitch).

The Mealybug test's own comment states the rule:

> Window reactivation zero pixels should be present when window is already activated and
> the pixel that the window reactivates on is on the same cycle as the window tile
> nametable read.

### 7.1 The condition

Immediately after the activation block, the main loop checks a five-part condition:

```
if WX == position_in_line + 7
   and wx_triggered == true
   and window_is_being_fetched == false
   and state == GET_TILE_T1
   and bg_fifo.size == 8:
    insert_bg_pixel = true
```

Every conjunct is load-bearing:

| Condition | Meaning |
|---|---|
| `WX == position_in_line + 7` | Exact equality at pixel `x = WX - 7` |
| `wx_triggered` | The window is **already** active |
| `!window_is_being_fetched` | The window fetch has settled (this is not the initial activation) |
| `state == GET_TILE_T1` | The fetcher is at the start of the Get Tile step, the cycle of the window tile nametable read |
| `bg_fifo.size == 8` | The BG FIFO is full |

The last two conjuncts are what make the quirk recur only on certain lines. The trigger
position is `p = WX - 7`. The fetcher reaches `GET_TILE_T1` with a full FIFO on a fixed
8-pixel cadence, so the trigger only fires when `p` lands on that phase. With `WX = LY`,
`p = LY - 7`, and the phase alignment recurs every 8 lines, so the phantom pixel appears on
lines where `LY ≡ 4 (mod 8)`.

That is only half of the rule. The phase alignment selects *which* lines are candidates;
a second constraint — the position at which the next `WX` write lands in the pixel stream —
caps the sequence. See [§7.4](#74-why-the-quirk-stops-mid-frame).

### 7.2 How the pixel is inserted

The flag is consumed in `render_pixel_if_possible`, at the point where a BG pixel would
normally be popped:

```
if bg_fifo.size == 0:
    return

if insert_bg_pixel:
    insert_bg_pixel = false
    fifo_item = { pixel = 0, palette = 0, priority = 0, bg_priority = false }
else:
    fifo_item = pop(bg_fifo)
```

The inserted item is a zeroed FIFO item: `pixel = 0`, `palette = 0`, `priority = 0`,
`bg_priority = false`. It is a raw color index 0, which on DMG is then mapped through
`BGP` entry 0. Crucially, the FIFO is **not** cleared and the fetcher is **not** reset. The
phantom pixel is simply prepended to the stream, shifting the rest of the line by one
pixel. That one-pixel shift is the visible artifact.

### 7.3 Why this is not the other glitches

Three different mechanisms can insert a zero pixel or shift the window. They must not be
confused:

| Mechanism | Trigger |
|---|---|
| **Window reactivation** (this section) | Window **enabled**, already active, `WX` re-hit at the nametable-read cycle |
| **Window-disabled insertion** | Window **disabled** (`!WIN_ENABLE`) but `WY` triggered |
| **CGB WX glitch** | CGB only, `WX` write while window active |

The window-disabled path lives inside the `PUSH` state and requires `!WIN_ENABLE`; it
rewinds the FIFO read pointer and writes a single zero item with `size = 1`. The CGB path
is CGB-only and never runs on DMG. The reactivation path is the only one that applies to a
DMG ROM that keeps the window enabled and rewrites `WX` mid-line.

### 7.4 Why the quirk stops mid-frame

The `m3_wx_4_change` / `m3_wx_5_change` ROMs write three different `WX` values on every
line: `WX = 4`, then `WX = LY`, and finally `WX = 80` (about 96 T after the first write).
`WX = LY` makes `p = LY - 7` grow by one each line, so the phase candidates are all lines
with `LY ≡ 4 (mod 8)` (`12, 20, 28, …`). But the `WX = 80` write lands at a **fixed
position in the pixel stream**, roughly `x = 92`, and once `WX` is 80 the comparison
`WX == position_in_line + 7` can no longer match. Only candidates *before* that position
fire, so the phantom stops after `LY = 92` (`p = 85`) and `LY = 100` (`p = 93`) is silent.

This is why the quirk cannot be reproduced with a local `(x, state, fifo)` test alone: at
the candidate pixel the fetcher state is byte-identical on a firing line (`LY = 92`) and a
silent one (`LY = 100`). What differs is the **absolute dot** of the pixel. The position of
the `WX = 80` write in the pixel stream is fixed by the Mode-3 start-of-line timing and by
how long the window activation stalls the output.

The window's left edge is at `x = WX - 7`. For `WX < 7` that position is **negative**: the
window activates while it is still off-screen, and clearing the FIFO there stalls the
first visible pixel until the window's first tile is fetched. The stall is part of the
line's timing, so it shifts every subsequent pixel — including the `WX = 80` write. The
implementation therefore evaluates the window activation *after* the warm-up (on the first
visible pixel), not while the fetcher is still warming:

- QtreGB's warm-up ends at `k_pixel0_dot = 95`; `PPU::pipeline_push_pixel` calls
  `Fetcher::check_mode` right there, and `Fetcher::check_mode` refuses to activate while
  `m_warming` is set.
- Activating during the warm-up would clear the FIFO *before* pixel 0 and let the warm-up
  fetch refill it, swallowing the stall and misplacing the `WX = 80` write by 8 pixels.

With the stall modelled, `m3_wx_4_change`, `m3_wx_4_change_sprites` and `m3_wx_5_change`
all reach `diff = 0`.

---

## 8. Hardware behaviour catalogue

Each entry is one obscure behaviour and the hardware research it comes from.

### 8.1 Window reactivation zero pixel

Covered in full in [§7](#7-the-window-reactivation-zero-pixel). Condition: window already
active, `WX == position_in_line + 7`, fetcher at `GET_TILE_T1`, BG FIFO full. Source: Pan
Docs `pixel_fifo` and the Mealybug test comment.

### 8.2 Window-disabled pixel insertion glitch

When the window is **disabled** but `WY` has triggered, and `WX` is hit at the right cycle,
the `PUSH` state inserts a single zero pixel instead of pushing a tile row:

```
if wy_triggered and LCDC.win_enable == 0 and not disable_window_pixel_insertion_glitch:
    logical_position = position_in_line + 7
    if logical_position > 167:
        logical_position = 0
    if WX == logical_position:
        rewind fifo read pointer by one
        fifo[read_pointer] = { pixel = 0, ... }
        fifo.size = 1
        return
```

The `disable_window_pixel_insertion_glitch` flag is cleared at the start of Mode 3, so the
glitch is live for the whole line unless something suppresses it. Source: Pan Docs
`pixel_fifo` and the Mealybug `WIN_EN` notes.

### 8.3 TILE_SEL bitplane mixing glitch

Because LCDC bit 4 is re-read at both data steps, a write between them mixes bitplanes
from the two tilesets. The hardware resolves the mixed read as follows:

```
function data_for_tile_sel_glitch():
    if last_tileset:                 # bit 4 set at the low step
        if current_tile & 0x80:      # tile index in the $8800 range
            return current_tile      # the low bitplane is reused as the high one
        return data_for_sel_glitch
    return data_for_sel_glitch
```

Source: Matt Currie, "The Comprehensive Game Boy PPU Documentation", `TILE_SEL (bit 4)`.

### 8.4 SCY is re-read at every T1

`fetcher_y` is called at `GET_TILE_T1`, `LOW_T1` and `HIGH_T1`. A `SCY` write between the
low and high bitplane fetches makes the two bitplanes come from different tile rows. This
is the "tile bitplane mixing" behaviour the Mealybug tests exercise. Source: Mealybug
`SCY` notes.

### 8.5 WX == 0 activation

`WX == 0` does not use the normal `WX == position_in_line + 7` rule. It activates during
the fine-scroll region, with three sub-cases. On DMG with `SCX & 7` set, one extra T-cycle
is inserted. Source: Pan Docs `pixel_fifo`.

### 8.6 WX == 166 special case

`WX == 166` sets the `WX == 166` interrupt glitch on activation and, on DMG, bumps
`window_y` without a full activation. `WX == 167` never activates because the guard is
`WX < 166`. Source: Pan Docs `pixel_fifo`.

### 8.7 DMG horizontal desync

On DMG, `WX == position_in_line + 6` also activates the window, and `lcd_x` is decremented
to model the LCD/PPU desync. It is suppressed for one cycle after a `WX` write by
`wx_just_changed`. Source: Mealybug `WX` notes.

### 8.8 Object fetch abort on OBJ_EN clear

Clearing LCDC bit 1 while an object fetch is in flight aborts it on DMG. The object-fetch
loop checks the abort flag after every advance and jumps to the abort path. Source: Pan
Docs `Rendering`.

### 8.9 The junk FIFO fill and SCX fine-scroll discard

At the start of Mode 3 the BG FIFO is filled with 8 junk pixels that are dropped, and the
first `SCX & 7` pixels are discarded by nudging `position_in_line`. See
[§11](#11-scanline-start). Source: Pan Docs `pixel_fifo` and GBEDG.

---

## 9. The object fetch state machine

Objects are fetched by a separate loop that runs inside the main Mode 3 loop, before the
per-cycle render. It pauses the BG fetcher until the BG FIFO has data, then overlays a
sprite row into the object FIFO.

The match position is `position_in_line + 8`:

```
function x_for_object_match():
    ret = position_in_line + 8
    if ret > -16:                    # unsigned comparison
        return 0
    return ret
```

The object loop:

```
while n_visible_objs != 0 and objects_x[top] < x_for_object_match():
    n_visible_objs -= 1

during_object_fetch = true
while n_visible_objs != 0
      and LCDC.obj_enable == 1
      and objects_x[top] == x_for_object_match():

    while state < GET_TILE_DATA_HIGH_T2 or bg_fifo.size == 0:
        advance_fetcher_state_machine()
        cycles_for_line += 1
        consume_one_T_cycle()
        if object_fetch_aborted:
            goto abort_fetching_object

    advance_fetcher_state_machine(); cycles_for_line += 1; consume_one_T_cycle()
    advance_fetcher_state_machine()
    mode2_y_bus = oam_read(obj * 4 + 2)
    object_flags = oam_read(obj * 4 + 3)
    cycles_for_line += 2; consume_two_T_cycles()

    object_low_line_address = get_object_line_address(objects_y[top], mode2_y_bus, object_flags)
    object_tile_data[0] = vram_read(object_low_line_address)
    cycles_for_line += 2; consume_two_T_cycles()

    during_object_fetch = false
    cycles_for_line += 1
    object_tile_data[1] = vram_read(object_low_line_address + 1)
    consume_one_T_cycle()

    overlay_object_row(oam_fifo, object_tile_data[0], object_tile_data[1], ...)
    n_visible_objs -= 1

abort_fetching_object:
object_fetch_aborted = false
during_object_fetch = false
```

The key structural point is the inner wait loop. The object fetch **pauses the BG fetcher**
by running it forward until it has reached `GET_TILE_DATA_HIGH_T2` (state 5) or the BG FIFO
is non-empty. Only then does it read the sprite's OAM entry, fetch the two sprite bitplanes,
and overlay them. Each advance costs one T-cycle, so the object fetch stretches Mode 3 by a
deterministic number of cycles per sprite. `get_object_line_address` handles 8x16 height,
Y-flip, and the CGB VRAM bank.

---

## 10. BG/OBJ FIFO merge and priority

The mixer is `render_pixel_if_possible`. It pops one BG item and one OBJ item per pixel
and decides which wins.

```
function render_pixel_if_possible():
    # Rendering does not occur as long as an object at x=0 is pending
    if n_visible_objs != 0 and LCDC.obj_enable == 1 and objects_x[top] == 0:
        return

    if bg_fifo.size == 0:
        return

    if insert_bg_pixel:
        insert_bg_pixel = false
        fifo_item = zero_item
    else:
        fifo_item = pop(bg_fifo)
    bg_priority = fifo_item.bg_priority

    if oam_fifo.size > 0:
        oam_item = pop(oam_fifo)
        if oam_item.pixel != 0 and LCDC.obj_enable == 1:
            draw_oam = true
            bg_priority = bg_priority or oam_item.bg_priority

    ...
    pixel = fifo_item.pixel if bg_enabled else 0
    if pixel != 0 and bg_priority:
        draw_oam = false
```

The rules, in order:

1. **An object at x=0 blocks rendering.** While a visible object sits at `x == 0`, the
   mixer returns early and emits nothing. This is the "object at x=0 pending" stall.
2. **BG priority is an OR of two sources.** `bg_priority` starts as the BG item's own
   `bg_priority` (the CGB attribute bit 7) and is OR-ed with the sprite's `bg_priority`
   (OAM bit 7). If the BG pixel is non-zero and `bg_priority` is set, the sprite is
   suppressed (`draw_oam = false`).
3. **DMG vs CGB on `BG_EN`.** If LCDC bit 0 is clear, DMG sets `bg_enabled = false` (BG
   renders as color 0) while CGB clears `bg_priority` instead.

Per-sprite priority is resolved when the object FIFO is loaded, not when it is popped.
`overlay_object_row` fills the FIFO to 8 with transparent items, then writes each sprite
pixel only into an empty slot or a slot with worse priority:

```
function overlay_object_row(lower, upper, palette, bg_priority, priority):
    while fifo.size < 8:
        fifo.push(zero_item)

    for i in 0 .. 7:
        pixel = ((lower >> (7 - i)) & 1) | (((upper >> (7 - i)) & 1) << 1)
        target = fifo[i]
        if pixel != 0 and (target.pixel == 0 or target.priority > priority):
            target = { pixel, palette, priority, bg_priority }
```

The `priority` passed in is the OAM index on CGB or 0 on DMG. On DMG every sprite has
priority 0, so the first sprite to write a slot wins (OAM order). On CGB the lower OAM
index wins.

---

## 11. Scanline start

Mode 3 begins by clearing both FIFOs, filling the BG FIFO with 8 junk pixels, and resetting
the fetcher:

```
on mode_3_start:
    disable_window_pixel_insertion_glitch = false
    clear(bg_fifo)
    clear(oam_fifo)
    push_bg_row(0, 0)                # 8 junk pixels, dropped anyway
    lcd_x = 0
    state = GET_TILE_T1
```

The junk fill exists so the mixer has something to pop while the first real tile is being
fetched. Those 8 pixels are discarded by the fine-scroll logic.

`position_in_line` starts at `-16` (see [§2.3](#23-position_in_line)). The fine-scroll
discard happens in `render_pixel_if_possible`, which nudges the counter forward to skip
`SCX & 7` pixels:

```
if (position_in_line + 16) < 8:      # unsigned comparison
    if position_in_line == -17:
        position_in_line = -16
    else if (position_in_line & 7) == (SCX & 7):
        position_in_line = -8
    else if window_is_being_fetched and (position_in_line & 7) == 6 and (SCX & 7) == 7:
        position_in_line = -8
    else if position_in_line == -9:
        position_in_line = -16
        return
    else:
        line_has_fractional_scrolling = true
```

The first tile is pre-fetched with `x = SCX >> 3` because at the start
`position_in_line + 16 < 8` is true.

### 11.1 The render batching fast path

When there is no window and no object interference, the whole line can be rendered in one
pass instead of the cycle-stepped loop. There, `fractional_scroll = SCX & 7` and the first
tile's data is shifted by it:

```
tile_x = SCX / 8
fractional_scroll = SCX & 7
```

The decision to use the fast path is disabled (returns 0) whenever a window is active, an
object is visible, HDMA is on, or the CPU is stopped. For the Mealybug `m3_*` tests, which
change registers mid-line, use objects, or use the window, the fast path is never taken.
The cycle-stepped loop is the relevant one.

---

## 12. How this maps to QtreGB today

QtreGB's fetcher is a **4-state** machine, not 7. The states are declared in
`core/inc/fetcher.h`:

```cpp
// core/inc/fetcher.h:17-23
enum class FetchEntry:u8
{
	FS_TILE,
	FS_DATA0,
	FS_DATA1,
	FS_PUSH
};
```

It is driven from `PPU::pipeline_process`, which ticks the fetcher on every **even** dot
and pushes a pixel on every dot:

```cpp
// core/src/ppu.cpp:568-577
void PPU::pipeline_process()
{
	//LOG("line ticks", "======================== " + to_string(static_cast<int>(m_line_ticks)));
	if((m_line_ticks & 1) == 0)
	{
		m_fetcher.tick();
	}

	pipeline_push_pixel();
}
```

`Fetcher::tick` runs `check_obj`, `check_mode`, then `sm_bg`, and `sm_bg` advances exactly
one `FetchEntry` per call:

```cpp
// core/src/fetcher.cpp:638-672
	switch(m_state_bg)
	{
		case FetchEntry::FS_TILE:
		{
			fill_map_background();
			m_state_bg = FetchEntry::FS_DATA0;
		}
		break;

		case FetchEntry::FS_DATA0:
		{
			load_data_background(0);
			m_state_bg = FetchEntry::FS_DATA1;
		}
		break;

		case FetchEntry::FS_DATA1:
		{
			load_data_background(1);
			m_state_bg = FetchEntry::FS_PUSH;
		}
		break;

		case FetchEntry::FS_PUSH:
		{
			bool result;
			result = push_data_background();

			if(result)
			{
				m_state_bg = FetchEntry::FS_TILE;
			}
		}
		break;
	}
```

### 12.1 The ad-hoc fixes

Three shortcuts stand in for the cycle-accurate model:

1. **Discarding `SCX % 8` pixels.** `pipeline_push_pixel` pops and throws away the first
   `m_lcd->m_scroll_x % 8` pixels instead of modelling the fine-scroll pre-fetch:

   ```cpp
   // core/src/ppu.cpp:603-612
   	if (m_discarded_pixels < (m_lcd->m_scroll_x % 8))
   	{
   		m_bgw_fifo.pop();
   		if(!m_spr_fifo.empty())
   		{
   			m_spr_fifo.pop();
   		}
   		m_discarded_pixels++;
   		return;
   	}
   ```

2. **Clearing the FIFO on a BG/WIN switch.** `check_mode` uses `>=` and clears the FIFO
   on every transition, with no reactivation path:

   ```cpp
   // core/src/fetcher.cpp:540-560
   		if(m_bg_mode == FetchModeBG::FS_BG_BGD)
   		{
   			if(m_current_x + 7 >= m_lcd->m_win_x &&
   				m_lcd->get_lcdc_win_enable() &&
   				m_lcd->m_win_x <= 166 &&
   				m_lcd->m_win_y < PPU::YRES &&
   				is_window
   			)
   			{
   				m_bgw_fifo.clear();
   				m_bg_mode = FetchModeBG::FS_BG_WIN;
   				m_state_bg = FetchEntry::FS_TILE;
   				m_bg_need_recalculate = true;
   				m_bg_tile_to_push = 0;
   ```

3. **A per-line object array.** `fill_object_array` precomputes all sprite pixels for the
   line into `m_object_array[160]` at the start of Mode 3, and `pipeline_push_pixel`
   indexes it by screen X:

   ```cpp
   // core/src/ppu.cpp:645-653
   			FIFOElement spr_element = m_fetcher.get_object_array()[m_pushed_x];
   			if(spr_element.get_color_index() != 0)
   			{
   				if(!spr_element.get_priority() || bgw_color_index == 0)
   				{
   					u8 palette = spr_element.get_palette() ? m_lcd->m_obj_palette_1 : m_lcd->m_obj_palette_0;
   					pixel_data = (palette >> (spr_element.get_color_index() * 2)) & 0x03;
   				}
   			}
   ```

### 12.2 Why it misses the sub-pixel behaviour

The 4-state machine cannot express the timing the `m3_*` tests probe, for four concrete
reasons:

- **No T1/T2 split.** Each `FetchEntry` does the address computation and the VRAM read in
  the same tick. The hardware splits them so a register write can land between the address
  computation and the read. Without the split, a mid-fetch write to `SCX`, `SCY` or LCDC
  bit 4 cannot change the address that the read uses, so the bitplane-mixing and
  tile-select glitches are unrepresentable.
- **Registers are not re-read at the right cycle.** `fill_map_background` and
  `load_data_background` read `m_scroll_x`, `m_scroll_y` and the LCDC bits, but the
  fetcher advances on a fixed even-dot cadence that is not aligned to the hardware's
  per-state sampling. A write that should affect only the high bitplane affects both, or
  neither.
- **No window reactivation insertion.** `check_mode` only switches BG to WIN and clears the
  FIFO. It has no path that inserts a single zero pixel when the window is already active
  and `WX` is re-hit at the nametable-read cycle. The `>=` comparison also cannot express
  the exact-equality trigger, and it never distinguishes first activation from
  reactivation.
- **Objects are precomputed per line.** `fill_object_array` resolves sprites at the start
  of the line, so the object fetch cannot pause the BG fetcher at `x_for_object_match` and
  cannot stretch Mode 3 by the per-sprite cycle cost. The `m3_*` tests that mix sprites
  with mid-line register writes depend on that pause.

The net effect is that QtreGB renders the right pixels for a static frame but the wrong
pixels whenever a register changes mid-scanline or a sprite interacts with the window,
which is exactly the `m3_*` and `dmg-acid2` failure set.

---

## 13. Reimplementation recipe

If you were building a cycle-accurate fetcher from nothing, the shortest correct path is:

**Step 1. Model the clock.** Count the PPU in T-cycles. One fetcher state is one T-cycle.
Keep a `position_in_line` that starts at `-16` and increments by one per emitted pixel.
Keep a `window_y` that starts at `-1`.

**Step 2. Build the two FIFOs.** Each holds 8 items of `{pixel, palette, priority,
bg_priority}`. Provide `clear`, `pop`, `push_row` (expand two bitplanes into 8 items) and
`overlay_object_row` (fill to 8 with transparent items, then write non-zero sprite pixels
into empty or worse-priority slots).

**Step 3. Build the 7-state machine.** States: `GET_TILE_T1`, `GET_TILE_T2`,
`GET_TILE_DATA_LOW_T1`, `_T2`, `GET_TILE_DATA_HIGH_T1`, `_T2`, `PUSH`. Advance exactly
one state per T-cycle. `T1` computes the address and samples the selecting registers;
`T2` performs the VRAM read.

**Step 4. Sample registers live.** Do not latch `SCX`, `SCY`, `WX`, `LY` or the LCDC bits
into a per-line snapshot. Read them from the register file at the exact state that needs
them. Re-read `SCY` and LCDC bit 4 at both data steps so bitplane mixing works.

**Step 5. Make `PUSH` a waiting state.** It must repeat every T-cycle until the BG FIFO is
empty, then push 8 pixels and return to `GET_TILE_T1`. Do not give it a fixed 2-cycle
duration.

**Step 6. Sync the PPU before every register write.** Before applying a write to any PPU
register, run the PPU forward to the exact cycle of the write. Then apply the write with
its side effects: `WX` recomputes the WX glitch, `WY` schedules a deferred `WY == LY`
check, `LCDC` schedules the same check and aborts an in-flight object fetch if `OBJ_EN`
clears.

**Step 7. Implement the window trigger as exact equality.** Activate when
`WX == position_in_line + 7` and the window is not already active. On activation:
`window_y++`, `window_tile_x = 0`, clear the BG FIFO, reset the fetcher to `GET_TILE_T1`,
set `wx_triggered`. Handle `WX == 0`, `WX == 166` and the DMG `position_in_line + 6`
desync as separate cases.

**Step 8. Implement the reactivation zero pixel.** When the window is already active, the
fetch has settled, `WX == position_in_line + 7`, the fetcher is at `GET_TILE_T1`, and the
BG FIFO is full, set a flag that makes the next pop return a zeroed item instead of a real
one. Do not clear the FIFO and do not reset the fetcher.

**Step 9. Implement the object fetch as a pause.** When a visible object's X equals
`position_in_line + 8`, run the BG fetcher forward until it reaches
`GET_TILE_DATA_HIGH_T2` or the BG FIFO is non-empty, then read the sprite's OAM entry,
fetch its two bitplanes, and overlay them into the object FIFO. Charge one T-cycle per
advance.

**Step 10. Merge and resolve priority.** Pop one BG and one OBJ item per pixel. Suppress
the sprite if the BG pixel is non-zero and `bg_priority` (BG attribute bit 7 OR sprite bit
7) is set. On DMG, `BG_EN == 0` blanks the BG layer; on CGB it clears `bg_priority`.

**Step 11. Handle the scanline start.** Clear both FIFOs, fill the BG FIFO with 8 junk
pixels, reset the fetcher, and let the fine-scroll logic discard the first `SCX & 7`
pixels by nudging `position_in_line`.

The failure modes worth knowing about, because all are silent:

- Giving `PUSH` a fixed duration instead of making it wait. The fetch cadence then drifts
  by a cycle or two per tile, and every mid-line test is off by a growing amount.
- Latching `SCX`/`SCY` per line. The bitplane-mixing and tile-select tests then pass on
  static frames and fail on every mid-line write.
- Using `>=` for the window trigger. It cannot distinguish first activation from
  reactivation, so the zero pixel is either emitted on every line or on none.
- Omitting the `state == GET_TILE_T1 && bg_fifo.size == 8` gate on the reactivation.
  Without it the phantom pixel appears on the wrong lines, which is the `LY ≡ 4 (mod 8)`
  pattern.

---

## 14. References

- [Pan Docs, *Rendering*](https://gbdev.io/pandocs/Rendering.html) and
  [*Pixel FIFO*](https://gbdev.io/pandocs/pixel_fifo.html) - the fetcher stages, the FIFO
  description, the window activation rule, and the window reactivation rule ("a pixel with
  color value of 0 and the lowest priority is pushed onto the background FIFO").
- [GBEDG, *PPU*](https://hacktix.github.io/GBEDG/ppu/) - the four fetch steps, the
  `X >= WX - 7` window rule, the FIFO description, and the `PUSH` step's variable length.
- [Matt Currie, *The Comprehensive Game Boy PPU Documentation*](https://github.com/mattcurrie/mealybug-tearoom-tests/blob/master/the-comprehensive-game-boy-ppu-documentation.md)
  - the `TILE_SEL`, `WIN_EN` and `SCY` notes; the source of the bitplane-mixing behaviour.
- [mooneye-gb test suite](https://github.com/Gekkio/mooneye-test-suite) - the PPU timing
  tests that pin down the fetcher cadence and the mid-scanline write behaviour.
- [Mealybug Tearoom Tests](https://github.com/mattcurrie/mealybug-tearoom-tests) - the
  `m3_*` acceptance ROMs and their expected frames, the tests this document is written
  against.
