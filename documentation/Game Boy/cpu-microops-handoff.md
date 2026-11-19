> **Nota (2026-11-19):** las rutas `tmp/...` que aparecen en este documento
> son históricas. Se eliminó la política de copias periódicas y los backups
> locales; el código de referencia está en Git.

# QtreGB — Handoff: CPU micro-ops (DocBoy-style)

**Fecha:** 2026-11-15 (actualizado 2026-11-16)
**Worktree:** `/home/pedro/projects/QtreGB/.wip/ppu-v4` (rama `ppu-v4`)
**Backup de esta sesión:** `tmp/20261115_013151/`
**Estado del árbol:** implementación final. `CpuMicro` es el único CPU y el
PPU nativo es el único PPU; no hay flags de selección (ver §20). Las secciones
1–19 conservan el histórico del desarrollo.

---

## 1. Resumen

Se ha escrito un CPU paralelo basado en **micro-ops** (modelo DocBoy) y se ha
integrado en `QtreGB` tras un flag de compilación. El set de instrucciones
válido está completo salvo `STOP` (0x10) y los 11 opcodes inválidos.

Resultado con el CPU nuevo (`QTR_CPU_MICRO=1`):

- doctest **102/102** (487 assertions)
- blargg **11/15**
- mts **33/34**
- PPU/m3 **sin recalibrar**

La base por defecto sigue verde:

- doctest **102/102** (487)
- PPU acceptance **12/12**
- blargg **15/15**
- mts **34/34**
- harness **15 PASS / 10 FAIL / 8 SKIP**

Los fallos restantes del CPU nuevo son de **fase de escritura (t1) frente al
timer/PPU de QtreGB**, calibrados a escrituras en t3. No son fallos del set de
instrucciones.

---

## 2. Qué se ha hecho

### 2.1 Motor `CpuMicro`

Archivos:

- `core/inc/cpu_micro.h`
- `core/src/cpu_micro.cpp`
- `tests/test_cpu_micro.cpp`

Modelo:

- Un M-cycle = 4 T-cycles: `t0`, `t1`, `t2`, `t3`.
- `t0`: decode/prefetch + ejecuta el micro-op actual.
- `t1`: commit de escritura (`write_request` + `flush_write`).
- `t2`: fase interna.
- `t3`: commit de lectura (`read_request` + `flush_read`).
- El **último micro-op de cada instrucción es `fetch()`**: lee el siguiente
  opcode en t0, lo confirma en t3 e incrementa PC.
- `m_fetching`/`m_fetching_cb` decodifican el opcode base y el segundo byte CB.
- Tablas:
  - `table()`: 256 opcodes base.
  - `cb_table()`: 256 opcodes CB.
  - `isr_table()`: 5 micro-ops (`isr_m0..m4`).
- `Instruction` contiene hasta 6 micro-ops.
- Condicionales: el micro-op de condición llama a `fetch()` y termina la
  instrucción antes de tiempo (`fetch()` pone `m_instruction=nullptr`).
- CB: `cb_prefix` → `fetch_cb()`; el M-cycle siguiente decodifica `cb_table`.
- ISR: estados `None/Pending/Serving`, `check_interrupt(phase)` en t0..t3
  (mapeo DocBoy: t0→3, t1→0, t2→1, t3→2), timing DMG normal 1 M-cycle (2 si
  se detecta en phase 3), tabla `HALTED` preparada.
- `ImeState` de 4 estados: `Disabled → Requested → Pending → Enabled`.
  `EI` pide IME y avanza un estado al final de cada instrucción, por lo que
  el interrupt no se acepta hasta después de la instrucción siguiente.
- `HALT` implementado con el halt-bug básico de DocBoy.
- `serialize` + `State` compatibles con `Z80GB::State` (los primeros 10 tags
  son `a,f,b,c,d,e,h,l,pc,sp`).
- Shim de integración: `ICPURegisters`, `read_IF/write_IF/read_IE/write_IE`,
  `reset(bool)`, `is_end_m_cycle`, `is_interruption_time`, `read_register/16`,
  `connect_error_reporter`, `peek_next_opcode`.

