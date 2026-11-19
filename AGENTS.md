# QtreGB — Agent Rules

Game Boy **DMG** emulator in **C++17** (SDL2 + ImGui). The `core/` is meant to
become a reusable library decoupled from the GUI. The CPU is the micro-op
pipeline (`CpuMicro`) and the PPU is the native dot-driven pipeline (`PPU`);
both are the only implementations — there are no selection flags.

## Build & test
- `make -j$(nproc)` → `build/QtreGB-GUI` (C++17, `-O2 -g -flto`, `-Wall -Wextra -Wpedantic`).
  Para un build sin optimizar: `make clean && make GUI_DEBUG=1`.
  Para desactivar LTO: `make clean && make LTO_FLAGS=`.
  Para la traza `CPUEVT` de desarrollo: `make clean && make QTR_IRQDBG=1`.
- `make run_tests` → doctest suite in `tests/` (253 cases, 4209 assertions).
- `make run_blargg` (15) · `make run_mts` (42).
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

## Licensing
- Project code: **GPL-3.0-or-later**, full text in `LICENSE.txt`.
- Project visual assets: CC0 1.0, see `assets/LICENSE`.
- Name/logo/icon use: `TRADEMARK.md`.
- Third-party notices: `THIRD_PARTY_NOTICES.md`.
- `third_party/` is tracked; `lib/` is ignored. ImGuiFileDialog is vendored in
  `third_party/ImGuiFileDialog` (MIT, with local i18n changes) and wired into
  the GUI build for file open/save dialogs.

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
- `core/inc/cart.h` + MBC1/no-MBC/MBC2/MBC3/MBC5 cartridges: battery-backed
  RAM is saved to `<rom>.sav` next to the ROM. MBC2 uses 512 bytes (one nibble
  per address) and can import 256-byte packed mGBA saves; MBC1/no-MBC/MBC5
  save all RAM banks raw. MBC3 supports 7-bit ROM banking, up to 32 KB RAM and
  a host-time RTC (latch, halt, day carry); its save appends the VBA64 48-byte
  RTC block after RAM. MBC30 is detected by an MBC3 RAM size of `$05` (64 KB)
  and uses 8-bit ROM banks and 8 RAM banks. `mbc3-tester`, `rtc3test`
  (basic/range/sub-second) and Mealybug `mbc3_rtc` pass. MBC1 supports 1 MB+
  secondary banking (bits 5-6) and MBC1M detection (secondary bits 4-5). MBC5
  supports 9-bit ROM banking (up to 8 MB), bank 0 at `$4000`, 4-bit RAM banking
  (up to 128 KB), the rumble variants (`$1C-$1E`) and battery. The `mbc5`
  mooneye ROM-size tests pass. The GUI saves on ROM switch/reset/close and
  loads it when a battery cartridge starts.
- `gui/src/app_config.cpp`: cross-platform config under the **Qtre**
  ecosystem. Lookup order: `QTR_CONFIG` override, portable `QtreGB.ini` next
  to the executable, then `Qtre/QtreGB/config.ini` (XDG_CONFIG_HOME on Linux;
  `SDL_GetPrefPath("Qtre", "QtreGB")` on Windows/macOS). ImGui uses
  `IniFilename = nullptr`; there is no `imgui.ini`. It stores window geometry,
  the `[graphics]` options (palette, grid, ghosting, present mode, background),
  `[audio]` (enabled, volume; defaults enabled at 50%), the input model in
  `[input]` / `[shortcuts]` (player devices, bindings, global shortcuts and the
  opposite-directions flag), `[ui] language` (system, en or es), `[paths]`
  (ROMs, BIOS, battery, save states, screenshots and the two "same as ROM"
  flags), `[emulation] fast_forward_speed` (200, 300 or max) and `[recent]`
  (up to 10 recent player 1 ROM paths, newest first).
- `gui/inc/recent_files.h` + `gui/src/recent_files.cpp`: recent player 1 ROM
  list. `remember()` moves a path to the front, removes duplicates and caps
  the list at 10; `set()` preserves the saved order and drops empty entries.
  Only the normal `File → Open ROM...` flow records paths. The File menu shows
  the list between `Load state` and `Exit` with numbered basenames and the full
  path as a tooltip; missing files are removed when selected. Link/player 2
  ROMs and session loads never touch this list.
