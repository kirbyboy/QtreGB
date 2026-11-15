# Plan de la línea de arquitectura PPU

Fecha: 2026-11-15
Worktree: `.wip/ppu` (rama `wip/ppu`)
Principal: `ppu-v3` (rama `ppu-v3`)

## Regla de oro

La principal `ppu-v3` está congelada en **SM8**:

- Harness oficial con BIOS: **15 PASS / 10 FAIL / 8 SKIP**.
- doctest 62/62, blargg 15/15, mts 34/34, PPU acceptance 12/12.
- No se toca hasta que la línea de arquitectura supere el baseline con las
  invariantes verdes.

El worktree `.wip/ppu` es la **línea de arquitectura**. No se fusiona en la
principal hasta cumplir el criterio de aceptación final.

## Punto de partida: A.1

Snapshot: `.wip/ppu/tmp/fetcher-proto/ppu_archA1.cpp` / `.h`.

Incluye:

- Fetch OBJ alineado con DocBoy: la comprobación OBJ se hace al entrar en
  `BgPush`, antes de consumir el wait.
- `x0_extra = 0` (fudge eliminado).
- Lecturas post-sprite de `bg_map_change` LY=0: 102/104/106 (= DocBoy).
- Invariantes: doctest 62/62, blargg 15/15, mts 34/34, PPU acceptance 12/12.
- PNG en cuarentena (dependían del fudge): `bgp_change_sprites`,
  `bg_map_change`, `tile_sel_win_change`, `win_map_change`,
  `high_5_bits`, `obp0_change`.
- `arch_gate.sh`: 25/25 con el baseline actual.

## Puertas

### 1. Invariantes (hard gate, cada paso)

- doctest 62/62
- blargg 15/15
- mts 34/34 (incluye `intr_timing`, `ei_*`, `reti_intr_timing`, `halt_ime*`)
- PPU acceptance 12/12 (Mooneye `intr_*`, `lcdon_*`, `stat_*`,
  `hblank_ly_scx`, sprites)
- `dmg-acid2` diff=0

Comando:

```bash
./temp/scripts/arch_gate.sh
```

El baseline está en `temp/arch_baseline.txt`. Si una fase cambia
intencionadamente la lista de verdes, se edita ahí y se documenta.

### 2. Equivalencia de trazas (por subsistema)

```bash
./temp/scripts/trace_diff.sh --all
./temp/scripts/trace_diff.sh --all --strict-events   # eventos fatales
```

Fixtures: `temp/trace_fixtures.txt`.
Herramientas: `fetcher_trace` (QtreGB) y DocBoy instrumentado
(`DOCBOY_RDBG`, `DOCBOY_LCDCDBG`, `DOCBOY_IRQDBG`, `DOCBOY_OBJDBG`,
`DOCBOY_VECDBG`).

### 3. Tests de calibración (NO son puerta durante arquitectura)

Todos los `m3_*` con writes a mitad de línea (`bg_map`, `tile_sel`, `scy`,
`bgp`, `bg_en`, `obj_en`, `obj_size`, `wx_*`, `window_timing_wx_0`,
`multiple_wx`, `wx_6`, y las variantes `*2`). Se recalibran una sola vez al
final.

## Secuencia de la línea

### Fase 1 — Fase global CPU/PPU (siguiente)

Objetivo: mover el write de LCDC del handler de 106 a 100 sin romper Mooneye.

Enfoque:

1. Aplicar el desplazamiento de fase **después del LCD-on** o **por línea**
   (no en reset: el boot ROM reinicia la fase al encender el LCD).
2. Instrumentar el dot de IRQ, vector y write del handler en QtreGB y DocBoy.
3. Buscar el offset de fase que haga coincidir el write y las lecturas con
   DocBoy, manteniendo las invariantes.
4. Candidatos a reutilizar: A.2g/A.2h (shift STAT + liberación OAM,
   9/12 con `QTR_VBLANKIRQ=3`).

Criterio de aceptación: invariantes verdes + `trace_diff` de lecturas
idéntico en los fixtures + write del handler alineado.

### Fase 2 — Modelo de la línea STAT