### 2.2 Cobertura de instrucciones

Portado:

- NOP, LD r,r', LD r,d8, LD rr,d16, LD (HL),r, LD r,(HL), LD (HL),d8,
  LD (BC/DE),A, LD A,(BC/DE), LD (HL±),A, LD A,(HL±), LD (a16),A,
  LD A,(a16), LD (a16),SP, LD SP,d16, LD SP,HL.
- LDH (a8),A / LDH A,(a8), LD (C),A / LD A,(C).
- ALU A,r y ALU A,d8 (ADD/ADC/SUB/SBC/AND/XOR/OR/CP).
- INC/DEC r, INC/DEC (HL), INC/DEC rr, ADD HL,rr, ADD SP,e, LD HL,SP+e.
- RLCA/RRCA/RLA/RRA, DAA, CPL, SCF, CCF.
- JP, JP cc, JP (HL), JR, JR cc, CALL, CALL cc, RET, RET cc, RETI, RST.
- PUSH/POP BC/DE/HL/AF.
- EI, DI, HALT.
- CB completo: rotate/shift, BIT, RES, SET, registro y `(HL)`.

Pendiente:

- `STOP` (0x10).
- Opcodes inválidos: `D3 DB DD E3 E4 EB EC ED F4 FC FD` (deben quedar
  como unimplemented/error).
- `is_opcode_implemented()` sigue devolviendo **12/256** no implementados
  (los anteriores).

### 2.3 Integración

- `common.mk`: `QTR_CPU_MICRO=1` añade `-DQTR_CPU_MICRO`.
- `core/inc/QtreGB.h`: alias `Cpu` (`CpuMicro` o `Z80GB`), alias `CpuState`,
  `Cpu m_cpu`.
- `core/src/QtreGB.cpp`: inicializador condicional del CPU; callbacks IF/IE
  del `InterruptController` enganchados al CPU activo bajo el flag.
- `core/inc/Z80GB.h`: añadido `peek_next_opcode()`.
- `mts/mts_runner.cpp`: usa `CpuState` y `peek_next_opcode()`.
- `blargg/blargg_runner.cpp`: usa `CpuState`.

---

## 3. Resultados

### 3.1 Base por defecto (`Z80GB`)

| suite | resultado |
|---|---:|
| doctest | **102/102** (487 assertions) |
| PPU acceptance | **12/12** |
| blargg | **15/15** |
| mts | **34/34** |
| harness | **15 PASS / 10 FAIL / 8 SKIP** |

### 3.2 CPU nuevo (`QTR_CPU_MICRO=1`)

| suite | resultado |
|---|---:|
| doctest | **102/102** (487) |
| blargg | **11/15** |
| mts | **33/34** |

Fallos blargg:

- `02-write_timing`: todas las escrituras se miden **1 M-cycle antes**
  (`36:2-3`, `70:1-2`, `E2:1-2`, `E0:2-3`, `EA:3-4`).
- `03-modify_timing`: CB RMW mide `0/0` (no determina el ciclo).
- `instr_timing`: timeout (100M T-cycles).
- `03-op sp,hl`: corregido durante la integración; ya pasa.

Fallos mts:

- `rapid_toggle`: 1 assertion.

PPU/m3:

- `lcdon_timing-GS` falla con el CPU nuevo.
- El resto de PPU acceptance y los m3 no se han recalibrado todavía.

---

## 4. Diagnóstico de los fallos restantes

El CPU nuevo escribe en **t1**. El timer y la PPU de QtreGB están calibrados
para el CPU viejo, que escribía en **t3**. Eso produce:

- `02-write_timing` / `03-modify_timing`: offset de un M-cycle.
- `instr_timing`: el test usa el timer para medir; el timeout apunta al mismo
  acoplamiento. No se ha confirmado que sea un bug funcional.
