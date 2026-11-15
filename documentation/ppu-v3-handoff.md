# PPU v3 — Handoff limpio (rama `ppu-v3`)

Este documento sustituye al handoff antiguo
(`.wip/ppu/documentation/Game Boy/ppu-window-pipeline-handoff.md`, secciones 66–84).
La rama de partida es `master` (`3c658e0`), no `wip/ppu`.

## Documentos de la rama

- `documentation/ppu-v3-handoff.md` — este documento: estado, arquitectura,
  baseline y plan general.
- `documentation/ppu-v3-f1-scheduler.md` — guía paso a paso de F1 (reloj de
  4 fases), para implementarlo a mano.
- `documentation/ppu-v3-f2-cpu-write-phase.md` — hallazgo de F2: la premisa de
  mover writes a T1 era incorrecta; el LR35902 escribe en T3. Incluye la
  evidencia de Blargg y los ticks correctos.
- `documentation/ppu-v3-f3-bus.md` — F3: `VideoBus`, propiedad de VRAM/OAM,
  `BusCpuView` y acceso raw de la PPU. Incluye el diseño y los resultados.
- `documentation/ppu-v3-f4-dot-timeline.md` — F4: timeline objetivo, selector
  por dot, estrategia híbrida F4-A y plan F4-B + F5.
- `documentation/ppu-v3-f5-pipeline.md` — F5: pipeline de píxeles nuevo
  (OAM por LX, BG/window prefetcher, OBJ fetcher, FIFOs, pixel transfer),
  resultados V2 vs F5 y ajustes pendientes.
- `documentation/ppu-v3-f6-delayed-registers.md` — F6: retardos de 1
  T-cycle, STAT IRQ por dot, timeline de fin de línea, resultados y fallos
  restantes.
- `documentation/baselines/ppu-v2-baseline-2026-11-12.tsv` — baseline de
  frames de la PPU V2 (`name`, `status`, `diff`).
- `mts/README.md` — harness de frames y runner de Mooneye.

## 0. Qué se ha descartado y por qué

Se elimina del contexto de trabajo todo el historial del intento de port
estructural de DocBoy a la arquitectura V2:

- El "port DocBoy" de las secciones 66, 82 y 83 (fetch-ahead, multi-stage,
  `last_*` infrastructure, OAM scan detallado, fases Dummy/Discard/Visible).
- El diagnóstico del bucle de interrupciones de `m3_window_timing` y el fix
  propuesto con `m_prev_stat_mode` (sección 84).
- Los planes `ppu-continuation-plan.md`, `ppu-fetch-ahead-plan.md`,
  `ppu-fetcher-refactor-plan.md`, `ppu-full-port-plan.md`,
  `ppu-sprite-fetch-plan.md`, `ppu-sprite-fetch-port-plan.md`.
- Todas las referencias a backups `tmp/...`.

**Motivo:** ese port se hizo encima del scheduler V2, que es incompatible con
el modelo de DocBoy. El PPU nuevo recibía un reloj y unos accesos a memoria que
no respetaban el orden de fases ni la contención de buses, así que los diffs no
podían bajar. No es una base válida ni sus parches son reutilizables.

Lo único que se conserva de aquel trabajo es la conclusión: **el problema no es
solo la PPU; es el modelo de tiempo y el bus de todo el core.**

### Decisión de desarrollo

DocBoy se usa como **referencia de comportamiento y de arquitectura**, no como
código fuente para copiar. Cada función se reimplementa en QtreGB con nuestro
estilo, nuestros nombres y nuestros comentarios, citando el test o la
observación que justifica el comportamiento. No se traslada código de DocBoy.

---

## 1. Estado de partida (baseline real)

Rama `ppu-v3`, creada desde `master` (`3c658e0`):

- Build: `make -j$(nproc)` sin errores.
- Doctest: **53/53 test cases, 195/195 assertions**.
- Blargg: **15/15**.
- Mooneye (`make run_mts`): **34/34**.
- PPU V2 en `master`: `PPU` + `Fetcher` + dos `FIFO` + `LCD` con registros.
- Harness de frames portado a esta rama (ver F0). Medición de `master` con
  `QUIET=1 REPORT=/tmp/qtrebg_frames/baseline.tsv ./mts/compare_ppu_frames.sh`:
  **0 PASS / 25 FAIL / 8 SKIP**.
- Baseline guardado en
  `documentation/baselines/ppu-v2-baseline-2026-11-12.tsv` (`name`, `status`,
  `diff`). Es la referencia que F1 debe reproducir exactamente.

### Baseline por ROM (2026-11-12)

| ROM | Estado | diff |
|---|---:|---:|
| dmg-acid2 | FAIL | 1346 |
| m2_win_en_toggle | FAIL | 18060 |
| m3_bgp_change | FAIL | 7245 |
| m3_bgp_change_sprites | FAIL | 8158 |
| m3_lcdc_bg_en_change | FAIL | 3474 |
| m3_lcdc_bg_map_change | FAIL | 2386 |
| m3_lcdc_obj_en_change | FAIL | 146 |
| m3_lcdc_obj_en_change_variant | FAIL | 1271 |
| m3_lcdc_obj_size_change | FAIL | 1622 |
| m3_lcdc_obj_size_change_scx | FAIL | 1353 |
| m3_lcdc_tile_sel_change | FAIL | 1430 |
| m3_lcdc_tile_sel_win_change | FAIL | 1747 |
| m3_lcdc_win_en_change_multiple | FAIL | 6175 |
| m3_lcdc_win_en_change_multiple_wx | FAIL | 6016 |
| m3_lcdc_win_map_change | FAIL | 2308 |
| m3_obp0_change | FAIL | 432 |
| m3_scx_high_5_bits | FAIL | 9041 |
| m3_scx_low_3_bits | FAIL | 540 |
| m3_scy_change | FAIL | 10579 |
| m3_window_timing | FAIL | 2341 |
| m3_window_timing_wx_0 | FAIL | 2926 |
| m3_wx_4_change | FAIL | 11527 |
| m3_wx_4_change_sprites | FAIL | 27 |
| m3_wx_5_change | FAIL | 11098 |
| m3_wx_6_change | FAIL | 8174 |

Los 8 SKIP son ROMs sin PNG esperado en `vendor/` (`*_change2` y
`win_without_bg`). No cuentan como fallo.

El harness escribe una línea compacta por ROM y, con `REPORT=...`, una tabla
TSV; con `QUIET=1` solo imprime fallos y el resumen. Así no hace falta volcar
salidas largas en la conversación.

---

## 2. Por qué la arquitectura V2 no puede funcionar

La V2 trata cada componente como una máquina aislada que avanza "cuando le
toca":

- `QtreGB::tick()` llama a `m_cpu.tick()`, `m_timer.timer_tick()` y
  `m_ppu.ppu_tick()` una vez por T-cycle, en ese orden, sin fases.
- La PPU se modela con `m_line_ticks` y un `switch (mode)`: al llegar a ciertos
  umbrales (80, 456, etc.) hace transiciones y dispara interrupciones.
- El CPU hace sus lecturas/escrituras de bus **dentro** de `m_t_cycle == 4`,
  sin separar petición y confirmación.
- La contención VRAM/OAM es un booleano (`m_vram_read_locked`) que se activa y
  desactiva por modo, no por dot.