- `gui/src/display_processor.cpp`: native 160×144 palette/ghosting pass.
  `DisplaySettings` exposes DMG, Pocket and custom palettes, integer scaling,
  LCD-grid overlay and configurable ghosting. LCD grid is drawn in screen
  space with ImGui, only when the effective scale is at least 2×; the GUI's
  `Graphics` menu edits the settings and `QtreGB.ini` stores them under
  `[graphics]`.
- `gui/inc/input_settings.h` + `gui/src/input_settings.cpp`: input data model.
  A player has one device (`None`, `Keyboard` or a controller GUID) and eight
  Game Boy bindings. Controls are encoded as `key:<SDL scancode>`,
  `button:<SDL gamecontroller button>` or `axis:<axis>:<+/-1>`. Global
  shortcuts have one keyboard and one "any controller" slot. `sanitize()`
  removes invalid/duplicate controls, forbids Ctrl/Alt/Esc/Backspace, and
  makes shortcuts win over player bindings. Defaults: P1 keyboard (arrows,
  Z/X, Enter/RightShift), P2 none, F/F5/F8 for fullscreen/save/load.
- `gui/inc/input_manager.h` + `gui/src/input_manager.cpp`: SDL input backend.
  Opens every connected GameController once (shared by both players), tracks
  keyboard/controller state, hotplug, capture mode, edge-triggered shortcuts
  and analog axes as buttons (60% press / 40% release hysteresis). It is shut
  down before `SDL_Quit()`.
- `gui/inc/input_directions.h` + `gui/src/input_directions.cpp`: resolves
  opposite directions in the GUI. With the option off, the latest pressed
  direction wins and releasing it restores the still-held one; simultaneous
  presses tie-break Right over Left and Down over Up.
- `Settings → Controllers` / `Settings → Key bindings`: scrollable ImGui
  windows with per-row Bind/Clear controls and capture mode (Esc/Backspace
  clears, Ctrl/Alt are rejected, Shift is allowed). Changing a player device
  clears that player's bindings; assigning a shortcut removes conflicting
  player bindings and warns.
- `gui/inc/path_settings.h` + `gui/src/path_settings.cpp`: working-directory
  model for ROMs, battery saves, save states, screenshots and the global
  256-byte BIOS file path. Battery and state files use flat names based on the
  ROM stem when a central directory is configured, or stay next to the ROM
  when the independent "Same folder as ROM" checkbox is enabled (the default).
  `Settings → Paths...` edits these values with Apply/Cancel; applying saves
  the current battery in the old location first, then updates every running
  instance in place. The BIOS path is loaded globally; active link sessions
  keep the BIOS latched at session start.
- Fast forward: hold the global `Fast forward` shortcut (Space by default;
  speed in `Settings → Fast Forward speed`: 200%, 300% or MAX). At 200/300 the
  main accumulator runs 2×/3× emulated frames and presents only the last one.
  MAX ignores wall-clock accumulation and runs until roughly 75% of the
  display interval (capped at 12 ms), leaving CPU headroom. Audio is discarded
  while fast-forwarding. `File → Exit` closes the app cleanly.
- While the Controllers, Key bindings or Paths windows are open, the input
  manager is UI-blocked: game controls and global shortcuts do not reach the
  emulator, but binding capture still works.
- Link audio routing: the global `Audio` menu keeps enable/volume; `Link →
  Audio` selects `Player 1`, `Player 2` or `Both` (default Player 1,
  persisted as `[audio] link_source`). Each `EmulatorInstance` has a separate
  output-enabled flag; muted instances pause their SDL device and discard the
  APU ring. Without a link session P1 stays audible.
- Link rendering: each PPU has its own frame phase. Player 1's texture is
  refreshed when P1 reaches line 0; player 2's texture is refreshed inside
  `EmulatorFrontend::tick()` when P2 reaches its own line 0. The combined
  320×144 texture uses the latest completed frame of each machine, avoiding a
  mixed-row horizontal seam (P2 can be at most one frame behind).
- `gui/inc/session_state.h` + `gui/src/session_state.cpp`: `.qss.png`
  link-session container, version 2. On disk it is a valid 320×144 PNG image
  (the combined frame that is displayed) followed by the container bytes, so
  file managers can show a thumbnail. The container stores two ROM identities
  (name, informative path, size and FNV-1a 64 hash), BIOS metadata, UTC
  creation time, emulated frames, the app build, a combined 320×144 RGBA
  preview and the two raw core state blobs with header/payload CRCs. Version 1
  `.qss` files (without PNG/new metadata) still load through the fallback
  path. The filename is `Game1 - Game2 (short-hash).qss.png` in
  player 1's save-state directory. Loading validates current ROMs, then tries
  the saved path and finally `paths.roms/<filename>` (root only). F5/F8 save
  or load the session `.qss.png` while a link session is active; otherwise
  they use the selected slot `.sr0.png`…`.sr9.png`. Player 2 has no
  independent state. Game changes recreate the session without auto-saving the
  old `.qss`.
