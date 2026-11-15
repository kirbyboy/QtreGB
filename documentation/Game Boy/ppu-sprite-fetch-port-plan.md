# Plan: Port del sprite fetch DocBoy (modelo paralelo)

> Referencia del proyecto: **DocBoy**. SameBoy solo se usa como contraste;
> DocBoy reproduce el PNG esperado con `--delay 70224`.

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

## Plan de migración (pasos, puede no compilar hasta completarse)

### Fase S1 — Infraestructura (COMPLETADA)
- [x] S1.1: Añadir `m_obj_prefetch_state` (enum ObjPrefetchState) al fetcher.
- [x] S1.2: Añadir `m_obj_tile_data_lo/hi`, `m_obj_tile_number`, `m_obj_flags`, `m_obj_palette`, `m_obj_priority`, `m_obj_x`.
- [x] S1.3: Inicializar en `reset()`.

### Fase S2 — Reemplazar check_obj con prefetcher-based due (1-2 días)
- [ ] S2.1: Crear `obj_prefetcher_due()` que determina qué sprites son "due" basado en LX (LX en rango X-8..X).
- [ ] S2.2: Modificar `tick()` para llamar `obj_prefetcher_due()` en lugar de `check_obj(x)`.
- [ ] S2.3: Eliminar `x_for_object_match` (enfoque SameBoy descartado) y `m_warming` branch.
- [ ] S2.4: Compilar.

### Fase S3 — Implementar prefetcher states (2-3 días)
- [ ] S3.1: `obj_prefetcher_get_tile_0()`: leer tile number del sprite due.
- [ ] S3.2: `obj_prefetcher_get_tile_1()`: leer flags (palette, priority, flip).
- [ ] S3.3: `obj_pixel_slice_fetcher_get_tile_data_low_0/1()`: leer bitplane low.
- [ ] S3.4: `obj_pixel_slice_fetcher_get_tile_data_high_0/1()`: leer bitplane high.
- [ ] S3.5: `obj_pixel_slice_fetcher_get_tile_data_high_1_and_merge_with_obj_fifo()`: merge en obj_fifo.
- [ ] S3.6: Compilar.

### Fase S4 — Integrar prefetcher con fetch state machine (2-3 días)
- [ ] S4.1: Modificar `sm_bg()` para intercalar BG y OBJ fetches (paralelo).
- [ ] S4.2: Eliminar `m_obj_in_fixed`, `m_obj_fixed_left`, `m_current_oam_entry`.
- [ ] S4.3: Eliminar `m_mode == FS_OBJ` (ya no se necesita, prefetcher es paralelo).
- [ ] S4.4: Compilar.

### Fase S5 — Eliminar m_warming (1 día)
- [ ] S5.1: Reemplazar `if(m_warming)` con `if(m_lx < 8)` o phase check.
- [ ] S5.2: Eliminar `m_warming` member y `set_warming()`.
- [ ] S5.3: Actualizar PPU (eliminar `set_warming` calls).
- [ ] S5.4: Compilar.

### Fase S6 — Validación (1 día)
- [ ] S6.1: Build limpio.
- [ ] S6.2: DocBoy headless (cuando el usuario lo indique).
- [ ] S6.3: Harness (cuando el usuario lo indique).

## Reglas de ejecución
1. Cada paso debe compilar antes de pasar al siguiente (si es posible).
2. Si un paso es muy grande, se divide en sub-pasos compilables.
3. No se ejecutan tests ni harness hasta que el usuario lo indique.
4. Cada paso se documenta en el handoff.
5. Si la compilación falla, se corrige antes de continuar.

## Checkpoint actual
Fase S1 completada. Fase S2 pendiente.