- Los registros LCD se leen/escriben de forma inmediata; no hay copias
  retardadas (`last_lcdc`, `last_bgp`, `last_wx`, `last_ly`, `last_lyc`).
- El STAT IRQ se genera de forma síncrona desde la PPU, sin línea de IRQ
  latcheada ni detección de flanco.

Eso produce cuatro clases de error que ningún parche local arregla:

1. **Orden dentro del T-cycle.** El CPU, la PPU, el timer y el DMA ven el mismo
   tick, pero no en el mismo instante físico. Una lectura del CPU y una lectura
   de VRAM de la PPU en el mismo T-cycle se resuelven según quién posee el bus
   en la fase correcta, no según el orden de llamadas.
2. **Efectos retardados.** Un write a LCDC/SCX/BGP/WX no lo ve la PPU en el
   mismo T-cycle; un write a STAT tarda 1 T-cycle; LYC==LY tarda 2 o 4 dots en
   reflejarse; el STAT IRQ tarda 1 T-cycle. Sin estos retardos explícitos, los
   tests `m3_*` no pueden pasar.
3. **Contención por dot.** Las ventanas de acceso a VRAM/OAM dependen del dot
   exacto (OAM scan, pixel transfer, HBlank), no solo del modo.
4. **Interrupciones.** IF/IE se muestrean en fases concretas del M-cycle; el
   CPU no puede comprobar interrupciones "cuando acaba la instrucción" sin más.

La conclusión de DocBoy es la correcta: **no se modelan componentes que se
tickean por separado; se modela un reloj global y un orden de fases.**

---

## 3. El modelo correcto: DocBoy como referencia

DocBoy se consulta para entender **qué hace el hardware y en qué orden**, no
para copiar su implementación. Las rutas y métodos de la sección 7 sirven para
leer el comportamiento; el código de QtreGB se escribe de nuevo. Cuando una
decisión se base en una observación de DocBoy o en un test, se deja el
comentario correspondiente (`mealybug/m3_*`, `mooneye/*`).

### 3.1 Reloj global: `Core::cycle()` = 4 T-cycles

Un M-cycle = 4 T-cycles. `Core::cycle()` ejecuta siempre las cuatro fases:

```text
t0: cpu.tick_t0(); ppu.tick(); lcd.tick(); apu.tick_t0(); buses
t1: cpu.tick_t1(); ppu.tick(); lcd.tick(); dma.tick(); apu.tick_t1(); buses
t2: cpu.tick_t2(); ppu.tick(); lcd.tick(); apu.tick_t2(); buses
t3: cpu.tick_t3(); timers.tick(); serial.tick(); ppu.tick(); lcd.tick();
    dma.tick(); interrupts.tick_t3(); buses
```

Detalles importantes:

- La PPU y el LCD reciben **4 ticks por M-cycle** (un dot por T-cycle).
- El CPU recibe **4 ticks por M-cycle**, pero cada fase hace una cosa distinta.
- El timer recibe **1 tick por M-cycle** y suma 4 al divisor (`div16 += 4`).
- El DMA recibe **2 ticks por M-cycle** (t1 y t3).
- `interrupts.tick_t3()` limpia el bloqueo de IF una vez por M-cycle.

El orden dentro de cada fase es fijo. En DocBoy el CPU va **antes** que la PPU:
la petición de bus del CPU se emite en `tick_microop()` (t0) y se confirma en
t1 (write) o t3 (read); la PPU avanza su dot después. La adquisición que hace
la PPU en su dot afecta al acceso del CPU del **siguiente** T-cycle; la que
hizo en el dot anterior es la que ve el CPU en el flush actual.

No hace falta copiar las ramas CGB (`ENABLE_CGB`), speed-switch ni HDMA. El
objetivo es DMG.

### 3.2 CPU: petición y confirmación separadas

DocBoy parte cada acceso en dos:

```text
CPU::read(addr)   ->  bus.read_request(addr)       (fase t0 del micro-op)
CPU::flush_read() ->  bus.flush_read_request()     (fase t3)
CPU::write(addr,v)->  bus.write_request(addr)      (fase t0)
CPU::flush_write()->  bus.flush_write_request(v)   (fase t1)
```

Así el acceso real a memoria ocurre en el T-cycle que le corresponde, y el
`Bus` puede resolver la contención con el estado de la PPU/DMA en ese instante.
Además, el `Bus` mantiene `requests`, `address`, `data` y `decay`: el valor del
bus se queda unos T-cycles y se puede leer como open bus.

El CPU de DocBoy es micro-op por M-cycle (`tick_microop()`), y las interrupciones
se comprueban en fases concretas:

```text
tick_single_speed_0: check_interrupt<3>(); tick_microop();
tick_single_speed_1: check_interrupt<0>(); flush_write(); idu.tick_t1();
tick_single_speed_2: check_interrupt<1>();
tick_single_speed_3: check_interrupt<2>(); flush_read();
```

`check_interrupt<T>()` usa una tabla de tiempos por tipo de interrupción, estado
halted y fase. Es lo que hace que `intr_timing` y los tests de STAT IRQ cuadren.
En QtreGB habrá que portar esa tabla o, como mínimo, el muestreo por fase.

### 3.3 PPU: una función por dot (`tick_selector`)

DocBoy no usa `switch (mode)` con umbrales. Usa un puntero a función que dice
qué hace **este dot exacto**:

```text
dots [0, 456)
ly   [0, 154)
lx   = X + 8, [0, 168)
```

Selectores principales:

```text
oam_scan_even / oam_scan_odd / oam_scan_77 / oam_scan_done
oam_scan_after_turn_on / _78 / _79

pixel_transfer_dummy_lx0
pixel_transfer_discard_lx0 / _wx0_scx
pixel_transfer_lx0
pixel_transfer_lx8

hblank / hblank_452 / _453 / _454 / _455
hblank_last_line / _453 / _454 / _455
hblank_first_line_after_turn_on

vblank / vblank_453 / _454 / _455
vblank_last_line / _2 / _3 / _4 / _7 / _453 / _454 / _455
```

Cada función hace exactamente el trabajo de un dot y cambia `tick_selector` al
siguiente estado. No hay bucles "hasta el final de la línea".

### 3.4 Línea de 456 dots (DMG)

| Dots | Qué pasa |
|---|---|
| 453–455 de la línea anterior | Se lee la entry 0 de OAM, se prepara el OAM scan |
| 0–79 | OAM scan (modo 2). Se leen 40 entries, una cada 2 dots; se guardan las 10 primeras que caen en la línea |
| 77 | La PPU adquiere VRAM (y OAM en DMG) |
| 78–79 | `oam_scan_done`: STAT pasa a Pixel Transfer un dot antes |
| 80 | `enter_pixel_transfer()`: reset del fetcher, `lcd.reset_row_cursor()` |
| 80–82 | Fetch dummy: se llena el BG FIFO con basura; se calcula `SCX % 8` |
| 83… | Se descartan `SCX % 8` píxeles; LX=0..7 se popean sin pintar |
| LX=8..167 | Se empujan los 160 píxeles visibles, uno por dot si el FIFO está listo |
| LX=168 | `enter_hblank()`; se liberan VRAM/OAM |
| 452 | `hblank_452`: OAM IRQ, empieza el incremento de LY |
| 453 | `hblank_453`: se adquiere OAM, se lee entry 0, se desactiva LYC IRQ, termina el incremento de LY |
| 454 | `hblank_454`: se confirma entry 0, se reactiva LYC IRQ, STAT pasa a OAM |
| 455 | `hblank_455`: se pide entry 1, `dots=0`, `enter_oam_scan()` |

