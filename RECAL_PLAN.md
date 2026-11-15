# Plan de recalibración completa (fase A.4)

Fecha: 2026-11-15
Base: WIP A.4 (`ppu_archA4_immediateirq_wip.*`)
Línea: `.wip/ppu` (rama `wip/ppu`)
Principal congelada: `ppu-v3` (SM8, 15 PASS)

## Objetivo

Partiendo del cambio de scheduler A.4 (IRQ OAM entregada en el dot del
evento; write LCDC 106→102), recuperar y superar el baseline:

- PPU acceptance **12/12**
- mts **34/34**, doctest **62/62**, blargg **15/15**
- Harness oficial con BIOS **≥15 PASS**
- `dmg-acid2` = 0

Estado inicial de la fase (medido):

- PPU acceptance: **9/12**
  - Pasan: hblank_ly_scx_timing, intr_1_2_timing, intr_2_mode0_timing,
    intr_2_mode3_timing, intr_2_oam_ok_timing, lcdon×2,
    stat_irq_blocking, stat_lyc_onoff.
  - Fallan: intr_2_0_timing, intr_2_mode0_timing_sprites,
    vblank_stat_intr.
- PNG con BIOS: **6 PASS**
  - Pasan: dmg-acid2, m2_win_en_toggle, scx_low_3_bits,
    wx_4_change, wx_4_change_sprites, wx_5_change.
  - En recalibración: el resto (bgp_change 21, bg_map 304, tile_sel 428,
    win_map 114, obp0 36, high_5 35, window_timing 3, ...).
- mts 34/34, invariantes CPU verdes.

El baseline está en `temp/arch_baseline.txt`.

## Orden de la fase

### Subfase 1 — Compensación Mooneye (invariantes)

Objetivo: PPU acceptance 12/12 con A.4.

1. `intr_2_0_timing`: la entrega inmediata adelanta la IRQ y cambia la
   relación mode 2 → mode 0. Ajustar el evento compañero (flag mode 0 /
   HBlank IRQ) con la misma filosofía que el fix de OAM.
2. `intr_2_mode0_timing_sprites`: fetch OBJ + mode 0. Usar la traza de
   sprites y el source del test.
3. `vblank_stat_intr`: VBlank/quirck OAM. Ya sabemos que los offsets
   enteros no bastan; usar el modelo de línea/edge.

Cada test recuperado se añade al baseline. No se pasa a la subfase 2
hasta tener PPU 12/12.

### Subfase 2 — Recalibración m3

Con la fase del scheduler fija:

1. Medir la tabla completa de diffs (DocBoy con BIOS + harness PNG).
2. Recalibrar en este orden:
   - `x0wait`/`x_extra` (fetches de sprite),
   - waits de activación de ventana (`startup`/`midline`),
   - latch de SCX,
   - delays de paleta (BG/OBJ/ventana),
   - budget de mode 0 / transiciones.
3. Objetivo: recuperar los 15 PASS del baseline y, si es posible,
   acercarse a los 24 m3 + acid2 a 0.

### Subfase 3 — Validación y porte

1. `arch_gate.sh` con el baseline actualizado (≥15 PNG + 12 PPU).
2. Trazas vs DocBoy para los fixtures críticos.
3. Copiar `core/src/ppu.cpp` + `core/inc/ppu.h` a `ppu-v3`,
   compilar y revalidar allí.
4. Solo entonces actualizar el baseline de la principal.

## Reglas

- No cambios aislados sin medir su efecto en la tabla completa.
- Cada subfase se cierra con números exactos.
- No commit ni push sin petición explícita.
- No tocar `timer.cpp` ni el despacho de CPU sin revalidar Blagg/Mooneye.
- WIPs en `tmp/fetcher-proto/`; baseline en `temp/arch_baseline.txt`.

## Subfase 1 — primer resultado (mode 0)

- A.4 + mode 0 −4 T (lx=163, budget 248, IRQ 161):
  `intr_2_0` PASS; `intr_2_mode0` y `hblank_ly_scx` fallan.
- A.4 + mode 0 −2 T (lx=165, budget 250, IRQ 163):
  `intr_2_0` PASS y `intr_2_mode0` PASS; `hblank_ly_scx`,
  `intr_2_mode0_timing_sprites` y `vblank_stat_intr` fallan.
  Neto: 9/12, pero cambia el conjunto de verdes.
- Conclusión: el modo 0/HBlank/SCX es otra frontera en conflicto; hay
  que resolverlo junto con el modelo de línea STAT, no con un offset
  único. WIP en `ppu_archA4_mode0shift2_wip.*`; base restaurada a A.4.

## Subfase 1 — barrido mode 0/HBlank (conclusión estructural)

Barrido de 12 combinaciones (flag 165/167, budget 250/252, IRQ 161/163/165)
contra intr_2_0, intr_2_mode0, hblank_ly_scx y mode0_timing_sprites:

- IRQ 161/163: intr_2_0 PASS, intr_2_mode0 PASS, hblank FAIL.
- IRQ 165: intr_2_0 FAIL, intr_2_mode0 PASS, hblank PASS.
- sprites falla en todas.
- flag/budget no cambian el resultado.

No existe combinación que satisfaga intr_2_0 y hblank a la vez. El
conflicto es estructural (fase CPU/PPU del handler vs dot del IRQ
HBlank), no de parámetros del PPU. La recalibración completa no puede
cerrarse con el modelo actual de dots; requiere el modelo de fase
CPU/PPU (o de línea STAT) de fondo.

WIP del barrido: `ppu_archA4_mode0sweep_wip.*`.
Base restaurada: A.1 (PPU 12/12, mts 34/34).

## Subfase 1 — modelo de línea STAT implementado (base de la fase)

- Default: `m_stat_model = true` (QTR_STATMODEL=0 lo desactiva).
- Nivel único: LYC || (OAM latch || pulse VBlank || mode2) || VBlank ||
  HBlank; edge en subida con entrega inmediata de la IRQ.
- Quirk OAM-in-VBlank como pulso de un tick para no suprimir el IRQ
  OAM de la línea 0.
- Efecto: write LCDC **102** (DocBoy 100), handler de línea 0
  restaurado.
- PPU acceptance: **8/12**.
  Pasan: hblank_ly_scx, intr_1_2, intr_2_mode0, intr_2_mode3,
  intr_2_oam_ok, lcdon×2, stat_irq_blocking.
  Fallan: intr_2_0 (o hblank, conflicto de dot), intr_2_mode0_sprites,
  stat_lyc_onoff, vblank_stat_intr.
- Barrido del IRQ HBlank (161/163/165) con el modelo: mismo conflicto
  intr_2_0 ↔ hblank_ly_scx; sprites/vblank/lyc siguen.
- Baseline de la fase: `temp/arch_baseline.txt` (8 PPU + 6 PNG;
  arch_gate 18/18).
- WIP: `ppu_statmodel_pulse_hblankknob_wip.*`.
- Siguiente: refinar el modelo para sprites, VBlank y LYC; luego
  recalibrar m3.

## Subfase 1 — refinamiento mode 0 (modelo)

- Con el modelo STAT y el IRQ HBlank explícito desactivado, el edge
  HBlank sale del flag mode 0.
- Barrido lx 163/165/167 (budget lx+85):
  - 163: intr_2_0 PASS, hblank/ mode0 FAIL.
  - 165: hblank PASS, intr_2_mode0 PASS, intr_2_0 FAIL.
  - 167: intr_2_mode0 PASS, intr_2_0/hblank FAIL.
- Se fija **lx=165, budget=250** como default del modelo.
- PPU acceptance: **8/12** (el conjunto verde no cambia; hblank queda
  estable).
- Quedan: intr_2_0 (conflicto de 2 dots con HBlank), sprites, lyc,
  vblank.
- WIP: `ppu_statmodel_mode0165_wip.*`.

## Subfase 1 — LYC en transiciones (sin cambio)

- `update_stat_irq()` con el modelo evalúa el nivel usando LYC **actual**
  (power-on/transiciones), mientras el tick usa el LYC retardado.
- `stat_lyc_onoff` sigue fallando; `intr_2_0`, sprites y vblank siguen.
- hblank/mode0/mode3/oam_ok/intr_1_2/lcdon/blocking siguen verdes.
- PPU acceptance: 8/12.
- WIP: `ppu_statmodel_lyctrans_wip.*`.
- Pendiente: fuente de stat_lyc_onoff, modelo OBJ/mode0 de sprites,
  relación VBlank/quirk, y el conflicto de 2 dots de intr_2_0/HBlank.

## Subfase 1 — VBlank y conflicto HBlank

- VBlank resuelto con el modelo+pulso: IRQ VBlank en **453**, pulso en
  **454**. `vblank_stat_intr` PASS, sin romper intr_1_2/blocking.
- Conflicto HBlank: `intr_2_0` necesita el IRQ en 163; `hblank_ly_scx`
  en 165. No se pueden satisfacer ambos con un edge fijo.
