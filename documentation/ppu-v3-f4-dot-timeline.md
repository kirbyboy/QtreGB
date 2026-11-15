# F4 — PPU por dots: timeline y selector (guía)

## Estado (2026-11-13)

- **F4-A hecho:** `TickSelector` + `m_tick_selector`; `ppu_tick()` despacha
  por selector. Comportamiento idéntico.
- **F4-B1 hecho:** contador `m_dots` 0-based; las funciones de modo usan
  `m_dots`; el pipeline V2 sigue usando `m_line_ticks` con sus umbrales.
- **F4-B2 hecho:** OAM scan por dots (Y en dots pares, X/tile/flags en impares);
  la lista `m_oam_line_entry` se construye durante el scan y se ordena/carga en
  el dot 79.
- **Verificado:** doctest **60/60** (221 assertions), blargg 15/15, mts 34/34
  y harness **idéntico** al baseline.
- **Siguiente:** F5.1 (BG/window prefetcher + FIFO), F5.2 (OBJ prefetcher),
  F5.3 (ventana), F5.4 (pixel transfer final y limpieza).

## 0. Objetivo y dependencia

F4 sustituye el estado de la PPU basado en `m_line_ticks` + `switch(mode)` por
una máquina de estados por dot con un `tick_selector`.

Pero hay una dependencia importante: **la timeline de DocBoy no se puede
portar completa sin el fetcher de F5**. El fetcher V2 actual:

- Usa `m_line_ticks` con umbrales fijos (`81`, `92`, `289`).
- Precalcula los sprites de la línea en `m_object_array[160]`.
- Depende de `m_pushed_x` y de un solo FIFO de BG + otro de sprites.

El modelo de DocBoy necesita:

- `oam_entries[168]` con los sprites por LX.
- Prefetchers de BG/ventana/OBJ y un pixel-slice fetcher.
- Un `bg_fifo`/`obj_fifo` que se vacían al ritmo de `lx`.
- Ventana activada por `lx`/`last_wx`, no por `m_current_x + 7`.

Por eso F4 se divide en dos:

- **F4-A (híbrido, sin cambio de comportamiento):** introduce `dots` y el
  `tick_selector`, pero sigue llamando al pipeline V2 con sus umbrales
  actuales. Sirve para tener la arquitectura y validar que no rompe nada.
- **F4-B + F5 (timeline real):** sustituye la timeline y el fetcher a la vez.
  Es donde los diffs de `m3_*` empiezan a bajar.

---

## 1. Timeline objetivo (DocBoy, DMG)

Una línea son 456 dots. `dots` va de 0 a 455; `ly` de 0 a 153.

| Dots | Estado |
|---|---|
| 453–455 de la línea anterior | Se lee la entry 0 de OAM y se prepara el OAM scan |
| 0–79 | OAM scan (modo 2): 40 entries, una cada 2 dots |
| 77 | La PPU adquiere VRAM (y OAM en DMG) |
| 78–79 | `oam_scan_done`: STAT pasa a Pixel Transfer un dot antes |
| 80 | `enter_pixel_transfer()`: reset del fetcher, `lcd.reset_row_cursor()` |
| 80–82 | Fetch dummy; se calcula `SCX % 8` |
| 83… | Se descartan `SCX % 8` píxeles; LX=0..7 se popean sin pintar |
| LX=8..167 | Se empujan los 160 píxeles visibles |
| LX=168 | `enter_hblank()`; se liberan VRAM/OAM |
| 452–455 | OAM IRQ, `LY++`, se prepara el siguiente OAM scan |
| 143 → VBlank | `hblank_last_line`; `enter_vblank()` dispara VBlank IRQ |
| 153 → nuevo frame | LY=0 en el dot 2, LYC IRQ desactivada hasta el 6, `enter_new_frame()` |

Detalles que F4-B/F5 deben respetar:

- El modo `STAT` se actualiza en dots concretos (79, 454, 455), no al mismo
  tiempo que el modo interno.
- OAM se adquiere al final del HBlank anterior y se libera al entrar en
  HBlank; VRAM se adquiere en el dot 77 y se libera al entrar en HBlank.
- La primera línea tras encender el LCD tiene un schedule especial
  (`is_glitched_line_0`): STAT sigue en HBlank durante el OAM scan, OAM no se
  adquiere, no se escanea ningún sprite y HBlank se retrasa 2 dots.
- La última línea de VBlank resetea LY a 0 en el dot 2 y desactiva LYC IRQ
  entre los dots 2 y 6.

---

## 2. Timeline actual (V2)

`ppu_tick()` hace:

