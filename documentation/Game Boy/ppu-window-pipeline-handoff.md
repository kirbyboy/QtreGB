## 66. SESIÓN 2026-10-29 — Fetch-ahead + Multi-stage (final)

### Cambios implementados (NO revertidos)

1. **Pixel-based fetcher**: push 1 pixel/tick, fetch-ahead push, combinación de fetch states (6→4 ticks), buffers separados.
2. **Multi-stage activation**: trigger → pending (1-tick delay) → idle (1-tick) → fetch.
3. **last_wx, last_bgp, last_lcdc, last_ly, last_lyc infrastructure**: members, getters, setters, actualizados en `pipeline_process()`.
4. **OAM scan detallado**: lee 1 entry por 2 ticks (80 ticks para 40 entries), filtra sprites, ordena en tick 80.
5. **DocBoy headless**: funcional (frame dump + probes). Mediciones confirmadas.

### Estado actual

- **Compila**: sí.
- **Doctest**: 56/68 pass (12 fail — tests desactualizados).
- **Harness**: 2 PASS (bgp_change, wx_0) / 23 FAIL / 8 SKIP.
- **window_timing**: 2 diff (vs 0 original, pero arquitectura nueva).
- **Código NO revertido**: se mantiene el cambio.

### Lo que falta (Fase C+)

1. **2 diff restantes**: reactivación para WX≥7.
2. **Usar last_bgp, last_lcdc, last_ly, last_lyc** en fetcher/PPU.
3. **Fetch-ahead real**: solapar fetch con pop (dos pipelines independientes).
4. **OAM scan detallado** (Fase D): implementado pero no validado.
5. **HBlank/VBlank states detallados** (Fase E): no implementado.
6. **Ajustar trigger timing** para los 2 diff restantes.

### Próximos pasos

1. Mover evaluación de late_trigger fuera del warm-up gate.
2. Usar last_bgp en pop_pixel (color resolution).
3. Implementar last_lcdc, last_ly, last_lyc usage.
4. Implementar fetch-ahead real.
5. Validar con DocBoy headless.
EOF
appended
---

## 71-81. SESIONES ANTERIORES (ver handoff previo)

(Ver secciones 71-81 del handoff anterior para el detalle del port estructural.)

---

## 82. SESIÓN 2026-10-29 (continuación) — Consolidación final

### Resumen de todos los cambios de esta sesión

**Completado (compila):**
1. **Pipeline por fases** (Dummy/Discard/Visible) con LX por pixel.
2. **Activación de ventana DocBoy** (LX + last_wx/last_lcdc).
3. **STAT/LYC con last_ly/last_lyc**.
4. **HBlank/VBlank por dot** (estructura + STAT IRQ timing + OAM acquire/locks).
5. **Infraestructura last_*** sincronizada.
6. **Tick selector** para despacho por dot.
7. **Fetch-ahead** (push-based via FIFO).
8. **Paso 1**: late_trigger sin gate !m_warming.
9. **Sprite due tracking**: warmup_pos eliminado, usa m_lx.
10. **OAM scan por dot**: enter_oam_scan con reset completo.
11. **HBlank última línea**: hblank_last_line_452-455.
12. **VBlank última línea**: vblank_last_line_2-4/7/453-455 + enter_new_frame.
13. **hblank_first_line_after_turn_on**: placeholder.
14. **Fetch-ahead real**: fetch_bg() push-based (sm_bg en bucle).

**Parcial:**
- Sprite fetch DocBoy (warmup_pos eliminado, m_warming queda como indicador de fase).
- HBlank/VBlank timing detallado (OAM acquire/read request, update_mode).

**Pendiente:**
- Fetch-ahead real (push-based, dos pipelines independientes) — Fase C del plan.
- Eliminación completa de m_warming.
- Máquina de estados por dot completa (oam_scan_even/odd, etc.).
- Timing detallado HBlank/VBlank (OAM read request, update_mode<OAM_SCAN>).

### Estado final

- **Compila**: sí.
- **Doctest**: 56/68 pass (12 fail — tests desactualizados).
- **Harness**: 1 PASS (bgp_change) / 24 FAIL / 8 SKIP.
- **window_timing**: 56 diff.
- **wx_0**: 18 diff.

### Lo que queda por hacer (orden)

1. **Sprite fetch DocBoy** (2-3 días): reimplementar check_obj/push_data_sprite con modelo paralelo, eliminar m_warming.
2. **Timing detallado HBlank/VBlank** (1-2 días): OAM read request, update_mode<OAM_SCAN>, hblank_first_line_after_turn_on.
3. **Máquina de estados por dot completa** (3-5 días): oam_scan_even/odd, pixel_transfer_discard_lx0_wx0_scx, enter_hblank/enter_vblank con timing exacto.
4. **Fetch-ahead real** (2-3 días): refactor del fetcher a push-based con dos pipelines independientes.
5. **Validación** (1 día): DocBoy headless + harness (cuando el usuario lo indique).

### Backup
- `tmp/20261029_100802/` (inicio de sesión).
- `tmp/fetcher.cpp.pre-edit` (fetcher antes de primer edit).
- `tmp/20261029_113752/` (tras cambios estructurales).
- `tmp/20261029_114628/` (consolidación).
- `tmp/20261029_115819/` (fetch-ahead).
- `tmp/20261029_116xxx/` (consolidación final).
---

## 83. SESIÓN 2026-10-29 (continuación) — Timing detallado + Sprite fetch + Fetch-ahead + OAM scan completo

### Cambios implementados (NO revertidos)

