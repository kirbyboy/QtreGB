# PPU v3 — F10: sprite X>0 y alineación del pipe

Fecha: 2026-11-13. Rama `ppu-v3`.

## Problema

`m3_scx_high_5_bits` daba **84** píxeles con BIOS. El comparador de framebuffer
lo localizó en las líneas **8–86**, columnas **16–23**. En la línea 8 el tile
esperado era `0x44` (`0x9823`) y QtreGB mostraba `0x43` (`0x9822`).

La traza de la línea 8 mostró:

- sprite en X=1, penalty 10, arranca en el dot 86;
- fetch del tilemap x2 (`0x9822`, tile `0x43`) **3 dots antes** del write de
  `SCX=8`;
- el write llega en el dot 114 y el siguiente fetch ya salta a x4.

Para que el tile visible fuese `0x44`, el fetch de x2 tenía que ocurrir después
del write, usar `SCX=8` y seleccionar x3.

## Cambios

1. **X=0**: el primer sprite X=0 de un tile usa una penalización neta de
   **8 dots** en este scheduler. Pan Docs documenta 11; 3 de ellos se solapan
   con el startup de modo 3. Esto arregla el caso 0 del test de aceptación
   (`1 sprite X=0`), que antes fallaba por 1–2 dots.
2. **X>0 solitario**: si el sprite es el único de su grupo (misma X), se añaden
   **3 dots** al `post_wait`. El sprite empieza después del primer píxel de BG
   y el pipe necesita esos 3 dots para alinear el siguiente tile fetch con el
   punto de muestreo de `SCX`.
3. **Grupos de varios sprites en la misma X**: el primer sprite del grupo usa
   una penalización ajustada al bucket del test:
   - `p=0/1`: 10 dots;
   - `p=2/3`: 7 dots si la línea tiene más de un grupo de sprites, 6 si es el
     único grupo;
   - `p>=4`: 6 dots.
   Los sprites posteriores del mismo tile siguen pagando 6. X=0 conserva su
   caso especial de 8.
   Con esto pasan los grupos X=2..7 (caso 11) y los grupos partidos 5@2+5@162
   (caso 38); el fallo se mueve a **caso 53 (1 sprite X=1)**.

## Resultados

- `m3_scx_low_3_bits`: **0**.
- `m3_scx_high_5_bits`: **324 → 0** (con BIOS).
- `intr_2_mode0_timing_sprites`: sigue en **11/12**, pero ahora pasan los
  casos 0–52 y el fallo está en el **caso 53 (1 sprite X=1)**. El sprite único
  X=1 necesita el `+3` para high-5, pero el test de aceptación espera una
  duración más corta; es un conflicto de fase/prefetch, no del extra en sí
  (con extra 0 el caso 53 también falla).
- doctest: **62/62** (232 assertions).
- blargg: **15/15**.
- mts: **34/34**.
- PPU acceptance: **11/12**.
- Harness sin BIOS: **4 PASS / 21 FAIL / 8 SKIP**.
- Harness con BIOS: **5 PASS / 20 FAIL / 8 SKIP**.
  - PASS: `dmg-acid2`, `m2_win_en_toggle`,
    `m3_lcdc_win_en_change_multiple`, `m3_scx_low_3_bits`,
    `m3_scx_high_5_bits`.
- `halt_ime0_ei` e `instr_timing`: PASS.

## Mejoras visuales (F9 → F10, con BIOS)

| Test | F9 | F10 |
|---|---:|---:|
| `m3_bgp_change_sprites` | 2524 | **400** |
| `m3_lcdc_bg_en_change` | 1316 | **153** |
| `m3_lcdc_bg_map_change` | 640 | **192** |
| `m3_lcdc_obj_en_change_variant` | 800 | **512** |
| `m3_lcdc_obj_size_change` | 410 | **371** |
| `m3_lcdc_obj_size_change_scx` | 270 | **190** |
| `m3_lcdc_tile_sel_change` | 1344 | **896** |
| `m3_lcdc_tile_sel_win_change` | 662 | 879 |
| `m3_lcdc_win_map_change` | 310 | 439 |
| `m3_obp0_change` | 140 | **42** |
| `m3_scx_high_5_bits` | 84 | **0** |
| `m3_scy_change` | 10773 | **6832** |

Sin BIOS las mejoras son equivalentes; `m3_scx_high_5_bits` también baja a
**0**, pero `m3_scx_low_3_bits` sigue dependiendo de la VRAM post-boot.

## Fase de fin de modo 3 y ciclo de vida OAM (F10.1)

Se expone el bit STAT de modo 0 en `lx=167`, un dot antes de que termine el
último píxel; el framebuffer sigue recibiendo los 160 píxeles. El bloque se
condiciona a que no queden entradas OAM pendientes en el último tile ni haya
fetch/post activo. Para poder consultarlas, las entradas OAM ya no se borran al
empezar el fetch: se conservan hasta que se sirven (`m_oam_served_count`), que
es el orden de DocBoy (`increase_lx` limpia las entradas).

