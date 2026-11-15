# Plan: Refactor del fetcher core (push-based, fetch-ahead real)

## Objetivo
Cambiar el fetcher de secuencial (un estado por tick, pop dispara fetch) a push-based
con fetch-ahead real (fetch corre libre, rellena FIFO, pop consume). Dos pipelines
independientes: fetch (corre N ticks por delante) y pop (consume de la FIFO).

## Estado actual del fetcher
- `Fetcher::tick()`: avanza un estado del fetch (FS_TILE_T1 → FS_TILE_T2 → FS_DATA0_T1 → ... → FS_PUSH).
- `push_data_background()` / `push_data_window()`: empujan 8 píxeles a la FIFO cuando el fetch completa.
- `pop_pixel()`: consume de la FIFO (con discard, win_fine_offset, etc.).
- `sm_bg()`: ejecuta el fetch state machine (llamado dentro de tick()).
- Buffers separados: `bgw_fifo`, `spr_fifo`.
- `tick_checks()` / `tick_fetch()` separados (fetch-ahead de N ticks ya implementado).

## Problema
El fetcher es **pull-based**: el pop dispara el fetch (el fetcher solo avanza cuando el PPU hace tick()). El fetch-ahead real requiere que el fetch corra **libremente** (push-based), llenando la FIFO mientras el PPU hace pop.

## Plan de migración (checkpoints compilables)

### Fase A — Separar fetch de checks (COMPLETADA)
- [x] A1: Añadir `tick_checks()` (sprite/window checks, una vez por dot).
- [x] A2: Añadir `tick_fetch()` (fetch state machine, sm_bg).
- [x] A3: En `ppu_mode_xfer`, llamar `tick_checks()` ×1 + `tick_fetch()` ×N.
- [x] A4: Verificar que compila.

### Fase B — Fetch-ahead configurable (COMPLETADA)
- [x] B1: N=1, 3, 6 implementado (tick_fetch ×N).
- [x] B2: Verificar que compila.

### Fase C — Push-based fetch (3-5 días)
- [ ] C1: Crear `Fetcher::fetch_bg()` (corre el fetch state machine hasta push, sin consumir FIFO).
- [ ] C2: Crear `Fetcher::fetch_window()` (igual para window).
- [ ] C3: Modificar `sm_bg()` para llamar a `fetch_bg()` en lugar de avanzar un estado por tick.
- [ ] C4: El fetch corre libremente: `fetch_bg()` avanza hasta que la FIFO está llena o no hay más datos.
- [ ] C5: Verificar que compila y que el comportamiento no cambia (N=1).

### Fase D — Fetch-ahead real (2-3 días)
- [ ] D1: El PPU llama `tick_checks()` una vez + `fetch_bg()` (corre libre) por dot.
- [ ] D2: El pop (`pop_pixel`) consume de la FIFO sin afectar el fetch.
- [ ] D3: El fetch-ahead es natural: `fetch_bg()` llena la FIFO mientras `pop_pixel` consume.
- [ ] D4: Validar con DocBoy headless.

### Fase E — Integración con máquina de estados (2-3 días)
- [ ] E1: Migrar fetch-ahead a HBlank/VBlank (fetch prepara la siguiente línea).
- [ ] E2: Eliminar `pipeline_process()` (ya no se necesita).
- [ ] E3: Validar con harness.

## Notas
- El fetch-ahead real requiere que el fetcher sea **push-based**: el fetch corre libremente y empuja a la FIFO cuando completa.
- La arquitectura actual (tick → push en el mismo dot) no permite solapamiento real sin cambiar el modelo.
- Cada fase debe compilar antes de pasar a la siguiente.
- No se ejecutan tests ni harness hasta que el usuario lo indique.
- Si una fase es muy grande, se acota (ej: solo implementar fetch_bg para BG, no para window/sprites).

## Regla de compilación
Cada checkpoint debe compilar. Si un paso es muy grande, se divide en sub-pasos compilables.

## Checkpoint actual
Fase A y B completadas. Fase C pendiente.
EOF