- `rapid_toggle`: caso límite de timer; probablemente el mismo acoplamiento.
- `lcdon_timing`: fase CPU/PPU.

`mts 33/34` valida que el set funcional, la ISR, el timer y los vectores están
esencialmente bien.

---

## 5. Bugs reales corregidos en esta fase

1. **`INC/DEC r`**: el registro se decodifica con `(op >> 3) & 7`, no
   `op & 7`. El bug hacía que `INC E` incrementara H y el bucle de copia de
   blargg no terminara (timeout).
2. **`rr=3` en operaciones de 16 bits**: en `INC/DEC rr` y `ADD HL,rr`,
   `rr=3` es **SP**, no AF. El mapeo de `PUSH/POP` (3=AF) es distinto.
3. **Magic breakpoint de mts**: con prefetch, `PC` apunta al operando.
   Hay que consultar `peek_next_opcode()`, no `read(pc)`.
4. **ODR/layout al cambiar el flag**: los `.o` no distinguen flags. Si se
   compila la librería sin el flag y un runner con el flag (o al revés), el
   layout de `QtreGB` cambia y aparecen crashes (p. ej. puntero de cart
   corrupto). **Siempre `make clean` al cambiar `QTR_CPU_MICRO`.**

---

## 6. Cómo compilar y validar

### Base por defecto

```sh
make clean
make -j$(nproc)
make -C mts -j4
make -C blargg -j4

make run_tests
make run_blargg
make run_mts
./build/mts_runner vendor/mts/*/acceptance/ppu/*.gb
QTR_BIOS="$PWD/../../roms/bios/[BIOS] Nintendo Game Boy Boot ROM (World).gb" \
  ./mts/compare_ppu_frames.sh
```

### CPU nuevo

```sh
make clean
make -C core QTR_CPU_MICRO=1 -j$(nproc)
make -C tests QTR_CPU_MICRO=1 -j4
make -C mts QTR_CPU_MICRO=1 -j4
make -C blargg QTR_CPU_MICRO=1 -j4

make -C tests QTR_CPU_MICRO=1 run
make -C blargg QTR_CPU_MICRO=1 run
make -C mts QTR_CPU_MICRO=1 run
```

Nota: el `make` superior también compila la GUI; para el CPU nuevo es
suficiente con `core`, `tests`, `mts` y `blargg`. Al volver a la base, hacer
`make clean` y reconstruir sin el flag.

---

## 7. Siguiente sesión

### Prioridad 1 — alinear timer/PPU con el write en t1

Objetivo: que `02-write_timing`, `03-modify_timing`, `instr_timing` y
`rapid_toggle` pasen con el CPU nuevo.

Opciones:

1. **Portar el tick del timer de DocBoy** (tick en t2, `div16 += 4`, detección
   de flancos) con un plan escrito y revalidación completa. `core/src/timer.cpp`
   es sensible: no tocarlo sin plan.
2. **Ajustar la observación del bus** para timer/PPU (commit retrasado) sin
   mover el write del CPU. Cuidado: la PPU también está calibrada a la fase
   vieja.

Recomendación: medir primero el T-cycle exacto del write de `LD (HL),B`,
`LDH`, `LD (a16)` y compararlo con DocBoy; después decidir si el timer debe
tickear en t2 o si el bus debe retrasar la observación.

### Prioridad 2 — revalidar

- blargg 15/15, mts 34/34, doctest, PPU acceptance.
- Cuando el CPU nuevo esté verde, hacerlo el default (quitar el flag o
  invertirlo).

### Prioridad 3 — PPU/m3

- Quitar la compensación de paleta de 6 dots (`m_bg_palette_delay`).
- Recalibrar los delays por señal (BG, ventana, OBJ, LCDC/STAT).
- Revalidar harness: objetivo 24 m3 + dmg-acid2 a `diff = 0`.

### Pendientes menores

- `STOP` (0x10).
- Opcodes inválidos: decidir si se reportan como error o se ignoran.
- `HALT` halt-bug: validar con `halt_bug.gb` y los tests de mts.

