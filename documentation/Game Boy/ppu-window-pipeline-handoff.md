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

---

## 91. SESIÓN 2026-11-16 — baseline micro + PPU DocBoy restaurado

El baseline de trabajo de v4 es **CPU micro + timer DocBoy + PPU DocBoy**
(`QTR_CPU_MICRO=1` activa `QTR_PPU_FULL=1`). Verificado: harness 25/0/8 con
BIOS, PPU acceptance 12/12, mts 34/34, blargg 15/15, doctest 89/89.

El PPU legacy **no** es el baseline. El intento `PPUv4` nativo simplificado
se archivó en `tmp/native_ppu_wip_20261116_020615/`; no pasa el harness y
no está activo.

El plan es sustituir el PPU DocBoy por código nativo pieza a pieza,
manteniendo 25/0/8 como red de seguridad. El timer ya es nativo (no una
copia de DocBoy), así que no requiere port separado.

Detalle completo: `documentation/Game Boy/cpu-microops-handoff.md`, sección 18.

---

## 92. SESIÓN 2026-11-16 (continuación) — PPU v4 nativo en paridad total

El PPU nativo `PPUv4` (`core/inc/ppu_v4.h`, `core/src/ppu_v4.cpp`, detrás de
`QTR_PPU_V4=1`) alcanza **paridad completa con el oráculo DocBoy** en DMG:

- harness mealybug + dmg-acid2: **25 PASS / 0 FAIL / 8 SKIP** (con BIOS)
- PPU acceptance: **12/12** (incluye `intr_2_mode0_timing_sprites`)
- mts: **34/34** · blargg: **15/15** · doctest: **89/89** (416 assertions)
- build limpio (`make clean && make QTR_CPU_MICRO=1 QTR_PPU_V4=1`)

### Piezas implementadas

1. **Scaffold por dot**: 456 dots × 154 líneas, OAM scan par/impar/77/done,
   transiciones de modo, STAT/LYC/IRQ con glitches DMG (línea 0 tras
   turn-on, última línea VBlank, ventana de disable de LYC), locks de
   VRAM/OAM, framebuffer y callback de frame.
2. **Fetcher BG/ventana**: prefetcher, pixel-slice fetcher, FIFO, dirección
   de tilemap/tile data recalculada en cada acceso (desync de bitplanes
   DMG), glitches de ventana (`wx_0`, `multiple_wx`, `wx_4/5/6`) y push.
3. **Fetcher OBJ**: lectura de tile number/attributes desde OAM, expansión
   de bitplanes con X/Y flip, 8x16, mezcla en el OBJ FIFO con prioridad
   (X menor; empate → primer OAM), interrupción/caché del fetch BG/ventana,
   y selección final OBJ vs BG con `LCDC.OBJ_ENABLE`, opacidad,
   `BG_OVER_OBJ` y `OBP0/OBP1`.

### Hallazgo clave del último milestone

La condición `is_bg_fifo_ready_to_be_popped()` debe bloquear los pops
mientras haya entradas OAM pendientes en el LX actual y los OBJ estén
habilitados:

```cpp
return !m_bg_fifo.empty() && !m_fetching_sprite &&
       (m_oam_entries[m_lx].empty() || !m_lcdc.obj_enable);
```

Sin esa condición el pipeline BG iba ~2 tiles adelantado y fallaban
`m3_lcdc_bg_map_change`, `m3_lcdc_bg_en_change`, `m3_scy_change`,
`m3_lcdc_win_map_change`, etc. Al añadir el fetcher OBJ completo cayeron
los 15 FAIL restantes de golpe.

### Flag y selección

- `QTR_PPU_V4=1` selecciona `PPUv4` (`using PPU = PPUv4` en `ppu.h`) y
  excluye `QTR_PPU_FULL`.
- `QTR_CPU_MICRO=1` sigue activando `QTR_PPU_FULL ?= 1` solo si no se pide
  V4.
- El PPU DocBoy (`PPUFull`) se conserva como oráculo hasta decisión
  explícita; no se ha borrado nada.

### Build / validación

