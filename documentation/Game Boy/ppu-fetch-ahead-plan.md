# Plan: Fetch-ahead real (solapar fetch con pop)

## Objetivo
Cambiar el fetcher de secuencial (un estado por tick, pop dispara fetch) a push-based
con fetch-ahead (fetch corre libre, rellena FIFO, pop consume). Dos pipelines
independientes: fetch (corre N ticks por delante) y pop (consume de la FIFO).

## Estado actual del fetcher
- `Fetcher::tick()`: avanza un estado del fetch (FS_TILE_T1 → FS_TILE_T2 → FS_DATA0_T1 → ... → FS_PUSH).
- `push_data_background()` / `push_data_window()`: empujan 8 píxeles a la FIFO cuando el fetch completa.
- `pop_pixel()`: consume de la FIFO (con discard, win_fine_offset, etc.).
- `sm_bg()`: ejecuta el fetch state machine (llamado dentro de tick()).
- Buffers separados: `bgw_fifo`, `spr_fifo`.

## Problema
El fetcher es **pull-based**: el pop dispara el fetch (el fetcher solo avanza cuando el PPU hace tick()). El fetch-ahead requiere que el fetch corra **libremente** (push-based), llenando la FIFO mientras el PPU hace pop.

## Plan de migración (checkpoints compilables)

### Fase A — Infraestructura de fetch-ahead (1-2 días)
- [ ] A1: Añadir `m_fetch_ahead_depth` (N ticks de adelanto) al fetcher.
- [ ] A2: Crear `Fetcher::tick_fetch()` (avanza fetch sin pop) y `Fetcher::tick_pop()` (consume sin fetch).
- [ ] A3: En `ppu_mode_xfer`, llamar `tick_fetch()` N veces + `tick_pop()` una vez por dot.
- [ ] A4: Verificar que compila y que el comportamiento no cambia (N=1 = actual).

### Fase B — Fetch-ahead de 1 tick (2-3 días)
- [ ] B1: Implementar `tick_fetch()` que avanza el fetch state machine sin consumir FIFO.
- [ ] B2: Implementar `tick_pop()` que consume de la FIFO (pop_pixel) sin avanzar fetch.
- [ ] B3: Separar `sm_bg()` (fetch) de `pop_pixel()` (pop) en funciones independientes.
- [ ] B4: Verificar que N=1 produce el mismo resultado que antes.

### Fase C — Fetch-ahead de N ticks (3-5 días)
- [ ] C1: Aumentar N a 2, 3, 4 y medir diffs.
- [ ] C2: Ajustar N para minimizar diffs.
- [ ] C3: Validar con DocBoy headless.

### Fase D — Integración con máquina de estados (2-3 días)
- [ ] D1: Migrar fetch-ahead a la máquina de estados por dot (hblank/vblank).
- [ ] D2: Eliminar `pipeline_process()` (ya no se necesita).
- [ ] D3: Validar con harness.

## Notas
- El fetch-ahead real requiere que el fetcher sea **push-based**: el fetch corre libremente y empuja a la FIFO cuando completa. El PPU solo consume.
- La arquitectura actual (tick → push en el mismo dot) no permite solapamiento sin cambiar el modelo.
- Cada fase debe compilar antes de pasar a la siguiente.
- No se ejecutan tests ni harness hasta que el usuario lo indique.
- Si una fase es muy grande, se acota (ej: solo implementar tick_fetch para BG, no para sprites).

## Regla de compilación
Cada checkpoint debe compilar. Si un paso es muy grande, se divide en sub-pasos compilables.

## Checkpoint actual
Plan creado. No se ha empezado la implementación.
EOF