---

## 8. WIPs y backups

- Backup de esta sesión: `tmp/20261115_013151/`.
- WIPs del intento de prefetch anterior:
  - `tmp/fetcher-proto/Z80GB_prefetch_wip.{cpp,h}`
  - `tmp/fetcher-proto/mts_runner_peek_wip.cpp`
  - `tmp/fetcher-proto/ppu_prefetch_bgdelay_wip.cpp`
- El código del CPU nuevo está **en el árbol activo** (no es un WIP):
  `core/inc/cpu_micro.h`, `core/src/cpu_micro.cpp`,
  `tests/test_cpu_micro.cpp`.
- `RECAL_PLAN.md` recoge las subfases 23–29 con los resultados.

---

## 9. Reglas de trabajo

- Trabajar sólo en `.wip/ppu` (rama `wip/ppu`); no tocar el checkout
  principal.
- No commit ni push sin petición explícita.
- Inglés en código; español con el usuario.
- Antes de “done”: build limpio + suite relevante con números exactos.
- No usar `git stash`, `git reset --hard`, `git checkout --`, `git restore`
  ni `git clean -fdx`.
- `core/src/timer.cpp` es sensible: plan + revalidación antes de tocarlo.
- Al cambiar `QTR_CPU_MICRO`, **siempre** `make clean`.

---

## 10. Referencias

- DocBoy: `tmp/docboy/src/docboy/docboy/cpu/cpu.cpp` (tablas `instructions`,
  `instructions_cb`, `isr`, `check_interrupt`, `serve_interrupt`).
- Pan Docs / gbops (opcodes, ciclos, flags).
- AntonioND, *The Cycle-Accurate Game Boy Docs* (fases de bus).
- Blargg `mem_timing-2` / `instr_timing` y mts (árbitro empírico).
- `RECAL_PLAN.md` subfases 23–29.

---

## 11. Continuación 2026-11-15 — diagnóstico del timer (shim descartado)

Se intentó alinear el timer con el CPU nuevo sin portarlo:

- **Diagnóstico t3**: con `QTR_CPU_WRITE_T3=1` (flush de escritura en t3
  dentro del mismo M-cycle), blargg **15/15** y mts **33/34** (sólo
  `rapid_toggle`). Confirma que el set de instrucciones es correcto y que
  el resto de fallos blargg son de fase.
- **`rapid_toggle`** falla con t1 y con t3: es la fase absoluta del
  prefetch (un M-cycle antes) respecto al timer, no el T-phase.
- **Shim de escrituras diferidas** (buffer en `Timer`, retardo global y
  máscaras por registro):
  - retardo global 4 T: `rapid_toggle` PASS y blargg 15/15, pero mts
    **24/34** (10 tests de timer/div/intr fallan);
  - TIMA/TAC/TMA solos no arreglan `rapid_toggle`; DIV solo lo arregla
    pero rompe `div_timing`/`tim00`;
  - DIV delay 1-3 → C=DA; 4+ → C=E2.
  - No converge: hace falta portar el timer de DocBoy (tick por M-cycle
    en t2, `div16 += 4`, ventana de reload/load por M-cycle).

El shim y los cambios de `timer.cpp/h` se revirtieron desde el backup
`tmp/20261115_013151/`. El flag `QTR_CPU_WRITE_T3` de `common.mk` y
`cpu_micro.cpp` queda como diagnóstico (default off).

**Siguiente:** portar el timer de DocBoy y revalidar mts/blargg; después
recalibrar la PPU/m3. Ver `RECAL_PLAN.md` subfase 30.

---

## 12. Intento de port del timer de DocBoy (revertido)

Se portó `Timers` de DocBoy bajo `QTR_CPU_MICRO` (tick en T2,
`div16 += 4`, reload pendiente por M-cycle, DIV inicial 0xABCC).

Resultados con CPU micro + write t1:

- `rapid_toggle`: **PASS**.
- mts: **24/34** (10 tests de timer/div/intr fallan).
- blargg: **12/15** (`02-write_timing`, `03-modify_timing`,
  `instr_timing`).

El timer de DocBoy no es drop-in sobre el scheduler per-T de QtreGB; sus
ventanas de reload/load y la fase del scheduler están acopladas al pipeline
completo de DocBoy. Revertido desde `tmp/timer.pre-docboy.*` y al orden
original de `tick_once`.

Mejor estado del CPU micro con el timer actual:
- write t1: mts **33/34** (sólo rapid_toggle), blargg **11/15**.
- `QTR_CPU_WRITE_T3=1`: blargg **15/15**, mts **33/34**.

Siguiente: o portar el scheduler/timer/PPU completo de DocBoy, o aceptar
esas diferencias como deuda conocida y priorizar la PPU/m3 con el CPU
micro. La base por defecto sigue verde.

---

## 13. Hito: CPU + timer DocBoy verdes (T3)

El timer de DocBoy tickea en **T3**, no en T2. Con el CPU micro (write t1)
y el timer DocBoy (tick T3, `div16 += 4`) bajo `QTR_CPU_MICRO=1`:

- mts **34/34**
- blargg **15/15**
- `rapid_toggle`, `02-write_timing`, `03-modify_timing`, `instr_timing` PASS

Esto cierra la parte CPU/timer del pipeline DocBoy. La base por defecto
sigue con `Z80GB` + timer viejo.

Pendiente inmediato:

- `doctest` con `QTR_CPU_MICRO=1`: **439/489**; los 50 fallos son los tests
  del timer viejo (`test_timer.cpp`, `test_bus.cpp`) que asumen per-T. Hay
  que adaptarlos al modelo DocBoy.
- PPU/m3: frame m3 **en blanco** con CPU/timer nuevos. En el breakpoint:
  LCDC=0x91, BGP=0xFC, LY=0, mode=0, VRAM tile data no cero, tile map a
  cero, `nonwhite=0`. Es fase PPU/IRQ (LCDC/BGP/STAT/VBlank), no VRAM.
- PPU acceptance: **6/12**.

Siguiente: portar/alinear la fase PPU (IRQ STAT/VBlank incluidos) y adaptar
los tests del timer. La base por defecto permanece verde.

---

## 14. PPU/IRQ: breakpoint real y VBlank desplazado

- El frame m3 en blanco era un **breakpoint falso** en PC=0x60 (vector
  Joypad, byte 0x40). El runner de frame ahora ignora breakpoints con
  `state.pc < 0x0100`.
- Con el CPU micro el breakpoint real cae en ~23,47M T (base ~24,21M),
  pero el VBlank handler corre en **LY=0x40/mode 0** en vez de
  LY=0x90/mode 1. Frame parcial: 746 píxeles no blancos vs 16176 de la base.
- El PPU sí renderiza; el problema es la **fase IRQ VBlank/STAT** con el
  CPU/timer nuevos (dot de la IRQ, entrada del handler, `m_vblank_irq_dot`).

Siguiente: alinear la fase PPU/IRQ y luego recalibrar los m3. La base por
defecto sigue verde.

---

## 15. PPU/IRQ: STAT no se dispara con el CPU micro

Traza `CPUEVT` (`make clean && make QTR_IRQDBG=1`) sobre `m3_bgp_change`
con BIOS:

Base:
- VBlank IRQ dot=453 LY=144 mode=0.
- STAT mode 2 dot=1 LY=3; handler 0x62B en dot=34-38 LY=3/4.
- Handler VBlank 0x61D en dot=34 LY=144; breakpoint 0x626 en dot=74 LY=144.

CPU micro + timer DocBoy:
- VBlank IRQ sigue en dot=453 LY=144.
- **No hay PPUIRQ STAT** y no se entra a 0x62B/0x61D/0x626.
- Frame parcial en un 0x40 distinto (LY=0x40).