- Base elegida: IRQ HBlank explícito en **163** + flag mode 0 en 165.
  `intr_2_0` PASS, `vblank_stat_intr` PASS, `intr_2_mode0` PASS;
  `hblank_ly_scx` queda conocido-rojo (documentado).
- PPU acceptance: **9/12**. Baseline: 9 PPU + 6 PNG (arch gate 19/19).
- WIP: `ppu_statmodel_hblank163_wip.*`.
- Quedan: hblank_ly_scx, intr_2_mode0_timing_sprites, stat_lyc_onoff.

## Subfase 1 — tabla de fase/edge (implementada)

- `raise_stat_irq` del modelo ahora detecta la fuente del edge
  (LYC/OAM/VBlank/HBlank) y aplica un **retardo de entrega por fuente**
  (`QTR_SDLYC/SDOAM/SDVBLANK/SDHBLANK`).
- El edge HBlank sale del flag mode 0 (lx=165, dependiente de SCX);
  el IRQ explícito queda desactivado con el modelo.
- HBlank delay 0: `hblank_ly_scx` PASS; `intr_2_0` sigue fallando.
  Delays 1/2: empeoran ambos. El conflicto de `intr_2_0` ya no es del
  edge HBlank, es del boundary de instrucción del CPU (2 dots).
- Base: modelo + phase table, HBlank delay 0.
- PPU acceptance: **9/12** (hblank/vblank/mode0/mode3/oam_ok/intr_1_2/
  lcdon×2/blocking). Known-red: intr_2_0, sprites, lyc.
- Baseline actualizado (9 PPU + 6 PNG; arch gate 19/19).
- WIP: `ppu_statmodel_phasetable_wip.*`.

## Subfase 2 — PPU 12/12 (fase/edge + boundary de CPU)

- **Boundary de instrucción**: el CPU muestrea IF después del tick de PPU
  (`Z80GB::check_interrupts()` desde `QtreGB::tick_once`). Un edge STAT del
  mismo T-cycle se acepta en ese boundary. Resuelve `intr_2_0` sin mover el
  edge HBlank (write LCDC sigue en 102).
- **Fase por fuente**: OAM con 1 dot de entrega (`m_stat_delay_oam = 1`),
  igual que el pending de 1 T de SM8. Resuelve el arranque del sprite test.
- **Desacople HBlank**: la condición STAT HBlank se latcha en `lx=165`
  (dot del IRQ) pero el registro STAT visible cambia en `lx=167` (sin OBJ)
  o en `252 + penalty` (OBJ). El poll de modo del sprite test ve el cambio
  2 dots después del IRQ, como SM8.
- **LYC en power-on**: el flanco 0->1 durante las líneas LCD-on se entrega
  sincrónicamente; resuelve el round 4 de `stat_lyc_onoff`.
- PPU acceptance: **12/12**. Gate: **22/22** (build + 3 invariantes +
  12 PPU + 6 PNG).
- Write LCDC del handler de mode 2: **102** (DocBoy 100).
- PNG con BIOS: **6 PASS** (sin cambios en píxeles; la recalibración m3
  empieza ahora).
- WIP: `ppu_ppu12_wip.*`, `Z80GB_ppu12_wip.*`, `QtreGB_ppu12_wip.*`.

## Subfase 3 — paridad m3 con la principal (15 PASS)

- **Boundary de instrucción final**: se muestrea IF en el boundary inline
  (antes del tick de PPU), como SM8. Los edges STAT del mismo T-cycle se
  ven en el siguiente boundary. Esto restaura la fase de línea 0 que los
  m3 esperan (el handler de mode 2 escribe en el dot correcto).
- **Fase por fuente**: OAM +1 dot (`SDOAM=1`) y VBlank +1 dot
  (`SDVBLANK=1`). HBlank y LYC a 0. Con esto PPU vuelve a **12/12** sin
  el boundary post-PPU.
- **HBlank/modo visible**: condición STAT en lx=165, registro STAT visible
  en lx=167 (sin OBJ) o `252 + penalty` (OBJ). Mantiene el sprite test y
  alinea el poll de modo con SM8.
- **Pipeline**: se retira el OBJ temprano en BgPush y se vuelve a
  `x0_extra=4` (SM8). El sprite acceptance sigue en 12/12 gracias a la
  fase de OAM y al desacople mode 0.
- **Resultado m3**: **15 PASS / 10 FAIL / 8 SKIP**, exactamente la misma
  lista y los mismos diffs que la principal SM8:
  - PASS: dmg-acid2, m2_win_en_toggle, m3_bgp_change,
    m3_bgp_change_sprites, m3_lcdc_bg_map_change,
    m3_lcdc_tile_sel_win_change, m3_lcdc_win_en_change_multiple,
    m3_lcdc_win_map_change, m3_obp0_change, m3_scx_high_5_bits,
    m3_scx_low_3_bits, m3_window_timing, m3_wx_4_change,
    m3_wx_4_change_sprites, m3_wx_5_change.
  - FAIL (mismos que SM8): bg_en 132, obj_en 256, variant 352,
    obj_size 310, obj_size_scx 190, tile_sel 124,
    win_en_multiple_wx 2067, scy 6845, window_timing_wx_0 372,
    wx_6 13811.
- Write LCDC del handler de mode 2: **106** (SM8/hardware; línea 0
  incluida).
- Gate: **31/31** (build + doctest 62/62 + blargg 15/15 + mts 34/34 +
  12 PPU + 15 PNG).
- WIP: `ppu_m3parity_wip.*`, `Z80GB_m3parity_wip.*`,
  `QtreGB_m3parity_wip.*`.
- Siguiente fase: los 10 PNG que también fallan en SM8 (bg_en, obj_en,
  variant, obj_size, obj_size_scx, tile_sel, win_en_multiple_wx, scy,
  window_timing_wx_0, wx_6), sin regresiones en la lista verde.

## Subfase 4 — port a ppu-v3 y revalidación

- Portados a `ppu-v3` los 3 archivos que diferían:
  `core/inc/ppu.h`, `core/src/ppu.cpp`, `core/src/Z80GB.cpp`
  (`Z80GB.cpp` solo simplifica el `if` del boundary; el cambio funcional
  es el modelo STAT + tabla de fase/edge + HBlank/modo visible + LYC).
- Backup previo de los 3 archivos originales:
  `tmp/v3-pre-port-20261114_205547/`.
- `core/` de `ppu-v3` y `.wip/ppu` queda **idéntico** tras el port.
- Revalidación en `ppu-v3`:
  - build `make -j$(nproc)` OK.
  - PPU acceptance: **12/12**.
  - harness PNG con BIOS: **15 PASS / 10 FAIL / 8 SKIP**, salida
    idéntica byte a byte a la del worktree (misma lista y mismos diffs).
  - doctest: **62/62** (232 assertions).
  - blargg: **15/15**.
  - mts: **34/34**.
- Sin commit ni push. `ppu-v3` sigue en `d253f93` con los 3 archivos
  modificados en el working tree.

## Subfase 5 — mejoras estructurales de PPU (fase 2 m3)

- **FIFO OBJ**: el FIFO de sprites ahora avanza en lockstep con el pixel
  BG aunque el bit OBJ de LCDC esté apagado; solo el dibujo se condiciona
  al enable. Evita que píxeles de sprite obsoletos salgan ~96 px después.
  - m3_lcdc_obj_en_change: 256 -> **14**.
  - m3_lcdc_obj_en_change_variant: 352 -> **111**.
- **WX=0**: el retardo de activación de 1 T se aplica cuando WX=0 y
  SCX%8 != 0, no solo en la fase Discard.
  - m3_window_timing_wx_0: 372 -> **263**.
- **Bitplane desync DMG**: la fila de tile data se recalcula en cada
  acceso (low y high) con el SCY/LCDC actuales, como DocBoy/DMG. Un
  cambio de SCY o de tile-select entre low y high mezcla bitplanes.
  - m3_scy_change: 6845 -> **989**.
  - m3_lcdc_tile_sel_change: 124 (sin cambio, pero el modelo ya es el
    correcto; queda la fase del fetch del segundo tile).
- Sin regresiones: harness **15 PASS / 10 FAIL / 8 SKIP**, misma lista
  verde. PPU **12/12**. Gate **31/31** (doctest 62/62, blargg 15/15,
  mts 34/34).
- Fallos restantes y diffs actuales: bg_en 132, obj_en 14, variant 111,
  obj_size 310, obj_size_scx 190, tile_sel 124, win_en_multiple_wx 2067,
  scy 989, window_timing_wx_0 263, wx_6 13811.
- Knobs de diagnóstico eliminados (`QTR_OBJCUR`, `QTR_BGCUR`).
- WIP: `ppu_phase2_wip.*`. Backup: `tmp/20261114_213339/` (o el timestamp
  indicado por BACKUP-*).

### Intento rechazado — boundary de OBJ (fase 2)

- Añadir +1 dot al `post_wait` de los sprites con `X & 7 >= 6` hace
  `m3_lcdc_obj_en_change` **PASS (14 -> 0)**, pero rompe
  `m3_obp0_change` (0 -> 8) y `m3_bgp_change_sprites` (0 -> 152/304):
  el boundary de OBJ y el timing de paleta comparten el pipeline de
  salida. No es una mejora neta.
