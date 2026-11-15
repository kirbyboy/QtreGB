# F5 — Pipeline de píxeles nuevo (implementación)

## Estado

F5 está implementado y activo. La PPU ya no usa el fetcher V2 ni el
`m_object_array` para pintar:

- OAM scan por dots con entradas por LX (`m_oam_entries[168]`).
- BG/window prefetcher con FIFO de 8 píxeles.
- OBJ fetcher con `m_obj_pixels` y prioridad por X/número de OAM.
- Pixel transfer por estados: `Dummy`, `Discard`, `Lx0`, `Lx8`.
- `m_video_buffer` se escribe en `lx-8` (0..159).

El fetcher V2 (`Fetcher`, `FIFO`, `m_object_array`, `pipeline_process`,
`pipeline_push_pixel`) fue eliminado en F5.4. El camino activo es únicamente
el pipeline nuevo.

---

## Timing de mode 3

El pipeline usa esta secuencia de fin de línea (calibrada contra
`hblank_ly_scx_timing-GS`, `intr_2_0_timing` y el harness):

- **Warm-up de 4 dots**: la FIFO no empieza a drenar hasta el dot 85. El
  fetcher necesita su primer acceso antes de producir el píxel 0. Sin esto
  mode 0 empezaba en el dot 248 (169 dots de mode 3) en vez del mínimo
  hardware de 172.
- **OAM STAT IRQ en el dot 452** de HBlank (`update_stat_irq_for_oam_mode`).
  En hardware el IRQ de OAM se señaliza ahí, no en el dot 454.
- **STAT mode 0 1 M-cycle antes** del cambio real de modo: se eleva cuando
  `m_lx == 163` (5 dots antes del fin de mode 3).
- **`LY++` en el dot 448** de este scheduler. El valor nominal de hardware es
  453; la diferencia es la fase CPU/PPU de este emulador. La combinación
  `OAM=452`, `HBlank lx=163`, `LY=448` pasa a la vez
  `intr_2_0_timing`, `hblank_ly_scx_timing-GS` y el resto de tests STAT.
- **Quirk de VBlank**: el bit OAM también dispara STAT al entrar en VBlank
  (one-shot en el dot 454 de la línea 143), separado del OAM de líneas
  visibles.
- **Latch de fila BG**: la fila del tile (`SCY + LY`, o `m_window_line`) se
  latchea al leer el byte del tilemap. El área de datos de LCDC se vuelve a
  muestrear en `BgHigh` para que los tests de `LCDC tile select` sigan
  funcionando. Esto evita mezclar filas si SCY cambia entre los dos accesos y
  mejora `m3_scy_change` (11271 → 10773).

Con esto pasan 11 de los 12 tests PPU de aceptación; solo queda
`intr_2_mode0_timing_sprites`.

---

## Sprites X<8 y OBJ fetcher

Los sprites con `X < 8` están fuera de pantalla a la izquierda (la coordenada
X está desplazada 8). El pipeline ahora:

- Dispara el fetch del sprite durante `Lx0`/`Discard` cuando `m_lx == X`.
- Pausa los pops de BG mientras el OBJ fetcher trabaja. La guarda de
  `m_is_fetching_sprite` se comprueba **antes** de mirar el siguiente sprite:
  si no, dos sprites en la misma X arrancaban el fetch en dots consecutivos y
  se pisaban.
- Hace **2 dots por acceso** (`m_obj_fetch_wait`): low, high y push, con un
  `m_obj_post_wait` adicional tras el push.
- Cadencia de hardware para varios sprites en la misma X: **8, 8, 4, 8, 4…**
  dots por sprite (`m_obj_fetch_seq`), que coincide con los `cycles`
  2, 4, 5, 7, 8… del test.
- Consume y descarta un píxel de OBJ por cada pop de `Lx0`, de modo que los
  sprites X<8 no se pintan pero sí consumen su tiempo de fetch.

Esto mejora `m3_lcdc_obj_size_change` (670 → 350),
`m3_bgp_change_sprites` (10282 → 3505), `m3_lcdc_obj_size_change_scx`
(350 → 190) y `m3_lcdc_obj_en_change_variant` (1334 → 650).

`intr_2_mode0_timing_sprites` ya pasa los **11 primeros testcases** (1–10
sprites en X=0). Falla en el testcase X=2 con 10 sprites: la alineación
`X mod 8` del fetch necesita un ajuste de 4 T-cycles que todavía no está
modelado. El resto de la lógica de sprites (prioridad por X/número, merge) se
mantiene.

También se eliminó un **IRQ de OAM duplicado**: se elevaba en el dot 454 y
otra vez en el 455. El segundo se ha eliminado; ahora hay uno por línea.

