# Plan: Port del sprite fetch DocBoy (modelo paralelo)

## Objetivo
Reemplazar el modelo secuencial de sprite fetch (check_obj + push_data_sprite) por el
modelo paralelo de DocBoy (obj_prefetcher + obj_fifo merge), eliminando m_warming.

## Estado actual del sprite fetch (QtreGB)
- `check_obj(x)`: se llama 1 vez por tick. Un sprite es "due" cuando `x == OAM.X - 8` (mod 256). Si due, marca entry.m_marked=true, cambia mode a FS_OBJ, fill_map_sprite, y retorna (fetch empieza).
- `sm_bg()` (en FS_OBJ mode): catch-up (avanza BG fetcher hasta FIFO no vacío), luego fixed_dots (6 dots) de fetch de sprite data. Al final: load_data_sprite(0/1), push_data_sprite, mode → FS_BGD.
- `push_data_sprite()`: lee tile data, stampa en obj_fifo (merge con priority), marca entry.m_done.
- `pop_pixel()`: consume de spr_fifo en sync con bgw_fifo.

## Modelo DocBoy (paralelo)
- `obj_prefetcher_get_tile_0/1`: fetcha tile map entry para el sprite (paralelo con BG).
- `obj_pixel_slice_fetcher_get_tile_data_low/high_0/1`: fetcha tile data (paralelo).
- `obj_pixel_slice_fetcher_get_tile_data_high_1_and_merge_with_obj_fifo`: merge en obj_fifo.
- El prefetcher corre en paralelo con el BG fetcher, avanzando cuando hay "room" (FIFO not full, no sprite fetch in progress).

## Plan de migración (pasos compilables)

### Fase S1 — Preparación (1 día)
- [ ] S1.1: Añadir `m_obj_fifo` (ya existe como `spr_fifo` en fetcher). Verificar que es un FIFO de FIFOElement.
- [ ] S1.2: Añadir estado `m_obj_prefetcher_state` (idle/fetching) al fetcher.
- [ ] S1.3: Añadir `m_obj_tile_data_lo/hi` para almacenar datos del sprite en fetch.
- [ ] S1.4: Verificar que compila.

### Fase S2 — Prefetcher de tile map (2-3 días)
- [ ] S2.1: Implementar `obj_prefetcher_get_tile_0()`: lee OAM tile number del sprite due.
- [ ] S2.2: Implementar `obj_prefetcher_get_tile_1()`: lee atributos (flags, palette).
- [ ] S2.3: Determinar cuándo un sprite es "due" (LX en rango X-8..X).
- [ ] S2.4: Transición: cuando tile map está listo, pasar a fetch tile data.
- [ ] S2.5: Verificar que compila.

### Fase S3 — Prefetcher de tile data (2-3 días)
- [ ] S3.1: Implementar `obj_pixel_slice_fetcher_get_tile_data_low_0/1()`: lee bitplane low.
- [ ] S3.2: Implementar `obj_pixel_slice_fetcher_get_tile_data_high_0/1()`: lee bitplane high.
- [ ] S3.3: Implementar `obj_pixel_slice_fetcher_get_tile_data_high_1_and_merge_with_obj_fifo()`: merge en obj_fifo.
- [ ] S3.4: Verificar que compila.

### Fase S4 — Integración con fetcher (2-3 días)
- [ ] S4.1: Reemplazar `check_obj()` + `push_data_sprite()` con el prefetcher.
- [ ] S4.2: Eliminar `m_obj_in_fixed`, `m_obj_fixed_left`, `m_current_oam_entry` (ya no se necesitan).
- [ ] S4.3: Eliminar `m_warming` (reemplazar con `m_lx < 8` o phase check).
- [ ] S4.4: Eliminar `warmup_pos` (ya eliminado).
- [ ] S4.5: Verificar que compila.

### Fase S5 — Integración con PPU (1-2 días)
- [ ] S5.1: Migrar `tick()` a usar el prefetcher.
- [ ] S5.2: Migrar `sm_bg()` para manejar sprites en paralelo.
- [ ] S5.3: Verificar que compila.

### Fase S6 — Validación (1 día)
- [ ] S6.1: Build limpio.
- [ ] S6.2: DocBoy headless (cuando el usuario lo indique).
- [ ] S6.3: Harness (cuando el usuario lo indique).

## Reglas de ejecución
1. Cada sub-paso debe compilar antes de pasar al siguiente (si es posible).
2. Si un sub-paso es muy grande, se divide en sub-sub-pasos compilables.
3. No se ejecutan tests ni harness hasta que el usuario lo indique.
4. Cada sub-paso se documenta en el handoff.
5. Si la compilación falla, se corrige antes de continuar.

## Checkpoint actual
Plan creado. Fase S1 pendiente.
EOF