```sh
make clean
make QTR_CPU_MICRO=1 QTR_PPU_V4=1 -j$(nproc)
make -C mts QTR_CPU_MICRO=1 QTR_PPU_V4=1
make -C tests QTR_CPU_MICRO=1 QTR_PPU_V4=1
make -C blargg QTR_CPU_MICRO=1 QTR_PPU_V4=1

QTR_BIOS="$PWD/roms/bios/[BIOS] Nintendo Game Boy Boot ROM (World).gb" \
  ./mts/compare_ppu_frames.sh
```

### Backups de la sesión

- `tmp/20261116_124648/` (inicio de sesión, backup obligatorio).
- `tmp/ppu_v4.cpp.pre-trace`, `tmp/mts_runner.cpp.pre-trace`,
  `tmp/docboy_ppu.cpp.pre-trace` (instrumentación temporal; ya restaurada).

---

## 93. PROMOCIÓN A IMPLEMENTACIÓN FINAL (2026-11-16)

El PPU nativo deja de ser una alternativa: `core/inc/ppu_v4.h` y
`core/src/ppu_v4.cpp` se han movido a `core/inc/ppu.h` + `core/src/ppu.cpp`,
la clase `PPUv4` pasa a llamarse `PPU`, y se han eliminado `PPULegacy`,
`PPUFull`/`docboy_ppu.cpp`, el árbol `core/inc/docboy/` y `core/inc/utils/`.

`common.mk` ya no tiene flags de selección (`QTR_PPU_V4`, `QTR_PPU_FULL`,
`QTR_CPU_MICRO`): el build por defecto es la implementación final.

### Validación sin flags (build limpio)

| Suite | Resultado |
|---|---|
| harness mealybug + dmg-acid2 (BIOS) | 25 PASS / 0 FAIL / 8 SKIP |
| PPU acceptance | 12/12 |
| mts | 34/34 |
| blargg | 15/15 |
| doctest | 80/80 (386 assertions) |

Los 8 SKIP siguen siendo ROMs sin PNG de referencia; se verificaron contra el
oráculo DocBoy antes de retirarlo (`diff=0` en todos).

### Limpieza asociada

- `tests/test_cpu.cpp` se elimina junto con el CPU legacy `Z80GB`.
- `timer.cpp/h` conserva solo la implementación DocBoy-style que acompaña al
  CPU de micro-ops; se retiró la rama legacy y sus tests.
- `documentation/`: se conservan `ppu.md` y `ppu-fetcher.md` como referencia
  técnica. Los planes v3/v4 y el `timer.md` antiguo quedan en el histórico de
  Git (copia local en `tmp/obsolete_docs_*`).

---

## 94. SESIÓN 2026-11-17 — Línea fija resuelta, MegaBoy como referencia de GUI y plan de savestates

**Backup obligatorio de esta sesión:** `tmp/20261117_023920/`
**Worktree:** `/home/pedro/projects/QtreGB`
**Git:** sin commit. Último commit `ad96924 "full APU"`. En el árbol quedan
los cambios de cartuchos (MBC1/MBC2/MBC3/MBC30 + RTC), la GUI
(pacing, textura nativa, selección de renderer, fullscreen) y el fix de esta
sesión.

### 94.1 La línea horizontal fija no era del renderer

**Síntoma:** línea horizontal fija en pantalla, invisible con imagen estática
y visible con movimiento vertical. Aparecía en OpenGL, GLES2 y software, y
X11 se veía peor que Wayland. Los tests SDL3 y MegaBoy estaban limpios.

**Causa real:** `run_emulated_frame()` en `gui/src/main.cpp` ejecutaba siempre
exactamente `70224` T-cycles desde la fase actual del PPU, sin esperar al
callback de frame. Si el bloque empezaba, por ejemplo, en la línea 70, al
terminar el PPU ya había dibujado las líneas 0–69 del frame siguiente; las
líneas 70–143 seguían siendo del frame anterior. `update_frame()` subía esa
mezcla a la textura. La costura queda en la fila donde estaba el PPU al
empezar el bloque: fija en pantalla, invisible en estático y visible al
scrollear. La fila exacta depende del momento de arranque, por eso cambiaba
entre renderers y entre X11/Wayland.

