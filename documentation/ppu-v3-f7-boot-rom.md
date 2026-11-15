# PPU v3 — F7: boot ROM opcional

Fecha: 2026-11-13. Rama `ppu-v3`.

## Objetivo

Poder ejecutar una boot ROM DMG aportada por el usuario sin cambiar el
comportamiento por defecto (arranque post-boot) ni romper las suites. La ROM
no se empaqueta ni se copia al repositorio: se carga desde una ruta externa.

## API del core

- `QtreGB::load_boot_rom(std::vector<u8>) -> bool`
  - Exige exactamente `0x100` bytes.
  - Solo la almacena: **no** se aplica hasta el siguiente `load_cart()` o
    `reset()`.
- `QtreGB::reset()` / `QtreGB::load_cart()` reinician CPU, timer, PPU, DMA y
  el reloj de fases con la configuración de boot ROM vigente.
- `QtreGB::has_boot_rom()` indica si hay una boot ROM almacenada.
- `Bus`:
  - `set_boot_rom()` / `set_boot_rom_enabled()` / `disable_boot_rom()`.
  - Mientras está activa, las lecturas de `$0000-$00FF` salen de la boot ROM.
  - Un write a `$FF50` la desmapea (comportamiento DMG).
  - Fallback: al alcanzar `PC = $0100` también se desmapea, por si una ROM
    custom salta al cartucho sin escribir `$FF50`.
- `Z80GB::reset(bool boot_rom)`: PC `$0000` con boot ROM, `$0100` sin ella.
- `LCD::lcd_init(bool boot_rom)` / `PPU::ppu_init(bool boot_rom)`:
  - Con boot ROM: LCDC=0 (LCD apagada), modo HBlank, PPU idle. La boot ROM
    enciende la LCD al escribir LCDC; `ppu_power_on()` arranca la línea 0.
  - Sin boot ROM: estado post-boot actual, sin cambios.

## GUI

- `File → Load BIOS…`: carga un archivo `.gb`/`.bin` de 256 bytes.
- La BIOS cargada se muestra como `BIOS: loaded (reset to apply)` y pasa a
  `BIOS: active` tras `File → Reset` o al cargar otro cartucho.
- `EmulatorInstance` guarda cartucho y BIOS; `load_cart()`/`reset()` recrean
  `QtreGB`, aplican la BIOS y vuelven a cargar el cartucho.

## Runners

- `QTR_BIOS=/ruta/boot.gb` antes de `mts_runner` o `blargg_runner`.
- Si la variable está definida y el archivo no es de 256 bytes, el runner
  termina con `BIOS ERROR` (no se ignora en silencio).
- `mts_runner` usa un presupuesto de 60 M T-cycles con BIOS: el arranque DMG
  consume ~23,44 M T-cycles antes de entrar en `$0100`. Sin BIOS sigue en
  30 M. `MTS_MAX_T` permite forzar otro límite.

## Validación

Sin BIOS (comportamiento por defecto):

- doctest: 62/62 (232 assertions).
- blargg: 15/15.
- mts: 34/34.
- PPU acceptance: 11/12 (solo falla `intr_2_mode0_timing_sprites`).
- Harness: 3 PASS / 22 FAIL / 8 SKIP, idéntico al baseline F5.4.

Con BIOS DMG (`CRC32 0x59C8598E`, 256 bytes):

- doctest: 62/62 (232 assertions).
- blargg: 15/15.
- mts: 34/34 (con el presupuesto de 60 M).
- PPU acceptance: 11/12 (el mismo fallo).
- `dmg-acid2`: 0.
- Harness: 3 PASS / 22 FAIL / 8 SKIP.

Cambios de diff con BIOS respecto a sin BIOS (solo los que cambian):

| Test | Sin BIOS | Con BIOS |
|---|---:|---:|
| `m3_bgp_change_sprites` | 3505 | 2532 |
| `m3_lcdc_bg_map_change` | 1042 | 640 |
| `m3_lcdc_obj_en_change` | 146 | 256 |
| `m3_lcdc_obj_en_change_variant` | 650 | 760 |
| `m3_lcdc_tile_sel_change` | 1935 | 1536 |
| `m3_lcdc_tile_sel_win_change` | 1368 | 976 |
| `m3_lcdc_win_map_change` | 772 | 375 |
| `m3_obp0_change` | 432 | 128 |
| `m3_scx_low_3_bits` | 540 | 324 |

La BIOS confirma que `m3_scx_low_3_bits` dependía sobre todo de la VRAM
post-boot: mejora pero no llega a 0, así que la fuente/tile exactos siguen
siendo una limitación conocida. Los dos tests MBC que agotaban 30 M con BIOS
(`bits_bank1`, `bits_bank2`) pasan al subir el presupuesto; no era un fallo
del core.

## Cómo probar

```sh
make -j$(nproc)
make run_tests
make run_blargg
make run_mts

BIOS="roms/bios/[BIOS] Nintendo Game Boy Boot ROM (World).gb"
QTR_BIOS="$PWD/$BIOS" ./build/mts_runner vendor/mts/*/acceptance/ppu/*.gb
QTR_BIOS="$PWD/$BIOS" make run_mts
QTR_BIOS="$PWD/$BIOS" make run_blargg
QTR_BIOS="$PWD/$BIOS" QUIET=1 ./mts/compare_ppu_frames.sh
```