- Revertido al estado fase 2 (`ppu_phase2_wip.*`). Para cerrar estos
  1-2 px hace falta desacoplar el render de OBJ (pop/render stage tipo
  OAM FIFO), no otro offset de fetch.

## Subfase 6 — intento de modelo fino OBJ/ventana (revertido)

- **Ventana**: mover la comprobación de trigger al punto de pop/render
  (como DocBoy/SameBoy) no cambió `m3_wx_6_change` (13811). Probar
  `SDOAM=0..5` tampoco. La causa es la fase del handler m3: la escritura
  `WX=LY` cae después del trigger en `x = WX-7`, así que gana `WX=6`.
  No es la fórmula de ventana; es la fase CPU/M-cycle del handler.
  `m3_window_timing_wx_0` sigue en 263.
- **OBJ**: alinear el FIFO por X absoluta y descartar los píxeles
  offscreen de sprites con X<8 rompe `m3_obp0_change` y
  `m3_bgp_change_sprites` (0 -> 106) y no arregla `obj_en` (14 -> 72).
  El merge actual desde el índice 0 es el baseline validado.
- **Conclusión**: hace falta un port completo del OAM FIFO de DocBoy
  (posiciones absolutas, cola de sprites solapados, abort al desactivar
  OBJ) y del state machine de activación de ventana, no ajustes
  incrementales. El baseline fase 2 se restaura y se mantiene como gate.
- Estado restaurado: **15 PASS / 10 FAIL / 8 SKIP**, PPU **12/12**.

## Subfase 7 — desarrollo del modelo fino en worktree (en curso)

Experimentos realizados en `.wip/ppu`:

- **Enable OBJ en el flush de paleta**: obj_en 56, variant 152.
  Oculta píxeles izquierdos que el hardware ya había producido; descartado.
- **Enable OBJ con LCDC actual en el push**: obj_en 42, variant 138.
  El boundary correcto no es simplemente "actual vs retardado"; descartado.
- **Delay de paleta fijo (sin `out_delay`)**: bgp_change_sprites 3845,
  variant 957. Confirma que `out_delay` modela el reloj de paleta que
  sigue avanzando durante el stall del sprite; el modelo correcto debe
  usar el stall real del pipeline (fetch OBJ + post-wait solapados), no
  el `post_wait` empírico.
- **Trigger de ventana en el pop/push**: wx_6 sigue 13811; `SDOAM=0..5`
  tampoco lo mueve. La causa es la fase del handler m3 (la escritura
  `WX=LY` cae después del trigger `x = WX-7`), no la fórmula de ventana.
- **FIFO OBJ por X absoluta / drop offscreen**: rompe obp0 y
  bgp_change_sprites (106); el merge actual desde el índice 0 es el
  baseline válido.

Plan de implementación completa (siguiente):
1. Modelo del reloj de paleta: medir el stall real del pipeline por
   sprite (fetch + post-wait, solapado con el fetch BG) y usarlo como
   `out_delay`; eliminar los casos especiales 5/6.
2. OAM FIFO: mantener el merge por índice 0, añadir abort del fetch al
   desactivar OBJ y la fase exacta de fetch/render.
3. Ventana: portar el state machine de DocBoy (trigger en render,
   activación en el siguiente tick de fetcher, clear del BG FIFO,
   contador de línea de ventana, WX=0+SCX>0) y recalibrar los waits.
4. Fase del handler m3: investigar el desfase de M-cycle que hace ganar
   WX=6 en wx_6; puede requerir alinear la aceptación STAT sin romper
   PPU acceptance.

Estado actual restaurado: **15 PASS / 10 FAIL / 8 SKIP**, PPU **12/12**.

## Subfase 8 — comparación de fase con DocBoy (modelo fino)

Medición exacta en `m3_lcdc_obj_en_change` línea 49:

- DocBoy: escritura LCDC=81 en dot=100, lx=11. Render: x=0 dot=97,
  x=1 98, x=2 99, x=3 100, x=4 101. El PPU ve la escritura 1 dot
  después; x=4 se oculta.
- QtreGB: escritura LCDC=81 en dot=106. Push: x=0 dot=102, x=1 103,
  x=2 104, x=3 105, x=4 106. El push es 1 dot antes respecto a la
  escritura; x=4 usa el LCDC retardado (viejo) y se dibuja.

Intento de fase global: retrasar 1 dot el primer píxel visible
(`m_first_visible_delay`) alinea el boundary de OBJ (14 -> 10), pero
también avanza el fetcher y rompe bg_map/scx/window. Compensar el delay
de paleta a 5 recupera bgp, pero no bg_map/scx/window. El pixel y el
fetcher están acoplados.

Conclusión: el modelo fino requiere portar **juntos** el bucle de pixel
transfer/fetcher de DocBoy (fase de render + fase de fetch + paleta),
no un skip global ni offsets separados. Estado restaurado: 15 PASS,
PPU 12/12. Próximo desarrollo: port completo de ese bucle.

## Subfase 9 — port completo, hito 1 (render directo + ventana en pop)

- Se reemplaza el pipeline de paleta diferida por **resolución en el render**
  (BGP/OBP actuales, BGP|last_bgp en el tick de escritura) y se mueve el
  trigger de ventana al punto de pop/render, con activación en el siguiente
  tick de fetcher.
- PPU acceptance se mantiene **12/12** (la parte de modo/STAT/OAM no se tocó).
- Harness: **9 PASS / 16 FAIL / 8 SKIP**. Verdes: dmg-acid2, m2_win_en_toggle,
  m3_lcdc_bg_map_change, m3_lcdc_win_en_change_multiple, m3_scx_high_5_bits,
  m3_scx_low_3_bits, m3_wx_4_change, m3_wx_4_change_sprites, m3_wx_5_change.
- Rotos temporalmente por el cambio de modelo: bgp_change 4800,
  bgp_change_sprites 274, obp0_change 6, window_timing 1310,
  win_map 187, tile_sel_win 802. Es esperado: el modelo de paleta/ventana
  aún no está alineado con DocBoy.
- Referencia: **DocBoy**. Con el frame alineado (`--delay 70224`) DocBoy
  reproduce el PNG esperado en **todos** los tests comprobados (bgp, obp0,
  obj_en, variant, obj_size, tile_sel, scy, bg_map, win_map, window_timing,
  wx_0/4/5/6, win_en_multiple, bg_en, scx_high/low, m2_win_en_toggle,
  dmg-acid2). Los diffs anteriores eran del dump a delay 0. SameBoy queda
  solo como contraste.
- El skip de +1 dot (fase global) es un hack temporal; no se conserva en
  port1. El siguiente paso es portar el bucle slow de **DocBoy**:
  fetcher BG/OBJ, FIFOs, ventana y resolución de paleta en el mismo estado.
- WIP: `ppu_port1_wip.*`. Backup: `tmp/<timestamp>`.

### Corrección — DocBoy sí reproduce los PNG (alineación de frame)

La comparación con DocBoy a `--delay 0` estaba mal: `docboy_fbdump`
detiene la emulación en el PC mágico y el framebuffer puede estar a
medio frame. Con **`--delay 70224` (un frame)** DocBoy da **diff=0**
en todos los tests comprobados:

- obj_en, variant, obj_size, obj_size_scx, tile_sel, scy, bg_map,
  win_map, tile_sel_win, win_en_multiple, window_timing, wx_4,
  wx_4_sprites, wx_5, bgp, bgp_sprites, obp0, scx_high, scx_low,
  bg_en, window_timing_wx_0, wx_6, win_en_multiple_wx, m2_win_en_toggle.

Por tanto DocBoy es un oráculo de píxel válido; los diffs enormes de
wx_6/wx_0/win_en_multiple_wx eran del dump a delay 0, no del modelo.
El port puede seguir el modelo de DocBoy (ya estudiado e instrumentado);
SameBoy sigue siendo útil como contraste.

## Subfase 10 — medición BG: el render coincide, las escrituras no

`m3_bgp_change`, línea 1 (sin sprites):

- Render/push por píxel:
  - DocBoy: x=0 dot=91, x=1 92, ..., x=11 102.
  - QtreGB: x=0 dot=91, x=1 92, ..., x=11 102. **Coinciden exactamente.**
- Escrituras BGP:
  - DocBoy: 76 (10), 92 (11), 104 (10), 164 (12), 176 (10), 236 (0F), 248 (10).
  - QtreGB: 82 (10), 98 (11), 110 (10), 170 (12), 182 (10), 242 (0F), 254 (10).
  - **QtreGB escribe 6 dots después.**
- STAT IRQ: DocBoy dot=452, QtreGB dot=453 (+1).
- VEC (PC=0x48): DocBoy dots=11, QtreGB dot=14 (+3).