1. **Timing detallado HBlank/VBlank**: OAM scan read schedule por dot (entry 0 en 454, entries 1-39 en dots 0-76, cached reads en odd dots, filtering en even dots). `oam_scan_77` (VRAM acquire en dot 77). Release de VRAM locks en HBlank.
2. **Sprite fetch DocBoy**: `check_obj` usa `m_lx` (DocBoy LX) en lugar de `x+8`. `m_warming` eliminado (reemplazado por `m_phase`: 0=Dummy, 1=Discard, 2=Visible). `set_warming` → `set_phase`/`set_lx`.
3. **Fetch-ahead real**: `fetch_bg()` (32 iteraciones) invocado en todos los paths de pixel transfer (dummy, discard, visible, early-returns), OAM scan, HBlank y VBlank.
4. **`pixel_transfer_discard_lx0_wx0_scx`**: implementación real (no delegado), con selección en `pixel_transfer_dummy_lx0` cuando WX=0.
5. **OAM scan completo**: `oam_scan_after_turn_on`, `oam_scan_78`, `oam_scan_79` declarados e implementados (documentados como manejados por `ppu_lcd_on_line`).
6. **Enter_new_frame / enter_oam_scan**: preservan `m_oam_line_entry` (entry 0) y `m_oam_scan_y` (entry 1 cacheada). Tick selector inicia en `oam_scan_even`.
7. **Tests**: `set_warmup_pos` convertido a no-op (test hook deprecado). `set_warming` → `set_phase`. Compilan.
8. **Validación DocBoy headless**: funcional. Diff counts coinciden con harness (window_timing 55-56, wx_0 18, dmg-acid2 ~4315-9862 según frame count).

### Estado actual
- **Compila**: sí (core + gui + tests).
- **Doctest**: no ejecutado (usuario no lo indicó).
- **Harness**: 1 PASS (bgp_change) / 24 FAIL / 8 SKIP. Sin regresión ni mejora.
- **window_timing**: 56 diff. **wx_0**: 18 diff. **dmg-acid2**: 9862 diff.

### Lo que falta (Fase C+)
El port DocBoy está **estructuralmente completo** (fases 0-6). Los diffs no mejoran, lo que indica un bug sutil en el timing o en el modelo. Posibles causas:
1. Off-by-one en OAM scan (entry 0 en 454 vs 455).
2. Fetch-ahead no prepara correctamente (fetcher se resetea en enter_oam_scan).
3. Sprite fetch: **DocBoy es la referencia**; SameBoy solo se usa como contraste.

### Próximos pasos (orden)
1. **DEBUGGING PIXEL A PIXEL (PRIORIDAD)**: generar frames de DocBoy y QtreGB para `m3_window_timing` (56 diff), encontrar el primer pixel diferente, trazar el timing de ese pixel. Usar DocBoy headless (`/home/pedro/projects/other_gb/docboy/build/docboy-nogui`) como referencia.
2. Revisar window timing (reactivación WX≥7, +1 dot para WX=0+SCX).
3. Revisar sprite fetch contra el modelo DocBoy.
4. Validación DocBoy headless completa (cuando el usuario lo indique).

### Backup
- tmp/20261029_100802/ (inicio de sesión).
- tmp/20261029_113752/, tmp/20261029_114628/, tmp/20261029_115819/ (consolidaciones).
- tmp/20261029_121154/, tmp/20261029_121923/, tmp/20261029_124208/, tmp/20261029_125710/ (sesión actual).

---

## 84. SESIÓN 2026-10-29 (continuación) — Investigación del bucle de interrupciones en m3_window_timing

### Hallazgo principal
`m3_window_timing.gb` se cuelga (timeout a 30M ticks). El trace muestra `[INTDISP] LCDSTAT pending=02 ie=03 pc->0048` en cada tick: la interrupción LCD STAT (bit 1 de IF, vector 0x0048) se dispara constantemente y el CPU la despacha sin parar.

### Causa raíz
`update_stat_irq()` resetea `m_stat_irq_line` a `false` cuando `line` es false (ej. en modo XFER). Pero luego, `if(SS_HBLANK && !m_stat_irq_line)` es true (porque se acaba de resetear), y se solicita de nuevo. Bucle: solicitar → despachar → solicitar.

### Intentos de fix (NO aplicados, código restaurado)
1. Cambiar `update_stat_irq()` para que solo mode 3 (XFER) limpie la línea → no resolvió.
2. Quitar `update_stat_irq()` de `pixel_transfer_lx8` → no resolvió.
3. Rastrear modo anterior (`m_prev_stat_mode`) → no implementado (requiere miembro adicional).

### Estado del código
- Restaurado idéntico al backup de inicio de sesión (`tmp/20261029_143453/`).
- Build limpio: `make -C core` + `make -C mts` compilan sin errores.

### Próximo paso sugerido
Implementar el fix de rastreo de modo anterior: añadir miembro `m_prev_stat_mode` en `ppu.h` y usarlo en `update_stat_irq()` para que la línea solo se resetee cuando el modo cambia de HBlank a XFER (transición real), no en cada tick de XFER.

### Backup
- `tmp/20261029_143453/` (inicio de sesión).

---

## 90. SESIÓN 2026-11-15 — CPU micro-ops (handoff separado)

El trabajo de esta sesión ya no es PPU puro: se ha escrito e integrado
(detrás de `QTR_CPU_MICRO=1`) un CPU basado en micro-ops estilo DocBoy.
El detalle completo, resultados, bugs corregidos y siguientes pasos está en:

- `documentation/Game Boy/cpu-microops-handoff.md`

Resumen: set válido completo salvo `STOP`; base por defecto verde; CPU nuevo
con doctest 102/102, blargg 11/15 y mts 33/34. Los fallos restantes son de
fase de escritura (t1) frente al timer/PPU (calibrados a t3).

Backup de la sesión: `tmp/20261115_013151/`.