```cpp
m_line_ticks++;
// LYC delayed update...
if(m_lcd_on_line != 255) { ppu_lcd_on_line(); return; }
switch(m_lcd->get_lcds_mode())
{
    case MODE_OAM:    ppu_mode_oam();    break;
    case MODE_XFER:   ppu_mode_xfer();   break;
    case MODE_HBLANK: ppu_mode_hblank(); break;
    case MODE_VBLANK: ppu_mode_vblank(); break;
}
```

`m_line_ticks` es **1-based**: el primer tick de la línea vale 1. Los umbrales
actuales:

- `ppu_mode_oam`: en `m_line_ticks == 1` carga todos los sprites de la línea;
  en `80` entra en XFER.
- `ppu_mode_xfer`: llama a `pipeline_process()` cada tick; sale a HBlank cuando
  `m_pushed_x > 160` o `m_line_ticks >= 289`.
- `pipeline_process`: `m_fetcher.tick()` solo en ticks pares;
  `pipeline_push_pixel` empieza a empujar a partir de `m_line_ticks == 92`.
- `ppu_mode_hblank`: en `456` incrementa LY y decide OAM/VBlank.
- `ppu_mode_vblank`: en `456` incrementa LY; al llegar a 154 empieza frame.
- `ppu_lcd_on_line`: schedule especial de las 3 primeras líneas con sus
  propios umbrales (78, 250, 450, 2, 4, 80, 84, 256, 456).

La V2 no bloquea VRAM durante el modo 3 normal; solo lo hace en el schedule
especial de LCD-on. Eso se corrige en F4-B/F5.

---

## 3. F4-A: selector híbrido (hecho)

Objetivo: introducir la arquitectura de selector sin tocar la lógica V2.

### 3.1 Cambios

En `PPU`:

```cpp
using TickSelector = void (PPU::*)();

void ppu_mode_oam();
void ppu_mode_xfer();
void ppu_mode_hblank();
void ppu_mode_vblank();

TickSelector m_tick_selector = &PPU::ppu_mode_oam;
```

En `ppu_tick()`:

```cpp
if(m_lcd_on_line != 255)
{
    ppu_lcd_on_line();
    return;
}

(this->*m_tick_selector)();
```

Las funciones de modo cambian el selector en cada transición:

- `ppu_mode_oam` al pasar a XFER:
  `m_tick_selector = &PPU::ppu_mode_xfer;`
- `ppu_mode_xfer` al pasar a HBlank:
  `m_tick_selector = &PPU::ppu_mode_hblank;`
- `ppu_mode_hblank` al pasar a VBlank u OAM:
  `m_tick_selector = &PPU::ppu_mode_vblank;` o `&PPU::ppu_mode_oam;`
- `ppu_mode_vblank` al empezar un frame nuevo:
  `m_tick_selector = &PPU::ppu_mode_oam;`
- `ppu_lcd_on_line` al terminar el schedule especial (t == 456,
  `m_lcd_on_line` pasa a 255):
  `m_tick_selector = &PPU::ppu_mode_oam;`
- `ppu_power_off`: `&PPU::ppu_mode_hblank;`
- `ppu_power_on` / `ppu_init`: `&PPU::ppu_mode_oam;`

`m_line_ticks` se mantiene. No se cambia el fetcher ni los umbrales.

### 3.2 Pasos

1. Añadir el typedef y el miembro `m_tick_selector`.
2. Cambiar el `switch` de `ppu_tick()` por `(this->*m_tick_selector)()`.
3. Poner el selector en cada transición de modo.
4. Build + doctest + blargg + mts + harness.
5. **Criterio:** los diffs del harness deben ser idénticos al baseline. Si
   cambian, falta actualizar el selector en alguna transición.

### 3.3 Resultado esperado

- Misma salida de frames.
- Arquitectura lista para F4-B: las funciones de modo pueden sustituirse por
  selectores por dot sin tocar `ppu_tick()`.

---

## 4. F4-B: timeline real + F5 (F4-B1/B2 hechos; F5 pendiente)

Cuando F4-A esté verde, el siguiente paso es sustituir la timeline y el
fetcher. Orden sugerido:

1. **Contador `dots` 0..455.** Cambiar de `m_line_ticks` 1-based a `dots`
   0-based. Ajustar los umbrales del pipeline V2 temporalmente para mantener
   la salida lo más cerca posible; o aceptar el cambio y medirlo.
2. **OAM scan por dots.** `oam_scan_even/odd/77/done`; leer una entry cada 2
   dots; guardar `oam_entries[168]` por LX; adquirir VRAM en el dot 77.