Conclusión: el pipeline BG/render ya está alineado con DocBoy en el caso sin
sprites. El desfase de 6 dots viene del **handler del CPU** (aceptación/
dispatch/entrada del handler), no del fetcher BG. El delay de paleta de 6
dots del modelo antiguo compensaba exactamente ese desfase; al pasar al
render directo se hizo visible.

Siguiente punto: alinear la fase del handler de mode 2 con DocBoy (o,
temporalmente, conservar una compensación de paleta de 6 dots). Sin eso,
bgp/obp0 no pueden pasar con el render directo. El fetcher BG ya no es el
bloqueo principal; OBJ y ventana siguen necesitando su modelo.

## Subfase 11 — dispatch del STAT mode 2: DocBoy vs Z80GB

Traza de DocBoy (`DOCBOY_CPUDBG`) en `m3_bgp_change`:

- IF STAT detectado en **dot=452, t=1**, pc=0192, cnt=0, remaining_ticks=1.
- ACCEPT/SERVE en **dot=454**.
- VEC (PC=0x48) en **dot=11**.

QtreGB:

- STATIRQ (IF) en dot=453.
- VEC en dot=14.
- El dispatch de QtreGB es ~3 dots más largo; el handler empieza en una
  fase distinta y las escrituras BGP acaban 6 dots después.

Causa: `Z80GB` acepta la interrupción sólo al final de la instrucción
(t=4) y arranca el dispatch en la fase de M-cycle siguiente. DocBoy usa
la tabla `INTERRUPTS_TIMINGS` muestreada en cada fase t (STAT normal:
1,1,1,2 ticks) y sirve la ISR en un boundary de micro-op con la fase
correcta. Eso acorta el dispatch ~3 dots.

Siguiente punto: portar a `Z80GB` el modelo de fase/tabla de DocBoy
(detección por t, remaining_ticks, serve en el boundary correcto) y
después recalibrar las fases STAT del PPU (SDOAM/SDVBLANK) y revalidar
PPU acceptance + mts.

## Subfase 12 — dispatch STAT: aceptación coincide, longitud no

Traza fina:

- DocBoy: IF dot=452 (t=1, rem=1); ACCEPT/SERVE dot=454; VEC dot=11.
- QtreGB (con IRQSTART instrumentado): STATIRQ dot=453;
  IRQSTART dot=454, pc=0191; VEC dot=14.
- La aceptación coincide en **454**. La diferencia es la **longitud del
  dispatch**: DocBoy llega a VEC en 13 ticks; QtreGB en 16.
- DocBoy sirve la ISR desde la fase parcial actual (serve_interrupt +
  isr_m0..m4, ~13 ticks). QtreGB espera al siguiente boundary de M-cycle
  completo (4 M-cycles = 16 ticks).

Intento de arreglo rápido: poner `m_t_cycle=4` al aceptar. Mueve el
IRQSTART a dot 0, cambia la instrucción aceptada y empeora bgp/obj;
revertido. No es un offset de fase, es la secuencia ISR.

Siguiente: portar a `Z80GB` el modelo de DocBoy `check_interrupt<t>` +
tabla de `remaining_ticks` + `serve_interrupt`/ISR por micro-ops, para que
el dispatch arranque en la fase parcial y dure los 13 ticks. Después,
recalibrar las fases STAT del PPU y revalidar.

## Subfase 13 — ISR de DocBoy tick a tick y límite del tweak

Secuencia exacta de DocBoy (`m3_bgp_change`, línea 1):

- IF dot=452 t=1 rem=1.
- ACCEPT/SERVE dot=454.
- ISR0 dot=454 (+0), ISR1 dot=2 (+4), ISR2 dot=6 (+8),
  ISR3 dot=10 (+12), VEC dot=11 (+13), ISR4/fetch dot=14 (+16).

QtreGB: IRQSTART 454, VEC 14 (dispatch de 16 ticks hasta el fetch).
El intento `m_dispatch_first`/`m_t_cycle=4` arranca el case 1 antes, pero
también cambia el dispatch de VBlank y la fase del NOP slide; la
aceptación STAT cae en dot 0/otra instrucción y bgp/obj empeoran.
Revertido.

Conclusión: hace falta portar el **state machine completo** de
interrupciones de DocBoy (check_interrupt<t> + INTERRUPTS_TIMINGS +
serve_interrupt + isr_m0..m4) para todos los tipos, no un offset por
dispatch. La fase del handler STAT depende también de la fase del
dispatch de VBlank.

Estado: Z80GB restaurado; PPU 12/12. Worktree PPU en port1 (9 PASS).

## Subfase 14 — ISR de DocBoy implementado; el offset restante es el handler

- Se implementa en `Z80GB` la secuencia ISR de DocBoy: aceptación inline en
  el boundary (dot 454), ISR1 +4, ISR2 +8, ISR3 +12 (push low + vector),
  ISR4/fetch +16. La ruta HALT conserva el dispatch antiguo.
- PPU acceptance: **12/12**.
- Traza: IRQSTART 454, VEC 13 (antes 14). Pero las escrituras BGP del
  handler siguen en 82/98/110... (DocBoy 76/92/104...): el offset de 6
  dots está en la **entrada/ejecución de las primeras instrucciones del
  handler**, no en el dispatch.
- Diffs m3 idénticos a port1: bgp 4800, obp0 6, bgp_sprites 274,
  obj_en 14, variant 158.
- Harness: ver `/tmp/opencode/intmodel_full.txt`.
- Siguiente: alinear la fase de fetch/ejecución del handler (jp/LDH/ADD)
  con DocBoy, o conservar la compensación de paleta de 6 dots. El port
  de instrucciones del CPU es otra fase grande.
- WIP: `Z80GB_intmodel_wip.*`.

## Subfase 15 — opción 2: compensación de paleta + intento object buffer

- Restaurado el estado fase 2 (compensación de paleta de 6 dots):
  **15 PASS / 10 FAIL / 8 SKIP**, PPU **12/12**.
- Modelo CPU/ISR aparcado (`Z80GB_intmodel_wip.*`).
- Intento de **object buffer por línea** (estilo SameBoy):
  - obj_en 14 (igual), variant 111 (igual).
  - obj_size empeora: 310 -> 576; obj_size_scx 190 -> 754.
  - El resto de tests verdes se mantienen.
  - Revertido: el size change a mitad de línea necesita modelar el
    fetch/altura por sprite, no reconstruir el buffer de la línea.
- Siguiente objetivo: state machine de ventana (wx_6 13811, wx_0 263,
  win_en_multiple_wx 2067), que es el bloque de fallos más grande y no
  depende del modelo de CPU.

## Subfase 16 — ventana: knobs descartados

- `wx_0` en la base fase 2: borde del window por línea:
  - SCX>0: QtreGB 1 px tarde respecto al PNG (excepto línea 1, +2).
  - SCX=0: línea 0 correcta (11), líneas 8/16 en 4 (esperado 11).
- Probar `QTR_WX0START` (trigger WX=0 en lx=11 desde el inicio de línea):
  rompe todos los tests de ventana (wx_4 15026, win_map 754, etc.).
- Probar `QTR_WX0DELAY0` (quitar el delay de 1 T con SCX>0):
  wx_0 empeora 263 -> 372; el delay es necesario en este modelo.
- Conclusión: el modelo de ventana actual es localmente óptimo; el
  residual (wx_0 263, win_en_multiple_wx 2067) requiere el state machine
  completo (trigger en tile boundary, latch de enable, arranque WX=0 por
  línea), no knobs. `wx_6` sigue dependiendo de la fase CPU.
- Base restaurada: 15 PASS / 10 FAIL / 8 SKIP, PPU 12/12.

## Subfase 17 — ventana: latch de disable (parcial)

- Implementado latch de disable al siguiente tile push:
  - win_en_change_multiple_wx: 2067 -> **1450** (mejora).
  - win_en_change_multiple: 0 -> **487** (rompe).
- Causa: el test que pasa alterna off/on con poco espacio; cancelar el
  disable si el enable llega antes del push no reproduce el boundary de
  tile exacto. El state machine correcto debe **encolar** los eventos
  enable/disable y aplicarlos en el boundary de tile, no cancelarlos.
- Revertido: base 15 PASS / 10 FAIL / 8 SKIP, PPU 12/12.
- Siguiente: port completo del state machine de ventana (cola de eventos
  + tile boundary + window line por activación).

## Subfase 18 — ventana: muestreo WIN_EN en tile boundary (conservado)

- En vez de latch/cancelar, se muestrea WIN_EN al inicio de cada tile
  fetch (`BgTilemap`): si está off, el tile actual termina como ventana y
  el siguiente pasa a BG. Si el enable vuelve antes del boundary, la
  ventana continúa.
- Resultado:
  - win_en_change_multiple_wx: 2067 -> **1556**.
  - win_en_change_multiple: **PASS** (se recupera).
  - window_timing, wx_4/5, win_map, tile_sel_win: PASS.
  - wx_0 263, wx_6 13811 (sin cambio).
- Sin regresiones en la lista verde. PPU 12/12.
- El residual de win_en_multiple_wx (1556) está en los segmentos de
  activación/desactivación y en la fase del handler (WX=LY y toggles
  caen ~6 dots tarde); probablemente comparte la fase CPU.
