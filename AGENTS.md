# QtreGB — Agent Rules

Game Boy **DMG** emulator in **C++17** (SDL2 + ImGui). The `core/` is meant to
become a reusable library decoupled from the GUI. The CPU is the micro-op
pipeline (`CpuMicro`) and the PPU is the native dot-driven pipeline (`PPU`);
both are the only implementations — there are no selection flags.

## Build & test
- `make -j$(nproc)` → `build/QtreGB-GUI` (C++17, `-O0 -g`, `-Wall -Wextra -Wpedantic`).
- `make run_tests` → doctest suite in `tests/` (80 cases, 386 assertions).
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