Solo si la fase 1 no resuelve los tests de VBlank.

- Estado único de línea STAT (latch/edge) en vez de la mezcla actual
  `m_stat_irq_line` + `m_last_stat_irq`.
- Quirk OAM-in-VBlank como fuente del mismo nivel/edge, no IRQ aparte.
- Clear solo al entrar en mode 3 y sin LYC.
- Tests objetivo: `vblank_stat_intr`, `intr_1_2_timing`,
  `stat_irq_blocking`.

### Fase 3 — HBlank/LY/SCX y sprites

- `hblank_ly_scx_timing`
- `intr_2_mode0_timing_sprites`

### Fase 4 — Recalibración m3

- Restaurar los PNG en cuarentena y recalibrar `x0wait`, waits de ventana,
  latch de SCX, delays de paleta y `x_extra`.
- Objetivo final: **≥ 15 PASS con BIOS** (superar el baseline) y,
  si es posible, acercarse a los 24 `m3_*` + `acid2` a 0.

### Fase 5 — Porte a la principal

Solo cuando:

- todas las invariantes estén verdes,
- el harness oficial con BIOS sea **≥ 15 PASS**,
- `dmg-acid2` = 0,
- el `trace_diff` de los fixtures pase.

Entonces se copia `core/src/ppu.cpp` + `core/inc/ppu.h` (y solo esos, si no
hay más cambios) a `ppu-v3`, se compila y se revalida allí.

## WIPs guardados

En `.wip/ppu/tmp/fetcher-proto/`:

- `ppu_sm8.*` — baseline de la principal (15 PASS).
- `ppu_archA1.*` — punto de partida de la línea (invariantes 12/12,
  PNG 9 PASS).
- `ppu_archA2_statshift_wip.*` — shift STAT coherente (7/12 PPU).
- `ppu_archA2b_oamvblank_wip.*` — extensión OAM/VBlank (6/12).
- `ppu_archA2c_oamrel_wip.*` — shift + liberación OAM (8/12;
  `intr_2_oam_ok` PASS).
- `ppu_archA2d_vblankshift_wip.*` — VBlank shift (7/12).
- `ppu_archA2e_quirkknob_wip.*` — barrido del quirk (sin efecto).
- `ppu_archA2f_lastlineirq_wip.*` — IRQ de LY=143 sin shift
  (`vblank_stat_intr` PASS, rompe blocking).
- `ppu_archA2g_vblanksplit_wip.*` — offsets VBlank separados (9/12 con
  `QTR_VBLANKIRQ=3`).
- `ppu_archA2h_mergedquirk_wip.*` — quirk fusionado con el edge de VBlank
  (9/12 con `QTR_VBLANKIRQ=3`).

- `Z80GB_cpu8_wip.cpp` — experimento CPU 8-M (descartado: rompe
  `intr_timing` y Mooneye).

## Reglas de trabajo

- No commit ni push sin petición explícita.
- No usar `git stash`, `reset --hard`, `checkout --`, `restore`, `clean -fdx`.
- Antes de editar: copia a `tmp/` (o snapshot en `tmp/fetcher-proto/`).
- No tocar `core/src/timer.cpp` sin plan y revalidación de timing.
- No cambiar el despacho de interrupción del CPU sin revalidar Blargg y
  Mooneye completos.
- Comentarios y errores en inglés; comunicación con el usuario en español.
- Evidencia antes de "hecho": build limpio + suites + harness con números
  exactos.

## Comandos útiles

```bash
# invariantes + baseline
./temp/scripts/arch_gate.sh

# trazas vs DocBoy
./temp/scripts/trace_diff.sh --all
./temp/scripts/trace_diff.sh vendor/mealybug/ppu/m3_lcdc_bg_map_change.gb 0 80 140

# build de la línea
make -C .wip/ppu -j$(nproc)
make -C .wip/ppu/mts -j4
.wip/ppu/temp/scripts/build_tools.sh

# harness PNG con BIOS
QTR_BIOS="$PWD/roms/bios/[BIOS] Nintendo Game Boy Boot ROM (World).gb" \
  .wip/ppu/mts/compare_ppu_frames.sh
```
