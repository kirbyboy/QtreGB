# Plan de port de la PPU DocBoy a QtreGB

## Objetivo
Portar la máquina de estados por dot de DocBoy (ppu.md §20) a QtreGB, reemplazando
la pipeline heurística actual (warm-up + anchor_dot + pixel0_dot) por la máquina de
estados exacta de DocBoy.

## Fases del plan (cada una es un checkpoint compilable)

### Fase 0 — Infraestructura (YA COMPLETADA)
- [x] `last_*` latched en `ppu_tick()`.
- [x] `IFetcherRegs` con `get_last_*`.
- [x] `PpuPhase` enum (Dummy, Discard, Visible) en PPU.
- [x] `m_tick_selector` (puntero a función miembro) en PPU.
- [x] `m_lx` (LX por pixel) en PPU y fetcher.
- [x] `m_enable_lyc_eq_ly_irq` flag.
- [x] `enter_pixel_transfer`, `pixel_transfer_dummy_lx0`, `pixel_transfer_discard_lx0`, `pixel_transfer_lx0`, `pixel_transfer_lx8`, `enter_hblank`, `enter_vblank`, `hblank_452-455`, `enter_oam_scan`, `vblank_453-455` declarados.

### Fase 1 — Pixel transfer (COMPLETADA parcialmente)
- [x] `enter_pixel_transfer`: reset fetcher, set mode, tick_selector = dummy_lx0.
- [x] `pixel_transfer_dummy_lx0`: 3 dots dummy, transición a discard/visible.
- [x] `pixel_transfer_discard_lx0`: discard hasta consumo, transición a visible.
- [x] `pixel_transfer_lx0`: visible (pop, LX++, push si LX>=8, activación, hblank a 168).
- [x] `pixel_transfer_lx8`: push visible (LX>=8).
- [ ] `pixel_transfer_discard_lx0_wx0_scx`: caso WX=0 + SCX.

### Fase 2 — HBlank por dot
- [x] `hblank_452`: raise OAM STAT, begin_increase_ly (DMG: no-op).
- [x] `hblank_453`: disable LYC IRQ, end_increase_ly (increment LY), window line counter.
- [x] `hblank_454`: re-enable LYC IRQ, update_stat_irq.
- [x] `hblank_455`: transición a enter_oam_scan.
- [ ] `hblank` (fallback para non-last lines).
- [ ] `hblank_last_line`, `hblank_last_line_453/454/455`.

### Fase 3 — VBlank por dot
- [ ] `vblank_453/454/455` con STAT IRQ timing.
- [ ] `vblank_last_line`, `vblank_last_line_2/3/4/7/453/454/455`.
- [ ] `vblank` (fallback).
- [ ] `vblank_first_line_after_turn_on` (lcdon timing).

### Fase 4 — OAM scan por dot
- [ ] `oam_scan_even` / `oam_scan_odd` / `oam_scan_77`.
- [ ] `oam_scan_done`.
- [ ] `oam_scan_after_turn_on` / `78` / `79`.
- [ ] `enter_oam_scan` completo (ya parcial).

### Fase 5 — Sprite fetch DocBoy
- [ ] Eliminar `warmup_pos` del due de sprites.
- [ ] Reemplazar `x_for_object_match` con modelo DocBoy (fetch por LX).

### Fase 6 — Fetch-ahead real
- [ ] Refactor fetcher a push-based con dos pipelines.

### Fase 7 — HBlank first line after turn on
- [ ] `hblank_first_line_after_turn_on`.

## Reglas de ejecución
1. Cada fase debe compilar antes de pasar a la siguiente.
2. No se revierten por regresiones de diff, solo por errores de compilación.
3. No se ejecutan tests ni harness hasta que el usuario lo indique.
4. Cada fase se documenta en el handoff.

## Checkpoint actual
Fase 1 parcialmente completada. Fase 2 en curso.
EOF