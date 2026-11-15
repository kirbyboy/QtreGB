# Plan completo: Port de PPU DocBoy a QtreGB

## Objetivo
Portar la máquina de estados por dot de DocBoy (ppu.md §20) a QtreGB, reemplazando
la pipeline heurística actual por la máquina de estados exacta de DocBoy.

## Estado actual (lo que ya está hecho)
- Pipeline por fases (Dummy/Discard/Visible) con LX por pixel.
- Activación de ventana DocBoy (LX + last_wx/last_lcdc).
- STAT/LYC con last_ly/last_lyc.
- HBlank/VBlank por dot (estructura + STAT IRQ timing).
- Infraestructura last_* sincronizada.
- Tick selector (m_tick_selector) para despacho por dot.
- Fetch-ahead inherente (un fetch stage por dot, decoupled via FIFO).
- warmup_pos eliminado (sprite due tracking usa m_lx).
- Paso 1 del handoff (late_trigger sin gate warm-up).

## Plan de migración (pasos compilables, sin tests hasta que el usuario lo indique)

### Paso 1 — Infraestructura OAM por dot (1-2 días)
- [ ] 1.1: Añadir `oam.acquire()` / `oam.release()` (OAM bus locking) — mapear a `m_oam_read_locked`/`m_oam_write_locked`.
- [ ] 1.2: Añadir `oam_scan_read_request()` / `oam_scan_flush_read_request()` — leer primer OAM entry en dot 453.
- [ ] 1.3: Añadir `begin_increase_ly()` / `end_increase_ly()` (DMG: no-op / ++ly) — ya tenemos `m_lyc_update_countdown`.
- [ ] 1.4: Añadir `enable_lyc_eq_ly_irq` flag — ya implementado.
- [ ] 1.5: Añadir `update_mode<OAM_SCAN>()` — cambiar STAT mode a OAM en dot 454.

### Paso 2 — HBlank timing exacto (1-2 días)
- [ ] 2.1: `hblank_452`: raise OAM STAT (ya), begin_increase_ly (no-op DMG).
- [ ] 2.2: `hblank_453`: OAM acquire, read first OAM entry, disable LYC IRQ, end_increase_ly (increment LY) — parcialmente hecho.
- [ ] 2.3: `hblank_454`: flush read, re-enable LYC IRQ, update_mode<OAM_SCAN>, update_stat_irq.
- [ ] 2.4: `hblank_455`: oam scan read request, dots=0, enter_oam_scan.
- [ ] 2.5: `hblank` (fallback): corre dots 0-451, transiciona a hblank_452 en 452.
- [ ] 2.6: `hblank_last_line`, `hblank_last_line_453/454/455`.
- [ ] 2.7: `hblank_first_line_after_turn_on` (lcdon timing).

### Paso 3 — VBlank timing exacto (1-2 días)
- [ ] 3.1: `vblank()` corre dots 0-452, transiciona a vblank_453 en 453.
- [ ] 3.2: `vblank_453`: begin_increase_ly, disable LYC IRQ.
- [ ] 3.3: `vblank_454`: end_increase_ly, re-enable LYC IRQ, update_stat_irq.
- [ ] 3.4: `vblank_455`: dots=0, transition to vblank/vblank_last_line.
- [ ] 3.5: `vblank_last_line`, `vblank_last_line_2/3/4/7/453/454/455`.

### Paso 4 — OAM scan por dot (1 día)
- [ ] 4.1: `oam_scan_even` / `oam_scan_odd` / `oam_scan_77` / `oam_scan_done`.
- [ ] 4.2: `oam_scan_after_turn_on` / `78` / `79`.
- [ ] 4.3: Integrar OAM scan por dot en enter_oam_scan.

### Paso 5 — Pixel transfer por dot (2-3 días)
- [ ] 5.1: `enter_pixel_transfer`: reset fetcher, acquire vram/oam, reset_row_cursor.
- [ ] 5.2: `pixel_transfer_dummy_lx0`: dummy fetch 3 dots, check_window_activation si discard>0.
- [ ] 5.3: `pixel_transfer_discard_lx0`: discard (apply_window_activation, apply_fill, pop, fetcher_tick, push_stage).
- [ ] 5.4: `pixel_transfer_discard_lx0_wx0_scx`: caso WX=0 + SCX.
- [ ] 5.5: `pixel_transfer_lx0`: visible (apply_window_activation, apply_fill, pop, push si lx>=8, check_window_activation, lx++).
- [ ] 5.6: `pixel_transfer_lx8`: push visible (lx>=8).
- [ ] 5.7: `handle_window_disabled_glitch` (DMG: no-op).
- [ ] 5.8: Eliminar pipeline_push_pixel (código muerto).

### Paso 6 — Activación de ventana DocBoy (1-2 días)
- [ ] 6.1: `frame_latch`: window.frame_latch = frame_latch or (lcdc.win_enable and ly == wy).
- [ ] 6.2: `check_window_activation()`: condición DMG (lx == last_wx) or (lx == last_wx + 1 and !last_lcdc.win_enable).
- [ ] 6.3: `apply_window_activation()`: clear FIFO, active=true, tile_x=0.
- [ ] 6.4: `setup_fetcher_for_window()`: reset fetcher for window.
- [ ] 6.5: `activate_window()`: window.activating = true, line_counter++.

### Paso 7 — Sprite fetch DocBoy (2-3 días)
- [ ] 7.1: Eliminar warmup_pos del due de sprites.
- [ ] 7.2: Reemplazar x_for_object_match con modelo DocBoy (fetch por LX).
- [ ] 7.3: Integrar obj_prefetcher en la máquina de estados.
- [ ] 7.4: Eliminar m_warming.

### Paso 8 — Fetch-ahead real (2-3 días)
- [ ] 8.1: Refactor fetcher a push-based.
- [ ] 8.2: Dos pipelines independientes (fetch + pop).
- [ ] 8.3: Validar.

### Paso 9 — HBlank first line after turn on (1 día)
- [ ] 9.1: `hblank_first_line_after_turn_on`.

### Paso 10 — Validación (1 día)
- [ ] 10.1: DocBoy headless.
- [ ] 10.2: Harness (cuando el usuario lo indique).

## Reglas de ejecución
1. Cada paso debe compilar antes de pasar al siguiente.
2. No se revierten por regresiones de diff, solo por errores de compilación.
3. No se ejecutan tests ni harness hasta que el usuario lo indique.
4. Cada paso se documenta en el handoff.
5. Si un paso es muy grande, se divide en sub-pasos compilables.

## Checkpoint actual
Plan creado. Fases 1-6 parcialmente completadas (pipeline por fases, LX, activación, STAT/LYC, HBlank/VBlank scaffolding, fetch-ahead N=1).
EOF