La escritura de STAT/LCDC con el CPU en t1 no habilita la IRQ STAT en el
modelo actual del PPU. El CPU/timer están bien (mts 34/34, blargg 15/15);
el siguiente bloque es alinear la fase PPU/IRQ (STAT/LCDC/update_stat_irq)
con el CPU/timer DocBoy. La base por defecto sigue verde.

---

## 16. PPU con CPU micro: 15 PASS y adaptador de fase

### Falso breakpoint resuelto

`CpuMicro::peek_next_opcode()` devolvía `m_io_data` con la lectura anterior
mientras el fetch estaba en vuelo. Si esa lectura era LY=0x40, el runner
creía ver el breakpoint `0x40` y dumpeaba un frame parcial. Ahora, con
`m_fetching` y lectura pendiente, lee `m_read_addr` (la dirección real del
opcode). El CPU micro llega al breakpoint real y el frame es completo.

### Adaptador de fase PPU/CPU (bajo `QTR_CPU_MICRO`)

El PPU estaba calibrado para escrituras del CPU viejo en T3; el CPU micro
escribe en T1. Se añadió:

- `LCD::apply_delayed_writes()` al final de `LCD::tick()`: retraso de 3 T
  para registros PPU (LCDC excluido para no romper el encendido).
- Retardos por registro: BGP/OBP0/SCX/SCY=4, resto=3, BGP en LY=0=8.
- `m_bgp_pre_tick` para conservar la ventana `BGP | last_BGP` de 1 T.
- Vista retardada de LCDC para el pipeline (histórico 4 T y retardos por
  bit: BG_EN/OBJ_EN/OBJ_SIZE=4; BG_MAP/TILE_DATA/WIN_EN/WIN_MAP=3).
- `tile_data` LY=0 retardo 5; `STAT OAM delay=0`; `WINWAIT_LY0=2`.
- Glitch de reactivación de ventana con WX retardado 2 T y activación
  WX=0 según `SCX & 7`.

Defaults bajo `#ifdef QTR_CPU_MICRO` en `core/src/ppu.cpp` y
`core/src/lcd.cpp`; las variables `QTR_*` siguen para calibrar.

### Números (build micro limpio, sin variables)

- mts 34/34
- blargg 15/15
- PPU acceptance 8/12
- doctest 437/489 (52 fallos de los tests del timer viejo)
- Harness PPU **15 PASS / 10 FAIL / 8 SKIP** (antes 1/24/8)

Fallan: bg_en 140, obj_en 16, obj_en_variant 112, obj_size 310,
obj_size_scx 190, tile_sel 124, win_en_multiple_wx 1406, scy 1039,
wx_0 263, wx_6 13809.

### Base por defecto

Sigue verde: doctest 102/102, PPU 12/12, mts 34/34, blargg 15/15,
harness 15/10/8. La calibración micro está aislada con `#ifdef`.

Backup: `tmp/20261115_233557/`.

Siguiente: para los diez m3 restantes hace falta muestrear cada registro
en la fase exacta del fetcher (no un retardo global), sobre todo en
ventana/SCY/wx_6. La alternativa limpia es portar el fetcher completo de
DocBoy, que ya está identificado en `tmp/full-fetcher-wip/`.

---

## 17. Port completo del PPU DocBoy: 25/0/8

### Integración

- `QTR_PPU_FULL=1` selecciona `PPUFull`, un wrapper sobre el PPU real de
  DocBoy (`core/src/docboy_ppu.cpp`, `core/inc/docboy/...`).
- `QTR_CPU_MICRO=1` activa `QTR_PPU_FULL=1` por defecto.
- La base sin flags mantiene `PPULegacy` y sigue verde.
- `QTR_PPU_FULL=0` construye el micro con el PPU legacy.

Adaptadores del wrapper:

- LCD → framebuffer y callback de frame.
- Interrupts → IT_VBLANK / IT_LCD_STAT.
- DMA → estado activo para OAM.
- VramBus/OamBus → VRAM/OAM y bloqueos del bus del CPU.
- El bus enruta FF40–FF4B por `read_lcd_register`/`write_lcd_register`.
- LY/STAT/registros se reflejan en el LCD de QtreGB para debug/GUI.

