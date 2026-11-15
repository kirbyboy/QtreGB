# PPU v3 — F9: muestreo de `SCX` bajo (3 bits)

Fecha: 2026-11-13. Rama `ppu-v3`.

## Problema

`m3_scx_low_3_bits` daba **324** píxeles de diferencia con BIOS. El harness y
el comparador de framebuffer localizaron el fallo en las **líneas 72–143**,
columnas **150–159**: el contenido del último tile aparecía desplazado
**+2 píxeles**.

Ejemplo de la línea 72, columnas 144–159:

```text
esperado/DocBoy: ........####....
QtreGB:          ..........####..
```

## Causa

La fuente del test (`mealybug-tearoom-tests/src/ppu/m3_scx_low_3_bits.asm`)
confirma el mecanismo:

- El handler de STAT modo 2 escribe `SCX=0`.
- En las primeras 72 líneas ejecuta un `jr c` tomado (4 T-cycles extra) antes
  de escribir `SCX=2`; en las líneas 72+ el salto no se toma.
- Los **3 bits bajos de SCX se muestrean una vez por línea**, al inicio del
  primer ciclo B real del fetcher. Si el write llega después de ese punto,
  el valor no afecta a la línea.

QtreGB usaba el valor vivo de `SCX` en cada tile, así que el write tarde de
las primeras 72 líneas también desplazaba el último tile. Al introducir el
`skip` sin latch de línea, el error se movió al semicuadro superior.

## Implementación

- `m_bg_line_scx_low`: valor de `SCX & 7` latcheado una vez por línea.
  - Se inicializa en el arranque del pipeline.
  - Se actualiza en `m_dots == 89`, el inicio del primer ciclo B real después
    del startup de modo 3 en este scheduler.
- `m_bg_fetch_skip`: al hacer push de un tile de BG, se descartan los píxeles
  de diferencia entre el `SCX & 7` latcheado y el del tile anterior.
  - El primer tile de la línea sigue usando el discard inicial de `SCX % 8`.
  - Los tiles posteriores solo hacen `skip` si el valor latcheado cambió.
- La ventana no usa este `skip` (tiene su propia rejilla).
- El tilemap x sigue usando el `SCX` vivo; solo se latchean los 3 bits bajos
  para la alineación de píxeles.

## Resultados

- `m3_scx_low_3_bits` con BIOS: **0** diferencias (antes 324).
- DocBoy con `--delay 70224`: 0 contra el PNG esperado.
- QtreGB con el fix: 0 contra el PNG esperado (y contra DocBoy).
- El resto de diffs visuales no cambia: el fix es quirúrgico.
- doctest: **62/62** (232 assertions).
- blargg: **15/15**.
- mts: **34/34**.
- PPU acceptance: **11/12** (solo `intr_2_mode0_timing_sprites`).
- Harness sin BIOS: **3 PASS / 22 FAIL / 8 SKIP**.
- Harness con BIOS: **4 PASS / 21 FAIL / 8 SKIP**.
  - PASS: `dmg-acid2`, `m2_win_en_toggle`,
    `m3_lcdc_win_en_change_multiple`, `m3_scx_low_3_bits`.
- `halt_ime0_ei` e `instr_timing`: PASS.

## Siguiente objetivo

`m3_scx_high_5_bits` se resolvió en **F10**: el fallo estaba en la alineación
del pipe para sprites en X>0, no en el muestreo de `SCX`. Ver
`documentation/ppu-v3-f10-sprite-pipe.md`. El siguiente objetivo es el modelo
de penalización de grupos de sprites en la misma X (caso X=2 de
`intr_2_mode0_timing_sprites`).