3. **Pixel transfer.** `pixel_transfer_dummy_lx0`,
   `pixel_transfer_discard_lx0`, `pixel_transfer_lx0`, `pixel_transfer_lx8`;
   reemplazar el pipeline V2 por los prefetchers/FIFOs de F5.
4. **HBlank/VBlank.** `hblank_452..455`, `hblank_last_line`,
   `vblank_453..455`, `vblank_last_line`.
5. **Encendido/apagado.** `turn_on`, `turn_off`, `is_glitched_line_0`,
   `oam_scan_after_turn_on`.
6. **Ventana y sprites.** `check_window_activation`, `activate_window`,
   `w.wly`, y la mezcla en `obj_fifo`.
7. **Locks por dot.** Mover `m_oam_bus`/`m_vram_bus` acquire/release a los
   dots exactos.
8. **Validación.** Medir `m3_window_timing`, `m3_wx_0`, `dmg-acid2` y el resto.

F4-B y F5 **no tienen que ir en un solo commit**. Se pueden separar en
capas, aunque hay una parte que sí está acoplada: el state machine de pixel
transfer (`pixel_transfer_dummy_lx0`, `discard_lx0`, `lx0`, `lx8`) es a la vez
timeline y fetcher, y sustituye a `pipeline_push_pixel`. Esa parte se hace
cuando se sustituye el fetcher.

Orden incremental recomendado:

1. **F4-B1 — timeline con el fetcher V2 como caja negra. (HECHO)**
   - Contador `dots` 0..455.
   - Selectores para OAM scan, HBlank y VBlank.
   - Dots especiales 452–455 y última línea.
   - Locks de OAM/VRAM en los dots actuales.
   - Turn on/off.
   - El pixel transfer sigue llamando al pipeline V2, adaptando sus umbrales
     a `dots`.
   - **Criterio:** build + doctest + blargg + mts; harness medido (idealmente
     idéntico). No se esperan mejoras de píxeles todavía.

2. **F4-B2 — OAM scan por dots. (HECHO)**
   - Leer una entry cada 2 dots y guardar `oam_entries[168]` por LX.
   - Al terminar el scan, construir la lista ordenada que espera el fetcher
     V2 (puente temporal) o empezar a usar las entries por LX.
   - **Criterio:** los sprites siguen apareciendo; harness medido.

3. **F5.1 — BG/window prefetcher + FIFO.**
   - Sustituir la parte de BG/ventana del pipeline V2.
   - Mantener temporalmente el path de sprites V2 si hace falta.

4. **F5.2 — OBJ prefetcher + `obj_fifo`.**
   - Sprites por LX, mezcla BG/OBJ, eliminar `m_object_array`.

5. **F5.3 — Ventana.**
   - `check_window_activation`, `activate_window`, `w.wly`.

6. **F5.4 — Pixel transfer final y limpieza.**
   - Sustituir `pipeline_push_pixel` por `pixel_transfer_lx8`.
   - Eliminar los restos del fetcher V2 (`m_warming`, `m_object_array`,
     umbrales de `m_line_ticks`).

La timeline final de pixel transfer y el fetcher nuevo se hacen juntos en
F5.4; antes de eso, F4-B1/F4-B2 son puentes válidos y testeables.

---

## 5. Riesgos

- **Off-by-one:** el contador V2 es 1-based; DocBoy usa 0-based. Un shift de
  un dot cambia OAM scan, STAT y ventanas. Usar tests de `mts` y el harness.
- **`m_pushed_x`/`m_line_ticks`:** el pipeline V2 sale a HBlank por umbrales
  que no existen en DocBoy. En F4-A se mantienen; en F4-B desaparecen.
- **OAM scan:** la V2 lee todos los sprites en el tick 1; DocBoy los lee uno a
  uno cada 2 dots. No se puede portar sin `oam_entries[168]` (F5).
- **LCD-on schedule:** tiene sus propios umbrales y es fácil romper
  `lcdon_timing`/`lcdon_write_timing`. Mantenerlo aislado hasta F4-B.
- **STAT IRQ:** F4 no implementa `last_*`/`pending_write`/flanco (F6). No
  esperes `m3_*` a diff=0 solo con F4.
- **Harness:** F4-A debe ser idéntico; F4-B puede cambiar los frames. Medir
  antes/después y no interpretar un cambio como regresión automática.

---

## 6. Qué no hacer en F4-A

- No cambiar `m_line_ticks` por `dots` todavía.
- No tocar `Fetcher`, `FIFO`, `pipeline_push_pixel` ni `m_object_array`.
- No mover los locks a dots exactos.
- No tocar el CPU, el timer ni el DMA.

F4-A es andamiaje; el cambio real de píxeles es F4-B + F5 + F6.