La última línea visible (LY=143) usa `hblank_last_line`: no vuelve a OAM scan,
pasa a VBlank. La última línea de VBlank (LY=153) resetea LY a 0 en el dot 2,
desactiva LYC IRQ entre los dots 2 y 6, y en 453–455 prepara el nuevo frame.

`enter_vblank()` dispara el VBlank IRQ. `enter_new_frame()` resetea el contador
de ventana (`w.wly = 255`), limpia `w.active_for_frame`, entra en OAM scan y
llama a `lcd.new_frame()`.

### 3.5 Fetcher: cuatro unidades y dos FIFOs

El fetcher de DocBoy no es un único `Fetcher` con `FS_TILE/DATA0/DATA1/PUSH`.
Son cuatro unidades que avanzan en paralelo, cada una con su selector de dos
dots:

- **BG/window prefetcher** (`bwf`): calcula la dirección del tilemap y lee el
  número de tile.
- **Window prefetcher** (`wf`): calcula la X del tilemap de la ventana.
- **OBJ prefetcher** (`of`): lee tile number + attributes de la entry de OAM.
- **Pixel slice fetcher** (`psf`): lee los dos bytes de datos del tile y los
  convierte en 8 píxeles.

Estados típicos de BG:

```text
bgwin_prefetcher_get_tile_0
bg_prefetcher_get_tile_0
bg_prefetcher_get_tile_1
bg_pixel_slice_fetcher_get_tile_data_low_0
bg_pixel_slice_fetcher_get_tile_data_low_1
bg_pixel_slice_fetcher_get_tile_data_high_0
bg_pixel_slice_fetcher_get_tile_data_high_1
```

Cada estado "0" es el primer dot y "1" el segundo. La lectura real de VRAM se
hace en el estado "0"; el "1" es el segundo T-cycle del acceso. El push al
`bg_fifo` se habilita al final (`high_1`), y `handle_pixel_slice_fetcher_push()`
lo ejecuta cuando el FIFO está vacío.

`bg_fifo` y `obj_fifo` tienen 8 píxeles. El fetcher puede ir por delante del
pop: si el FIFO no está vacío, el push espera. El pop ocurre en
`pixel_transfer_lx8()`:

```text
si bg_fifo no está vacío y no hay sprite fetch pendiente:
    bg = bg_fifo.pop_front()
    si obj_fifo no está vacío:
        obj = obj_fifo.pop_front()
        si obj es opaco y (bg == 0 o BG-over-OBJ permite):
            color = paleta OBJ
    si no:
        color = paleta BG
    lcd.push_pixel(color)
```

El sprite fetch **para** el pop del BG FIFO (`is_bg_fifo_ready_to_be_popped()`
devuelve false mientras `is_fetching_sprite`), y el BG fetch se reanuda después
con `cache_bg_win_fetch()` / `restore_bg_win_fetch()`.

### 3.6 Ventana

- `w.active_for_frame` se activa en cuanto `lcdc.win_enable && ly == wy`
  (se comprueba **cada dot**, no solo en pixel transfer).
- `check_window_activation()` compara `lx` con `last_wx` (WX tiene 1 T-cycle de
  retardo). En DMG acepta también `lx == last_wx + 1` si `last_lcdc.win_enable`
  era 0: es el glitch de `m3_lcdc_win_en_change_multiple_wx`.
- Al activar: `w.activating = true`, `++w.wly`, se deshabilita el push del
  pixel slice fetcher y el siguiente tick del fetcher se salta
  (`pixel_slice_fetcher_idle`).
- En el siguiente dot de pixel transfer, `handle_pending_window_activation()`
  llama a `activate_window()`: vacía `bg_fifo`, `w.active = true`,
  `w.just_activated = true`, `wf.tilemap_x = 0`.
- `w.wly` es la fila de tiles de la ventana. Se incrementa solo cuando la
  ventana se activa, no en cada línea; se resetea a 255 al empezar frame para
  que la primera activación la deje en 0. Esto es lo que necesita `dmg-acid2`
  (ojo derecho, se oculta la ventana con WX=240, y la barbilla sigue desde el
  mismo contador).

Los glitches CGB de ventana (`disabled_while_activating`, fills parciales) no
hacen falta para DMG; se pueden dejar fuera de la primera implementación.

### 3.7 Sprites

- El OAM scan guarda las entries por **LX**, no en una lista ordenada global:
  `oam_entries[168]`, cada una con hasta 10 entries. `increase_lx()` limpia la
  entry de ese LX después de servirla.
- Una entry se guarda si `obj_y <= ly < obj_y + height` y `x < 168`. Las que
  tienen `x >= 168` cuentan para el límite de 10 pero no se guardan.
- `is_obj_ready_to_be_fetched()` es cierto si `oam_entries[lx]` no está vacío y
  `lcdc.obj_enable`.
- El OBJ prefetcher tarda 6 dots: tile_0, tile_1 (lee tile+flags de OAM),
  low_0, low_1, high_0, high_1 (lee los dos bytes de VRAM y mezcla en
  `obj_fifo`).
- Si hay varios sprites en el mismo LX, se fetchean en secuencia.
- La mezcla sprite-sprite se hace al final:
  - pixel opaco sustituye a transparente;
  - si ambos son opacos y mismo X, gana el que está antes en OAM (`number`).
- En DMG la prioridad BG/OBJ se decide en `pixel_transfer_lx8()`: el OBJ gana
  si es opaco y (`bg == 0` o `BG_OVER_OBJ == 0`).

El modelo de "object array" de 160 píxeles precalculados de la V2 **no se usa**.
Es una de las causas de los errores de alineación con SCX y ventana.

### 3.8 Buses y contención por T-cycle

DocBoy tiene buses con máscara de adquirientes:

```cpp
uint16_t acquirers;   // bits: PPU, CPU, DMA...
void acquire<Dev>();
void release<Dev>();
bool is_acquired() const;
```

`VramBus` y `OamBus` heredan de `VideoBus`. El CPU accede a través de una
`View<Device::Cpu>`:

```cpp
flush_read_request<Cpu>():
    if (is_acquired()) { return 0xFF; }   // PPU o DMA tienen el bus
    return Bus::flush_read_request<Cpu>();

flush_write_request<Cpu>(value):
    if (is_acquired()) { return; }        // el write se pierde
    Bus::flush_write_request<Cpu>(value);
```

La PPU adquiere/suelta en dots concretos:

- OAM: se adquiere al final del HBlank anterior (dot 453) y se suelta al entrar
  en HBlank (o en el dot 75 del OAM scan, glitch DMG de `lcdon_write_timing`).
- VRAM: se adquiere en el dot 77 del OAM scan y se suelta al entrar en HBlank.
- DMA: es un `Device` más; adquiere OAM mientras copia.