---

## Ventana

- La activación se hace **1 dot después del trigger** (`m_window_activating`):
  se descarta la FIFO de BG y se reinicia el fetcher con el tilemap de ventana.
- **WX=0 con SCX%8 > 0** añade un dot extra (`m_window_activation_delay`),
  como el caso `discard_lx0_wx0_scx` de DocBoy.
- El trigger compara `m_lx` con `m_last_wx + 1` (y el caso de glitch
  `+2` con `LCDC.WIN` recién activado). El `+1` es la fase de ventana que
  hace coincidir `wx_4_change`/`wx_5_change` con el patrón esperado.
- La ventana se **re-activa al inicio de cada línea** (`m_window_active` se
  limpia en el `Dummy`); la fila de ventana (`m_window_line`) avanza en cada
  activación, no al final de la línea. Así el segundo encendido de la ventana
  en la misma línea muestra la siguiente fila.
- Si `LCDC.WIN` se apaga durante mode 3, se aborta el fetch de ventana y el
  fetcher vuelve a BG en el siguiente tile; los píxeles ya metidos en la FIFO
  se drenan. La comprobación de apagado usa el `LCDC` **retardado 1 T-cycle**
  (`m_last_lcdc`), que es lo que ve la PPU.
- El fetcher de **ventana** hace 2 dots por acceso (startup de 6 dots, como
  pide `m3_window_timing`); el fetcher BG sigue a 1 dot. Este reparto mejora
  `window_timing`/`wx_0`/`tile_sel_win`, pero sube un poco `multiple_wx`
  (485 → 545).
- El pipeline de píxeles usa el `BGP` **inmediato** (`m_bg_palette`), no el
  OR con el valor retardado. Mejora `bgp_change` (3360 → 2945),
  `bgp_change_sprites` (3919 → 3505) y `window_timing`.
- El pipeline también usa el `LCDC` **inmediato** para BG/OBJ enable.
  Mejora `m3_lcdc_bg_en_change` (1814 → 1338). El `LCDC` retardado sigue
  usándose para el glitch de ventana (`win_enable_last`).

Resultado: `m3_wx_4_change` 7179 → **230**, `m3_wx_5_change` 8759 → **638**,
`m3_window_timing` 2749 → **670**, `m3_window_timing_wx_0` 2525 → **592**.

---

## Resultados

Tests base:

- `make run_tests`: **60/60, 221 assertions**.
- `make run_blargg`: **15/15**.
- `make run_mts`: **34/34**.
- Guardrails: `halt_ime0_ei` **PASS**, `instr_timing` **PASS**.
- Aceptación PPU: **11/12 PASS**. Solo falla
  `intr_2_mode0_timing_sprites.gb`.

Harness visual: **3 PASS / 22 FAIL / 8 SKIP**.

- `dmg-acid2`: **0** (primer diff=0).
- `m2_win_en_toggle`: **0**.
- `m3_lcdc_win_en_change_multiple`: **0**.

Tabla F6 → F5:

| ROM | F6 | F5 | Δ |
|---|---:|---:|---:|
| dmg-acid2 | 130 | **0** | -130 |
| m2_win_en_toggle | 3775 | **0** | -3775 |
| m3_bgp_change | 8652 | **2945** | -5707 |
| m3_bgp_change_sprites | 10556 | **3505** | -7051 |
| m3_lcdc_bg_en_change | 2657 | **1338** | -1319 |
| m3_lcdc_bg_map_change | 2386 | **1042** | -1344 |
| m3_lcdc_obj_en_change | 146 | 146 | 0 |
| m3_lcdc_obj_en_change_variant | 1334 | **650** | -684 |
| m3_lcdc_obj_size_change | 710 | **350** | -360 |
| m3_lcdc_obj_size_change_scx | 350 | **190** | -160 |
| m3_lcdc_tile_sel_change | 2574 | **1935** | -639 |
| m3_lcdc_tile_sel_win_change | 2874 | **1368** | -1506 |
| m3_lcdc_win_en_change_multiple | 9522 | **0** | -9522 |
| m3_lcdc_win_en_change_multiple_wx | 6027 | **545** | -5482 |
| m3_lcdc_win_map_change | 2276 | **772** | -1504 |
| m3_obp0_change | 432 | 432 | 0 |
| m3_scx_high_5_bits | 604 | **84** | -520 |
| m3_scx_low_3_bits | 540 | 540 | 0 |
| m3_scy_change | 11000 | **10773** | -227 |
| m3_window_timing | 2749 | **670** | -2079 |
| m3_window_timing_wx_0 | 2525 | **592** | -1933 |
| m3_wx_4_change | 7179 | **230** | -6949 |
| m3_wx_4_change_sprites | 10 | 10 | 0 |
| m3_wx_5_change | 8759 | **638** | -8121 |
| m3_wx_6_change | 13684 | 13799 | +115 |