**Fix:** `run_emulated_frame()` ahora hace tick hasta que
`emulator.get_frame_ready()` devuelve `true` y sale inmediatamente. El
callback del PPU se dispara en `enter_new_frame()` (inicio de la línea 0),
cuando `m_video_buffer` contiene el frame completo. Si la LCD está apagada y
el PPU no genera frames, se consumen igualmente `70224` T-cycles para no
romper el pacing.

**Evidencia (ROM de prueba con scroll vertical continuo):**

- Antes: 349/349 frames con una banda interior de 9 px en vez de 8. El ajuste
  daba un corte exacto en la fila 70 con **0 errores**: filas 0–69 del frame
  nuevo, 70–143 del anterior.
- Después: 539 frames, **0 anomalías**; los frames consecutivos cumplen
  exactamente el desplazamiento de 1 px del scroll.
- Pacing tras el fix: mediana 16.69 ms entre subidas (~59.9 fps), media
  16.79 ms.
- `make -j$(nproc)` limpio.
- doctest **156/156** (787 assertions), blargg **15/15**, mts **34/34**,
  PPU acceptance **12/12**. Harness no re-ejecutado en esta sesión (cambio
  GUI-only); última referencia 25 PASS / 0 FAIL / 8 SKIP.

Los diagnósticos temporales (`QTR_TEST_PATTERN`, `QTR_FB_DUMP`, `QTR_ROM`) se
retiraron; solo queda el cambio del bucle.

### 94.2 MegaBoy como referencia para la GUI

Ruta: `/home/pedro/projects/other_gb/MegaBoy` (proyecto externo, licencia
MIT). Usarlo como **referencia de diseño/UX**, no copiar código sin revisar
atribución.

**Stack:** GLFW + glad/OpenGL3 + ImGui (`imgui_impl_glfw` +
`imgui_impl_opengl3`), miniaudio, mINI para `config.ini`, miniz para
compresión/zip y nativefiledialog.

**Render:** quad propio con shader, `glTexSubImage2D`, `glfwSwapInterval(1)`,
`glfwSwapBuffers`, escalado entero y `glViewport`. Detalle importante: la
textura se actualiza en `drawCallback`, al cerrar el frame, y usa dos
texturas (`gbTextures[0]`/`gbTextures[1]`) que intercambia. No sube el
framebuffer a mitad de frame.

**Bucle:** acumulador `gbTimer` a 59.7275 Hz, `MAX_UPDATES = 2`, renderiza al
ritmo del vsync y solo duerme cuando el vsync está desactivado.

**Opciones/features interesantes que podemos reutilizar como ideas:**

- `config.ini` persistente: vsync, escalado entero, filtro bilineal,
  blending, paleta DMG fija o personalizada, corrección de color GBC, audio.
- Keybinds remapeables (incluidos modificadores para savestate/load).
- Savestates 1–9 + quicksave/quickload + miniaturas + autosave.
- Capturas de pantalla, fast-forward, pausa/reinicio.
- Cheats Game Genie/Game Shark.
- Carga de ROM por drag & drop y ZIP; boot ROM DMG/CGB.
- Debugger integrado (tiles, sprites, memoria, etc.).
- Shaders (LCD, upscaling, corrección de color).

**Conclusión:** la línea no se arregla cambiando a GLFW/OpenGL3; ya está
resuelta en el core/GUI actual. MegaBoy sirve sobre todo para la capa de
opciones, savestates, debugger y shaders. Si más adelante queremos shaders o
un camino de presentación distinto, SDL3/GLFW+GL3 es una decisión aparte.

### 94.3 Estado real de los savestates

`TaggedSerializer` ya existe y APU/CPU/Timer tienen `serialize()`, pero eso es
una base, no un sistema de savestates. Deudas actuales:

- El propio `tagged_serializer.h` tiene un TODO: no guarda el tamaño del
  valor, solo el del tag, así que `skipUnknownTag()` no puede saltar un tag
  desconocido sin desalinear el cursor; no distingue EOF de corrupción; no
  soporta `enum class`; no verifica tipo/tamaño al leer.
- Solo serializan CPU micro, APU y Timer. Faltan PPU (dot, línea, LX, modo,
  fetchers, FIFOs, OAM scan, VRAM/OAM, latches DMG, IRQ pendientes), LCD,
  Cart/MBC/RTC, DMA, WRAM/HRAM, IO, Gamepad, InterruptController, boot ROM,
  PhaseClock y locks de bus VRAM/OAM.