El OAM scan usa lecturas de **dos bytes** (`read_word_request` /
`flush_read_word_request`) porque lee Y y X de una entry en dos T-cycles. Ese
detalle es necesario para el timing de OAM y para el OAM bug.

En QtreGB se puede empezar con una versión simplificada: un `VideoBus` con
`acquirers` y `View`, VRAM/OAM dentro del PPU, y el CPU/DMA como devices. Lo
importante es que la comprobación ocurra en el flush del CPU, no en un booleano
de modo.

### 3.9 Registros retardados e interrupciones

DocBoy guarda copias de 1 T-cycle:

```text
last_lcdc = lcdc;
last_bgp  = bgp;      // DMG: el color BG usa (bgp | last_bgp)
last_wx   = wx;
last_ly   = ly;
last_lyc  = lyc;
```

y las actualiza al final de cada `tick()`, después de ejecutar el dot. Por eso
el dot actual ve los valores del dot anterior. `pending_write` aplica writes
retardados (STAT en DMG: 1 T-cycle; LCDC/SCX/SCY/WX en CGB: 2 T-cycles).

STAT IRQ:

```text
raise_stat_irq() cada dot:
    last_stat_irq < irq  => pending_stat_irq = true
    pending_stat_irq     => se dispara el IF al siguiente tick
```

- El OAM IRQ no se comprueba cada dot: solo en la transición de modo
  (`hblank_452`, `enter_new_frame`).
- El VBlank IRQ se dispara en `enter_vblank()`.
- El HBlank IRQ se evalúa cada dot con `mode == HBLANK`.
- El LYC IRQ usa `last_lyc == last_ly` y se desactiva en el dot 453 y en la
  última línea de VBlank (dots 2–6).
- `enable_lyc_eq_ly_irq` es el flag que modela esas ventanas.
- En DMG el STAT mode que se lee puede ser HBlank durante OAM scan en la
  primera línea tras encender, durante DMA, y al final de VBlank
  (`tick_stat()`); el modo interno no cambia.

IF/IE:

```text
write_if(value): IF = 0xE0 | value; block_interrupts = true;
raise_interrupt(): si block_interrupts, se ignora;
tick_t3(): block_interrupts = false;
```

Ese bloqueo de un M-cycle es necesario para `stat_irq_blocking`.

### 3.10 LCD

`Lcd` es un componente separado del PPU:

- Tiene su propio contador de 456 dots por fila y 67488 T-cycles por frame
  (148 filas × 456).
- El PPU empuja píxeles a `row_pixels`; el LCD copia la fila entera al
  framebuffer en el dot 452.
- Si la PPU está apagada, el LCD limpia la fila en vez de copiarla (DMG: además
  borra toda la pantalla al apagar, porque el LCD pasivo no retiene el píxel).
- `turn_on_ppu()` / `turn_off_ppu()` sincronizan el contador del LCD con el
  PPU; `new_frame()` reengancha ambos tras un apagado/encendido.

Esto es lo que permite pasar `lcdon_timing` y los tests de apagado/encendido.
Para el objetivo inmediato (`m3_*` + `dmg-acid2`) se puede empezar con el
framebuffer actual y añadir la copia por fila después; pero el diseño objetivo
debe separar **registros/estado PPU** de **display/LCD**.

---

## 4. Mapa de QtreGB actual → destino

| QtreGB hoy | Destino |
|---|---|
| `QtreGB::tick()` (CPU, timer, PPU en el mismo tick sin fases) | `QtreGB::cycle()` con `t0..t3` y orden fijo |
| `Z80GB::tick()` (todo el acceso en `t_cycle == 4`) | `tick_t0..t3`; `read_request`/`flush_read` en t3; `write_request`/`flush_write` en t1 |
| `PPU::ppu_mode_oam/xfer/hblank/vblank` + `m_line_ticks` | `dots` + `tick_selector` (una función por dot) |
| `Fetcher` único con `FS_TILE/DATA0/DATA1/PUSH` y `m_warming` | BG/window prefetcher + OBJ prefetcher + pixel slice fetcher, selectores de 2 dots |
| `m_object_array[160]` precalculado | `oam_entries[168]` por LX + `obj_fifo` de 8 |
| `m_bgw_fifo` / `m_spr_fifo` (`FIFO` de 8) | `bg_fifo` / `obj_fifo` de 8, con push/pop condicionados por sprite fetch |
| `m_vram_read_locked` / `m_oam_read_locked` booleanos | `VideoBus` con `acquirers` + `View<Device>`; CPU/DMA/PPU |
| `LCD` con registros y `m_video_buffer` | PPU dueño de LCDC/STAT/LY/SCY/SCX/BGP/OBP/WY/WX; LCD dueño de framebuffer/fila/paleta |
| `InterruptController` sin fase | `tick_t3()` + bloqueo de IF; CPU `check_interrupt<T>` por fase |
| `DMA::dma_tick()` en `is_end_m_cycle()` | `dma.tick()` en t1 y t3; adquiere OAM |
| `Timer` tick cada T-cycle | Se puede dejar igual al principio; objetivo: tick en t3 con `div16 += 4` |

---

## 5. Plan de migración (cada fase compila y se valida)

No se toca la PPU nueva hasta que el reloj y el bus estén en su sitio. El orden
es importante: implementar el modelo de DocBoy sobre el scheduler V2 fue el
error del intento anterior.

> En este plan, "portar" significa **reimplementar el comportamiento** que se
> observa en DocBoy (o que describen los tests), no copiar su código.

### Fase 0 — Harness y baseline (HECHO, 2026-11-11; baseline guardado 2026-11-12)
- `mts/mts_runner.cpp`: `MTS_FRAME_OUT` escribe un PGM (P5) 160x144 con la
  paleta DMG; `MTS_FRAME_DELAY` avanza N T-cycles antes del dump
  (70224 T-cycles = un frame).
- `mts/compare_ppu_frames.sh`: compara contra los PNG esperados con
  `magick compare -metric AE`; `QUIET=1` deja solo fallos y resumen;
  `REPORT=file.tsv` escribe la tabla completa.
- `mts/README.md`: sección "PPU frame harness".
- Baseline en `master`: 0 PASS / 25 FAIL / 8 SKIP (tabla en la sección 1).
- **Aceptación:** cumplida; el script produce `PASS`/`FAIL diff=N` por ROM.

### Fase 1 — Scheduler de 4 fases (núcleo implementado, 2026-11-12)

**Guía detallada:** `documentation/ppu-v3-f1-scheduler.md`.

- `QtreGB::tick()` sigue siendo un T-cycle (API pública intacta).
- `QtreGB::cycle()` = cuatro T-cycles (un M-cycle).
- `PhaseClock` en `core/inc/phase_clock.h`; `tick_t0..t3` privados.
- En F1 las cuatro fases llaman al mismo `tick_once()`: solo se etiqueta el
  reloj, no se mueve comportamiento.
- **Estado:** completo. `PhaseClock` y el despacho por fases implementados;
  tests permanentes `tests/test_phase_clock.cpp` y `tests/test_scheduler.cpp`
  añadidos. Verificado: doctest **56/56** (208 assertions), blargg 15/15,
  mts 34/34 y harness idéntico al baseline
  (`documentation/baselines/ppu-v2-baseline-2026-11-12.tsv`).