- WIP: `ppu_winboundary_wip.*`.

## Subfase 19 — ventana: fila en trigger (DocBoy) + límite del abort

- La fila de ventana ahora se asigna/incrementa en el **trigger**, no en la
  activación (como DocBoy). Neutro para win_en_multiple_wx porque su mapa
  de ventana es uniforme, pero correcto y sin regresiones.
- Base: 15 PASS / 10 FAIL / 8 SKIP, PPU 12/12.
- win_en_multiple_wx sigue en 1556. El residual está en la
  **activación/abort** cuando WIN_EN se toca durante el fetch: el test
  apaga/enciende la ventana durante mode 3 y el hardware puede abortar la
  activación antes del primer píxel. Nuestro muestreo de boundary deja
  terminar el tile actual como ventana.
- Siguiente: portar el abort/disable-while-activating (DocBoy
  `w.disabled_while_activating` y el manejo DMG) o dejar el test para la
  fase CPU, que también desplaza los toggles ~6 dots.
- WIP: `ppu_winboundary_row_wip.*`.

## Subfase 20 — ventana: chequeo WIN_EN por fase (conservado)

- DocBoy comprueba `WIN_EN` en **cada fase del fetcher**, no sólo en el
  tilemap boundary. Si la ventana se apaga a mitad de fetch, las fases
  restantes de ese tile usan BG (tile partido).
- Implementado en `tick_bg_fetch`:
  `if(m_window_active && !WIN_EN) { active=false; fetch_window=false; }`.
- Resultado:
  - win_en_change_multiple_wx: 1556 -> **1412**.
  - win_en_change_multiple, window_timing, wx_4/5, win_map,
    tile_sel_win: PASS.
  - wx_0 263, wx_6 13811.
- Sin regresiones. PPU 12/12. Base 15 PASS.
- Residual 1412: timing de activación/toggles del handler (WX=LY y
  WIN_EN caen ~6 dots tarde, fase CPU) y el arranque WX=0.
- WIP: `ppu_winphase_wip.*`.

## Subfase 21 — fase CPU: desglose +4 / +2

Traza de instrucciones del handler (`m3_bgp_change`, 0x62B..0x633):

- DocBoy: 0x62B dot=30, 0x62D 42, 0x62E 46, 0x630 58, 0x632 66, 0x633 70.
- QtreGB: 0x62B dot=34, 0x62D 46, 0x62E 50, 0x630 62, 0x632 70, 0x633 74.
- **Handler +4 dots** (entrada/dispatch), constante en todas las instrucciones.
- La escritura del `LDH (C),A` (0x633) cae en 82 vs 76: +2 adicionales
  dentro de la instrucción (el write de QtreGB estaba en M2 T4).

Intento:
- ISR step 4 fetchando el handler en el mismo tick: no movió la entrada (+4 sigue).
- `in_ld_addr_r` write en M2 T2: BGP 82 -> 80 (-2), pero la compensación
  de paleta está calibrada a la fase vieja; bgp 4800->1745, obp0 6->50,
  bgp_sprites 274->1444, obj 14->88. Neto peor.
- Revertido. Base 15 PASS, PPU 12/12.

Conclusión: la fase CPU necesita un cambio coordinado:
1. Entrada del handler -4 (dispatch/ISR).
2. LDH write -2 (M2 T2).
3. Quitar la compensación de paleta de 6 dots y usar render directo.
WIP: `Z80GB_cpuphase_wip.*` (ISR + step4 + LDH t2).

## Subfase 22 — cambio coordinado CPU + render directo (no converge)

- CPU: ISR + step4 fetch + LDH M2 T2 + timer ISR 3 (steps a 4 ticks).
- PPU: render directo (sin compensación de 6 dots).
- Resultado: BGP writes 79/95 (DocBoy 76/92), aún +3; bgp 2190,
  obp0 79, bgp_sprites 2299, bg_map 728, wx_5 38, window_timing 443.
- La fase CPU por sí sola no cierra los 6 dots (queda ~3) y el render
  directo expone otras diferencias de fase del pixel/write. No converge
  con ajustes parciales.
- Revertido a la base verde: 15 PASS / 10 FAIL / 8 SKIP, PPU 12/12.
- WIP: `ppu_coordinated_wip.*`, `Z80GB_cpuphase2_wip.*`.
- Conclusión: hace falta un port dedicado de la fase de instrucciones
  completa del CPU (micro-ops/timing de fetch) y coordinarlo con el
  render directo; no es un ajuste de ISR/LDH.

## Subfase 23 — prefetch DocBoy-style (experimento, no conservado)

Objetivo: mover las escrituras/lecturas del CPU 4 dots antes (la parte de
prefetch del desglose +4/+2), sin tocar todavía la fase T1/T3.

Implementado:
- `prefetch_opcode()` en T3 del último M-cycle de la instrucción actual.
- `is_shifted_opcode()` para E2/F0/E0/EA/FA, LD r,d8, LD rr,d16,
  LD (HL),r, LD A,(BC/DE/HL±), INC/DEC (HL), etc.
- Las instrucciones desplazadas saltan su case 1 (pc++/lectura de registro)
  y usan su último M-cycle para prefetchear la siguiente; la longitud total
  se conserva, pero **blargg/mts no**: el índice de M-cycle del acceso no
  queda como el hardware espera (ver validación abajo).
- `prefetch-only` para JP/JR: no cambian su propia fase, sólo prefetchean.

Hallazgo del harness:
- Con prefetch, en el boundary `PC` apunta al operando, y el chequeo
  `m_bus.read(pc) == 0x40` daba falsos positivos (p. ej. el operando 0x40 de
  `LDH (0x40),A` en 0x4818). Hay que consultar el opcode prefetcheado
  (`peek_next_opcode()`), no el byte en PC. Con el fix, el breakpoint real
  de `m3_bgp_change` cae en ~767.516 T sin BIOS (10 frames), correcto.
- Con BIOS el boot ROM tarda ~24,2 M T (vs ~5 M esperados): el flujo del
  boot ROM se ralentiza con el prefetch; pendiente de investigar.

Resultados m3 (sin BIOS, barrido de `m_bg_palette_delay`):

| test | base | d=0 | d=2 | d=4 | d=6 |
|---|---:|---:|---:|---:|---:|
| m3_bgp_change | PASS | 3008 | 1184 | 1745 | 2000 |
| m3_bgp_change_sprites | PASS | 2534 | 2462 | 2566 | 2726 |
| m3_obp0_change | 6 | 432 | 432 | 432 | 432 |
| m3_lcdc_obj_en_change | 14 | 146 | 146 | 146 | 146 |
| m3_wx_4_change | PASS | 16 | 16 | 16 | 16 |
| m3_wx_5_change | PASS | 38 | 38 | 38 | 38 |

Lectura: el prefetch mueve **todos** los accesos ~4 dots antes. La PPU
tiene delays por señal calibrados a la fase vieja (BG 6, ventana/OBJ 0).
Bajar sólo el BG a 2 no recupera la base; obp0/obj_en/wx_4/wx_5 no dependen
del delay de BG y empeoran igual. No es un cambio drop-in: exige recalibrar
coordinadamente todos los delays de señal del PPU (o portar el pipeline de
DocBoy completo) y revisar el boot ROM.

Validación CPU del prefetch (WIP aplicado, con el fix de `peek_next_opcode`):

| suite | resultado | fallos |
|---|---:|---|
| doctest | **62/62** (232) | — |
| blargg | **12/15** | `instr_timing`, `01-read_timing`, `03-modify_timing` |
| mts | **31/34** | `rapid_toggle`, `div_timing`, `pop_timing` |
| PPU acceptance | **10/12** | `lcdon_timing`, `lcdon_write_timing` |

Conclusión: el desplazamiento genérico (salta case 1 + prefetch al final)
no reproduce el modelo de micro-ops de DocBoy. Aunque la longitud total de
la instrucción se conserva, los accesos quedan en el M-cycle equivocado
para blargg/mts. Hace falta portar la **secuencia de micro-ops por
instrucción** (cuál es el último micro-op, en qué fase se hace el flush de
read/write y cómo se cuenta el M-cycle), no un desplazamiento uniforme.
El prefetch, tal cual, no es viable.

Estado: prefetch revertido. Base restaurada y validada:
- PPU acceptance **12/12**
- doctest **62/62** (232 assertions)
- blargg **15/15**
- mts **34/34**
- harness **15 PASS / 10 FAIL / 8 SKIP**

WIP:
- `tmp/fetcher-proto/Z80GB_prefetch_wip.{cpp,h}`
- `tmp/fetcher-proto/mts_runner_peek_wip.cpp` (fix del magic breakpoint)
- `tmp/fetcher-proto/ppu_prefetch_bgdelay_wip.cpp` (knob QTR_BGDELAY)

## Subfase 24 — CPU micro-ops: esqueleto en paralelo (conservado)