Resultado con este estado:

- `intr_2_mode0_timing_sprites`: **11/12**, fallo en el **caso 53 (1 sprite
  X=1)**, HRAM `0x35`, 9.185.366 T-cycles;
- high-5 y low-3: **0**;
- harness visual idéntico al estado F10 (sin BIOS 4/21/8; con BIOS 5/20/8);
- el resto de suites sin cambios.

El caso 32 (10 sprites X=167) pasa con el ciclo de vida OAM. El caso 53 es el
bloqueo real: nuestro modo 3 para un sprite único X=1 dura ~186 dots, mientras
que el umbral del test está en 181–183. El modo 0 temprano no lo arregla; el
fallo del caso 32 que aparecía al adelantar modo 0 era un efecto colateral que
ocultaba el caso 53.

## Oráculo DocBoy (F10.2)

DocBoy pasa el test completo (`QUIT` en 0x4846). Duraciones de modo 3 medidas
en LY 0x43 (resolución de 4 T-cycles):

| caso | configuración | DocBoy | QtreGB |
|---|---|---|---:|---:|
| 52 | 1 sprite X=0 | 184 | 181 |
| 53 | 1 sprite X=1 | 184 | 186 |
| 54 | 1 sprite X=2 | 184 | 185 |
| 56 | 1 sprite X=4 | 180 | 183 |
| 32 | 10 sprites X=167 | 232 | 233 |

La traza por T-cycles de DocBoy (caso 53, LY 67) muestra:

- `lx=1` desde el dot 84; el fetch OBJ **no** arranca hasta el dot 89, cuando
  el fetcher de BG llega a la fase push;
- el fetch OBJ dura 5 T y termina en el dot 94;
- el píxel 1 se emite en el dot 95; **no hay post-wait**.

Nuestro modelo arranca el fetch en el mismo dot en que `lx` alcanza la X del
sprite y añade `post_wait` (3–9 dots). Para X=1 el stall total es 13 dots
frente a 10 de DocBoy. Esa diferencia de 2–3 dots es justo la que separa el
caso 53 del umbral.

Se probaron y descartaron, porque rompen high-5 o los grupos:

- disparar el sprite desde la fase push del fetcher (DocBoy) sin post-wait;
- `post_wait=0` y dejar que la penalización surja del fetch;
- retrasar el fetcher de BG 3 dots (antes o después del fetch) para conservar
  la fase de SCX de high-5;
- avanzar el fetcher 3 dots al inicio del fetch.

El motivo es que high-5 necesita que la lectura del tilemap ocurra 3 dots más
tarde en tiempo absoluto (después del write de SCX del CPU), y eso exige que
el stall desplace píxeles y fetcher a la vez. Sin un FIFO con relleno parcial
y un fetch de BG cacheado, el stall siempre alarga modo 3. El siguiente paso
es implementar ese prefetcher (push cuando hay hueco, cachear el fetch
interrumpido y fusionar OBJ en el FIFO), no seguir parcheando `post_wait`.

## Prototipo de prefetcher (F10.3, revertido)

Se implementó un prototipo con estas piezas:

- el fetch OBJ se dispara desde la fase **push** del fetcher de BG cuando la
  línea tiene un único sprite X>0;
- el píxel se bloquea mientras queden entradas OAM pendientes en `lx`
  (`is_bg_fifo_ready_to_be_popped` de DocBoy);
- el sprite solitario X>0 no usa `post_wait`;
- el modo 0 temprano se adelanta selectivamente para sprites solitarios con
  `X mod 8 >= 4` y para parejas en tiles adyacentes.

Resultado del prototipo:

- `intr_2_mode0_timing_sprites`: pasa los casos **0–77** y falla en el **caso
  78** (2 sprites en tiles adyacentes), tick 12.838.109;
- `m3_scx_high_5_bits`: **84** (regresión desde 0);
- harness con BIOS: **4 PASS / 21 FAIL / 8 SKIP** (antes 5/20/8);
- harness sin BIOS: **3 PASS / 22 FAIL / 8 SKIP** (antes 4/21/8);
- doctest 62/62, blargg 15/15, mts 34/34.

El prototipo se revirtió porque high-5 es prioritario. El problema de fondo
sigue siendo el mismo: high-5 (línea 8) y el caso 53 son **la misma
configuración** (un sprite X=1) y necesitan fases distintas. El camino desde
push da el timing del caso 53 (`d=178`) pero muestrea SCX 5–8 dots antes; el
camino desde píxel con `post_wait` da high-5 **0** pero `d=186`. El relleno
parcial del FIFO y el retraso post-push mueven la lectura del tilemap, pero no
llegan a los 8 dots necesarios sin vaciar el FIFO y alargar modo 3.