- **Aceptación:** build limpio, doctest completo, blargg 15/15, mts 34/34 y
  diffs del harness idénticos a F0 (comparar TSV before/after).

### Fase 2 — CPU: fase de escritura (investigado; T3 confirmado)

**Documento de hallazgo:** `documentation/ppu-v3-f2-cpu-write-phase.md`.

- Se probó mover los writes a T1 con `write_next_t1()` y `run_pending_write()`.
- Blargg `mem_timing-2` (`02-write_timing`, `03-modify_timing`) e
  `instr_timing` fallaron: 12/15.
- El LR35902 escribe en el **último T-cycle del M-cycle (T3)**, igual que lee.
  `mem_timing-2` lo confirma: `LD (HL),B` escribe en el ciclo 2,
  `LD (HL),d8` en el 3, `LD (nn),A` en el 4.
- Código restaurado a `HEAD` (`3a5bf59`): sin `PendingWrite` ni
  `write_next_t1()`. El test de fase T1 se eliminó.
- **F2 cancelado como cambio de fase.** El acceso del CPU ya está en el
  T-cycle correcto. El problema de la PPU está en el bus y en el modelo de
  dots, no en la fase de escritura del CPU.
- **Siguiente:** F3 (bus de vídeo con adquisición) y F4 (PPU por dots).

### Fase 3 — Bus de vídeo con adquisición (HECHO, 2026-11-13)

**Diseño y resultados:** `documentation/ppu-v3-f3-bus.md`.

- `core/inc/videobus.h`: `VideoDevice` (Cpu/Ppu/Dma), `VideoAccess`
  (Read/Write) y `VideoBus` con `acquire/release/is_acquired/reset`.
- `PPU`: `m_oam_bus`/`m_vram_bus` sustituyen los cuatro booleanos. Los getters
  `is_*_locked()` consultan el bus.
- La PPU accede a su VRAM/OAM en raw (no se bloquea a sí misma).
- `BusCpuView` comprueba DMA y los locks de la PPU para VRAM/OAM; devuelve
  `0xFF` o descarta writes.
- El DMA sigue ganando el bus por su camino actual
  (`ppu_oam_write_dma` + chequeo en `BusCpuView`); `VideoDevice::Dma` queda
  reservado.
- **Aceptación:** doctest **60/60** (221 assertions), blargg 15/15, mts 34/34
  y harness idéntico al baseline.
- **Nota:** F3 conserva las ventanas actuales (transiciones de modo y schedule
  de LCD-on). El bloqueo real de VRAM en modo 3 y las adquisiciones por dot
  son F4.

### Fase 4 — PPU por dots: timeline (F4-A/B1/B2 HECHOS)

**Guía:** `documentation/ppu-v3-f4-dot-timeline.md`.

- **F4-A (hecho):** `TickSelector` + `m_tick_selector`; `ppu_tick()` despacha
  por selector en vez de `switch(mode)`; cada transición de modo actualiza el
  selector.
- **F4-B1 (hecho):** `m_dots` 0-based (`m_line_ticks - 1`); las funciones de
  modo usan `m_dots`; el pipeline V2 sigue usando `m_line_ticks` y sus
  umbrales. Sin cambio de frames.
- **F4-B2 (hecho):** OAM scan por dots (Y en dots pares, X/tile/flags en
  impares); las entradas se guardan por LX y se usan en F5.
- **Verificado:** doctest **60/60** (221 assertions), blargg 15/15, mts 34/34
  y harness **idéntico** al baseline.

### Fase 5 — Fetcher, FIFOs, ventana y sprites (HECHO, núcleo)

**Documento:** `documentation/ppu-v3-f5-pipeline.md`.

- OAM scan por LX (`m_oam_entries[168]`), máximo 10 sprites por línea.
- BG/window prefetcher con FIFO de 8 píxeles.
- OBJ fetcher con `m_obj_pixels` de 8 píxeles y prioridad por X/número de OAM.
- Pixel transfer `Dummy → Discard → Lx0 → Lx8`; 160 píxeles visibles.
- Ventana básica (`m_window_active_for_frame`, `m_window_tile_x`,
  `m_window_line`) y limpieza de FIFO al activarse.
- El camino nuevo ya no usa `Fetcher`, `m_object_array` ni
  `pipeline_process`; fueron eliminados en F5.4.
- **Resultados:** doctest 60/60, blargg 15/15, mts 34/34; harness ejecuta las
  33 ROMs. `dmg-acid2` baja de 1346 a **130**, `m3_scx_high_5_bits` de 9041 a
  **604**, `m3_wx_4_change` de 11527 a **7179**. Algunos tests empeoran
  (`bgp_change`, `scy_change`, `wx_6_change`): falta F6 y ajuste de
  ventana/sprites. Tabla completa en el documento de F5.
- **F5.4 hecho:** eliminados `Fetcher`/`FIFO`/`pipeline_*`/`ppu_mode_xfer`
  y campos V2 muertos; sin cambios de comportamiento.
- **F5.2 timing de mode 3:** warm-up de 4 dots, OAM STAT en el dot 452,
  STAT mode 0 en `m_lx == 163` y `LY++` en el dot 448 de este scheduler.
  Aceptación PPU **11/12**; solo falla `intr_2_mode0_timing_sprites`.
- **F5.3 sprites X<8 y ventana:** fetch de sprites fuera de pantalla durante
  `Lx0`/`Discard`, 2 dots por acceso OBJ, guarda multi-sprites, cadencia
  8/8/4/8/4… por sprite y descarte de sus píxeles; ventana activada 1 dot
  después del trigger, caso WX=0/SCX y trigger `WX+1`.
  Harness: **3 PASS / 22 FAIL / 8 SKIP**.
  `dmg-acid2` **0**, `m2_win_en_toggle` **0**,
  `m3_lcdc_win_en_change_multiple` **0**, `m3_scx_high_5_bits` **84**,
  `m3_wx_4_change` **230**, `m3_wx_5_change` **638**,
  `m3_window_timing` **670**, `m3_window_timing_wx_0` **592**,
  `m3_bgp_change` **2945**, `m3_bgp_change_sprites` **3505**,
  `m3_lcdc_bg_en_change` **1338**, `m3_lcdc_win_map_change` **772**,
  `m3_lcdc_tile_sel_win_change` **1368**,
  `m3_lcdc_win_en_change_multiple_wx` **545**,
  `m3_scy_change` **10773** (latch de fila BG en el tilemap).
  `intr_2_mode0_timing_sprites` pasa los 11 primeros testcases; falla en
  X=2 por la alineación `X mod 8`. Detalle en el documento de F5.

### Fase 6 — Registros retardados y STAT IRQ

**Documento:** `documentation/ppu-v3-f6-delayed-registers.md`.

- **Hecho:** `m_last_lcdc`, `m_last_bgp`, `m_last_wx`, `m_last_ly`,
  `m_last_lyc`; `LCD::tick()` aplica el write de STAT 1 T-cycle después;
  `raise_stat_irq()` evalúa LYC/HBlank/VBlank por dot con
  `m_pending_stat_irq` (1 T) y línea STAT latchada (`m_stat_irq_line`).
  OAM IRQ solo en transición; VBlank DMG con `vblank_int || oam_int`; LYC con
  `m_enable_lyc_eq_ly_irq`.
