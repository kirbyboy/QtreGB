# PPU v3 — F8: modelo de sprites X mod 8

Fecha: 2026-11-13. Rama `ppu-v3`.

## Problema

El modelo anterior usaba una cadencia heurística de fetch OBJ (`8, 8, 4, 8, 4…`
dots) y un camino rápido (`m_obj_fast_fetch`) para sprites en la misma X. Eso
hacía que todos los sprites del mismo tile costaran lo mismo, sin importar su
`X mod 8`. El test `intr_2_mode0_timing_sprites` fallaba en el caso de 10
sprites a X=2.

## Modelo implementado

Se sustituyó el heurístico por el algoritmo de penalización de OBJ de Pan Docs:

1. Cada OBJ cuesta **6 dots** planos (tres accesos de 2 dots).
2. El **primer OBJ de un tile** de BG/ventana paga además
   `max(0, 5 - pixel_x_dentro_del_tile)` dots de espera.
3. Los OBJ posteriores del **mismo tile** solo pagan los 6 dots planos.
4. Excepción DMG: un OBJ con OAM X=0 paga 11 dots si es el primero de su tile.

Para implementarlo:

- `m_obj_considered_tiles` guarda los tiles ya considerados en la línea.
- La clave de tile se calcula con `floor_div8`/`floor_mod8`, teniendo en cuenta
  `SCX` y la ventana activa.
- `m_obj_penalty` es la penalización total del fetch actual.
- `tick_obj_fetch()` usa siempre 1 dot de espera entre los accesos de 2 dots y
  fija `m_obj_post_wait = m_obj_penalty - 5`.
- Se eliminaron `m_obj_x_total`, `m_obj_fetch_seq`, `m_obj_fast_fetch`,
  `m_last_fetch_lx` y la cadencia 8/8/4.

Esto preserva las diferencias relativas entre sprites del mismo tile y de
tiles distintos, que es lo que valida el test.

## Resultados

- doctest: **62/62** (232 assertions).
- blargg: **15/15**.
- mts: **34/34**.
- PPU acceptance: **11/12**. Solo falla `intr_2_mode0_timing_sprites`.
- `halt_ime0_ei` e `instr_timing`: PASS.
- `dmg-acid2`: **0**.
- Harness: **3 PASS / 22 FAIL / 8 SKIP**.

El test de sprites ahora falla en el primer caso (1 sprite en X=0), a **1–2
dots** del borde de la ventana de NOP. El patrón de penalización sí es
correcto: en una variante experimental con un crédito de 3 dots en la primera
fetch de la línea, el test pasó los casos X=1, X=2, X=3… y los grupos de 5+5
sprites hasta el caso 39; el crédito se descartó porque empeoraba el harness.

### Qué pasa si se fuerza 12/12

Moviendo la IRQ OAM de 452 a 455 y el cambio STAT a modo 3 del dot 79 al 80,
`intr_2_mode0_timing_sprites` y las 12 aceptaciones PPU pasan. Pero el harness
empeora mucho porque se desplaza la fase global de los handlers:

| Test | Baseline | 12/12 forzado |
|---|---:|---:|
| `m3_bgp_change` | 2945 | 5805 |
| `m3_lcdc_bg_en_change` | 1338 | 3167 |
| `m3_lcdc_win_map_change` | 772 | 1468 |
| `m3_window_timing` | 670 | 1745 |

Por eso se revirtió: no merece la pena cambiar aceptación por fidelidad de
píxeles. El siguiente bloque debe reconciliar la fase del pipeline (startup de
12 dots real) con la IRQ/dispatch, en vez de compensarla moviendo la IRQ.

## Cambios visuales respecto al baseline

Sin BIOS:

| Test | Antes | Después |
|---|---:|---:|
| `m3_bgp_change_sprites` | 3505 | **3497** |
| `m3_lcdc_bg_en_change` | 1338 | **1316** |
| `m3_lcdc_obj_en_change_variant` | 650 | 690 |
| `m3_lcdc_obj_size_change` | 350 | 410 |
| `m3_lcdc_obj_size_change_scx` | 190 | 270 |
| `m3_lcdc_tile_sel_change` | 1935 | **1743** |
| `m3_lcdc_tile_sel_win_change` | 1368 | **1044** |
| `m3_lcdc_win_map_change` | 772 | **708** |

Con BIOS:

| Test | Antes | Después |
|---|---:|---:|
| `m3_bgp_change_sprites` | 2532 | **2524** |
| `m3_lcdc_bg_en_change` | 1338 | **1316** |
| `m3_lcdc_obj_en_change_variant` | 760 | 800 |
| `m3_lcdc_obj_size_change` | 350 | 410 |
| `m3_lcdc_obj_size_change_scx` | 190 | 270 |
| `m3_lcdc_tile_sel_change` | 1536 | **1344** |
| `m3_lcdc_tile_sel_win_change` | 976 | **662** |
| `m3_lcdc_win_map_change` | 375 | **310** |
| `m3_obp0_change` | 128 | 140 |

## Cómo probar

```sh
make -j$(nproc)
make run_tests
make run_blargg
make run_mts
./build/mts_runner vendor/mts/*/acceptance/ppu/*.gb
QUIET=1 REPORT=/tmp/qtrebg_frames/f8.tsv ./mts/compare_ppu_frames.sh
```