`m3_wx_6_change` empeora ligeramente y `m3_scy_change` baja pero sigue siendo
uno de los diffs altos. `m3_scx_high_5_bits` se queda en 84, muy cerca de 0.

---

## Hallazgo: fase CPU/PPU de 4 T-cycles

Los tests de handler de mode 2 (`bgp_change`, `window_timing`, `wx_6`, etc.)
están desplazados ~4 píxeles. Al mover el IRQ OAM visible de 452 a 448:

- `m3_window_timing`: 670 → **28**
- `m3_bgp_change`: 2945 → **513**
- Pero la aceptación PPU cae de 11/12 a **6/12**.

Probé el salto de fase global de `.wip/ppu`
(`k_post_boot_ppu_phase = 4`, saltar 4 dots en el primer VBlank): **no cambia
nada** en estos tests. El motivo es que el salto desplaza a la vez el IRQ de
STAT y el muestreo de píxeles, así que la fase relativa del handler queda
igual.

La diferencia de ~4T no es un déficit del dispatch: quitar el M-cycle extra
del dispatch (`case 5`) mantiene `instr_timing` en PASS y no cambia ningún diff
mealybug.

Con OAM en 448 los handlers mealybug mejoran, pero mooneye cae a 6/12. Probé a
recalibrar HBlank (`lx` 166/167) y `LY` (448/450/452) para conservar los
intervalos: el máximo fue **2/7** tests mooneye, así que no es un offset
recuperable por calibración. La conclusión es que el desfase viene del
**estado/fase post-boot** (boot ROM), no de la fase global ni del dispatch.
Hasta que exista boot ROM opcional, se mantiene OAM 452 (mooneye 11/12) y se
aceptan los diffs de handler mealybug.

`m3_scx_low_3_bits` es otro caso: el diff está en la última columna y depende
del tile $19 de la VRAM que deja la boot ROM; el core no ejecuta boot ROM ni
inicializa ese logo/fuente. Se acepta como **limitación conocida** hasta que
exista boot ROM opcional (ver handoff).

## Lo que falta de F5

1. **`intr_2_mode0_timing_sprites`**:
   - Ya pasan los 11 primeros testcases (X=0, 1–10 sprites).
   - Falla en X=2 con 10 sprites: falta el ajuste de alineación
     `X mod 8` (4 T-cycles) del fetch de sprites.
2. **`m3_wx_6_change` / `m3_lcdc_win_en_change_multiple_wx` / `m3_scy_change`**:
   - `multiple_wx` está en 545; `wx_6_change` sigue alto (13799) y
     `scy_change` (10773) depende del timing de sprites (X=1,2,8,9,10).
3. **Prioridad sprite-sprite**:
   - El merge por X/número está implementado; falta validarlo con solapes.
4. **F5.4 — Hecho**:
   - Eliminados `Fetcher`, `FIFO`, `fetcher.h/.cpp`, `fetcher_bg.*`,
     `fifo.*`, `m_object_array`, `m_warming`, `pipeline_process`,
     `pipeline_push_pixel`, `ppu_mode_xfer`, `load_line_sprites`,
     `m_oam_line_entry`, `m_line_x`, `m_fetched`, `m_discarded_pixels`,
     `m_pixel_fifo`, `m_bg_tile_*`, `m_window_tile_*` y helpers V2.
   - Sin cambios de comportamiento: harness idéntico y suites verdes.

---

## Cómo probar

```sh
make run_tests
make run_blargg
make run_mts
./build/mts_runner vendor/mts/*/acceptance/ppu/*.gb
./build/mts_runner vendor/mts/*/acceptance/halt_ime0_ei.gb
./build/blargg_runner roms/blargg/instr_timing/instr_timing.gb
QUIET=1 REPORT=/tmp/qtrebg_frames/f5.tsv ./mts/compare_ppu_frames.sh
```

Para revertir al estado anterior a F5:

```sh
cp /tmp/opencode/f5-backup/ppu.h core/inc/ppu.h
cp /tmp/opencode/f5-backup/ppu.cpp core/src/ppu.cpp
cp /tmp/opencode/f5-backup/fetcher.h core/inc/fetcher.h
cp /tmp/opencode/f5-backup/fetcher.cpp core/src/fetcher.cpp
cp /tmp/opencode/f5-backup/fifo.h core/inc/fifo.h
cp /tmp/opencode/f5-backup/fifo.cpp core/src/fifo.cpp
make -C core
```