- **Timeline:** HBlank LY++ en 453, modo en 454, selector/reset en 455;
  VBlank normal LY++ en 453; línea 153 resetea LY en el dot 2 y el frame
  empieza en el 455 (`m_vblank_last_line`).
- **Resultados:** doctest 60/60 (221 assertions), blargg 15/15, mts 34/34,
  `halt_ime0_ei` PASS, `instr_timing` PASS. Harness visual prácticamente
  igual que F5 (`dmg-acid2` 130).
- **Tests de aceptación PPU:** **8/12 PASS** (`intr_1_2_timing`,
  `intr_2_0_timing`, `intr_2_mode3_timing`, `lcdon_timing`,
  `lcdon_write_timing`, `stat_irq_blocking`, `stat_lyc_onoff`,
  `vblank_stat_intr-GS`). Fallan `hblank_ly_scx_timing-GS`,
  `intr_2_mode0_timing`, `intr_2_mode0_timing_sprites` e
  `intr_2_oam_ok_timing`.
- **Bugs corregidos:** selector en 454 que saltaba el reset de línea; LY=1 al
  empezar frame por el reset de la línea 153; flag LYC pisado por
  `raise_stat_irq`; línea STAT limpiada incondicionalmente en modo 3; IRQ de
  LYC del encendido perdido antes del check de fin de opcode; bit OAM tratado
  como condición continua durante VBlank (el quirk es one-shot).
- **Pendiente:** duración exacta del modo 3/SCX (los `intr_2_mode0*`,
  `oam_ok`, `hblank_ly_scx`). El harness visual necesita que el pipeline
  consuma los registros en el dot exacto.
- **Aceptación:** `m3_bgp_change`, `m3_wx_5_change`, `m3_lcdc_*_change`,
  `stat_lyc_onoff`, `stat_irq_blocking`.

### Fase 7 — LCD por filas y apagado/encendido
- Separar el display del PPU: `row_pixels`, copia en el dot 452, limpieza de
  filas, `turn_on_ppu`/`turn_off_ppu`, `new_frame`.
- **Aceptación:** `lcdon_timing`, `lcdon_write_timing`, `dmg-acid2`, y el resto
  de tests de apagado/encendido.

### Fase 8 — Validación completa
- 24 `m3_*` + `dmg-acid2` con `diff = 0`.
- `make run_tests` (53/53), `make run_blargg` (15/15), `make run_mts` (34/34).
- Si algo no baja, **no se parchea a ciegas**: se compara con DocBoy headless
  y se busca el primer píxel/dot distinto.

---

## 6. Invariantes y reglas de código

1. **Un solo reloj.** Solo `QtreGB::cycle()` avanza el tiempo. Ningún
   componente se tickea a sí mismo ni desde fuera del ciclo.
2. **La PPU avanza un dot por T-cycle.** `dots` en `[0,456)`, `ly` en `[0,154)`,
   `lx` en `[0,168)`. Assertions siempre que se pueda.
3. **Una función de estado = un dot.** No bucles "hasta el final de la línea",
   no umbrales mágicos tipo `if (line_ticks == 80)`.
4. **Los retardos son explícitos.** Cada `last_*`, `pending_write`,
   `pending_stat_irq` lleva un comentario con el test que lo justifica
   (`mealybug/...`, `mooneye/...`).
5. **El bus se comprueba en el flush del CPU**, no en un booleano de modo.
6. **El CPU no lee/escribe memoria directamente**: usa `request`/`flush`.
7. **La PPU no pinta en el framebuffer final**: empuja al row buffer del LCD.
8. **Nada de `m_warming`, `m_object_array` ni estados V2** en el código nuevo.
9. **Cada cambio se valida con build + doctest + el harness**, y se anota el
   diff por ROM. Sin números, no está hecho.
10. **DocBoy no se copia:** se reimplementa el comportamiento observado y se
    cita el test o la observación que lo justifica; nada de trasladar código
    literal.

---

## 7. Referencias de DocBoy (consulta de comportamiento, no copia)

Ruta base: `/home/pedro/projects/other_gb/docboy/src/docboy/docboy/`

| Qué | Archivo |
|---|---|
| Scheduler de 4 fases | `core/core.cpp` (`Core::cycle`, `tick_t0..t3`) |
| Composición de componentes | `gameboy/gameboy.h` |
| CPU micro-op y fases | `cpu/cpu.cpp` (`tick_microop`, `tick_single_speed_*`, `check_interrupt`) |
| PPU tick y selectores | `ppu/ppu.cpp` (`Ppu::tick`, `TICK_SELECTORS`, `FETCHER_TICK_SELECTORS`) |
| Timeline OAM/pixel/HBlank/VBlank | `ppu/ppu.cpp` (`oam_scan_*`, `pixel_transfer_*`, `hblank*`, `vblank*`, `enter_*`) |
| Fetcher BG/ventana/OBJ | `ppu/ppu.cpp` (`bgwin_prefetcher_*`, `win_prefetcher_*`, `obj_prefetcher_*`, `*_pixel_slice_fetcher_*`) |
| Ventana | `ppu/ppu.cpp` (`check_window_activation`, `activate_window`, `setup_fetcher_for_window`, `tick_window`) |
| STAT IRQ / LYC | `ppu/ppu.cpp` (`raise_stat_irq`, `update_stat_irq*`, `is_lyc_eq_ly`, `tick_stat`) |
| Registros retardados | `ppu/ppu.cpp` (`tick`, `tick_pending_write`, `write_*_real`) |
| Encendido/apagado | `ppu/ppu.cpp` (`turn_on`, `turn_off`, `oam_scan_after_turn_on`, `hblank_first_line_after_turn_on`) |
| Bus de vídeo | `bus/videobus.h`, `bus/vrambus.*`, `bus/oambus.*` |
| Bus CPU / MMU | `bus/cpubus.*`, `mmu/mmu.*`, `bus/bus.*` |
| LCD | `lcd/lcd.h`, `lcd/lcd.cpp` |
| Timer | `timers/timers.cpp` (`tick` suma 4 a `div16`) |
| Interrupciones | `interrupts/interrupts.h` (`write_if`, `raise_interrupt`, `tick_t3`) |

DocBoy headless (referencia visual):
`/home/pedro/projects/other_gb/docboy/build/docboy-nogui`.

---

## 8. Estado actual y siguiente paso

- Rama `ppu-v3` creada desde `master` y bajada en el checkout principal
  (`/home/pedro/projects/QtreGB`). `.wip/ppu` sigue en `wip/ppu`.
- **F0 completado (2026-11-11; baseline guardado 2026-11-12):** harness de
  frames + baseline.
  - `mts/mts_runner.cpp`: `MTS_FRAME_OUT`, `MTS_FRAME_DELAY`.
  - `mts/compare_ppu_frames.sh`: comparación y reporte TSV.
  - `mts/README.md`: documentación de uso.
  - Baseline: **0 PASS / 25 FAIL / 8 SKIP** (tabla en la sección 1; TSV en
    `documentation/baselines/ppu-v2-baseline-2026-11-12.tsv`).