- `core/inc/cpu_micro.h` + `core/src/cpu_micro.cpp`: motor DocBoy-style.
  - 4 fases por M-cycle (`t0..t3`).
  - `read_request`/`flush_read` en t3; `write_request`/`flush_write` en t1.
  - `fetch` como último micro-op de cada instrucción.
  - Tabla `Instruction[256]` con micro-ops; longitud = número de micro-ops.
- Primera familia portada: NOP, `LD r,r'` y `LD r,d8` (sólo registros).
- **No está cableado en QtreGB**: el CPU activo sigue siendo `Z80GB`; el
  motor nuevo vive en paralelo y se prueba aislado.
- Test: `tests/test_cpu_micro.cpp` (fetch en t3, 1/2 M-cycles, orden de
  lecturas, sin writes en la primera familia).
- Gate: doctest **63/63** (275 assertions), blargg **15/15**, mts **34/34**,
  PPU **12/12**, harness **15/10/8**.

Referencias usadas:
- DocBoy `instructions` / `instructions_cb` (secuencia de micro-ops).
- Pan Docs / gbops (opcodes, ciclos, flags).
- AntonioND, *The Cycle-Accurate Game Boy Docs* (fases de bus).
- Blargg `mem_timing-2` / `instr_timing` y mts (árbitro empírico).

Siguiente: familia 2 (memoria): `LD (HL),r`, `LD r,(HL)`,
`LD (HL),d8`, `LDH (a8),A` / `LDH A,(a8)`, `LD (a16),A` / `LD A,(a16)`,
`PUSH`/`POP`. Añadir test de writes en t1 y reads en t3 antes de
integrarla.

## Subfase 25 — CPU micro-ops: familia 2 (memoria)

Portada al motor paralelo `CpuMicro`:

- `LD (BC/DE/HL),A` y `LD A,(BC/DE/HL)` (`0x02/0x12/0x70-0x77`,
  `0x0A/0x1A/0x46-0x7E`).
- `LD (HL),d8` (`0x36`).
- `LDH (a8),A` / `LDH A,(a8)` (`0xE0/0xF0`).
- `LD (a16),A` / `LD A,(a16)` (`0xEA/0xFA`).
- `PUSH` / `POP` BC, DE, HL, AF (`0xC5/D5/E5/F5`,
  `0xC1/D1/E1/F1`).
- Secuencias copiadas de DocBoy: write request en t0 + flush en t1;
  read request en t0 + flush en t3; el último micro-op es `fetch`.
- `read_reg16`/`write_reg16` (AF enmascara F a 0xF0).

Tests nuevos (`tests/test_cpu_micro.cpp`):
- `LD (HL),B` escribe en **t1** del M-cycle correcto (tick 29 = M7 t1).
- `LD B,(HL)` lee en **t3** (tick 23 = M5 t3).
- `LD (HL),d8`: lectura del inmediato en t3 y write en t1.
- `LDH`/`LD (a16)` en ambos sentidos.
- `PUSH`: dos writes en t1, SP decrementado; `POP`: dos reads en t3.
- `LD (BC),A`: write en t1.

Gate: doctest **73/73** (324 assertions), blargg **15/15**, mts **34/34**,
PPU **12/12**, harness **15/10/8**.

Siguiente: familia 3 (control): `JP`, `JR`, `CALL`, `RET`, `RST`,
`PUSH/POP` ya hechos. Después CB y la ISR.

## Subfase 26 — CPU micro-ops: familia 3 (control de flujo)

Portada al motor paralelo `CpuMicro`:

- `JP a16` (`0xC3`), `JP cc,a16` (`0xC2/CA/D2/DA`), `JP (HL)` (`0xE9`).
- `JR e` (`0x18`), `JR cc,e` (`0x20/28/30/38`).
- `CALL a16` (`0xCD`), `CALL cc,a16` (`0xC4/CC/D4/DC`).
- `RET` (`0xC9`), `RET cc` (`0xC0/C8/D0/D8`), `RETI` (`0xD9`).
- `RST n` (`0xC7/CF/D7/DF/E7/EF/F7/FF`).
- `LD SP,d16` (`0x31`) y `LD SP,HL` (`0xF9`).

Mecanismo de condicionales:
- El micro-op de condición llama a `fetch()` si no se cumple.
- `fetch()` ahora pone `m_instruction = nullptr`; el motor detecta el cambio
  y termina la instrucción, saltándose los micro-ops restantes.
- Longitudes resultantes: `JP cc` 3/4 M-cycles; `JR cc` 2/3;
  `CALL cc` 3/6; `RET cc` 2/5.
- `check_condition()` usa los bits 3-4 del opcode (NZ/Z/NC/C).
- `RETI` activa `m_ime` en su micro-op de set PC.

Tests nuevos:
- `JP`, `JP Z` tomado/no tomado, `JR` y `JR NZ` tomado/no tomado.
- `CALL` con writes de la dirección de retorno en t1.
- `CALL Z` no tomado (3 M-cycles, sin writes).
- `RET` y `RET Z` tomado/no tomado; reads de la pila en t3.
- `RST 38h`, `RETI` (IME), `JP (HL)`, `LD SP,d16`/`HL`.

Gate: doctest **88/88** (377 assertions), blargg **15/15**, mts **34/34**,
PPU **12/12**, harness **15/10/8**.

Siguiente: CB (registro y `(HL)`) y después la ISR.

## Subfase 27 — CPU micro-ops: familia 4 (CB)

Portada al motor paralelo `CpuMicro`:

- Prefijo `0xCB`: `cb_prefix` → `fetch_cb()` lee el segundo opcode en t0 y
  lo confirma en t3; el siguiente M-cycle decodifica `cb_table[sub]`.
- Grupo rotate/shift (RLC/RRC/RL/RR/SLA/SRA/SWAP/SRL):
  - registro: 2 M-cycles (prefijo + operación + fetch).
  - `(HL)`: 4 M-cycles (prefijo, read en t3, modify+write en t1, fetch).
- BIT:
  - registro: 2 M-cycles.
  - `(HL)`: 3 M-cycles (prefijo, read en t3, test + fetch).
- RES/SET:
  - registro: 2 M-cycles.
  - `(HL)`: 4 M-cycles (read en t3, write en t1, fetch).
- Flags:
  - rotate/shift: Z/N/H/C.
  - BIT: Z/N/H, C sin tocar.
  - RES/SET: no tocan flags.
- `m_fetching_cb` evita marcar el prefijo CB como boundary de instrucción.
- `cb_rotate()` implementa los 8 grupos con el carry correcto.

Tests nuevos:
- `SWAP A` (registro) y `SWAP (HL)` con read en t3 y write en t1.
- `BIT 7,A` (flags) y `BIT 0,(HL)` (read t3, sin write).
- `SET 3,B` (resultado y flags intactos).

Gate: doctest **93/93** (404 assertions), blargg **15/15**, mts **34/34**,
PPU **12/12**, harness **15/10/8**.

Siguiente: ISR, y después cablear el CPU nuevo y recalibrar la PPU.

## Subfase 28 — CPU micro-ops: ISR e interrupciones

Portado al motor paralelo `CpuMicro`:

- Estados `InterruptState`: `None` / `Pending` / `Serving`.
- `check_interrupt(phase)` en las 4 fases, con el mapeo de DocBoy:
  t0→3, t1→0, t2→1, t3→2.
- Timing DMG normal: 1 M-cycle de retardo, 2 si se detecta en phase 3.
  Tabla `HALTED` incluida para cuando se porte `HALT`.
- Contador `remaining_ticks` decrementado en t0; servicio en boundary
  (`microop_index == 0` y `end_opcode`).
- `ImeState` de 4 estados: `Disabled → Requested → Pending → Enabled`.
  - `EI` pone `Requested`; avanza un estado al final de cada instrucción,
    así el interrupt no se acepta hasta después de la instrucción siguiente.
  - `DI` desactiva; `RETI` activa directamente.
- ISR de 5 micro-ops:
  1. `pc--` (deshacer el prefetch).
  2. `sp--`.
  3. push PC high (write en t1).
  4. push PC low (write en t1) + vector + limpiar IF.
  5. `ime=Disabled`, estado None, `fetch()` del handler.
- Prioridad: bit más bajo de `IE & IF & 0x1F`; vectores
  0x40/0x48/0x50/0x58/0x60.
- El IF se limpia en el micro-op 3 (después del push high), replicando la
  semántica de cancelación de DocBoy.
- `request_interrupt()`, `read_if/write_if`, `read_ie/write_ie` públicos
  para test y futura integración.

Tests nuevos:
- VBlank ISR tras `EI` + `NOP`: return address 0x0102, SP=0xFFFC,
  pushes en t1, fetch del vector en t3, IF limpiado, IME desactivado.
- Prioridad VBlank sobre STAT: vector 0x40, IF queda con STAT pendiente.

Gate: doctest **95/95** (420 assertions), blargg **15/15**, mts **34/34**,
PPU **12/12**, harness **15/10/8**.

Siguiente: cablear `CpuMicro` en `QtreGB` sustituyendo `Z80GB` y ejecutar
las suites completas; después recalibrar la PPU y los m3.