Se copiaron las utilidades mínimas de DocBoy (`utils/*`, `specs.h`,
`pixelmap.h`). El fichero `pixelmap.h` son las tablas DMG de expansión de
tiles.

### Resultados

Micro + PPU completo, con BIOS DMG:

- Harness: **25 PASS / 0 FAIL / 8 SKIP**.
- `dmg-acid2` + 24 `m3_*`: **diff 0**.
- PPU acceptance **12/12**.
- mts **34/34**, blargg **15/15**.
- doctest **89/89** (416 assertions).

Sin BIOS quedan fallos de fase; no es el camino objetivo. Los PNG
`_dmg_blob` y el objetivo se validan con BIOS.

Base por defecto: doctest 102/102, PPU 12/12, mts 34/34, blargg 15/15,
harness 15/10/8.

### Build y flags

```sh
make clean
make -C core QTR_CPU_MICRO=1 -j$(nproc)
make -C mts QTR_CPU_MICRO=1 -j4
make -C blargg QTR_CPU_MICRO=1 -j4
make -C tests QTR_CPU_MICRO=1 -j4
```

`make clean` es obligatorio al cambiar flags: los objetos no codifican el
flag y mezclar layouts provoca segfaults.

Tests del timer: la suite per-T queda bajo `#ifndef QTR_CPU_MICRO` y se
añaden tests del timer DocBoy bajo el flag.

Backup: `tmp/20261115_234441/`.

---

## 18. v4 — baseline micro + PPU DocBoy y plan de PPU nativo (2026-11-16)

### Baseline correcto

El baseline de v4 **no es el PPU legacy**. Es el pipeline que ya funciona:

- `QTR_CPU_MICRO=1` → `CpuMicro` + timer DocBoy + `QTR_PPU_FULL=1`
- PPU DocBoy (wrapper `PPUFull` sobre `docboy_ppu.cpp`)

Verificado en `/home/pedro/projects/QtreGB/.wip/ppu-v4`:

- harness **25 PASS / 0 FAIL / 8 SKIP** con BIOS
- PPU acceptance **12/12**
- mts **34/34**
- blargg **15/15**
- doctest **89/89** (416 assertions)

No se debe volver al baseline legacy para trabajar en el PPU. El PPU
legacy sigue disponible, pero no es el punto de partida.

### Timer: nativo, no copia

La rama `QTR_CPU_MICRO` de `core/src/timer.cpp` es una implementación
nativa de QtreGB, no una copia de DocBoy. Usa:

- `set_div()`, `inc_tima()`, `on_falling_edge_inc_tima()`
- `handle_pending_tima_reload()`
- `m_timer_state`, `m_last_div_signal`

DocBoy se usó para estudiar la semántica (tick en T3, reload en dos
pasos, edges de DIV/TAC), pero el código es propio. **No hay un port de
timer pendiente**; ya es nativo y está validado por mts/blargg y por los
tests de timer bajo `QTR_CPU_MICRO`.

### PPU nativo: estrategia

El `PPUv4` nativo anterior (máquina de estados + BG/ventana simplificados)
no pasó el harness completo y se archivó en:

- `tmp/native_ppu_wip_20261116_020615/`

No es el baseline. A partir de ahora:

1. Mantener micro + PPU DocBoy verde como base.
2. Sustituir el PPU DocBoy por código nativo **pieza a pieza**, validando
   contra 25/0/8 en cada paso.
3. Orden previsto:
   - fetcher BG/ventana exacto (prefetcher + pixel-slice + FIFO),
   - ventana/glitches (`wx_0`, `multiple_wx`, `wx_6`),
   - OBJ prefetcher/FIFO y mezcla,
   - registros/STAT y edges.
4. No eliminar la copia de DocBoy hasta que la versión nativa alcance
   paridad.

Trazas de referencia ya medidas (BIOS, `m3_bgp_change`):

