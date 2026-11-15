# Plan de continuación — Port PPU DocBoy a QtreGB

## Objetivo
Completar el port de la máquina de estados por dot de DocBoy para resolver los diffs
restantes (window_timing 56 px, wx_0 18 px) y los 24 ROMs fallidos.

## Estado actual (lo que ya está)
- Pipeline por fases (Dummy/Discard/Visible) con LX por pixel.
- Activación de ventana DocBoy (LX + last_wx/last_lcdc).
- STAT/LYC con last_ly/last_lyc.
- HBlank/VBlank por dot (estructura + STAT IRQ timing + OAM acquire/locks).
- Infraestructura last_* sincronizada.
- Tick selector para despacho por dot.
- Fetch-ahead inherente (push-based via FIFO).
- warmup_pos eliminado (sprite due tracking usa m_lx).
- OAM scan por dot (enter_oam_scan con reset completo).
- HBlank última línea (hblank_last_line_452-455).
- VBlank última línea (vblank_last_line_2-4/7/453-455 + enter_new_frame).

## Plan de continuación (pasos compilables)

### Paso 1 — Timing detallado HBlank/VBlank (1-2 días)
**Objetivo**: portar los detalles de timing que faltan en HBlank/VBlank.

1.1. **OAM acquire/read request en 453-454**:
    - `oam.acquire()` en hblank_453 (ya: m_oam_read_locked = true).
    - `oam_scan_read_request()` en hblank_453 (ya: lee m_oam_ram[0].m_y).
    - `oam_scan_flush_read_request()` en hblank_454 (flush del read request).
    - Impacto: OAM scan empieza un dot antes (453 vs 455).

1.2. **update_mode<OAM_SCAN> en 454**:
    - STAT mode cambia a OAM en dot 454 (ya implementado: `m_lcd->set_lcds_mode(MODE_OAM)` en hblank_454).
    - Verificar que update_stat_irq se llama correctamente.

1.3. **begin_increase_ly / end_increase_ly con delay de 2 dots**:
    - DocBoy: begin_increase_ly en 452 (DMG: no-op), end_increase_ly en 453 (++ly).
    - Nuestro código ya incrementa LY en hblank_453. OK.
    - El delay de la bandera LYC (4/2 dots) ya está implementado (m_lyc_update_countdown).

1.4. **hblank_first_line_after_turn_on** (lcdon timing):
    - Ya manejado en ppu_lcd_on_line() (m_lcd_on_line == 0).
    - No requiere cambios (es un path separado).

1.5. **vblank_last_line timing detallado**:
    - vblank_last_line_453: OAM acquire, read first OAM entry, mode OAM.
    - vblank_last_line_454: flush read.
    - vblank_last_line_455: enter_new_frame.
    - Ya implementado parcialmente. Falta: OAM read request en 453.

### Paso 2 — Sprite fetch DocBoy (2-3 días)
**Objetivo**: eliminar m_warming del due de sprites, reemplazar x_for_object_match con modelo DocBoy.

2.1. **Analizar modelo DocBoy de sprites**:
    - OAM scan recoge hasta 10 sprites por línea (ya implementado).
    - Durante pixel transfer, sprite se vuelve "due" cuando su X está dentro del rango de fetch.
    - DocBoy usa obj_prefetcher que corre en paralelo con bg_prefetcher.

2.2. **Reemplazar check_obj con modelo DocBoy**:
    - Actual: check_obj(x) se llama con x = position. Sprite due when position == OAM.X - 8.
    - DocBoy: sprite due when LX está dentro del rango (X-8 .. X).
    - Implementar: en tick_checks, para cada sprite en OAM scan entries, si LX >= OAM.X - 8 && LX < OAM.X, fetch sprite.

2.3. **Eliminar m_warming**:
    - Reemplazar `if(m_warming)` con `if(m_lx < 8)` (dummy/discard phase) o `if(m_phase != Visible)`.
    - El fetcher necesita saber la fase. Opción: pasar m_phase a fetcher via setter, o derivar de m_current_x.

2.4. **Validar**: sprites en dmg-acid2 y m3_*_sprites.

### Paso 3 — Máquina de estados por dot completa (3-5 días)
**Objetivo**: portar oam_scan_even/odd/77/done y pixel_transfer_discard_lx0_wx0_scx.

3.1. **OAM scan por dot con tick_selector**:
    - Reemplazar ppu_mode_oam (llamado cada tick) por oam_scan_even/odd via tick_selector.
    - oam_scan_even: lee Y, incrementa entry, transiciona a odd.
    - oam_scan_odd: lee X, filtra sprite, transiciona a even.
    - oam_scan_77: manejo especial en dot 77.
    - oam_scan_done: sort, transition to pixel transfer.

3.2. **pixel_transfer_discard_lx0_wx0_scx**: caso WX=0 + SCX (one-dot-earlier fetch).

3.3. **enter_hblank/enter_vblank con timing exacto**:
    - enter_hblank: mode HBLANK, tick_selector = hblank.
    - enter_vblank: mode VBLANK, tick_selector = vblank.

### Paso 4 — Fetch-ahead real (2-3 días)
**Objetivo**: refactor del fetcher a push-based con dos pipelines independientes.

4.1. **Push-based fetch**: fetch_bg() corre libremente, empuja a FIFO cuando completa.
4.2. **Dos pipelines**: fetch (corre N ticks por delante) + pop (consume de FIFO).
4.3. **Integración con máquina de estados**: fetch prepara la siguiente línea durante HBlank/VBlank.

### Paso 5 — Validación (1 día)
5.1. DocBoy headless.
5.2. Harness (cuando el usuario lo indique).

## Reglas de ejecución
1. Cada paso debe compilar antes de pasar al siguiente.
2. No se revierten por regresiones de diff, solo por errores de compilación.
3. No se ejecutan tests ni harness hasta que el usuario lo indique.
4. Cada paso se documenta en el handoff.
5. Si un paso es muy grande, se divide en sub-pasos compilables.

## Checkpoint actual
Plan creado. Fases 1-6 parcialmente completadas (pipeline por fases, LX, activación, STAT/LYC, HBlank/VBlank scaffolding, fetch-ahead N=1).