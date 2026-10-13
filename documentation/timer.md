# The DMG Timer in QtreGB

A complete, from-scratch description of the `qtreGB::Timer` class: what it models, why it
looks the way it does, and every obscure hardware behaviour it reproduces. Someone reading
this document should be able to reimplement the class from scratch and get byte-identical
behaviour.

- **Source:** [`src/timer.cpp`](../src/timer.cpp), [`inc/timer.h`](../inc/timer.h)
- **Tests:** [`tests/test_timer.cpp`](../tests/test_timer.cpp)
- **Primary reference:** [Pan Docs — *Timer and Divider Registers*](https://gbdev.io/pandocs/Timer_and_Divider_Registers.html)
  and [*Timer Obscure Behaviour*](https://gbdev.io/pandocs/Timer_Obscure_Behaviour.html)

---

## Table of contents

1. [What the hardware actually is](#1-what-the-hardware-actually-is)
2. [The core mechanism: a falling edge](#2-the-core-mechanism-a-falling-edge)
3. [Driving the timer: one call per T-cycle](#3-driving-the-timer-one-call-per-t-cycle)
4. [Class anatomy](#4-class-anatomy)
5. [`timer_tick()` step by step](#5-timer_tick-step-by-step)
6. [Overflow and the reload](#6-overflow-and-the-reload)
7. [The A/B cycle model](#7-the-ab-cycle-model)
8. [Obscure behaviour catalogue](#8-obscure-behaviour-catalogue)
9. [The `m_tima_write_block` mechanism](#9-the-m_tima_write_block-mechanism)
10. [Savestates](#10-savestates)
11. [Reimplementation recipe](#11-reimplementation-recipe)
12. [Verification](#12-verification)
13. [Known limitations](#13-known-limitations)
14. [References](#14-references)

---

## 1. What the hardware actually is

The DMG timer is **not** a free-running counter. It is a small collection of registers hanging
off a shared divider, and only *one* of them is a real counter.

| Register | Address | Width | Role |
|---|---|---|---|
| `DIV`  | `0xFF04` | 16-bit internal, **only the high byte is readable** | The divider. This is the only thing that counts continuously. |
| `TIMA` | `0xFF05` | 8-bit | The timer counter. Only ever *incremented*, never free-running. |
| `TMA`  | `0xFF06` | 8-bit | The reload value for `TIMA`. |
| `TAC`  | `0xFF07` | 8-bit | Frequency select + enable. |

The critical structural idea:

> `TIMA` does not count on its own. It is incremented by an *external event* — the falling
> edge of one particular bit of `DIV`.

Everything else in this document is a consequence of that one sentence.

### 1.1 The divider

`DIV` increments once per T-cycle and is 16 bits wide, so it wraps every 65536 T-cycles. It
cannot be written meaningfully: **any write to `0xFF04` resets it to zero**, regardless of the
value written. Because only the high byte is exposed, a write cannot even be emulated as a
normal store.

There is no register telling software what `DIV` currently is beyond its high byte, which is
why `read_div()` returns `m_div >> 8`.

### 1.2 Bit selection

`TAC` bits 0–1 select **which bit of `DIV`** produces the timer's tick:

| `TAC & 0x03` | Selected bit | Frequency | Period (T-cycles) |
|---|---|---|---|
| `0b00` | bit 9  | 4096 Hz   | 1024 |
| `0b01` | bit 3  | 262144 Hz | 16 |
| `0b10` | bit 5  | 65536 Hz  | 64 |
| `0b11` | bit 7  | 16384 Hz  | 256 |

`TAC` bit 2 is the enable. If it is clear, the timer is stopped and `TIMA` never moves —
but see [§8.10](#810-disabling-with-the-selected-bit-high-fires-a-tick-dmg), because *the act
of disabling* can still generate a tick.

> **Note on `selected_bit()` returning `u16`.** Bit 9 does not fit in a `u8`. An earlier version
> of this class returned `u8` and `1 << 9` (= 512) truncated to **0**, which silently made
> mode 0 never tick. The return type is `u16` for this reason, and the header carries a comment
> so it is not "simplified" back.

---

## 2. The core mechanism: a falling edge

Define:

```cpp
bool Timer::signal()
{
    return (m_tac & 0x04) && (m_div & selected_bit());
}
```

This is the state of one wire: "the timer is enabled **and** the currently selected divider
bit is set."

A tick is generated **only on the transition from set to unset** — a *falling edge* of that
wire:

```cpp
old_signal = signal();
/* ... advance the divider ... */
new_signal = signal();

if(old_signal && !new_signal)
{
    apply_timer_tick();
}
```

Both the rising edge and the steady-state-high period do nothing. This is why `TIMA` is
incremented at a *lower* rate than the nominal frequency, and why the first increment does not
happen immediately after enabling the timer.

Because the selected bits (3, 5, 7, 9) all have even periods, their falling edges always land
on a T-cycle that is `0 mod 4` — i.e. the **first T-cycle of an M-cycle**. This is a useful
anchor for reasoning about alignment; more on it in [§7](#7-the-ab-cycle-model).

---

## 3. Driving the timer: one call per T-cycle

The emulator core drives the timer around the CPU's turn in `QtreGB::tick()`:

```cpp
m_timer.begin_cpu_phase();   // mark: MMIO this cycle observes end-of-cycle state
m_cpu.tick();
m_timer.timer_tick();        // then the cycle's increment / reload / edge lands
```

The timer never reads the CPU clock itself; it is purely a passive consumer of these calls.
`begin_cpu_phase()` sets `m_cpu_phase`, which is what lets accesses made during the CPU's turn
be judged against the *end* of the current T-cycle — see [§5.1](#51-sampling-convention-reads-and-window-decisions-see-the-end-of-the-cycle).
It is cleared again at the end of every `timer_tick()`.

This granularity is the whole reason the obscure behaviours are expressible at all: cycle A
and cycle B are each exactly one M-cycle, which is exactly **4 T-cycles**, which is exactly the
number of `timer_tick()` calls that elapse before the reload lands.

> **Consistency note.** The M-cycle-based Mooneye test ROMs and this T-cycle-based
> implementation agree on every timing here, because the divider's selected bits only fall on
> `T ≡ 0 (mod 4)` and CPU MMIO writes are also M-cycle aligned. "Reload on the next M-cycle"
> and "reload 4 T-cycles later" denote the same instant on real hardware.

---

## 4. Class anatomy

```cpp
u16 m_div;                     // the divider; only the high byte is readable
u8  m_tima;                    // the counter, driven only by falling edges
u8  m_tma;                     // reload value
u8  m_tac;                     // frequency select + enable
u8  m_reload_pending;          // cycle A: T-cycles until the TMA reload
u8  m_load_active;             // cycle B: TIMA stays latched to TMA
bool m_tima_write_block;       // suppress the next edge in this M-cycle
bool m_div_increment_absorbed; // this cycle's DIV increment was consumed by a DIV write
bool m_cpu_phase;              // CPU runs before this cycle's timer_tick
```

`m_reload_pending` and `m_load_active` are the entire A/B model. They are two *separate*
counters because a single one cannot express the hardware: the reload window is **two M-cycles**,
and software's ability to *influence* the outcome is gone in the second one, even though the
`Load` wire is asserted for the whole of that second M-cycle. See
[§7](#7-the-ab-cycle-model).

The two `bool`s are sampling machinery, not A/B state: `m_cpu_phase` gates the end-of-cycle
convention of [§5.1](#51-sampling-convention-reads-and-window-decisions-see-the-end-of-the-cycle),
and `m_div_increment_absorbed` lets a `DIV` write consume its own T-cycle's increment
([§8.7](#87-a-div-write-can-generate-a-tick)).

### 4.1 Power-on state

```cpp
void Timer::timer_init()
{
    m_div = 0xAC00;
    m_tima = 0x00;
    m_tma = 0x00;

    m_tac = 0x00; // TAC is stored as 0x00 here. Software still reads 0xF8 back, because the unmapped upper bits are applied on read, not stored.
    m_reload_pending = 0;
    m_load_active = 0;
    m_tima_write_block = false;
    m_div_increment_absorbed = false;
    m_cpu_phase = false;
}
```

**Every member is initialised here, and that is load-bearing.** `Timer` has no constructor
and no in-class member initialisers, so a bare `Timer t;` is *default-initialised*: its scalars
keep whatever was already on the stack. `timer_init()` is what establishes a defined state, so
it must not be skipped and must not be narrowed to "only the divider".

`DIV` is **not** zero at power-on. Pan Docs specifies `DIV = 0xABCC` on a DMG; the internal
counter is modelled here as `0xAC00`, which puts the next bit-9 falling edge 512 T-cycles
away rather than 564. This is a deliberate simplification of the boot value and is not
observable except through timing right after a reset. See [§13](#13-known-limitations).

> **`TAC` reads back `0xF8`, not the byte you wrote.** This is the single most counter-intuitive
> part of the register. Real hardware leaves`TAC` bits 3–7 unwired to the write bus, so they
> never latch; they read high because the data bus idles at VDD.
> See [§8.12](#812-tac-read-back-is-not-a-round-trip). The internal `m_tac` is therefore `0x00`
> after `timer_init()` while `read_tac()` returns `0xF8` — the upper bits are applied on read,
> never stored.

---

## 5. `timer_tick()` step by step

```cpp
void Timer::timer_tick()
{
    bool old_signal = signal();
    if(m_div_increment_absorbed)
    {
        m_div_increment_absorbed = false;
    }
    else
    {
        m_div++;
    }
    bool new_signal = signal();

    if(m_reload_pending > 0)
    {
        m_reload_pending --;

        if(m_reload_pending == 0)
        {
            m_tima = m_tma;
            m_load_active = 4;

            if(m_interrupt_handler)
            {
                m_interrupt_handler(InterruptType::IT_TIMER);
            }
        }
    }
    else if(m_load_active > 0)
    {
        m_load_active --;
    }
    else if(old_signal && !new_signal)
    {
        if(!m_tima_write_block)
        {
            apply_timer_tick();
        }
    }

    m_tima_write_block = false;
    m_cpu_phase = false;
}
```

The ordering is the whole design, so it is worth stating explicitly as a priority chain:

0. **The increment may already be consumed.** If a `DIV` write happened during this cycle's
   CPU phase, `m_div_increment_absorbed` skips the `m_div++` — the write absorbed this
   T-cycle's increment ([§8.7](#87-a-div-write-can-generate-a-tick)).
1. **Cycle A wins over everything.** While `m_reload_pending > 0`, the divider edge is not even
   examined. `TIMA` cannot increment during cycle A, no matter what the divider does.
2. **Cycle B is next.** While `m_load_active > 0`, edges are likewise ignored, and the counter
   just drains.
3. **Only when neither is active** does a falling edge get a chance to increment `TIMA` — and
   then only if `m_tima_write_block` is not set.
4. **Finally, `m_tima_write_block` and `m_cpu_phase` are unconditionally cleared**, at the end
   of *every* tick. Both are one-shot flags with a lifetime of at most one T-cycle.

Two subtleties worth calling out because they are easy to get wrong:

- The interrupt is raised at the **end of cycle A**, not at the overflow. By the time software
  can observe it, `TIMA` has already been reloaded.
- `m_tima_write_block` is cleared *after* the edge is evaluated, so the block covers the edge
  in the same tick it was set, and expires at the end of that tick. See
  [§9](#9-the-m_tima_write_block-mechanism).

### 5.1 Sampling convention: reads and window decisions see the end of the cycle

A CPU MMIO access and the cycle's `timer_tick()` happen in the same T-cycle, CPU first.
Hardware registers sample at the *end* of the T-cycle, so an access made mid-cycle must not
observe the stale pre-increment state the emulator happens to hold in memory. The class
therefore separates **stored** state from **observed** state:

- `m_cpu_phase` is set by `begin_cpu_phase()` before the CPU runs and cleared at the end of
  `timer_tick()`. While it is set, `read_div()` returns `div_end_of_cycle() >> 8` and
  `read_tima()` returns `peek_tima()` — both dry-run the coming tick without mutating.
- `write_div()` and `write_tac()` evaluate their falling edges on `div_end_of_cycle()`
  ([§8.7](#87-a-div-write-can-generate-a-tick), [§8.8](#88-a-tac-frequency-change-can-generate-a-tick)).
- `write_tima()` / `write_tma()` judge the reload/load windows as if the coming tick already
  landed ([§8.1](#81-tima-write-in-cycle-a-cancels-the-reload-and-the-interrupt)).

The helpers behind this:

```cpp
u16 Timer::div_end_of_cycle()  // m_div as it will be at the end of this T-cycle
{
    if(m_div_increment_absorbed) return m_div;  // this cycle's increment was consumed
    return static_cast<u16>(m_div + 1);
}

bool Timer::edge_pending();    // will the coming tick see a falling edge?
u8  Timer::peek_tima();        // what read_tima() will effectively observe
```

`peek_tima()` folds in everything at once: `m_reload_pending == 1` means the reload lands
this tick, so it already returns `m_tma`; a pending divider edge returns the post-increment
value (including the `0xFF → 0x00` overflow); the load window and the write-block flag are
honoured exactly as `timer_tick()` would honour them.

The boundary T-cycles are calibrated against the Mooneye `tima_write_reloading` /
`tma_write_reloading` ROMs: a write arriving on the T-cycle whose tick performs the reload
(`m_reload_pending == 1`) is judged as cycle B and **dropped**, and a write arriving on cycle
B's last T-cycle (`m_load_active == 1`) is judged as post-cycle B and **accepted** — the two
ROMs' `C` and `L` assertion blocks are exactly these two instants.

---

## 6. Overflow and the reload

```cpp
void qtreGB::Timer::apply_timer_tick()
{
    if(m_tima == 0xFF)
    {
        m_tima = 0x00;
        m_reload_pending = 4;
    }
    else
    {
        m_tima++;
    }
}
```

Note what is **absent**: the overflow does not request the interrupt. It only:

1. Sets `TIMA` to `0x00` — and it *stays* `0x00` for the four T-cycles of cycle A.
2. Arms the reload by setting `m_reload_pending = 4`.

The interrupt is requested four ticks later, when the reload actually lands
([§5](#5-timer_tick-step-by-step)). Software that polls `TIMA` during cycle A will observe
`0x00`; software that polls `IF` will not yet see the timer flag. Both are correct, and both
are load-bearing for the Mooneye tests.

---

## 7. The A/B cycle model

This is the subtle part, and the reason this class has two counters.

### 7.1 What the hardware does

An overflow does not have one special M-cycle — it has **two consecutive M-cycles**, 8 T-cycles
in total. Pan Docs names them *cycle A* and *cycle B*, and both are full M-cycles:

- **Cycle A — the M-cycle in which the overflow happens.** The selected bits all have even
  periods, so a falling edge always lands on the first T-cycle of an M-cycle
  ([§2](#2-the-core-mechanism-a-falling-edge)). The overflow therefore happens on cycle A's
  *first* T-cycle, and `TIMA` reads `$00` for all four T-cycles of that M-cycle. The `Load`
  line is **not** asserted yet: nothing has been copied, and `IF` has not been touched. This
  is the window in which software can still *cancel* the reload.
- **Cycle B — the M-cycle one M-cycle later.** The copy from `TMA` into `TIMA` and the `IF`
  timer flag both land on cycle B's *first* T-cycle. The `Load` line is asserted for the
  **entirety** of cycle B.

The three sentences this rests on, quoted from Pan Docs:

> When TIMA overflows, the value from TMA is copied, and the timer flag is set in IF, but one
> M-cycle later.
>
> This means that TIMA is equal to `$00` for the M-cycle after it overflows.
>
> The "Load" signal stays enabled for the entirety of cycle B.

Pan Docs illustrates it with SYS as the lower 8 bits of the system counter and `TAC = $FD`,
under the caption *"TIMA overflows on cycle A, but the interrupt is only requested on cycle B"*:

```
M-cycle   A                     B
SYS       2B 2C 2D 2E 2F 30 31
TIMA      FE FF FF 00 23 24 24
TMA       23 23 23 23 23 23 23
IF        E0 E0 E0 E0 E4 E4 E4
```

The shape to read off it: `TIMA` reaches `$00`, and one M-cycle later the column shows `$23` —
the `TMA` value — with `IF` bit 2 set at the same time. That is the `$00`-then-reload-then-flag
sequence of cycles A and B. The table is a schematic, and Pan Docs does not mark exactly which
of the system-counter columns belong to A and which to B, so do not read cycle boundaries off
it; the three sentences quoted above are the authoritative statement, and the prose above is
what the implementation follows.

The practical consequence: **cycle A is a window of opportunity, cycle B is already decided.**

> **A note on units, because this is easy to misread.** "Two cycles" here means *two M-cycles*,
> i.e. 4 + 4 = 8 T-cycles — not two halves of a single 4-T-cycle M-cycle. Each of A and B is
> exactly one M-cycle wide, which is why each is modelled by a counter loaded with `4`
> ([§7.2](#72-why-one-counter-is-not-enough)). The reload therefore happens 4 T-cycles after
> the overflow, not 2, and not 1.

### 7.2 Why one counter is not enough

A tempting simplification is a single `reload_in` countdown:

```cpp
// WRONG — this cannot express cycle B correctly
if(m_reload_in > 0)
{
    m_reload_in--;
    if(m_reload_in == 0) { m_tima = m_tma; /* interrupt */ }
}
```

This handles cycle A, but it has no separate state for the M-cycle in which the reload
happened. That M-cycle needs its own behaviour:

- A `TIMA` write must be **ignored**.
- A `TMA` write must be **mirrored into `TIMA` immediately**.
- The divider edge must still be suppressed.

None of that is expressible with one countdown, because the moment of the reload and the
*duration of the write-blocking window* are different intervals. Hence:

| Counter | Set to | Decremented | Meaning |
|---|---|---|---|
| `m_reload_pending` | `4` on overflow | until 0 | cycle A — the cancel window |
| `m_load_active` | `4` on reload | until 0 | cycle B — the latched window |

They are set at different moments (overflow vs. reload) and drain independently, giving
two consecutive M-cycles of distinct behaviour.

### 7.3 The state machine

```
                    overflow
                       |
     +-----------------v------------------+
     |  CYCLE A  (m_reload_pending = 4)   |   TIMA reads 0x00
     |  write TIMA -> CANCELS reload+IRQ  |   no divider edge
     |  write TMA  -> stored only         |
     +-----------------+------------------+
                       | 4 T-cycles elapse
                       | TIMA = TMA, IT_TIMER requested
     +-----------------v------------------+
     |  CYCLE B  (m_load_active = 4)       |   TIMA latched to TMA
     |  write TIMA -> IGNORED              |   no divider edge
     |  write TMA  -> mirrored into TIMA   |
     +-----------------+------------------+
                       | 4 T-cycles elapse
                       v
                  NORMAL OPERATION
```

> **Boundary T-cycles: writes are judged at end of cycle.** The diagram shows each window as
> one rule, but because a write is judged against the state at the *end* of its T-cycle
> ([§5.1](#51-sampling-convention-reads-and-window-decisions-see-the-end-of-the-cycle)), the
> **last** T-cycle of each window already belongs to the next one: a `TIMA` write arriving
> when `m_reload_pending == 1` lands together with the reload and is **dropped** (it behaves
> as cycle B), and one arriving when `m_load_active == 1` is **accepted** (it behaves as
> normal operation). The `TMA` mirror shifts the same way: it stops at `m_load_active > 1`,
> so no mirror happens on the closing T-cycle — but a `TMA` written on the reload T-cycle
> (`m_reload_pending == 1`) still reaches `TIMA` because the imminent reload copies it.
> These are precisely the `C` and `L` assertion blocks of the two Mooneye ROMs.

### 7.4 Behaviour table

| Register operation | Normal | Cycle A | Cycle B |
|---|---|---|---|
| Read `TIMA` | current value | `0x00` | `TMA` (latched) |
| Write `TIMA` | applied | **cancels reload + interrupt**, value kept | **ignored** |
| Write `TMA`  | stored | stored, applied at reload | stored **and mirrored into `TIMA`** |
| Divider edge | increments `TIMA` | suppressed | suppressed |
| `IT_TIMER` on reload | yes | **no, if cancelled** | yes (already raised) |

The cycle-A and cycle-B columns describe each window as a whole; on each window's *last*
T-cycle the end-of-cycle rule shifts the write into the next column — see the boundary note
above, and [§7.5](#75-write-time-state-machine) for the write-time view.

### 7.5 Write-time state machine

The rule the implementation actually codes: dry-run the coming tick, then apply the table
above to the resulting phase. At write time (CPU phase, before this cycle's `timer_tick()`):

| `m_reload_pending` | `m_load_active` | Phase at cycle end | `write_tima` | `write_tma` mirror |
|---|---|---|---|---|
| `> 1`  | 0  | still cycle A | **cancel + accept** | no (reload copies it) |
| `== 1` | 0  | reload lands now → cycle B opens | **dropped** | no (reload copies the new `TMA`) |
| 0      | `> 1` | still cycle B | **dropped** | **yes** |
| 0      | `== 1` | cycle B closes now → normal | **accepted** | no |
| 0      | 0  | normal | **accepted** | no |

Note `m_reload_pending > 0` and `m_load_active > 0` are mutually exclusive: the load counter
is only ever armed on the tick that drains the pending counter to zero, and the edge branch
that would re-arm `m_reload_pending` is unreachable while either counter is nonzero.

---

## 8. Obscure behaviour catalogue

Each entry cites the Pan Docs behaviour it implements and the code that implements it.

### 8.1 TIMA write in cycle A cancels the reload *and* the interrupt

```cpp
void Timer::write_tima(u8 value)
{
    // End-of-cycle judgment (same convention as reads): pending>1 is cycle A,
    // the write cancels the reload. pending==1 or load_active>1 means the
    // reload/load still lands this T-cycle, so hardware drops the write (MTS C).
    if(m_reload_pending > 1)
    {
        m_reload_pending = 0;
        m_tima = value;
        m_tima_write_block = true;
    }
    else if(m_reload_pending == 0 && m_load_active <= 1)
    {
        m_tima = value;
        m_tima_write_block = true;
    }
}
```

The function has three possible outcomes, decided by the end-of-cycle phase
([§7.5](#75-write-time-state-machine)):

1. **Cancel + accept** (`m_reload_pending > 1`): cycle A proper. The reload is still more than
   one T-cycle away, so the write cancels it — value kept, `m_reload_pending` zeroed, cycle B
   never begins, no `TMA` copy, no `IF` timer bit. Writing `TIMA` here acts as if the overflow
   never happened. This is why the interrupt cannot be raised at overflow time
   ([§6](#6-overflow-and-the-reload)): if it were, a cycle-A write would have nothing left to
   cancel.
2. **Drop** (falls through both arms): `m_reload_pending == 1` — the reload lands with this
   very tick, so the write is judged as cycle B — or `m_load_active > 1`, cycle B proper.
   Nothing is touched; see [§8.2](#82-tima-write-in-cycle-b-is-ignored).
3. **Accept** (`m_reload_pending == 0 && m_load_active <= 1`): normal operation, plus cycle B's
   *last* T-cycle, where the window closes with the write and the end-of-cycle judgment hands
   the write back to normal operation.

The cycle-A arm comes first because it is the only one that has to touch a counter as well;
the two "accept" arms are otherwise identical, so their shared body is duplicated rather than
factored behind a flag.

> Implemented and verified by `tima_write_reloading` in the Mooneye suite — block `C` pins the
> drop on the reload T-cycle, block `L` pins the accept on cycle B's closing T-cycle.

### 8.2 TIMA write in cycle B is ignored

```cpp
    else if(m_reload_pending == 0 && m_load_active <= 1)
    {
        m_tima = value;
        m_tima_write_block = true;
    }
```

The write is silently dropped — it is not deferred, and it does not corrupt state — by
**falling through both arms**: the accepting arm's guard excludes every state in which the
reload or load still lands past this cycle, so nothing runs. Two windows are dropped:

- `m_load_active > 1` — cycle B proper: the load still stands at cycle end.
- `m_reload_pending == 1` — the reload lands with this tick; end-of-cycle judgment treats the
  write as cycle B's opening (MTS block `C`).

And one window that a naive "ignore all of cycle B" would wrongly drop:

- `m_load_active == 1` — cycle B's *closing* T-cycle. The window ends with this very tick, so
  the write is accepted (MTS block `L`).

The guard is written as a positive condition rather than an early `return`, which keeps the
function a single chain with one entry and one exit path.

One consequence worth knowing: `m_tima_write_block` is *not* raised by a dropped write —
nothing wrote `TIMA`, so there is no reload latch to protect. When the write *is* accepted on
cycle B's last T-cycle the flag is set but inert: that tick takes the `m_load_active > 0`
branch before the edge branch is reached, and the flag is cleared at the end of the tick
regardless ([§9](#9-the-m_tima_write_block-mechanism)).

> Implemented and verified by `tima_write_reloading` in the Mooneye suite.

### 8.3 TMA write in cycle B is mirrored into `TIMA`

```cpp
void Timer::write_tma(u8 value)
{
    m_tma = value;

    // Copy into TIMA only while the load is still active past this cycle;
    // at load_active==1 the load window closes with the write (MTS tma C).
    if(m_load_active > 1)
    {
        m_tima = value;
    }
}
```

Because the `Load` line is still asserted, a new `TMA` value is pushed into `TIMA` immediately
rather than waiting for the next overflow. The mirror is guarded by `m_load_active > 1` — i.e.
by *end-of-cycle* assertion of the load: on cycle B's last T-cycle (`m_load_active == 1`) the
window closes with the write and no mirror happens (MTS `tma_write_reloading` block `C`).
When the write lands on the reload T-cycle itself (`m_reload_pending == 1`, `m_load_active`
still 0) there is no mirror either, but the imminent reload copies the just-written `m_tma`
anyway, so the new value reaches `TIMA` through the reload. During the rest of cycle A the
same write only updates `m_tma`, and the value reaches `TIMA` at the (still-armed) reload.

> Implemented and verified by `tma_write_reloading` in the Mooneye suite.

### 8.4 The reload is the moment the interrupt is requested

Covered in [§6](#6-overflow-and-the-reload). Worth restating: `apply_timer_tick()` on overflow
does not touch `m_interrupt_handler`.

### 8.5 A `TIMA` write blocks the divider edge for the rest of the M-cycle

```cpp
m_tima = value;
m_tima_write_block = true;
```

Writing `TIMA` also writes to the internal "reload" register. If a divider edge lands in the
same M-cycle, the write wins and the increment is lost — even when `TIMA` was not near
overflow.

> Verified by the unit test *writing TIMA in the overflow cycle cancels the increment*, which
> picks the M-cycle so the edge lands on its last T-cycle.

### 8.6 The block does not survive its M-cycle

`m_tima_write_block = false;` runs unconditionally at the end of every `timer_tick()`. The
flag therefore covers the edge in the tick it was set, and nothing beyond. The next falling
edge — 1024 T-cycles later in mode 0 — increments normally.

### 8.7 A `DIV` write can generate a tick

```cpp
void Timer::write_div()
{
    // The write cycle can raise a falling edge twice: the absorbed increment
    // (k -> k+1) and the reset itself (last value -> 0). Tick if either side
    // of the increment held the selected bit.
    const u16 sel = selected_bit();
    const bool old_signal = (m_tac & 0x04) &&
        ((m_div & sel) || (div_end_of_cycle() & sel));
    m_div = 0;
    m_div_increment_absorbed = true;
    if(old_signal)
    {
        apply_timer_tick();
    }
}
```

Two things happen here:

1. **The increment of the write's own T-cycle is absorbed.** `m_div_increment_absorbed = true`
   makes the coming `timer_tick()` skip the `m_div++` ([§5](#5-timer_tick-step-by-step)) — the
   reset consumed that T-cycle's increment.
2. **The tick decision is an OR-edge over both sides of that increment.** Within this one
   T-cycle two falling edges are possible: the increment (`k → k+1`) and the reset
   (last value `→ 0`). `old_signal` is therefore true if the selected bit was set either
   *before* the increment (`m_div & sel`) or *after* it (`div_end_of_cycle() & sel`); since the
   write then zeroes the counter, either side held high means the bit falls to 0. A plain
   `signal()` evaluated before the reset misses the case where only the post-increment value
   had the bit — that gap is what made `tim01` and the `*_div_trigger` ROMs fail before the
   OR-edge was added.

Note this path does **not** set `m_tima_write_block` — a `DIV` write and a `TIMA` write are
different events with different effects.

> Verified by the unit test *write_div with the selected bit at 1 fires an immediate tick* and
> the Mooneye `*_div_trigger` ROMs.

### 8.8 A `TAC` frequency change can generate a tick

```cpp
void Timer::write_tac(u8 value)
{
    // Like write_div: both edges of the gate are evaluated on the
    // end-of-cycle divider value (see div_end_of_cycle).
    const u16 div = div_end_of_cycle();
    const bool old_signal = (m_tac & 0x04) && (div & selected_bit());
    // Only the lower 3 bits are connected; the upper 5 are discarded.
    m_tac = value & 0x07;
    const bool new_signal = (m_tac & 0x04) && (div & selected_bit());
    if(old_signal && !new_signal)
    {
        apply_timer_tick();
    }
}
```

Same edge rule, applied to a change of the selected bit — but both sides of the gate are
compared against the *same* end-of-cycle divider value (`div_end_of_cycle()`), so the decision
does not depend on whether the increment for this T-cycle has already been applied in memory.
If the new configuration is low and the old one was high, `TIMA` ticks immediately.

> Verified by the unit test *write_tac with a frequency change fires an immediate tick*.

### 8.9 A `DIV` or `TAC` write that does *not* drop the selected bit does nothing

The `old && !new` condition means writing `DIV` while the selected bit is already low produces
no tick. Same for a `TAC` change that keeps the signal high. Both are covered by unit tests
asserting `TIMA` is unchanged.

### 8.10 Disabling with the selected bit high fires a tick (DMG)

```cpp
t.write_tac(0x04);   // enabled, bit 9 selected
/* ... reach a state where bit 9 is 1 ... */
t.write_tac(0x00);   // disable
```

On DMG, clearing the enable bit while the selected bit is high fires one tick. This falls out
of the same `old && !new` rule: the enable bit is part of `signal()`, so disabling is a falling
edge of the wire.

### 8.11 Enabling does **not** fire a tick (DMG)

The asymmetry with [§8.10](#810-disabling-with-the-selected-bit-high-fires-a-tick-dmg) is
real: on DMG, going from disabled to enabled produces **no** tick, even if the selected bit is
high. `TIMA` will not move until the next genuine falling edge.

### 8.12 `TAC` read-back is not a round-trip

```cpp
u8 Timer::read_tac()
{
    // Pan Docs / schematics: only TAC bits 0-2 are R/W. Bits 3-7 are not wired
    // to the write bus, so they never latch anything and always read back as 1
    // (the data bus idles high). This is why read-back is NOT a round-trip.
    return m_tac | 0xF8;
}
```

`TAC` bits 3–7 are **not connected to the write bus**, so writing them latches nothing. They
always read as `1`, because the chip's data bus idles at VDD before each access and any bit a
register does not actively drive stays high. The *Cycle-Accurate Game Boy Docs*, derived from the
DMG schematic, puts it as: *"Only the lower 3 bits are (R/W)."*

| Operation | bits 3–7 | bits 0–2 |
|---|---|---|
| `write_tac(x)` | discarded | stored as `x & 0x07` |
| `read_tac()` | always `1` (contributes `0xF8`) | returned as stored |

So `write_tac(0x07)` then `read_tac()` gives `0xFF`, and `write_tac(0x00)` then `read_tac()`
gives `0xF8`. Software that reads `$FF07` therefore never sees a value in the range
`0x00`–`0x07`.

**A note on why the write-side mask looks redundant.** It is not observable through
`read_tac()`, because that ORs `0xF8` — which saturates exactly the bits the mask clears. Nor
does it affect behaviour, since the logic only reads `m_tac & 0x04` and `m_tac & 0x03`. It is
kept because it is what the hardware does and because it keeps the stored state canonical: a
savestate taken after `write_tac(0xFF)` is byte-identical to one taken after `write_tac(0x07)`.
Without the mask those two states diverge in the savestate while remaining indistinguishable to
software, which is exactly the kind of latent difference that bites later.

### 8.13 `DIV` reads expose only the high byte

`read_div()` returns `m_div >> 8`. Software cannot observe the low byte and cannot write a
meaningful value.

---

## 9. The `m_tima_write_block` mechanism

This flag exists for a single reason: **on hardware, writing `TIMA` also writes to the
internal reload latch.** A divider edge arriving in the same M-cycle is therefore suppressed,
whichever of the two "wins".

Its lifetime is tightly bounded:

| Property | Value |
|---|---|
| Set by | an **accepted** `TIMA` write: cycle A proper (`m_reload_pending > 1`), cycle B's closing T-cycle (`m_load_active == 1`), or normal operation |
| Not set by | a **dropped** `TIMA` write (`m_reload_pending == 1` or `m_load_active > 1`), a `DIV` write, a `TAC` write |
| Read by | the falling-edge branch of `timer_tick()` |
| Cleared by | the unconditional `m_tima_write_block = false;` at the end of **every** tick |

Because the flag is cleared at the end of every tick regardless of branch, it is inherently a
one-shot. Note that in cycle A the flag is set but never consulted, because cycle A's branch
runs before the edge branch is even reached.

---

## 10. Savestates

```cpp
void Timer::serialize(TaggedSerializer& s)
{
    s.process("div", m_div);
    s.process("tima", m_tima);
    s.process("tma", m_tma);
    s.process("tac", m_tac);
    s.process("timer_reload_pending", m_reload_pending);
    s.process("tima_load_active", m_load_active);
    s.process("tima_write_block", m_tima_write_block);
    s.process("div_increment_absorbed", m_div_increment_absorbed);
}
```

All eight fields of persistent state are persisted. `m_reload_pending` and `m_load_active` are
both required: a savestate taken during cycle A and resumed must not lose a pending reload, and
one taken during cycle B must not lose the latched-write window. `m_div_increment_absorbed`
matters for the same reason: a savestate taken between a `DIV` write and that T-cycle's
`timer_tick()` must not resurrect the consumed increment. `m_cpu_phase` is deliberately *not*
serialized — it is transient, re-established by `begin_cpu_phase()` at the start of every
cycle.

Round-trip is covered by the unit test *savestate with TaggedSerializer (write and read)*.

---

## 11. Reimplementation recipe

If you were writing this class from nothing, the shortest correct path is:

**Step 1 — the divider.** A 16-bit counter, incremented once per T-cycle, reset to 0 by any
write to `0xFF04`. Expose only `>> 8` on read. Initialise it to `0xAC00`.

**Step 2 — the edge.** Compute `signal() = enabled && (div & selected_bit)`, where
`selected_bit()` is a `u16` returning `1 << 9 / 1 << 3 / 1 << 5 / 1 << 7`. Increment `TIMA`
only on `old && !new`.

**Step 3 — the overflow.** On a tick with `TIMA == 0xFF`, set `TIMA = 0x00` and arm a
4-T-cycle reload counter. **Do not request the interrupt yet.**

**Step 4 — the A/B split.** Add the second counter. On the reload tick, copy `TMA` into `TIMA`
and request the interrupt here. Arm the *second* counter for 4 more T-cycles.

**Step 5 — the write rules.** Judge every write against the state at the **end** of its
T-cycle (dry-run the coming tick), because that is what the hardware register sees:

```
write TIMA (state as of the END of the T-cycle):
    pending > 1 (still cycle A)   -> cancel reload + IRQ, keep value, block edge
    pending == 1 (reload lands now)   -> drop
    load_active > 1 (still cycle B)   -> drop
    load_active == 1 (window closes now) / idle -> store, block edge

write TMA:
    load_active > 1 (cycle B still stands) -> store AND mirror into TIMA
    otherwise                              -> store only
```

**Step 6 — the edge-blocking flag.** Set it on an *accepted* `TIMA` write, clear it
unconditionally at the end of every tick.

**Step 7 — the self-triggering writes.** `DIV` and `TAC` writes re-evaluate the gate around
the change and fire a tick if the write itself caused a falling edge. Two details: a `DIV`
write *absorbs* its own T-cycle's increment (`m_div_increment_absorbed`), and its edge test
ORs both sides of that increment (pre- and post-increment value of the selected bit) — see
[§8.7](#87-a-div-write-can-generate-a-tick). The `TAC` test evaluates both sides of the gate
on the end-of-cycle divider. These rules also produce
[§8.10](#810-disabling-with-the-selected-bit-high-fires-a-tick-dmg) and
[§8.11](#811-enabling-does-not-fire-a-tick-dmg) without special-casing them.

**Step 8 — branch priority in the tick.** absorb-or-increment → cycle A → cycle B → edge →
clear the block flag (and the CPU-phase flag). Getting this order wrong produces off-by-one
reloads that no single unit test will catch.

**Step 9 — end-of-cycle sampling for reads.** Flag the CPU's turn (`begin_cpu_phase()`), and
while it lasts return dry-runs of the coming tick: `div + 1` for `DIV`, the landing reload for
`pending == 1`, the post-increment value for a pending edge. Without this, reads race the
increment and land one T-cycle early.

The three failure modes worth knowing about, because all are silent:

- Returning `u8` from `selected_bit()` truncates `1 << 9` to `0`, and mode 0 simply never
  ticks.
- Implementing only one of the two cycles. Cycle-A-only passes the "reload happens" tests and
  fails the "TIMA write is ignored" test; cycle-B-only fails the "write cancels" test. The two
  Mooneye ROMs `tima_write_reloading` and `tma_write_reloading` each fail differently, which is
  exactly what they were written to catch.
- Judging writes against the pre-increment state instead of the end-of-cycle state. The A/B
  windows then sit one T-cycle off: blocks `C` and `L` of the two write-reloading ROMs invert
  (7F↔FE), while every other ROM still passes.

---

## 12. Verification

```sh
make run_tests   # 17 unit test cases, 82 assertions
make run_mts     # Mooneye acceptance suite: 13 of 13 passing
```

Unit tests cover, in order: power-on `DIV`, `DIV` reset and advance, disabled timer, mode 0
and mode 1 edges, overflow and reload timing, the write-blocks-the-edge rule, all three A/B
write cases, `DIV`-write tick, `TAC`-write tick, disable-fires-a-tick, MMIO round-trip,
`TAC` upper-bits discard, `timer_init()` completeness, and savestate round-trip.

Mooneye `timer/` status: **13 of 13 passing** — `div_write`, `rapid_toggle`, `tim00`–`tim11`,
`tim00_div_trigger`–`tim11_div_trigger`, `tima_reload`, `tima_write_reloading`, and
`tma_write_reloading`, with every assertion block (`B C D E H L`) matching.

---

## 13. Known limitations

**Historical note: `rapid_toggle` used to fail by one loop iteration — fixed.** The ROM
toggled `TAC` in a 68-T-cycle loop and the reference overflowed 68 T-cycles (one iteration)
earlier than this implementation. The root cause was phase, not logic: `write_tac()` (and
`write_div()`) evaluated their edges on the pre-increment divider. Evaluating both sides of
the gate on the end-of-cycle divider ([§5.1](#51-sampling-convention-reads-and-window-decisions-see-the-end-of-the-cycle),
[§8.8](#88-a-tac-frequency-change-can-generate-a-tick)) restored the phase and the ROM now
passes, as does the rest of the suite.

**`timer_init()` used to leave `m_tima`, `m_tma` and `m_tac` uninitialised — fixed.** They were
only zero in practice because `QtreGB`'s constructor happens to spell its member
`m_timer(Timer())`, and `Timer()` is a *value*-initialisation that zeroes a class with no
user-provided constructor. That made correctness depend on an accident in a different file, and
it hid in the test suite: `Timer t;` there is a *default*-initialisation, so those tests were
reading stack garbage and passing only because every one of them writes the registers before
reading them. `timer_init()` now sets all nine fields, and a unit test (*timer_init() defines
TIMA, TMA and TAC regardless of prior state*) fails if it ever stops doing so.

**The remaining fragility is a future constructor.** The `m_timer(Timer())` idiom only zeroes
members while `Timer` has no user-provided constructor. Adding an empty `Timer::Timer() {}`
would silently turn that zeroing off and nothing would fail loudly.

**Power-on `DIV` is approximated** as `0xAC00` rather than Pan Docs' `0xABCC`
(see [§4.1](#41-power-on-state)).

**CGB double-speed is not modelled** (see [§8.14](#814-cgb-double-speed-mode-is-not-modelled)).

**Interrupt dispatch timing is not cycle-accurate.** The `IT_TIMER` callback fires at the
reload instant, but the CPU's interrupt polling and the `push` that follows it are not modelled
at T-cycle granularity. This affects the Mooneye `interrupts/` ROMs, not the timer ones.

---

## 14. References

- [Pan Docs — Timer and Divider Registers](https://gbdev.io/pandocs/Timer_and_Divider_Registers.html)
  — register map, bit selection table, `TAC` write side effects.
- [Pan Docs — Timer Obscure Behaviour](https://gbdev.io/pandocs/Timer_Obscure_Behaviour.html)
  — the reload window, cycle A vs cycle B, the three obscure write behaviours. This is the
  document the A/B model is built from.
- [Mooneye test suite — `timer/`](https://github.com/mattcurrie/mooneye-test-suite) —
  `tima_write_reloading` and `tma_write_reloading` are the two ROMs that pin down the A/B
  semantics.