- **F1 completo (2026-11-12)** — `PhaseClock` + `QtreGB::tick()` por fases +
  `cycle()`. Tests permanentes en `tests/test_phase_clock.cpp` y
  `tests/test_scheduler.cpp`. Verificado: doctest **56/56** (208 assertions),
  blargg 15/15, mts 34/34 y harness idéntico al baseline.
- **F2 investigado (2026-11-13):** la premisa de mover los writes a T1 era
  incorrecta. El LR35902 escribe en el último T-cycle (T3), igual que lee.
  Con el código restaurado: doctest 56/56, blargg 15/15, mts 34/34 y harness
  idéntico al baseline. Documento de hallazgo:
  `documentation/ppu-v3-f2-cpu-write-phase.md`.
- **F3 completo (2026-11-13):** `VideoBus` con `acquire/release` por
  dispositivo y acceso; la PPU tiene `m_oam_bus`/`m_vram_bus`; `BusCpuView`
  resuelve la contención del CPU; la PPU accede a su memoria en raw. Verificado:
  doctest **60/60** (221 assertions), blargg 15/15, mts 34/34 y harness
  idéntico al baseline. Diseño: `documentation/ppu-v3-f3-bus.md`.
- **F4-A completo (2026-11-13):** `TickSelector`/`m_tick_selector` en la PPU;
  `ppu_tick()` despacha por selector, manteniendo `m_line_ticks` y el fetcher
  V2. Verificado: doctest **60/60** (221 assertions), blargg 15/15, mts 34/34
  y harness idéntico al baseline. Guía:
  `documentation/ppu-v3-f4-dot-timeline.md`.
- **F4-B1/B2 completos (2026-11-13):** `m_dots` 0-based para la timeline y
  OAM scan por dots (Y en pares, X/tile/flags en impares), manteniendo el
  pipeline V2. Verificado: doctest **60/60** (221 assertions), blargg 15/15,
  mts 34/34 y harness idéntico al baseline. Guía:
  `documentation/ppu-v3-f4-dot-timeline.md`.
- **F5 (núcleo) completo (2026-11-13):** OAM por LX, BG/window
  prefetcher, OBJ fetcher, FIFOs y pixel transfer `Dummy/Discard/Lx0/Lx8`.
  Verificado: doctest **60/60** (221 assertions), blargg 15/15, mts 34/34.
  Harness: `dmg-acid2` **1346 → 130**, `m3_scx_high_5_bits` **9041 → 604**,
  `m3_wx_4_change` **11527 → 7179**; algunos tests empeoran porque falta F6.
  Documento: `documentation/ppu-v3-f5-pipeline.md`.
- **F6 (2026-11-13):** retardos de 1 T-cycle para `LCDC/BGP/WX/LY/LYC`,
  write de STAT, STAT IRQ por dot con línea latchada, OAM solo en transición,
  VBlank DMG y timeline de los dots 452–455. Verificado: doctest **60/60**
  (221 assertions), blargg 15/15, mts 34/34, `halt_ime0_ei` PASS,
  `instr_timing` PASS. Al cerrar F6: **8/12 PASS**.
  Documento: `documentation/ppu-v3-f6-delayed-registers.md`.
- **F5.2/F5.3 timing, sprites y ventana (2026-11-13):** warm-up de 4 dots,
  OAM STAT en el dot 452, STAT mode 0 en `m_lx == 163`, `LY++` en el dot 448,
  fetch de sprites X<8 con guarda multi-sprites y cadencia 8/8/4, ventana con
  activación retardada y trigger `WX+1`. Verificado: doctest **60/60**
  (221 assertions), blargg 15/15, mts 34/34, `halt_ime0_ei` PASS,
  `instr_timing` PASS. Aceptación PPU: **11/12 PASS**; solo falla
  `intr_2_mode0_timing_sprites` (11 primeros testcases OK, falla en X=2).
  Harness: **2 PASS / 23 FAIL / 8 SKIP**; `dmg-acid2` **0**,
  `m2_win_en_toggle` **0**, `m3_scx_high_5_bits` **84**,
  `m3_wx_4_change` **230**, `m3_wx_5_change` **638**,
  `m3_window_timing` **845**, `m3_lcdc_win_map_change` **708**.
  Documento: `documentation/ppu-v3-f5-pipeline.md`.
- Handoff viejo archivado en `.wip/ppu`; **no se usa**.
- **F5.4 — Limpieza (2026-11-13):** eliminados `Fetcher`, `FIFO`,
  `fetcher.h/.cpp`, `fetcher_bg.*`, `fifo.*`, `pipeline_process`,
  `pipeline_push_pixel`, `ppu_mode_xfer`, `load_line_sprites`,
  `m_oam_line_entry`, `m_line_x`, `m_fetched`, `m_discarded_pixels`,
  `m_pixel_fifo`, `m_bg_tile_*` y `m_window_tile_*`. Verificado: doctest
  60/60 (221 assertions), blargg 15/15, mts 34/34, PPU 11/12, harness
  idéntico. Documento: `documentation/ppu-v3-f5-pipeline.md`.
- **Siguiente paso:** el desfase de ~4T de los handlers mealybug apunta al
  **estado/fase post-boot (boot ROM)**, no a la fase global ni al dispatch:
  quitar el M-cycle extra del dispatch mantiene `instr_timing` y no cambia
  los diffs; OAM 448 + recalibración HBlank/LY solo llega a 2/7 mooneye. Se
  mantiene OAM 452 y se aceptan esos diffs hasta el bloque de boot ROM
  opcional. Después, `wx_6`, `multiple_wx` y el modelo de sprites X mod 8.
- **F8 — Modelo de sprites X mod 8 (2026-11-13): implementado.** Se sustituyó
  la cadencia heurística 8/8/4 por el algoritmo de Pan Docs: 6 dots planos por
  OBJ, `max(0, 5 - p)` extra para el primer OBJ de cada tile, 6 para los
  siguientes del mismo tile y 11 para el primer OBJ con OAM X=0. Añadido
  `m_obj_considered_tiles`. Resultado: PPU 11/12 (el fallo se mueve a 1 sprite
  X=0, a 1–2 dots del borde), doctest 62/62, blargg 15/15, mts 34/34,
  `dmg-acid2` 0. Mejora 5 diffs visuales y empeora ligeramente 3. Forzar 12/12
  moviendo OAM a 455 y STAT modo 3 a 80 dispara los diffs mealybug
  (`bgp_change` 2945→5805, `window_timing` 670→1745), así que se descartó.
  Ver `documentation/ppu-v3-f8-sprites.md`.
- **F9 — Muestreo de `SCX` bajo (2026-11-13): implementado.** El test
  `m3_scx_low_3_bits` fallaba en el último tile (líneas 72–143, +2 px). Su
  fuente confirma que los 3 bits bajos de `SCX` se muestrean una vez por línea
  al inicio del primer ciclo B real. Añadido `m_bg_line_scx_low` (latch en el
  dot 89) y `m_bg_fetch_skip` (delta entre tiles). Resultado con BIOS:
  `m3_scx_low_3_bits` **0** (antes 324), harness **4 PASS / 21 FAIL / 8 SKIP**;
  sin BIOS sigue **3/22/8**. doctest 62/62, blargg 15/15, mts 34/34, PPU 11/12.
  Ver `documentation/ppu-v3-f9-scx-low.md`.