- modo 3 en dot 79, modo 0 en 251, modo 2 en 455;
- pulso OAM STAT en dot 452;
- primer tile BG MAP/LOW/HIGH en 83/85/87;
- segundo MAP en 91.

### Build

```sh
make clean
make -C core QTR_CPU_MICRO=1 -j$(nproc)
make -C mts QTR_CPU_MICRO=1 -j4
make -C blargg QTR_CPU_MICRO=1 -j4
make -C tests QTR_CPU_MICRO=1 -j4
```

`make clean` es obligatorio al cambiar flags: los objetos no codifican
los flags y un build mezclado produce resultados falsos o segfaults.

### Backups

- Baseline/oráculo: tag `ppu-oracle-25-0-8` (worktree `.wip/ppu`).
- Backup de esta sesión: `tmp/20261116_021319/`.
- WIP nativo archivado: `tmp/native_ppu_wip_20261116_020615/`.

---

## 19. v4 — PPU nativo `PPUv4` en paridad (2026-11-16, continuación)

El PPU nativo detrás de `QTR_PPU_V4=1` sustituye ya al PPU DocBoy con
paridad total en DMG:

| Suite | Resultado |
|---|---|
| harness mealybug + dmg-acid2 (BIOS) | 25 PASS / 0 FAIL / 8 SKIP |
| PPU acceptance | 12/12 |
| mts | 34/34 |
| blargg | 15/15 |
| doctest | 89/89 (416 assertions) |

### Cómo se llegó

- Hito 1: scaffold por dot + OAM/STAT/LYC (acceptance 11/12, harness 1/24/8).
- Hito 2: fetcher BG/ventana completo (harness 10/15/8).
- Hito 3: fetcher OBJ completo → harness 25/0/8 y acceptance 12/12.

La clave del hito 3 fue que los pops del BG FIFO se bloquean mientras haya
entradas OAM pendientes en el LX actual y `LCDC.OBJ_ENABLE=1`
(`is_bg_fifo_ready_to_be_popped()`), lo que sincroniza el pipeline BG con
el oráculo; además el fetcher OBJ (OAM, bitplanes, flip, 8x16, merge con
prioridad, interrupción/caché del fetch BG) y la selección OBJ vs BG con
`OBP0/OBP1`, opacidad y `BG_OVER_OBJ`.

### Selección de PPU

- `QTR_PPU_V4=1` → `PPUv4` y **no** `QTR_PPU_FULL`.
- `QTR_CPU_MICRO=1` → `QTR_PPU_FULL ?= 1` solo si no se pide V4.
- El oráculo DocBoy (`PPUFull` + `docboy_ppu.cpp`) se conserva intacto.

### Estado

El PPU nativo es el camino validado; queda pendiente decidir cuándo se
retira el oráculo DocBoy del árbol. No hay commits en esta sesión.

Backups: `tmp/20261116_124648/` (inicio) y `tmp/*.pre-trace`.

---

## 20. PROMOCIÓN A IMPLEMENTACIÓN FINAL (2026-11-16)

`CpuMicro` es ya el único CPU: `QtreGB` lo instancia incondicionalmente y se
han eliminado `Z80GB`, `operations.*` y `tests/test_cpu.cpp`. El timer se
queda con la implementación DocBoy-style que acompaña al CPU de micro-ops; la
rama legacy de `timer.cpp` y sus tests también se retiraron.

`common.mk` ya no define `QTR_CPU_MICRO`, `QTR_PPU_V4` ni `QTR_PPU_FULL`; el
flag de diagnóstico `QTR_CPU_WRITE_T3` también se eliminó dejando el
comportamiento por defecto (flush de escritura en T1).

### Validación final sin flags

| Suite | Resultado |
|---|---|
| doctest | 80/80 (386 assertions) |
| PPU acceptance | 12/12 |
| mts | 34/34 |
| blargg | 15/15 |
| harness (BIOS) | 25 PASS / 0 FAIL / 8 SKIP |