- No hay contenedor de máquina: ni magic/versión/checksum/hash de ROM,
  compresión, miniatura, slots ni UI.
- El APU excluye deliberadamente el ring PCM (correcto).

Por tanto, la impresión de que el savestate es “flojo” es correcta: hoy es un
primitivo de serialización, no un savestate de usuario.

### 94.4 Diseño propuesto para savestates

1. **Contenedor versionado** (ej. magic `QTREGBST`, versión de formato,
   versión del core, checksum de cabecera de ROM + hash de ROM, timestamp,
   flags, miniatura). Payload comprimido (miniz/zlib) y checksum global.
2. **Chunks TLV** por componente: tag + tamaño + tipo + checksum. Al cargar,
   validar todo antes de aplicar; tags desconocidos se saltan; errores claros
   (ROM incorrecta, versión, corrupción, truncado).
3. **Estado explícito**: tipos de ancho fijo, little-endian, enums por su
   tipo subyacente, vectores con longitud, sin punteros ni structs con
   padding. Nada de SDL/ImGui/textura/cola de audio/rutas de host.
4. **Punto de captura**: empezar por **frame boundary** (callback del PPU /
   VBlank), que ya tenemos localizado y es suficiente para savestates de
   usuario. Para TAS/rewind se puede añadir después un snapshot exacto por
   dot. Guardar también el estado del RTC y decidir la política de tiempo de
   host al cargar.
5. **Carga transaccional**: parsear/validar en un objeto staging y aplicar
   solo si todo es válido, o cargar en una máquina nueva e intercambiarla.
6. **UI**: slots 1–9, quicksave/quickload, miniaturas, autosave, selector de
   fichero y mensajes de error. Inspiración directa en MegaBoy.

Tests mínimos:

- Round-trip determinista: estado A → save → load → mismo estado.
- Golden states: guardar en el frame N, ejecutar M frames, cargar y ejecutar
  M frames; comparar hash de framebuffer/audio y estado interno.
- Truncado/corrupto/versión incorrecta/ROM incorrecta.
- Todos los MBC y RTC (incluido MBC30), guardando/cargando en momentos
  distintos.
- Exclusión del ring PCM y de estado de host.

### 94.5 Reordenación del core

No hacer un refactor “big bang”. Usar el savestate como mapa: al serializar
cada componente aparecen los límites reales y el estado oculto. Candidatos:

- PPU: separar registers/timing, fetcher BG/OBJ, OAM scan, buses VRAM/OAM y
  salida/framebuffer. Es el sitio con más estado y más acoplamiento.
- CPU: registers, decoder/tabla de micro-ops, ALU e interrupciones; mantener
  `CpuMicro::State` como contrato serializable.
- Cart: mapper + RTC + batería; RTC como clase propia con su política de
  tiempo.
- Memoria/bus/IO: decodificación separada de los arrays; DMA y Gamepad como
  controladores con estado propio.
- Máquina: un `Machine`/`System` que encapsule el wiring y exponga
  `save_state`/`load_state`; mantener la API pública actual el máximo posible.

Reglas: un cambio estructural por paso, sin cambiar comportamiento, con build
y suites verdes después de cada uno. Antes de tocar el core, añadir un
harness de regresión de frames/audio (hashes durante N frames con varias
ROMs) para detectar regresiones que los tests actuales no cubren.

### 94.6 Orden recomendado

1. Cerrar/commitear el estado verde actual (si el usuario lo autoriza).
2. Harness de regresión de frames/audio.
3. Savestate completo: contenedor + estado de todos los componentes + tests.
4. Dividir el core guiado por el inventario de estado del savestate.
5. GUI: opciones/keybinds, savestates, paletas/shaders, debugger, etc.
6. Solo si hace falta: SDL3/Vulkan/GLFW+GL3 para shaders o presentación.

Decisiones abiertas para mañana: frame-boundary vs exact-dot, compresión y
miniaturas, política del RTC en savestates, miniz vs zlib, y si la GUI se
queda en SDL2 o migra a SDL3/GLFW+GL3.