- **F10 — Sprites X>0 y alineación del pipe (2026-11-13): implementado.**
  `m3_scx_high_5_bits` (84) era el tile `0x43` en vez de `0x44`: el fetch de
  x2 ocurría 3 dots antes del write de `SCX=8`. Cambios: X=0 primer sprite
  con penalización neta 8 (11 menos 3 de solape con el startup) y +3 de
  `post_wait` para un sprite X>0 solitario. Los grupos de varios sprites en la
  misma X mantienen F8. Resultado: `m3_scx_low_3_bits` y `m3_scx_high_5_bits`
  a **0**, harness **5 PASS / 20 FAIL / 8 SKIP** con BIOS (4/21/8 sin BIOS),
  doctest 62/62, blargg 15/15, mts 34/34, PPU 11/12. Los grupos de sprites se
  refinaron después: p=0/1→10, p=2/3→7 (solo si hay varios grupos), p>=4→6;
  pasan los casos 0–52 y el fallo queda en el **caso 53 (1 sprite X=1)**. Ver
  `documentation/ppu-v3-f10-sprite-pipe.md`.
- **F10.1 — ciclo de vida OAM y modo 0 temprano (2026-11-13):
  implementado.** Las entradas OAM se conservan hasta servirse
  (`m_oam_served_count`) y el modo 0 temprano en `lx=167` se condiciona a que
  no queden pendientes en el último tile. Resultado: el caso 32 pasa; el fallo
  real es el **caso 53 (1 sprite X=1, HRAM 0x35, 9.185.366 T-cycles)**;
  high-5/low-3 siguen a 0 y el harness no cambia (4/21/8 sin BIOS, 5/20/8 con
  BIOS). doctest 62/62, blargg 15/15, mts 34/34, PPU 11/12. El estado visual
  validado está en `documentation/ppu-v3-f10-sprite-pipe.md` (F10.1/F10.2).
- **Prototipo de prefetcher (F10.3, revertido):** disparo del OBJ desde la
  fase push del fetcher para líneas de un solo sprite, bloqueo de píxel por OAM
  pendiente, sin `post_wait` para X>0 y modo 0 temprano selectivo. Lleva la
  aceptación de los casos 0–52 a los casos **0–77**, pero high-5 vuelve a
  **84** y el harness baja a 4/21/8 con BIOS (3/22/8 sin BIOS). Se revirtió:
  high-5 y el caso 53 son la misma configuración (sprite X=1) y necesitan
  fases distintas; el relleno parcial y el retraso post-push no llegan a
  alinear la lectura de SCX sin vaciar el FIFO. Ver F10.3 en
  `documentation/ppu-v3-f10-sprite-pipe.md`.
- **Modelo completo de fetcher (F10.4, WIP):** en
  `.wip/ppu/tmp/full-fetcher-wip/` hay un fetcher BG de 6 dots (2 ticks por
  acceso), OBJ desde la fase push, orden pop-antes-de-fetcher y grupo OBJ
  continuo. Pasa los casos **0–18** de la aceptación; el arranque coincide con
  DocBoy (fetch de X=0 en dot 89) y el grupo X=8 llena el FIFO. High-5 queda en
  65–172; el barrido de la latch de SCX en 87–91 da 250 y el muestreo directo
  por tile rompe high-5/low-3 (8356/324): falta modelar el retardo/fase de
  SCX, no solo el punto de muestreo. En el caso 0 el primer poll lee mode 3 en
  t=688403 y el modo 0 llega en t=688410: faltan ~7 dots de fase. El árbol
  activo sigue en el estado validado F10.1. Ver el README del WIP.
- **Siguiente paso real:** continuar el WIP F10.4: muestreo de SCX por tile,
  calibración del modo 0 del CPU y revalidación de ventana. Después,
  `wx_6`/`multiple_wx` y la fase de muestreo de BGP/LCDC.
- **Diseño original — boot ROM opcional (implementado en F7):** cargar una boot
  ROM aportada por el usuario (`QTR_BIOS` o menú `File → Load BIOS…`),
  ejecutarla desde `$0000`, desmapear al escribir `$FF50` y seguir en `$0100`. Sin boot ROM, mantener el
  arranque post-boot actual. Los tests seguirían sin boot ROM para ser
  deterministas. Esto desbloquearía `m3_scx_low_3_bits` (tile $19 de la VRAM
  post-boot) y otros tests que asumen el logo/fuente de la boot ROM.
- **F7 — Boot ROM opcional (2026-11-13): implementado.** `QtreGB::load_boot_rom()`
  acepta exactamente 256 bytes y la deja almacenada; se aplica en el siguiente
  `load_cart()`/`reset()`. `Bus` mapea `$0000-$00FF`, desmapea al escribir
  `$FF50` y tiene fallback al llegar a `$0100`. `Z80GB::reset(true)` arranca en
  `$0000`; `LCD::lcd_init(true)`/`PPU::ppu_init(true)` parten del estado de
  power-on DMG (LCDC=0, LCD apagada) y el PPU se enciende cuando la boot ROM
  escribe LCDC. GUI: menú `File → Load BIOS…` y `Reset`, con la BIOS aplicada
  solo en el siguiente reset. Runners: variable `QTR_BIOS`; `mts_runner` sube
  el presupuesto a 60 M T-cycles cuando hay BIOS (el arranque consume ~23,4 M).
  Sin BIOS todo queda igual. Ver `documentation/ppu-v3-f7-boot-rom.md`.
- Antes de tocar `core/src/timer.cpp`: plan explícito y revalidación de los
  tests de timing (regla del proyecto).

### Comparación de salidas

El harness está pensado para no volcar textos largos:

- Comparación determinista: `magick compare -metric AE` devuelve un número.
- `QUIET=1` imprime solo fallos y el resumen.
- `REPORT=file.tsv` deja la tabla completa en disco.
- Para diff de texto/código, `diff`/`cmp`/`sha256sum` son suficientes; un LLM
  local solo haría falta para interpretar diferencias, no para contarlas.

Si se quiere delegar la interpretación de diferencias grandes a una IA local,
hace falta su interfaz (Ollama, llama.cpp server, LM Studio, CLI, etc.) y el
formato de entrada/salida. El harness ya puede dejar el contexto en TSV/PGM
para no reenviar todo el stdout.

---

## 9. Cómo retomar

1. `git branch --show-current` → `ppu-v3`.
2. `make -j$(nproc)` y `make run_tests` (62/62).
3. `make run_blargg` (15/15) y `make run_mts` (34/34).
4. Comprobar que el árbol sigue en el baseline:

   ```sh
   QUIET=1 REPORT=/tmp/qtrebg_frames/retomar.tsv ./mts/compare_ppu_frames.sh
   cut -f1-3 /tmp/qtrebg_frames/retomar.tsv > /tmp/qtrebg_frames/retomar-summary.tsv
   diff -u documentation/baselines/ppu-v2-baseline-2026-11-12.tsv \
           /tmp/qtrebg_frames/retomar-summary.tsv
   ```

   `diff` vacío = el árbol está como lo dejamos.

5. Leer `documentation/ppu-v3-f1-scheduler.md` y seguir los pasos 0–6.
6. No commitear sin pedirlo. No tocar `core/src/timer.cpp` sin plan explícito.