- `gui/inc/i18n.h` + `gui/src/i18n.cpp`: gettext-style translation catalog.
  English source strings are the keys and the fallback; `lang/es.po` holds the
  Spanish translation. `Settings → Language` offers System default, English and
  Español; the choice is stored as `[ui] language` in `QtreGB.ini`.
  `QTR_LANG_DIR` overrides the catalog directory (otherwise `<exe>/lang`,
  `<exe>/../lang` and `<cwd>/lang` are tried).
- `gui/src/file_dialog_i18n.cpp`: fills ImGuiFileDialog's runtime string table
  from `i18n::tr()`. File dialogs are asynchronous: the frontend stores a
  pending open/save action and renders ImGuiFileDialog after the main window.
  Player input and global shortcuts are blocked while a dialog is open.
- Save-state slots: 10 slots per ROM (`.sr0.png`…`.sr9.png`), selected with
  `Next state slot` / `Previous state slot` (PageUp/PageDown by default,
  wrapping 0↔9). A new game or application start selects slot 0. The main
  window has a fixed bottom status bar showing the current slot, the real
  emulated FPS and recent save/load/slot-change messages; session states have
  no slots. FPS counts completed emulated frames, not presentation refreshes.
- `gui/inc/state_store.h` + `gui/src/state_store.cpp`: savestate container for
  one player, `<rom>.sr<slot>.png`. On disk it is a valid 160×144 PNG image
  (the screenshot) followed by the container bytes, so file managers can show
  a thumbnail. The container wraps the core blob with magic/version,
  core format/schema, UTC creation time, emulated frames, slot/kind/note,
  ROM identity (title, filename, size, FNV-1a hash, mapper, CGB flag, cart
  type, header/global checksum), BIOS identity (size, hash, active) and an
  optional uncompressed 160×144 RGBA preview. Header and payload have CRC-32
  and writes are atomic (temp + rename). Old `.srN` files containing only the
  raw container or the raw `QTGBST01` core blob still load through the legacy
  path. PNG-wrapped sizes measured: Tetris 115.432 B (112.7 KiB), and the
  combined link state 260.685 B (254.6 KiB). Format documented in
  `documentation/Game Boy/savestate-container-format.md`.
- `File → State browser...`: floating window with the 10 slot thumbnails for
  the current ROM, or the single combined `.qss.png` preview while a link
  session is active. It shows creation date, emulated frames/time and file
  size, and exposes Load / Save here / Refresh. A successful load closes the
  browser; a failed load leaves it open. Player input and global shortcuts are
  UI-blocked while it is open.
- `gui/inc/fps_counter.h` + `gui/src/fps_counter.cpp`: 0.5 s sliding window
  over completed emulated frames. The main loop records a frame in
  `run_emulated_frame()`; `EmulatorFrontend::draw_main_status_bar()` prints
  `FPS: --` until the first window closes and drops back to `--` after one
  second without frames (LCD off).
- `gui/src/main.cpp`: vsync-aware frame pacer. Emulation runs at the Game Boy
  rate (70224 T-cycles per frame, 59.7275 Hz), independent of the monitor.
  If SDL present actually blocks for the monitor refresh, the display drives
  presentation; otherwise the loop sleeps to cap at 59.7275 Hz. Measured with
  Kirby on a 60 Hz monitor: ~11% of one core (MegaBoy: ~24%).
- `core/inc/QtreGB.h` + `core/src/QtreGB.cpp`: machine wiring (`Cpu = CpuMicro`,
  `PPU`).
- The DocBoy PPU oracle was removed after reaching parity; it remains in Git
  history (`wip/ppu`, tag `ppu-oracle-25-0-8`).

## libretro (siguiente tarea)
- `libretro/Makefile` es un placeholder sin targets. La primera versión será
  DMG `.gb`, single-player, RTC con el comportamiento actual y estados con el
  blob del core. `retro_serialize_size` reservará **320 KiB** (doble del peor
  caso conservador). El Link Cable queda fuera.
- `libretro/screen-test/` está ignorada por Git: solo para probar localmente
  una simulación de pantalla física (shader o post-proceso), nunca para
  commitear.

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