## Subfase 29 — CPU micro-ops: set completo e integración tras flag

Set portado:
- ALU A,r / ALU A,d8, INC/DEC r y (HL), 16-bit (LD rr,d16,
  INC/DEC rr, ADD HL,rr), LD (a16),SP, ADD SP,e, LD HL,SP+e,
  LD (HL±),A / LD A,(HL±), LD (C),A / LD A,(C), RLCA/RRCA/RLA/RRA,
  DAA, CPL, SCF, CCF, HALT.
- Sólo quedan sin implementar `STOP` (0x10) y los 11 opcodes inválidos.
- `is_opcode_implemented()` permite auditar el set; faltan 12/256.

Integración:
- Flag `QTR_CPU_MICRO=1` en `common.mk`; `QtreGB` usa
  `CpuMicro` en lugar de `Z80GB` (alias `Cpu`/`CpuState`).
- Shim de compatibilidad en `CpuMicro`: `ICPURegisters`, `read_IF/IE`,
  `reset(bool)`, `is_end_m_cycle`, `is_interruption_time`, `serialize` +
  `State`, `read_register/16`, `connect_error_reporter`.
- `peek_next_opcode()` en ambos CPUs para el magic breakpoint de mts.
- Los callbacks IF/IE del `InterruptController` se enganchan al CPU activo
  bajo el flag.

Resultados con `QTR_CPU_MICRO=1`:
- doctest **102/102** (487 assertions).
- blargg **11/15**: fallan `02-write_timing`, `03-modify_timing`,
  `instr_timing` (timeout); `03-op sp,hl` corregido (rr=3 es SP en
  INC/DEC rr y ADD HL,rr).
- mts **33/34**: sólo falla `rapid_toggle` (1 assertion).
- PPU/m3 pendientes de recalibrar; `lcdon_timing-GS` falla.

Diagnóstico:
- El set funcional está completo; mts 33/34 valida ISR, timer e
  instrucciones.
- Los fallos de `02-write_timing`/`03-modify_timing` son de **fase**: el
  CPU nuevo escribe en t1 y el timer/PPU de QtreGB están calibrados a
  escrituras en t3. No es un bug del set.
- `instr_timing` timeout y `rapid_toggle` apuntan al mismo acoplamiento
  timer/CPU.
- Siguiente bloque: alinear timer/PPU con el write en t1 (portar el tick
  del timer de DocBoy en t2 o ajustar la observación del bus), y después
  recalibrar los m3.

Base por defecto restaurada y verde:
- doctest **102/102** (487), PPU **12/12**, blargg **15/15**, mts **34/34**,
  harness **15/10/8**.

## Subfase 30 — diagnóstico timer/fase de escritura (shim descartado)

- Con `QTR_CPU_WRITE_T3=1` (flush de escritura en t3 dentro del mismo
  M-cycle): blargg **15/15**, mts **33/34** (sólo `rapid_toggle`).
  Esto confirma que el set de instrucciones es correcto y que el resto de
  fallos de blargg son de fase.
- `rapid_toggle` no depende de t1 vs t3: falla con ambos. Es la fase
  **absoluta** del prefetch (un M-cycle antes) respecto al timer.

Intento de shim: buffer de escrituras al timer con retardo configurable
(global y por registro), aplicado en `begin_cpu_phase`:
- Retardo global 4 T: `rapid_toggle` PASS y blargg 15/15, pero mts
  **24/34** (`tim00`, `tim01`, `tim10`, `tim11`, `tima_reload`,
  `intr_timing`, `div_timing`, `pop_timing` fallan).
- Máscaras: TIMA/TAC/TMA solos no arreglan `rapid_toggle`; DIV solo lo
  arregla, pero rompe `div_timing`/`tim00`.
- DIV delay 1-3: `rapid_toggle` C=DA (mal); 4+: C=E2 (peor).
- Conclusión: no existe un retardo único ni una máscara simple que
  satisfaga blargg y mts. El modelo per-T del timer no se puede alinear
  con un offset; hace falta portar el **timer de DocBoy** (tick por
  M-cycle en t2, `div16 += 4`, ventana de reload/load por M-cycle) o
  rediseñar el timer con la misma semántica.

Estado: shim y cambios de timer revertidos desde
`tmp/20261115_013151/`. Base por defecto restaurada y verde. El flag
`QTR_CPU_WRITE_T3` queda como diagnóstico (default off) en `common.mk` y
`cpu_micro.cpp`.

Siguiente: portar el timer de DocBoy y revalidar mts/blargg; después
recalibrar la PPU/m3.

## Subfase 31 — intento de port del timer de DocBoy (revertido)

Se portó el modelo de `Timers` de DocBoy bajo `QTR_CPU_MICRO`:
- `tick()` una vez por M-cycle en T2, `div16 += 4`.
- Detección de flanco del bit de DIV seleccionado por TAC.
- `TIMA` overflow: `Pending` → `Reload` en el siguiente M-cycle.
- `write_tima` ignorado en Reload; `write_tma` en Reload también escribe TIMA.
- DIV inicial 0xABCC; `QtreGB::tick_once` tickea el timer sólo en T2.

Resultados con CPU micro y write en t1:
- `rapid_toggle`: **PASS**.
- mts: **24/34** (10 tests de timer/div/intr fallan).
- blargg: **12/15** (`02-write_timing`, `03-modify_timing`, `instr_timing`).

Conclusión: el timer de DocBoy está acoplado a su scheduler/CPU/PPU
completo; no es un drop-in sobre el scheduler per-T de QtreGB. Los tests
mts de QtreGB están calibrados al modelo per-T actual y el modelo per-M
rompe sus ventanas de reload/load.

Revertido a `tmp/timer.pre-docboy.*` y al orden de `tick_once` original.
Base por defecto restaurada y verde.

Mejor estado del CPU micro:
- timer actual + write t1: mts **33/34** (sólo rapid_toggle), blargg **11/15**.
- timer actual + `QTR_CPU_WRITE_T3=1`: blargg **15/15**, mts **33/34**.

Siguiente decisión: portar el pipeline completo DocBoy (scheduler + timer
+ PPU de fases) o aceptar `rapid_toggle`/write timing como deuda conocida y
priorizar la recalibración PPU/m3 con el CPU micro.

## Subfase 32 — timer DocBoy en T3: CPU+timer verdes

Hallazgo clave: DocBoy tickea el timer en **T3** (no T2). El port anterior
(tick T2) rompía mts; con T3:

- CPU micro (write t1) + timer DocBoy (tick T3, `div16 += 4`):
  - mts **34/34**
  - blargg **15/15**
  - `rapid_toggle` PASS
  - `02-write_timing`, `03-modify_timing`, `instr_timing` PASS
- Implementado bajo `QTR_CPU_MICRO`; la base por defecto mantiene el CPU y
  timer viejos.
- `QtreGB::tick_once` bajo el flag: CPU en cada T, `timer_tick()` sólo en
  T3, PPU después.

Pendiente:
- doctest con `QTR_CPU_MICRO=1`: **439/489**, 50 fallos. Son los tests del
  timer viejo (`test_timer.cpp`, `test_bus.cpp`) que asumen semántica per-T;
  hay que adaptarlos/duplicarlos para el modelo DocBoy. No son fallos del CPU.
- PPU/m3: el frame m3 sale **en blanco** con el CPU/timer nuevos. En el
  breakpoint: LCDC=0x91, BGP=0xFC, LY=0, mode=0, VRAM tile data no cero,
  tile map a cero, framebuffer `nonwhite=0`. El LCD está encendido y el
  tile data escrito; el problema es la fase PPU/IRQ (escrituras LCDC/BGP y
  STAT/VBlank con la nueva fase), no la VRAM.
- PPU acceptance: **6/12** con el CPU/timer nuevos.

Siguiente bloque: portar/alinear la fase PPU (y los IRQ STAT/VBlank) con el
CPU/timer DocBoy, y adaptar los tests del timer.

## Subfase 33 — PPU/IRQ: breakpoint real y fase del VBlank

- El frame m3 en blanco era un **breakpoint falso**: el runner paraba en
  PC=0x60 (vector Joypad, byte 0x40) antes del `ld b,b` real.
- Añadido filtro en `mts_runner`: en `MTS_FRAME_OUT`, ignorar magic
  breakpoints con `state.pc < 0x0100`.
- Con el CPU micro, el breakpoint real cae en ~23,47M T (la base ~24,21M),
  pero el estado es distinto:
  - Base: LCDC=0x83, BGP=0xF8, LY=0x90 (VBlank), mode=1,
    nonwhite=16176 (frame completo).
  - Micro: LCDC=0x91, BGP=0xFC, LY=0x40, mode=0,
    nonwhite=746 (frame parcial).
  - El handler de VBlank corre en LY=0x40 (mode 0) en vez de VBlank: la
    fase de la IRQ VBlank/STAT con el CPU/timer nuevos está desplazada.
- El PPU sí renderiza (746 píxeles no blancos), pero el frame del
  breakpoint no es el esperado.