La solución completa requiere separar el **modo 0 visible por el CPU** del
final del pipeline de píxeles (presupuesto de dots por línea) o implementar el
fetcher de DocBoy con prefetchers BG/OBJ independientes y fusión en el OBJ
FIFO. Sin eso, cualquier ajuste de `post_wait` o de la fase del fetcher mueve
los dos tests en direcciones opuestas.

## Modelo completo de fetcher (F10.4, WIP aparte)

Se implementó el núcleo del fetcher de DocBoy en un snapshot de trabajo:

- fetch de BG con **6 dots por tile** (2 ticks por acceso a tilemap/low/high) y
  **2 dots de arranque** antes del primer acceso;
- el OBJ se sirve desde la fase **push** del fetcher; si el FIFO está vacío se
  empuja el tile primero y el OBJ espera al siguiente push;
- el píxel se hace pop **antes** de `tick_fetcher()` y `lx` avanza después,
  como en DocBoy;
- sin `post_wait` ni penalizaciones artificiales;
- el grupo de OBJ en la misma X se sirve de forma continua mientras el FIFO
  está lleno.

Resultados del WIP:

- con el gate de arranque en 83, la aceptación pasa los casos **0–18** y falla
  en el **19** (10 sprites X=10), d=235 frente a 236 de DocBoy;
- el arranque ya coincide con DocBoy: `lx=0` en 83, fetch OBJ de X=0 en 89,
  `lx=1` en 95;
- el grupo X=8 llena el FIFO (8 píxeles) y sirve los 10 OBJ seguidos, como
  DocBoy;
- high-5 queda en **65** con el gate 85 y **172** con el 83; aún no vuelve a 0.
  El barrido del latch de SCX en los dots 87–91 da **250** en todos los casos.
  El muestreo directo de `SCX%8` en cada `BgTilemap` (sin latch de línea)
  rompe high-5 (**8356**) y low-3 (**324**): hace falta modelar también el
  retardo y la fase exacta de SCX de DocBoy, no solo cambiar el punto de
  muestreo;
- en el caso 0 el primer poll del CPU (t=688403) lee `mode=3` y el modo 0 se
  escribe en t=688410: faltan ~7 dots de fase en el modo 0 visible por el CPU,
  o un ajuste equivalente del poll.

El código del WIP está en `.wip/ppu/tmp/full-fetcher-wip/` (ignorado por Git).
El árbol activo se restauró al estado validado F10.1 para no dejar una
regresión visual.

Lo que falta para cerrar la opción 2:

1. Modelar el retardo/fase de SCX y sustituir la latch única por muestreo en
   cada `BgTilemap`; el cambio directo rompe high-5 y low-3 (8356/324).
2. Alinear el modo 0 visible por el CPU (la fase del poll del test de sprites;
   con d=182 el caso 0 da B=2 en vez de 1).
3. Repetir la validación de ventana y de los cambios de registros a mitad de
   línea con el nuevo orden pop/fetcher.

## Experimentos de pipeline (descartados)

Se probaron dos cambios para separar fase y duración; ambos fueron neutros
para high-5, low-3 y el caso 53:

- **FIFO parcial**: empujar píxeles de BG en los huecos libres del FIFO de 8
  en vez de esperar a que esté vacío.
- **Retraso del fetcher**: no ejecutar `tick_fetcher()` en los primeros 3 dots
  de modo 3.

Conclusión: el camino crítico no es el llenado del FIFO ni el arranque del
fetcher por separado. La traza por dots de high-5 (línea 8) y del caso 53
(línea 68) muestra que el sprite arranca en el mismo punto (dots 86/87), hace
el mismo `post_wait=8` y la primera salida visible cae en el **mismo dot 106**
en ambos. La diferencia está en el **final de modo 3** y en los stalls
posteriores (high-5 con SCX=8, caso 53 con SCX=0), no en el arranque del
sprite ni en el FIFO.

Para resolver el caso 53 hay que ajustar la **fase global del final de modo
3/STAT** respecto al CPU, no el startup del sprite. Eso es un cambio de fase
de línea completa y puede entrar en conflicto con los tests de HBlank/STAT;
es el siguiente bloque de trabajo.

## Siguiente paso

La opción 2 está en curso. El WIP
`.wip/ppu/tmp/full-fetcher-wip/` ya tiene el fetcher de BG de 6 dots, el OBJ
desde push, el orden pop-antes-de-fetcher y el grupo de OBJ continuo. Pasa los
casos 0–18 y el arranque coincide con DocBoy, pero faltan:

1. calibrar el gate de arranque y el latch de SCX (high-5 sigue en 65–172);
2. alinear el modo 0 visible por el CPU con el poll del test de sprites
   (el caso 0 da B=2 con d=182);
3. revalidar ventana y cambios de registros a mitad de línea.

Después, atacar `m3_wx_6_change`/`m3_lcdc_win_en_change_multiple_wx` y el
muestreo de BGP/LCDC.
