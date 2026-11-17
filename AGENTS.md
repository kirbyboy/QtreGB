# QtreGB — Agent Rules

Game Boy **DMG** emulator in **C++17** (SDL2 + ImGui). The `core/` is meant to
become a reusable library decoupled from the GUI. The CPU is the micro-op
pipeline (`CpuMicro`) and the PPU is the native dot-driven pipeline (`PPU`);
both are the only implementations — there are no selection flags.

## Build & test
- `make -j$(nproc)` → `build/QtreGB-GUI` (C++17, `-O0 -g`, `-Wall -Wextra -Wpedantic`).
- `make run_tests` → doctest suite in `tests/` (156 cases, 787 assertions).
- `make run_blargg` (15) · `make run_mts` (34).
- PPU acceptance ROMs are **not** wired into `mts/Makefile`; run them manually:
  `./build/mts_runner vendor/mts/*/acceptance/ppu/*.gb` → 12/12.
- PPU frame harness (needs the DMG boot ROM):
  `QTR_BIOS="$PWD/roms/bios/[BIOS] Nintendo Game Boy Boot ROM (World).gb" ./mts/compare_ppu_frames.sh`
  → expected **25 PASS / 0 FAIL / 8 SKIP** (the 8 SKIP have no reference PNG).

## Working rules (MANDATORY)
1. **Never commit or push** unless the user explicitly asks. `git status`/`diff` are fine.
2. **Language**: code comments and error/exception strings in **English**; reply to
   the user in **Spanish**.
3. **Evidence before "done"**: a clean build and the relevant test suite passing,
   with exact numbers reported. No evidence ⇒ not done.
4. **Minimal, surgical fixes.** Never refactor while fixing a bug. Do not suppress
   errors (no fake casts, no swallowed exceptions).
5. `core/src/timer.cpp` is sensitive — do not modify it without a plan and
   re-validation of the timing tests (`mts` + `blargg`).
6. `sudo` is denied. ImageMagick is available at `/usr/bin/magick`.
7. Match existing style: `inc/` + `src/` split, `m_` member prefix, class-per-file.

## Architecture notes
- `core/inc/cpu_micro.h` + `core/src/cpu_micro.cpp`: micro-op CPU (`CpuMicro`),
  one micro-op per M-cycle, bus accesses timed per T-cycle.
- `core/inc/ppu.h` + `core/src/ppu.cpp`: native `PPU` (DMG). Dot state machine,
  OAM scan, STAT/LYC/VBlank timing, BG/window fetcher, OBJ fetcher and OAM/VRAM
  bus ownership.
- `core/inc/timer.h` + `core/src/timer.cpp`: DocBoy-style timer, one tick per
  M-cycle at T3.
- `core/inc/apu.h` + `core/src/apu.cpp`: APU register file, NR52 power state,
  frame-sequencer clock, all four generators (CH1/CH2 pulse, CH3 wave, CH4
  noise), length/envelope, CH1 sweep, a stereo mixer with NR50/NR51 and HPF,
  and a 48 kHz ring buffer. The frame sequencer is clocked by DIV bit 12
  (`Timer::read_div16()` → `APU::tick(div)`). The GUI drains the buffer to SDL
  (`EmulatorInstance::drain_audio`). `dmg_sound` is at 12/12. NRX2 writes
  implement the CGB-E zombie-mode glitch table (CH1/CH2/CH4), and pulse channels
  apply DocBoy's retrigger delay. SameSuite `div_write_trigger`,
  `div_write_trigger_10`, `channel_1/2_nrx2_glitch` and `*_restart_nrx2_glitch`
  pass with `QTR_PCM_DEBUG=1` (17/70 APU ROMs; the rest encode CGB-E-only
  behavior). `APU::serialize`
  covers registers, channel state, DIV phase, resampler and HPF state (the PCM
  ring is deliberately excluded). A full machine savestate still needs the
  PPU/LCD/cart/DMA parts (see `documentation/Game Boy/apu-plan.md`).
- `core/inc/cart.h` + MBC1/no-MBC/MBC2/MBC3 cartridges: battery-backed RAM is
  saved to `<rom>.sav` next to the ROM. MBC2 uses 512 bytes (one nibble per
  address) and can import 256-byte packed mGBA saves; MBC1/no-MBC save all RAM
  banks raw. MBC3 supports 7-bit ROM banking, up to 32 KB RAM and a host-time
  RTC (latch, halt, day carry); its save appends the VBA64 48-byte RTC block
  after RAM. MBC30 is detected by an MBC3 RAM size of `$05` (64 KB) and uses
  8-bit ROM banks and 8 RAM banks. `mbc3-tester`, `rtc3test`
  (basic/range/sub-second) and Mealybug `mbc3_rtc` pass. MBC1 supports 1 MB+
  secondary banking (bits 5-6) and MBC1M detection (secondary bits 4-5). The
  GUI saves on ROM switch/reset/close and loads it when a battery cartridge
  starts.
- `gui/src/main.cpp`: time-based frame pacer. Emulation runs at the Game Boy
  rate (70224 T-cycles per frame, 59.7275 Hz), independent of the monitor;
  rendering is presented once per loop with SDL vsync so the image is not torn
  and high-refresh monitors only repeat frames.
- `core/inc/QtreGB.h` + `core/src/QtreGB.cpp`: machine wiring (`Cpu = CpuMicro`,
  `PPU`).
- The DocBoy PPU oracle was removed after reaching parity; it remains in Git
  history (`wip/ppu`, tag `ppu-oracle-25-0-8`).

## PPU screenshot harness
- `MTS_FRAME_OUT=<file.pgm> ./build/mts_runner <rom.gb>` — dumps the first frame
  as PGM (P5) 160×144 with the DMG palette `0→255, 1→170, 2→85, 3→0`; stops at
  the software-breakpoint opcode `0x40`.
- `MTS_FRAME_DELAY=N` skips N frames; `MTS_TRACE=1` enables tracing.
- Framebuffer: `PPU::m_video_buffer`, pixel `(x,y)` at index `y*160 + x`.

## Copia de seguridad de código (OBLIGATORIO al iniciar sesión / leer un handoff)
- Carpeta local: `tmp/` (ignorada por Git; no se commitea).
- Al leer un handoff (`documentation/Game Boy/*.md`), crear una copia de `core/`,
  `gui/`, `mts/`, `tests/`, `common.mk` y Makefiles en `tmp/<YYYYMMDD_HHMMSS>/`
  (sin `build/`, `obj/`, `deps/`, `lib/`, `vendor/`).
- Comando de referencia:
  `mkdir -p tmp/$(date +%Y%m%d_%H%M%S) && rsync -a --exclude=build --exclude=obj --exclude=deps --exclude=lib --exclude=vendor --exclude=tmp core gui mts tests common.mk Makefile tmp/$(date +%Y%m%d_%H%M%S)/`
- Registrar la fecha creada en esta sección o en el handoff.

## Flujo seguro (sin Git destructivo)
- **Nunca** usar `git stash`, `git reset --hard`, `git checkout --`, `git restore`
  ni `git clean -fdx`.
- Antes de editar un fichero sensible:
  `cp core/src/ppu.cpp tmp/ppu.cpp.pre-edit` (equivalente para `timer.cpp`).
- Deshacer: `cp tmp/ppu.cpp.pre-edit core/src/ppu.cpp`.
- Commits solo si el usuario lo pide explícitamente, con build + tests verdes.