- PPU acceptance: 6/12 con el CPU/timer nuevos.

Siguiente: alinear la fase PPU/IRQ (dot de la IRQ VBlank/STAT, entrada del
handler y `m_vblank_irq_dot`) con el CPU/timer DocBoy; después los m3.
La base por defecto sigue verde.

## Subfase 34 — PPU/IRQ: STAT no se dispara con el CPU micro

Traza de IRQ (QTR_IRQDBG) sobre m3_bgp_change con BIOS:

Base (CPU/PPU viejos):
- PPU VBlank IRQ en dot=453, LY=144, mode=0.
- STAT mode 2 en dot=1, LY=3 (y HBlank dot=453, LY=4...).
- Handler STAT 0x62B en dot=34-38, LY=3/4.
- Handler VBlank 0x61D en dot=34, LY=144.
- `ld b,b` 0x626 en dot=74, LY=144 (frame completo).

CPU micro + timer DocBoy:
- PPU VBlank IRQ sigue en dot=453, LY=144.
- **No hay ninguna PPUIRQ STAT** y no se entra a 0x62B/0x61D/0x626.
- El CPU no habilita/atiende el STAT; el frame se dumpea en un 0x40
  distinto (LY=0x40, frame parcial).

Diagnóstico: la escritura del registro STAT (y la fase del tick del PPU)
con el CPU en t1 no llega a habilitar la IRQ STAT en el modelo actual del
PPU. No es un bug del CPU/timer: mts/blargg están verdes. Es la fase
PPU/IRQ (escritura de STAT/LCDC y `update_stat_irq`) la que está calibrada
al CPU viejo (t3). El PPU necesita el port/alineación de fase DocBoy.

La base por defecto sigue verde; el CPU micro+timer queda tras el flag.

## Subfase 35 — PPU con CPU micro: 15 PASS y calibración por registro

### El falso breakpoint

El "atasco en wait_vblank" era un falso positivo del harness:
`CpuMicro::peek_next_opcode()` devolvía `m_io_data`, que todavía contenía
la lectura anterior (p. ej. LY=0x40) mientras el fetch estaba en vuelo.
El runner creía ver un `0x40` y dumpeaba un frame parcial en LY=0x40.

Fix: si `m_fetching` y hay lectura pendiente, `peek_next_opcode()` lee
`m_read_addr` (la dirección real del opcode). Con eso el CPU micro llega
al breakpoint real de los tests y el frame deja de ser parcial.

### Calibración PPU/CPU

El PPU estaba calibrado para escrituras del CPU viejo en T3; el CPU micro
escribe en T1. En vez de reescribir el PPU, se añadió un adaptador de fase
bajo `QTR_CPU_MICRO`:

- `LCD::apply_delayed_writes()` al final de `LCD::tick()`: retrasa las
  escrituras de PPU 3 T (T1 -> T4 observable), excluyendo LCDC para no
  romper el encendido.
- Retardos por registro: BGP=4, OBP0=4, SCX=4, SCY=4, resto=3;
  BGP en LY=0 = 8 (el primer frame tiene otra fase).
- Ventana de glitch `BGP | last_BGP`: se captura el valor previo al apply
  (`m_bgp_pre_tick`) para no perder el OR de 1 T.
- Vista retardada de LCDC para el pipeline: histórico de 4 T, con retardos
  por bit (BG_EN=4, OBJ_EN=4, OBJ_SIZE=4, BG_MAP=3, TILE_DATA=3, WIN_EN=3,
  WIN_MAP=3). El poder/lectura de LCDC siguen inmediatos.
- `tile_data` en LY=0 con retardo 5 (arregla `tile_sel_win`).
- `m_stat_delay_oam = 0` (aceptación PPU 6/12 -> 8/12).
- `m_win_startup_wait_ly0 = 2` (arregla `window_timing`, diff 1 -> 0).
- Glitch de reactivación de ventana con WX retardado 2 T adicionales
  (`m_wx_glitch_delay`), y activación WX=0 según `SCX & 7`.

Todos los valores están como defaults bajo `#ifdef QTR_CPU_MICRO` en
`core/src/ppu.cpp` y `core/src/lcd.cpp`; las variables `QTR_*` siguen
disponibles para seguir calibrando.

### Resultados con el build micro limpio

- `mts`: 34/34
- `blargg`: 15/15
- PPU acceptance: 8/12
- doctest: 437/489 (52 fallos, todos de `test_timer.cpp`/`test_bus.cpp`,
  que asumen el timer per-T viejo)
- Harness PPU: **15 PASS / 10 FAIL / 8 SKIP**
  - Pasan: dmg-acid2, m2_win_en_toggle, bgp_change, bgp_change_sprites,
    bg_map_change, win_en_change_multiple, win_map_change, obp0_change,
    scx_high_5_bits, scx_low_3_bits, window_timing, wx_4_change,
    wx_4_change_sprites, wx_5_change, tile_sel_win_change.
  - Fallan (diff): bg_en 140, obj_en 16, obj_en_variant 112, obj_size 310,
    obj_size_scx 190, tile_sel 124, win_en_multiple_wx 1406, scy 1039,
    wx_0 263, wx_6 13809.

### Base por defecto

La base (sin `QTR_CPU_MICRO`) sigue verde:
doctest 102/102, PPU 12/12, mts 34/34, blargg 15/15, harness 15/10/8.
Toda la calibración micro está aislada con `#ifdef`.

Backup de esta sesión: `tmp/20261115_233557/`.

Siguiente: cerrar los 10 m3 restantes. Los candidatos con diff pequeño
(obj_en 16, tile_sel 124, bg_en 140, obj_size 310) necesitan muestrear el
registro en la fase exacta del fetcher, no un retardo global; los grandes
(scy 1039, multiple_wx 1406, wx_6 13809) apuntan al fetcher de ventana/SCY
tipo DocBoy. La alternativa limpia sigue siendo portar el fetcher completo.

## Subfase 36 — Port completo del PPU DocBoy: 25/0/8

### Arquitectura

Se añadió un PPU completo seleccionable:

- `QTR_PPU_FULL=1` compila `PPUFull` (`core/inc/ppu_full.h`,
  `core/src/ppu_full.cpp`) sobre el PPU real de DocBoy
  (`core/src/docboy_ppu.cpp` y `core/inc/docboy/...`).
- `QTR_CPU_MICRO=1` activa `QTR_PPU_FULL=1` por defecto en `common.mk`.
- La base sin flags sigue usando `PPULegacy` (`core/src/ppu.cpp`).
- `QTR_PPU_FULL=0` permite construir el micro con el PPU legacy.

El wrapper `PPUFull` adapta:

- `Lcd` → framebuffer `m_video_buffer` y callback de frame.
- `Interrupts` → `IT_VBLANK` / `IT_LCD_STAT`.
- `Dma` → estado activo para el arbitraje de OAM.
- `VramBus` / `OamBus` → `m_vram`, OAM y bloqueos `VideoBus` del CPU.
- El bus enruta FF40–FF4B por `read_lcd_register` /
  `write_lcd_register`; `PPULegacy` reenvía a `LCD`.
- `LY`, `STAT`, paletas y scroll se reflejan en el `LCD` de QtreGB para
  debug/GUI.

Se copiaron las utilidades mínimas de DocBoy (`utils/arrays.h`,
`asserts.h`, `bits.h`, `bitrange.h`, `fillqueue.h`, `queue.h`, `vector.h`,
`docboy/common/specs.h`, `docboy/ppu/pixelmap.h`).

### Resultados con `QTR_CPU_MICRO=1` (PPU completo)

Con BIOS DMG:

- Harness PPU: **25 PASS / 0 FAIL / 8 SKIP**.
- `dmg-acid2` y los 24 `m3_*` con PNG esperado: **diff = 0**.
- PPU acceptance: **12/12**.
- mts: **34/34**.
- blargg: **15/15**.
- doctest: **89/89**, 416 assertions.

Sin BIOS quedan fallos de fase; no es el camino objetivo. Los PNG
`_dmg_blob` y el objetivo se validan con BIOS.

### Base por defecto (sin flags)

Sigue verde y usa `PPULegacy`:

- doctest 102/102 (487 assertions)
- PPU acceptance 12/12
- mts 34/34
- blargg 15/15
- harness 15 PASS / 10 FAIL / 8 SKIP

### Tests del timer

`tests/test_timer.cpp` conserva la suite per-T para el timer viejo
(`#ifndef QTR_CPU_MICRO`) y añade cuatro tests del timer DocBoy bajo el
flag. El test de bus ajusta DIV inicial a 0xAB en micro.

### Build

```sh
make clean
make -C core QTR_CPU_MICRO=1 -j$(nproc)
make -C mts QTR_CPU_MICRO=1 -j4
make -C blargg QTR_CPU_MICRO=1 -j4
make -C tests QTR_CPU_MICRO=1 -j4
```

Siempre `make clean` al cambiar flags: los objetos no dependen del flag y
mezclar layouts provoca segfaults.

Backup del port: `tmp/20261115_234441/`.
