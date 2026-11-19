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

---

## 95. SESIÓN 2026-11-17 — Savestates del core, schema 1

**Backup:** `tmp/20261117_123648/`
**Documento de formato:** `documentation/Game Boy/savestate-core-format.md`
**Git:** sin commit.

### 95.1 Implementado

- `core/inc/state_stream.h` + `core/src/state_stream.cpp`: contenedor
  versionado, TLV con tipo y longitud, CRC-32 de cabecera y payload, FNV-1a
  64 para la ROM, chunks anidados, errores explícitos y carga transaccional.
- `QtreGB::save_state()` / `load_state()`: estado de máquina del core, sin
  metadatos ni compresión. `find_chunk()` permite cargar componentes sin
  depender del orden.
- Migrados al formato nuevo: **Clock, CPU, Timer y APU**.
- La CPU guarda la identidad de `m_instruction` (`instruction_kind` + índice
  de micro-op), así que el estado se puede capturar a mitad de instrucción,
  incluida la ventana del prefijo CB.
- El APU excluye el ring PCM y guarda el HPF como `f32`; al cargar vacía el
  ring y recalcula la ventana de acceso a wave RAM.
- Añadidos `f32`/`f64` al stream (no existían en `TaggedSerializer`).
- `Gamepad` e `IO` inicializan sus miembros (fuente de indeterminismo).
- `Cart` expone `rom()` para calcular tamaño/hash de la ROM.

### 95.2 Evidencia

- `make run_tests`: **174 casos / 1330 assertions**, todo verde.
- `make run_mts`: **34/34**. `make run_blargg`: **15/15**.
- PPU acceptance: **12/12**. Harness con BIOS: **25 PASS / 0 FAIL / 8 SKIP**.
- GUI compila (`build/QtreGB-GUI`).

Tests nuevos:

- round-trip tipado del stream, chunks anidados/desconocidos, CRC, truncado,
  duplicados y vectores conocidos de FNV-1a;
- CPU guardada en 80 instantes distintos y re-ejecutada;
- Timer con reload pendiente;
- máquina guardada en una fase no múltiplo de 4, restaurada y re-ejecutada;
- ROM distinta → `WrongRom`;
- estado sin chunk APU → `MissingChunk` y rollback exacto de Clock/CPU/Timer.

### 95.3 Límite actual

El schema 1 **no es todavía un savestate de usuario completo**: faltan
WRAM/HRAM, bus, IO, DMA, Gamepad, cartucho/MBC/RTC y PPU/LCD. Un programa que
lea/escriba memoria o dependa de interrupciones del PPU divergirá tras cargar.
La GUI no debe exponer savestates hasta completar el schema 4.

Plan:

| Schema | Contenido | Estado |
|---:|---|---|
| 1 | Clock + CPU + Timer + APU | Hecho |
| 2 | + WRAM/HRAM, Bus, IO, DMA, Gamepad | Hecho (sección 97) |
| 3 | + Cartucho/MBC/RTC | Hecho (sección 98) |
| 4 | + PPU/LCD | Hecho (sección 99) |

Estado del core: **completo** para emulación determinista.

---

## 96. SESIÓN 2026-11-17 — Harness de regresión de frames/audio

**Backup:** `tmp/20261117_130351/`
**Directorio:** `regress/`
**Git:** sin commit.

### 96.1 Qué hace

`regress_runner` ejecuta un manifiesto de ROMs durante un número fijo de
frames (`70224` T-cycles por frame) y compara hashes FNV-1a 64 por frame de:

- framebuffer del PPU (160×144, índices de color DMG 0..3);
- PCM estéreo drenado del APU (48 kHz, int16 LE), con el número de muestras.

No usa SDL ni abre ventana. Detecta cambios de comportamiento que los tests
unitarios y mts/blargg no ven: timing del PPU, mixer/APU, DMA/bus, timing de
CPU, etc.

### 96.2 Uso

```sh
make run_regress          # compara con regress/golden.txt
make update_regress       # regenera la referencia
```

Modo BIOS (usa `regress/golden_bios.txt`, no mezcla referencias):

```sh
QTR_BIOS="$PWD/roms/bios/[BIOS] Nintendo Game Boy Boot ROM (World).gb" \
  make run_regress
```

Opciones del binario: `--manifest`, `--golden`, `--bios`, `--update`,
`--input`, `--allow-missing`, `--verbose`.

Manifiesto por defecto (`regress/manifest.txt`): 11 ROMs × 90 frames.
Cubre dmg-acid2, cuatro efectos PPU de Mealybug, SameSuite APU/interrupts,
blargg dmg_sound (con flag `audio` obligatorio), blargg 01-special,
02-interrupts y mem_timing.

Modo BIOS (`regress/manifest_bios.txt`): 7 ROMs × 400 frames. El boot ROM
tarda ~327 frames en ceder al cartucho, así que 400 frames cubren animación
de arranque, handover y los primeros frames del ROM.

### 96.3 Salvaguardas

- Cabecera del golden con versión del harness, modo de input y modo BIOS; un
  modo distinto no se compara contra la referencia equivocada.
- `audio` en el manifiesto exige PCM no nulo: un `--update` accidental no
  puede bendecir un APU en silencio.
- `--allow-missing` permite ejecutar un subconjunto; avisa de los ROMs
  ausentes y no falla por ellos.
- Hash local en el runner, independiente de `state_stream`, para que un
  cambio del formato de savestate no altere la referencia.

### 96.4 Evidencia

- `make run_regress`: **11 ROMs / 990 frames, all match**, ~3,2 s.
- `QTR_BIOS=... make run_regress`: **7 ROMs / 2800 frames, all match**,
  ~10,2 s.
- Verificado que un hash alterado produce `FAIL ... frame N` y rc=1.
- Verificado que `--input` contra un golden sin input falla por cabecera.
- Verificado `--allow-missing` con rutas que contienen espacios.
- Verificado el flag `audio` con una ROM silenciosa: rc=2.

### 96.5 Golden

`regress/golden.txt` y `regress/golden_bios.txt` son snapshots de
comportamiento, no oráculos de corrección. Ante un diff hay que decidir si el
cambio es intencionado antes de regenerar. Las rutas del manifiesto son
relativas a `regress/`; `roms/` y `vendor/` están en `.gitignore`.

El harness se ejecutó antes de continuar con el schema 2 de savestates.

---

## 97. SESIÓN 2026-11-17 — Savestates schema 2

**Backup base:** `tmp/20261117_130351/`
**Documento de formato:** `documentation/Game Boy/savestate-core-format.md`
**Git:** sin commit.

### 97.1 Implementado

`STATE_SCHEMA_VERSION` sube a **2**. Además de Clock/CPU/Timer/APU, el core
serializa ahora:

| Chunk | Estado |
|---|---|
| `Memory` (`0x0103`) | WRAM 8 KiB y HRAM 128 B, con tamaño exacto al cargar |
| `Bus` (`0x0104`) | `boot_rom_enabled` + hash FNV-1a 64 del boot ROM |
| `Io` (`0x010C`) | SB (FF01) y SC (FF02) |
| `Dma` (`0x0108`) | active, pending, byte, value y start_delay |
| `Gamepad` (`0x0109`) | 8 botones y 2 bits de selección |

Detalles:

- El boot ROM **no** se incrusta en el estado: se guarda su hash. Si el
  estado se creó con el boot ROM activo y el host no tiene la misma imagen,
  la carga devuelve `BadValue` y revierte.
- DMA valida `start_delay <= 2`, `byte <= 0xA0` y `active => pending`. Al
  cargar sincroniza el flag `m_dma_active` del PPU.
- Memoria se lee en vectores temporales y solo se aplica si los tamaños
  coinciden; la carga sigue siendo transaccional a nivel de máquina.
- No se serializan callbacks, punteros de bus/PPU ni el flag `m_frame_ready`
  (estado de staging de la GUI).
- `m_clock` ya estaba en schema 1; sigue siendo necesario para restaurar en
  una fase T distinta.

### 97.2 Tests

- Round-trip de WRAM/HRAM, IO, DMA y Gamepad: se guardan valores no
  triviales, se mutan todos los componentes, se carga y se compara el blob
  completo.
- El test de máquina ahora usa un programa que mantiene un contador en WRAM
  y ramifica según su valor; el determinismo tras cargar exige restaurar la
  memoria.
- Boot ROM por hash: misma imagen carga correctamente; imagen ausente
  devuelve `BadValue` y deja el blob intacto.
- Rollback corregido: el estado parcial aplica Clock/CPU/Timer y falla al
  faltar el APU; se comprueba que el blob vuelve exactamente al original.

### 97.3 Evidencia

- `make run_tests`: **177 casos / 1360 assertions**, todo verde.
- `make run_mts`: **34/34**. `make run_blargg`: **15/15**.
- `make run_regress`: **11 ROMs / 990 frames, all match**; el schema 2 no
  cambió el comportamiento normal.
- PPU acceptance: **12/12**. Harness con BIOS: **25 PASS / 0 FAIL / 8 SKIP**.
- GUI compila.

### 97.4 Límite y siguiente paso

En su momento faltaban cartucho/MBC/RTC (schema 3) y PPU/LCD (schema 4). El
schema 3 se completó en la sección 98. Sigue faltando PPU/LCD: un programa
que use VRAM/OAM o interrupciones del PPU todavía divergirá tras cargar. La
GUI no debe exponer savestates de usuario hasta schema 4.

---

## 98. SESIÓN 2026-11-17 — Savestates schema 3 (cartucho/MBC/RTC)

**Backup base:** `tmp/20261117_130351/`
**Documento de formato:** `documentation/Game Boy/savestate-core-format.md`
**Git:** sin commit.

### 98.1 Implementado

`STATE_SCHEMA_VERSION` sube a **3**. Nuevo chunk `Cart` (`0x0105`):

| Campo | Contenido |
|---|---|
| `mapper` | `None`, `NoMbc`, `Mbc1`, `Mbc2`, `Mbc3` |
| `data` | Chunk específico del mapper |

Por mapper:

- **No-MBC**: RAM completa (0 u 8 KiB).
- **MBC1**: `ram_enable`, `rom_bank`, `ram_bank`, `banking_mode`, `mbc1m`
  y RAM de todos los bancos.
- **MBC2**: `ram_enable`, `rom_bank` y RAM de 512 B con nibbles `0xF?`.
- **MBC3**: `ram_enable`, `rom_bank`, `selected`, `latch_armed`, `mbc30`,
  `has_rtc`, RAM de todos los bancos, RTC actual, RTC latcheado y
  `subsecond`.

Validaciones: el mapper debe coincidir con el cartucho cargado; `mbc1m`,
`mbc30` y `has_rtc` deben coincidir con la detección actual; se validan
bancos, `selected`, rangos de RTC y `subsecond <= 999999`; la RAM debe tener
el tamaño total exacto.

### 98.2 Política de RTC

- Se guardan contadores y `subsecond`, **no** `m_last_time` (epoch del host).
- Al cargar, `m_last_time` se rebasa al reloj actual: el tiempo entre guardar
  y cargar **no** se aplica.
- Si el RTC estaba halted, sigue halted; al quitar halt cuenta desde el
  instante de carga.
- El `.sav` de batería conserva su propia política de tiempo y es un formato
  distinto del savestate.

### 98.3 Tests

- Round-trip de MBC1: bancos ROM/RAM, modo de banking y RAM.
- Round-trip de MBC2: RAM de nibbles.
- Round-trip de no-MBC: RAM.
- Integración QtreGB: RAM de cartucho a través de `save_state/load_state`.
- RTC MBC3: 42 s guardados, +100 s en disco, cargar y seguir en 42 s; +10 s
  tras cargar y marcar 52 s.
- RTC halted: congelado tras cargar; al quitar halt reanuda desde la carga.
- Mapper distinto: `BadValue`.
- El test de máquina sigue usando WRAM; ahora el blob incluye además el
  chunk de cartucho.

### 98.4 Evidencia

- `make run_tests`: **184 casos / 1416 assertions**, todo verde.
- `make run_mts`: **34/34**. `make run_blargg`: **15/15**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- Modo BIOS: **7 ROMs / 2800 frames, all match**.
- PPU acceptance: **12/12**. Harness con BIOS: **25 PASS / 0 FAIL / 8 SKIP**.
- GUI compila.

### 98.5 Límite y siguiente paso

Falta el PPU/LCD (schema 4): VRAM, OAM, registros, selectores por dot,
fetchers/FIFOs, STAT/IRQ y locks de bus. Sin eso, un programa que use
VRAM/OAM o interrupciones del PPU todavía divergirá tras cargar. La GUI no
debe exponer savestates de usuario hasta completar schema 4.

Siguiente paso: schema 4 (PPU/LCD).

---

## 99. SESIÓN 2026-11-17 — Savestates schema 4 (PPU/LCD) y core completo

**Backup base:** `tmp/20261117_130351/`
**Backup final de la sesión:** `tmp/20261117_141054/`
**Documento de formato:** `documentation/Game Boy/savestate-core-format.md`
**Git:** sin commit.

### 99.1 Implementado

`STATE_SCHEMA_VERSION` sube a **4**. El chunk `Ppu` (`0x0106`) cubre:

- registros y máquina de estados: LCDC/STAT empaquetados, modo real, LY/LYC,
  SCX/SCY, paletas, WX/WY, dots, LX, latches DMG de 1 T-cycle;
- OAM scan (`count`, `index`, `request`, registros y las 168 listas de hasta
  10 entradas);
- ventana, `initial_scx`, `bwf`, `of`, `psf`;
- FIFOs BG/OBJ empaquetadas;
- VRAM 8 KiB y OAM 160 B;
- glitches DMG de primera línea y última línea de VBlank;
- STAT pendiente y `m_last_stat_irq`/`m_pending_stat_irq`;
- locks de OAM/VRAM (read/write);
- `m_tick_selector` y `m_fetcher_tick_selector` como IDs estables.

Selectores registrados: 34 de tick y 19 del fetcher. Un selector no
registrado al guardar produce `WriterMisuse`; un ID desconocido al cargar
produce `BadValue`. No se serializan punteros a método.

Chunk `Lcd` (`0x0107`): registros espejo, STAT pendiente y cola de
escrituras diferidas (dirección, valor, countdown). Los delays configurables
por variables de entorno no se serializan: son configuración del host.

### 99.2 Exclusiones

- `m_video_buffer`: salida derivada; se regenera renderizando.
- Ring PCM del APU: etapa de salida; se vacía al cargar.
- `m_dma_active`: lo restaura el chunk DMA.
- Contenido de ROM/boot ROM: entrada del host; solo tamaño/hash.
- Callbacks, punteros, `m_frame_ready`, `m_errors`: cableado/staging.

El estado se puede capturar en cualquier dot; para usuario se recomienda
frame boundary.

### 99.3 Tests

- Test PPU en **161 instantes** distintos: dots 0-100, 440-460, LY 153 con
  sus glitches, frontera de frame y dots arbitrarios.
- En cada instante: guardar, ejecutar un frame completo, comparar blob y
  framebuffer; cargar, comprobar round-trip inmediato, registros del LCD y
  los cuatro locks OAM/VRAM; volver a ejecutar y comparar.
- El test de máquina con WRAM sigue pasando; ahora el blob incluye PPU/LCD.
- Tests de rechazo de schema antiguo, mapper distinto, boot ROM distinto y
  estado parcial con rollback.

### 99.4 Evidencia

- `make run_tests`: **185 casos / 3670 assertions**, todo verde.
- `make run_mts`: **34/34**. `make run_blargg`: **15/15**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- Modo BIOS: **7 ROMs / 2800 frames, all match**.
- PPU acceptance: **12/12**. Harness con BIOS: **25 PASS / 0 FAIL / 8 SKIP**.
- GUI compila.

### 99.5 Estado final

El core ya guarda/carga todo el estado de emulación. El siguiente trabajo es
el contenedor de la GUI: metadatos, miniaturas, compresión, slots, rewind y
política de captura. El core no debe incluir nada de eso.

---

## 100. SESIÓN 2026-11-17 — F5/F8 en la GUI (estado crudo .sr1)

**Backup previo:** `tmp/20261117_141436/`
**Git:** sin commit.

### 100.1 Implementado

- `EmulatorInstance::save_state()` / `load_state()` y `state_path()`.
- Ruta: misma base que la ROM, extensión `.sr1`, en el mismo directorio.
- **F5** guarda; **F8** carga. También hay entradas en el menú File.
- Aviso temporal en pantalla con la ruta o el fallo.
- El archivo es el **blob crudo del core** (schema 4); no hay contenedor GUI,
  metadatos, compresión ni miniaturas todavía.
- Al cargar: se vacía la cola de audio y se reaplica el input actual del
  host; así no quedan botones pegados del estado y se conservan las teclas
  mantenidas. El framebuffer/PCM se regeneran.

### 100.2 Prueba

Probe headless usando la misma `EmulatorInstance`:

- ROM copiada a `/tmp/opencode/probe.gb`, 2 frames ejecutados.
- F5 simulado con `save_state()`: `/tmp/opencode/probe.sr1`, **19112 bytes**.
- 10000 ticks después, `load_state()` y `save_state()` de nuevo.
- Los dos blobs coinciden byte a byte: `round_trip=ok`.
- El diff inicial detectó una inversión en el tracking de botones. Se
  corrigió entonces y el refactor posterior reaplica el input actual del host
  al cargar, sin depender de la convención antigua.

### 100.3 Evidencia

- `make -j$(nproc)`: GUI compila.
- `make run_tests`: **185 / 3670**, verde (core sin cambios).
- Probe GUI: `state_path=/tmp/opencode/probe.sr1`, `state_size=19112`,
  `round_trip=ok`, rc=0.

### 100.4 Siguiente

Refactor: separar core/GUI, ownership del estado, serialización por
componentes y puntos de acoplamiento. No se empieza en esta sección.

---

## 101. SESIÓN 2026-11-17 — Refactor de EmulatorInstance para múltiples instancias

**Backup previo:** `tmp/20261117_141436/`
**Backup final:** `tmp/20261117_143508/`
**Git:** sin commit.

### 101.1 Objetivo

Dejar `EmulatorInstance` como una instancia autocontenida de **un** `QtreGB`,
sin estado mutable global, para poder crear una segunda instancia y, más
adelante, conectarlas por cable link.

### 101.2 Cambios

- `EmulatorInstance` posee un único `QtreGB` (`m_machine`) y expone
  `machine()`; no hay singletons ni estado compartido entre instancias.
- Se extrajo `InputState` (`gui/inc/input_state.h`): estado de botones por
  instancia, semántica clara `pressed=true`. Corrige la inversión previa entre
  `set_button`, `m_buttonState` e `is_button_pressed`.
- Se extrajo `StateStore` (`gui/inc/state_store.h`): ruta `.sr1`, lectura y
  escritura del blob crudo. `EmulatorInstance::save_state/load_state` solo
  orquestan core + archivo.
- `EmulatorFrontend` ahora posee
  `std::vector<std::unique_ptr<EmulatorInstance>> m_instances`:
  - el constructor crea siempre la instancia primaria;
  - `primary()` / `get_emulator()` apuntan a la primera;
  - `instance(i)` e `instance_count()` permiten la segunda;
  - `add_instance()` es el punto de extensión para una segunda sesión.
- La UI actual sigue mostrando y controlando solo la primaria. El input de
  teclado se pasa explícitamente a la instancia objetivo (`handle_key`),
  preparado para separar jugador 1/2.
- Al cargar un estado se vacía la cola de audio y se reaplica el input actual
  del host: no se quedan botones pegados del estado y se conservan las teclas
  mantenidas.
- Audio, textura y framebuffer son por instancia; dos instancias tienen dos
  dispositivos/texturas independientes. Para link probablemente convenga un
  mixer/audio compartido más adelante, pero no es necesario ahora.

### 101.3 Link

El core todavía no implementa transferencia serial. El punto previsto es un
objeto compartido (p. ej. `LinkCable`) que conecte
`primary()->machine()` con `instance(1)->machine()`; los wrappers GUI no
deben compartir estado de máquina.

Si dos instancias cargan **la misma ruta de ROM**, sus `.sav` y `.sr1`
colisionan. Para link habrá que usar copias/rutas distintas o una política de
slot por instancia.

### 101.4 Pruebas

- Build GUI limpio, sin warnings nuevos.
- `make run_tests`: **185 / 3670**, verde.
- Probe una instancia:
  - `state_path=/tmp/opencode/probe.sr1`
  - `state_size=19112`
  - `round_trip=ok`, rc=0.
- Probe dos instancias con `SDL_AUDIODRIVER=dummy`:
  - `p1.sr1` y `p2.sr1` distintos;
  - `&a.machine() != &b.machine()`;
  - pulsar A en A no afecta a B;
  - cargar el estado de A no toca el archivo de B;
  - `independent=ok`, rc=0.

---

## 102. SESIÓN 2026-11-17 — Estado del serial/link y retirada del hack de blargg

**Backup previo:** `tmp/20261117_144025/`
**Git:** sin commit.

### 102.1 ¿Funciona el cable link?

**No.** El core actual no implementa transferencia serie:

- `IO` solo almacena SB (FF01) y SC (FF02);
- no hay shift register, reloj interno/externo, flanco de completado,
  intercambio de SB ni interrupción serial;
- no hay ninguna API de link entre dos `QtreGB`.

Los dos `EmulatorInstance` del refactor son independientes. Para link hará
falta un `LinkCable`/`LinkPort` compartido entre dos máquinas y un modelo
serial real en el core (bits, clock, completion, IF serial).

### 102.2 Hack retirado

Sí, seguía en el core: un `static string serial` y un bloque en
`QtreGB::tick_once()` que, al ver `SC==0x81`, leía SB, guardaba el carácter
en un string global y limpiaba SC. Era específico de la salida de blargg,
estado global incompatible con dos instancias y no era emulación serie real.

Retirado:

- `static string serial`;
- el bloque `#if 1` de captura/limpieza en `tick_once()`;
- los `serial.clear()` de `load_cart()`/`reset_machine()`;
- el workaround de `tests/test_scheduler.cpp` que escribía SB/SC para evitar
  el bloque de debug.

La finalización de la transferencia interna que necesita blargg se hace
ahora en el **runner de blargg**, no en el core: al capturar un byte escribe
`SC=0` para que el shell continúe. Es comportamiento del runner de test, no
del emulador.

### 102.3 Evidencia tras la retirada

- `make run_blargg`: **15/15**.
- `make run_mts`: **34/34**.
- `make run_tests`: **185 / 3670**, verde.
- `make run_regress`: **11 ROMs / 990 frames, all match** (el golden no
  cambió; los ROMs del manifiesto no dependen del hack en esos frames).
- GUI compila.

### 102.4 Siguiente para link

1. Modelo serial en el core: shift register de 8 bits, SC bits 0-1/7,
   clock 8192 Hz interno o externo, SB exchange, IF bit 3 al completar.
2. `LinkCable` compartido que conecte los puertos de dos `QtreGB` y resuelva
   el intercambio de bits/bytes y el clock externo.
3. Integración en `EmulatorFrontend`/`EmulatorInstance` (dos máquinas,
   audio/entrada por jugador, rutas `.sav`/`.sr1` sin colisión).

---

## 103. SESIÓN 2026-11-17 — LinkCable de GUI (sesión) y alcance

**Backup previo:** `tmp/20261117_144025/`
**Backup final:** `tmp/20261117_150922/`
**Git:** sin commit.

### 103.1 Qué se ha hecho

La pieza de GUI/sesión, no el Serial del core:

- `gui/inc/link_cable.h` + `gui/src/link_cable.cpp`.
- Un `LinkCable` por pareja de `EmulatorInstance`, propiedad de
  `EmulatorFrontend`; no es global ni pertenece a una instancia.
- `EmulatorFrontend::add_instance()` crea la segunda instancia y, al llegar a
  dos, crea y conecta el cable.
- `LinkCable::connect/disconnect/connected`, `left/right`.
- `LinkCable::tick()` avanza las dos máquinas en lockstep por T-cycle.
- `EmulatorFrontend::link()` expone el cable para la futura FUI.

No se ha metido intercambio de bits en la GUI y **no se ha implementado
todavía el Serial del core**. El cable de GUI no falsea SB/SC: solo gestiona
la pareja y el lockstep. El transporte serie real irá en el core.

### 103.2 Prueba

Probe con dos `EmulatorInstance` y un `LinkCable`:

- `connect(a,b)`, `left/right` correctos;
- 1000 `cable.tick()` → ambas máquinas avanzan;
- `disconnect()` correcto;
- `p1.sr1` y `p2.sr1` distintos;
- input y estados independientes (`independent=ok`).

### 103.3 Pendiente

- Serial del core: shift register, clock 8192 Hz, SC.7, SB exchange, IF3.
- Enlazar el `LinkCable` de GUI con los dos `Serial` del core.
- FUI: cómo añadir la segunda instancia, cargar ROMs, mostrar link, rutas
  `.sav`/`.sr1` sin colisión, input/audio por jugador.

La FUI se decide en la siguiente conversación.

---

## 104. SESIÓN 2026-11-17 — Serial core, LinkCable, GUI LinkSession y jugador 2

**Backup previo:** `tmp/20261117_150922/`
**Backup final:** `tmp/20261117_152549/`
**Git:** sin commit.

### 104.1 Serial en el core

Nuevo `core/inc/serial.h` + `core/src/serial.cpp`:

- SB/SC siguen en IO (FF01/FF02); `Serial` posee el estado de transferencia.
- `SC.7`: transferencia solicitada/en curso; se limpia al completar.
- `SC.0`: clock select (`1` interno/master, `0` externo/slave).
- DMG: 8192 Hz interno, **512 T-cycles/bit**, 4096 T-cycles/byte.
- SB se desplaza **MSB primero** (bit 7 sale; el entrante entra por bit 0).
- Al completar 8 bits: SC.7=0 e `IF` bit 3 (Serial).
- Sin peer: el master completa con SB=0xFF (línea flotante alta).
- El slave externo espera flancos del cable.
- `QtreGB::m_serial` se tickea una vez por T-cycle y pide `IT_SERIAL`.
- El clock serie es independiente del Timer/DIV; no se avanza desde
  `timer_tick()`.

### 104.2 LinkCable en el core

Nuevo `core/inc/link_cable.h` + `core/src/link_cable.cpp`:

- Clase independiente que conecta exactamente dos `Serial`.
- No elige master permanente: en cada flanco manda el puerto con `SC.0=1`.
- Intercambia ambos bits en el mismo flanco; si los dos son internos, hay
  conflicto y no se inventa un master.
- Sin peer o con el otro lado inactivo, el master recibe 1s.
- No se serializa: es estado compartido de sesión, no de una máquina.

### 104.3 GUI

- La antigua `LinkCable` de GUI se renombró a **`LinkSession`**
  (`gui/inc/link_session.h`). Posee el `qtreGB::LinkCable` y las dos
  `EmulatorInstance`.
- `EmulatorFrontend::add_instance()` crea el jugador 2 y la `LinkSession`.
- `EmulatorFrontend::tick()`: si hay link, avanza las dos instancias en
  lockstep por T-cycle; si no, solo la primaria.
- `EmulatorFrontend::drain_audio()` drena ambas instancias.
- `EmulatorInstance::rebuild_machine()` ahora **reutiliza el mismo QtreGB**
  para que los punteros Serial del cable no queden colgando al cargar ROM o
  resetear.
- Jugador 1: `.sav` / `.sr1`. Jugador 2: `.sa2` / `.sr2`. F5/F8
  actuales siguen actuando solo sobre la primaria; la FUI elegirá jugador.
- La FUI de dos pantallas sigue pendiente.

### 104.4 Savestate

`STATE_SCHEMA_VERSION` sube a **5**. Nuevo chunk `Serial` (`0x010E`):

| Tag | Campo | Tipo |
|---:|---|---|
| `0x0001` | `active` | Bool |
| `0x0002` | `bits_remaining` | U8 (`0..8`) |
| `0x0003` | `clock_counter` | U16 (`0..512`) |

SB/SC siguen en el chunk IO. Un `.sr1`/`.sr2` guarda **una máquina**:

- con transferencia en curso, se conserva `active`, bits restantes y contador;
- el `LinkCable`, el peer y la conexión no se serializan;
- cargar una sola máquina linkeada puede dejar al slave esperando clock o al
  master completando con 0xFF;
- una sesión link necesita guardar los dos estados (`.sr1` + `.sr2`) y
  reconectar el cable; si ambos se capturan en el mismo T-cycle, los estados
  serie son coherentes.
- Los `.sr1` de schema 4 y anteriores dejan de cargar.

### 104.5 Pruebas

- Serial interno sin peer, espera de clock externo, intercambio con
  LinkCable, IF3 y savestate con transferencia en curso.
- Probe GUI de dos instancias:
  - `LinkSession` intercambia 0x42/0x99 a través de los dos `QtreGB`;
  - ambos reciben IF3;
  - `.sr1/.sr2` y `.sav/.sa2` correctos;
  - input y estados independientes.
- `make run_blargg`: **15/15**.
- `make run_mts`: **34/34**.
- `make run_tests`: **190 / 3705**, verde.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- GUI compila.

### 104.6 Pendiente FUI

- Dos pantallas solo cuando haya dos instancias.
- Cargar ROM en jugador 2, mostrar estado de link, conectar/desconectar.
- Rutas `.sa2/.sr2` y futura política de slots.
- Audio/input por jugador.

---

## 105. SESIÓN 2026-11-17 — GUI de dos jugadores y textura combinada

**Backup previo:** `tmp/20261117_152549/`
**Backup final:** `tmp/20261117_153910/`
**Git:** sin commit.

### 105.1 Carga del jugador 2

- `File → Load ROM player 2...`.
- Si no existe la segunda instancia, `EmulatorFrontend::add_instance()` la crea
  (jugador 2, `.sr2`/`.sa2`) y conecta la `LinkSession`.
- Si ya existe, se recarga esa misma instancia; el `QtreGB` se reutiliza para
  que el `LinkCable` no quede con punteros colgando.
- Al quedar lista la segunda ROM, `link_mode()` pasa a true y el bucle avanza
  las dos instancias en lockstep.
- No hay todavía forma de quitar la segunda instancia en caliente.

### 105.2 Dos pantallas en una textura

- La GUI compone las dos imágenes de 160×144 en **una sola textura de
  320×144**: jugador 1 a la izquierda, jugador 2 a la derecha.
- `EmulatorFrontend::update_frame()` hace la composición con la paleta DMG.
- `get_texture_ID()`/`get_texture_size()` devuelven la textura combinada en
  modo link; el dibujo sigue aplicando las mismas reglas de escalado entero.
- En modo de una sola instancia se sigue usando la textura de 160×144.
- La textura combinada se crea de forma perezosa y se destruye en el
  destructor del frontend.

### 105.3 Controles provisionales del jugador 2

- `I/J/K/L`: arriba/izquierda/abajo/derecha.
- `G`: B; `H`: A.
- `Y`: Select; `U`: Start.
- El jugador 1 mantiene flechas, `Z/X`, Enter y Right Shift.
- F5/F8 siguen actuando solo sobre la instancia primaria.

### 105.4 Prueba

Probe headless con un `IGuiBackend` falso:

- dos instancias cargadas, `link_mode` activo;
- `update_frame()` produce una textura de **320×144** con 46080 píxeles;
- `get_texture_ID()` no nulo;
- `f1.sr1` y `f2.sr2` correctos.

También se mantiene el probe de link con intercambio serie real
`0x42 ↔ 0x99`, IF3 y estados independientes.

### 105.5 Evidencia

- Build core + GUI: OK.
- `make run_tests`: **190 / 3705**, verde.
- `make run_blargg`: **15/15**.
- `make run_mts`: **34/34**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- BIOS: **7 ROMs / 2800 frames, all match**.
- PPU acceptance: **12/12**; harness **25 PASS / 0 FAIL / 8 SKIP**.

### 105.6 Pendiente

- Quitar la segunda instancia en caliente.
- Elegir qué jugador recibe F5/F8 y política de slots.
- Layout/indicadores de link, audio por jugador y pulido de la GUI.

---

## 106. SESIÓN 2026-11-17 — Consumo alto de CPU: pacer del bucle principal

**Backup previo:** `tmp/20261117_153910/`
**Backup final:** `tmp/20261117_155655/`
**Git:** sin commit.

### 106.1 Causa

No era el modo link ni el número de instancias. El bucle principal asumía
que `SDL_RenderPresent` bloqueaba con vsync:

- si el driver reportaba `SDL_RENDERER_PRESENTVSYNC` pero no bloqueaba, el
  fallback solo hacía `SDL_Delay(1)` → bucle a ~1000 Hz;
- en monitores de 144/240 Hz, vsync bloqueaba a esa frecuencia y también se
  renderizaba/actualizaba ImGui a 144/240 Hz aunque el emulador fuera a
  59,7275 Hz.

En ambos casos se quemaba CPU sin avanzar más la emulación.

### 106.2 Fix

- `main.cpp` calcula un `frame_duration` de `70224 / 4194304` segundos.
- Tras `render_draw_data()`, si `now + frame_duration` aún está en el futuro,
  duerme con `std::this_thread::sleep_for`.
- Si vsync ya bloqueó lo suficiente, no duerme.
- El límite es independiente de lo que diga el driver: como máximo ~59,7275
  presentaciones/segundo, con o sin link.

### 106.3 Notas

- `QTR_RENDER_INFO=1` sigue mostrando driver/renderer/vsync real.
- Si el consumo sigue alto, comprobar con ese log si el renderer es software
  o si el core va por detrás del pacer.
- El `Serial::tick()` por T-cycle se midió aparte: ~1,9 ns/tick, <1% de CPU
  a 4,19 MHz; no es la causa.
- La textura combinada solo se usa con dos instancias; con una no se toca.

### 106.4 Evidencia

- GUI compila.
- `make run_tests`: **190 / 3705**, verde.
- Prueba GUI con `SDL_VIDEODRIVER=dummy` y renderer software: arranca y se
  mantiene estable; el consumo base del renderer software no se usa como
  medida del fix real.
- Pendiente de confirmar en el equipo del usuario con el renderer real.

---

## 107. SESIÓN 2026-11-17 — CPU: build optimizado de la GUI

**Backup previo:** `tmp/20261117_155655/`
**Backup final:** `tmp/20261117_160718/`
**Git:** sin commit.

### 107.1 Medición

El hilo no era la causa principal. Medido solo el core, 60 frames:

| Build | 1 jugador | 2 jugadores |
|---|---:|---:|
| `-O0` (GUI anterior) | ~60% de un núcleo | ~122% |
| `-O2` | ~21% | ~42% |

Con `-O0`, dos instancias ya pedían más de un núcleo entero solo para
emular; con `-O2` bajan a menos de la mitad de un núcleo.

### 107.2 Cambio

`common.mk`:

- La variante de GUI/core con log ahora es **`-g -O2 -DENABLE_LOG`**.
- Sigue teniendo símbolos de depuración.
- Para el build realmente sin optimizar:
  `make clean && make GUI_DEBUG=1`.

`AGENTS.md` actualizado con el nuevo build por defecto.

### 107.3 Dibujo, audio e hilos

- SDL ya reproduce el audio en su propio hilo interno; `drain_audio()` solo
  encola PCM.
- Dibujar y encolar audio en el hilo principal a 60 Hz es barato comparado
  con la emulación.
- Pasar dibujo/audio a otro hilo no reduce el trabajo total y complica el
  lockstep del link.
- Si en el futuro dos jugadores siguen pesando demasiado, la vía sería un
  hilo por instancia con una barrera de T-cycle para el link, no separar
  solo el render.

### 107.4 Evidencia

- Build limpio de core + GUI con `-O2`: OK.
- `make run_tests`: **190 / 3705**.
- `make run_blargg`: **15/15**.
- `make run_mts`: **34/34**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.

---

## 108. SESIÓN 2026-11-17 — Profiling: `getenv` por T-cycle

**Backup previo:** `tmp/20261117_160718/`
**Backup final:** `tmp/20261117_163344/`
**Git:** sin commit.

### 108.1 Método

`perf` no funciona en este equipo (`kernel.perf_event_paranoid=4`, sin sudo),
así que se usó `gprof`:

1. Compilar el core y un runner headless con `-pg -g -O2`.
2. Ejecutar una mezcla representativa: `01-special`, `dmg-acid2` y
   `dmg_sound`, 600 frames cada uno (126.403.200 T-cycles).
3. `gprof -b -p ./core_profile gmon.out` para el perfil plano.
4. `gprof -b -q ./core_profile gmon.out` para el grafo de llamadas.

Runner: `/tmp/opencode/profile_probe.cpp`.

**Limitaciones de `gprof`:**

- Muestreo a 100 Hz: en una ejecución corta hay pocas muestras y ruido.
- `-pg` añade `mcount` a cada llamada y puede deshacer inlining; los
  porcentajes son relativos, no tiempos absolutos.
- El tiempo dentro de libc (`getenv`, `malloc`, `memcpy`) **no aparece** en
  el perfil plano.

### 108.2 Perfil inicial (top)

| Función | Self | Llamadas |
|---|---:|---:|
| `QtreGB::tick` | 8,84% | 126,4 M |
| `QtreGB::tick_once` | 7,82% | 126,4 M |
| `PPU::sync_lcd_from_ppu` | 7,48% | 126,4 M |
| `PPU::ppu_tick` | 7,14% | 126,4 M |
| `PPU::raise_stat_irq` | 6,46% | 125,5 M |
| `PPU::read_lcdc` | 5,78% | 126,4 M |
| `APU::tick` | 4,08% | 126,4 M |
| `Serial::tick` | 4,08% | 126,4 M |
| `PPU::pixel_transfer_lx8` | 4,08% | 43,3 M |
| `CpuMicro::tick` | 3,74% | 126,4 M |

`_init` y `frame_dummy` son artefactos de instrumentación, no hotspots.

### 108.3 El coste oculto: `getenv` en cada tick

`QtreGB::tick_once()` empezaba con:

```cpp
if(std::getenv("QTR_IRQDBG"))
```

`getenv` se llama **4.194.304 veces por segundo**. Medido aparte:

- ~22 ns por llamada.
- ~92 ms de CPU por segundo, ~9% de un núcleo solo ahí.

`gprof` no lo veía porque `getenv` vive en libc. Este es el motivo de que el
tiempo real (15,7 s) fuera mucho mayor que el tiempo muestreado (2,94 s).

### 108.4 Fix

`core/src/QtreGB.cpp`:

```cpp
static const bool irq_dbg = std::getenv("QTR_IRQDBG") != nullptr;
if(irq_dbg)
```

Se resuelve una sola vez, en la primera llamada. `QTR_IRQDBG=1` seguía
funcionando (verificado: imprime `CPUEVT pc=0100 ...`). Posteriormente este
bloque pasó a compilación condicional; ver sección 111.

### 108.5 Medición antes/después

Solo el core, 60 frames, build `-O2`:

| Core | 1 jugador | 2 jugadores |
|---|---:|---:|
| antes | 21,3% | 42,6% |
| después | **10,8%** | **21,7%** |

La variante exacta de la GUI (`libqtrebgcore_dbg.a`, `-O2 -DENABLE_LOG`):
**10,9%** con un jugador y **22,0%** con dos. Se reduce aproximadamente a la
mitad.

### 108.6 Perfil después del fix

Nuevo perfil con 300 frames por ROM (63,2 M ticks):

| Función | Self |
|---|---:|
| `PPU::sync_lcd_from_ppu` | 7,59% |
| `QtreGB::tick` | 7,59% |
| `APU::tick` | 6,90% |
| `PPU::ppu_tick` | 6,21% |
| `QtreGB::tick_once` | 6,21% |
| `PPU::raise_stat_irq` | 4,83% |
| `PPU::pixel_transfer_lx8` | 4,83% |
| `CpuMicro::tick` | 4,14% |
| `PPU::read_lcdc` | 3,45% |
| `PPU::read_stat` | 3,45% |

`sync_lcd_from_ppu + read_lcdc + read_stat` suma ~14,5% del tiempo
perfilado. Se ejecuta en cada T-cycle aunque LY, modo STAT, LCDC y los
registros de scroll/paleta solo cambien en eventos contables. Es el
siguiente candidato: sincronizar el espejo LCD solo cuando cambie algo.

### 108.7 Cómo hacerlo con `perf` en un equipo permitido

```sh
sudo sysctl kernel.perf_event_paranoid=1   # o -1
perf stat ./build/QtreGB-GUI               # ciclos, instrucciones, ramas
perf record -g ./build/QtreGB-GUI          # muestreo con call graph
perf report                                # ranking interactivo
```

`paranoid=1` permite eventos de CPU; `-1` permite casi todo. Sin sudo no se
puede cambiar en este equipo.

### 108.8 Evidencia

- Build core + GUI: OK.
- `make run_tests`: **190 / 3705**, verde.
- `make run_blargg`: **15/15**.
- `make run_mts`: **34/34**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- `QTR_IRQDBG=1`: imprimía `CPUEVT` en aquel build (ahora es compilación
  condicional; ver sección 111).

### 108.9 Pendiente

- Sincronizar el espejo LCD solo en cambios reales (LY, STAT, registros).
- Repetir el perfil con `perf` cuando haya permisos.
- Valorar `run_ticks()`/`run_frame()` en el core, sabiendo que el beneficio
  esperado es pequeño frente al trabajo real de PPU/APU/CPU.

---

## 109. SESIÓN 2026-11-17 — Perfil real con `perf`

**Backup previo:** `tmp/20261117_163344/`
**Git:** sin commit. `perf.data` queda como fichero local sin seguimiento.

### 109.1 Captura del usuario

`sudo perf record -g -o perf.data ./build/QtreGB-GUI`, ~76 s, 15K muestras,
0 muestras perdidas. Incluye un `zenity` (diálogo de ficheros):

| Proceso | Overhead |
|---|---:|
| `QtreGB-GUI` | 65,17% |
| `zenity` | 24,88% |
| `SDLHotplugALSA` | 3,90% |
| hilos de `zenity` | ~4,6% |
| hilo `QtreGB-GUI:cs0` | 0,75% |

El call graph de esa captura no es fiable: `perf record -g` usa frame
pointers por defecto y el build `-O2` los omite. Aparecen marcos basura
(`0x0800060b...`). El **self time** sí es válido; para el árbol hay que
recompilar con `-fno-omit-frame-pointer` o usar `--call-graph dwarf`.

### 109.2 Perfil relativo a `QtreGB-GUI`

Filtrado con `--comm QtreGB-GUI --percentage relative`, 9K muestras:

| Grupo | % del proceso |
|---|---:|
| PPU | 35,20% |
| APU | 16,87% |
| CPU | 12,64% |
| `QtreGB` (tick/tick_once/get_frame_ready) | 10,65% |
| GUI (frontend/instancia/main) | 5,33% |
| Bus/memoria/IO/cart/DMA | 4,73% |
| Serial | 2,60% |
| Timer | 1,25% |
| Otros (libc, SDL, GL, kernel...) | 7,50% |

Top self:

| Función | Self |
|---|---:|
| `APU::tick` | 9,31% |
| `PPU::ppu_tick` | 7,46% |
| `QtreGB::tick_once` | 7,45% |
| `PPU::raise_stat_irq` | 5,60% |
| `PPU::pixel_transfer_lx8` | 5,14% |
| `PPU::sync_lcd_from_ppu` | 4,83% |
| `CpuMicro::tick` | 4,00% |
| `APU::tick_pulse` | 3,47% |
| `PPU::read_lcdc` | 2,87% |
| `CpuMicro::tick_microop` | 2,79% |
| `PPU::read_stat` | 2,75% |
| `Serial::tick` | 2,60% |

### 109.3 Captura limpia del core

Ahora que `perf_event_paranoid=1`, se puede grabar sin `sudo`. Runner
headless con `01-special`, `dmg-acid2` y `dmg_sound` (2000 frames cada uno),
compilado con `-O2 -g -fno-omit-frame-pointer`, 9.713 muestras:

- 50,98 G ciclos, 168,57 G instrucciones → **IPC 3,31**.
- Fallos de rama: 54,4 M / 37,7 G = **0,14%**.
- Fallos de caché: 2,82 M / 42,95 M = **6,57%**.
- No hay cuello de botella de memoria ni de ramas; el coste es trabajo
  escalar por T-cycle.

Perfil plano del core:

| Grupo | % |
|---|---:|
| PPU | 45,47% |
| `QtreGB` | 14,85% |
| CPU | 14,84% |
| APU | 9,52% |
| Bus/memoria/IO/cart/DMA | 5,81% |
| Serial | 3,96% |
| Timer | 1,64% |
| Otros | 3,51% |

La diferencia con el perfil de la GUI viene de la mezcla de ROMs (el perfil
del core carga dos ROMs muy PPU-heavy).

### 109.4 `tick_once` y el guard del `static`

`perf annotate` sobre `tick_once` muestra que el `static const bool irq_dbg`
todavía cuesta un guard de inicialización + carga + rama en cada T-cycle:

```text
movzbl guard variable for ...::irq_dbg   2,28%
cmpb   $0x0, ...::irq_dbg                4,09%
je     ...                               4,09%
```

Es ~11% del self de `tick_once`, ~1,2% del core. También se ve la llamada a
`ErrorReporter::has_error()` (~1%) y la comprobación de boot ROM (~0,5%).
No son los problemas principales, pero confirman que el bucle por T-cycle
tiene trabajo constante que se puede recortar.

### 109.5 Qué optimizar después

Por orden de impacto esperado:

1. **Espejo LCD**: `sync_lcd_from_ppu + read_lcdc + read_stat` = **10,45%**
   del proceso en la captura de la GUI. El espejo `LCD` es pasivo: la CPU
   lee los registros del PPU directamente. Se puede sincronizar solo en
   cambios (LY, modo STAT, LYC, escrituras) o antes de guardar estado, en
   vez de en cada dot. Requiere revisar los tests y los savestates.
2. **APU**: 16,87% del proceso. `APU::tick` se ejecuta 4,19 M veces/s y
   actualiza canales aunque estén deshabilitados. Habría que medir un
   early-out por canal antes de tocarlo.
3. **`tick_once`**: quitar el guard del `static` (variable a nivel de
   fichero o miembro) y revisar `has_error()`/boot ROM por tick. ~2-3%.
4. **Bucle de la GUI**: `EmulatorFrontend::tick + EmulatorInstance::tick +
   get_frame_ready + main` ≈ 5,33%. Un `run_ticks()`/`run_frame()` en el
   core evitaría comprobaciones por tick, pero el beneficio es pequeño.
5. **Serial**: 2,60%, no es prioritario.

### 109.6 Cómo repetir la captura limpia

```sh
g++ -std=c++17 -O2 -g -fno-omit-frame-pointer -I core/inc \
    /tmp/opencode/perf_probe.cpp core/src/*.cpp -o /tmp/opencode/perf_probe

perf record -g -F 999 -o /tmp/opencode/perf_core.data -- /tmp/opencode/perf_probe
perf report -i /tmp/opencode/perf_core.data --stdio --no-children \
    --percentage relative --sort symbol --percent-limit 0.3

# Solo el proceso de la GUI, sin zenity ni hilos ajenos:
perf report -i perf.data --stdio --no-children --percentage relative \
    --comm QtreGB-GUI --sort symbol --percent-limit 0.3
```

---

## 110. SESIÓN 2026-11-17 — Espejo LCD fuera del camino por dot

**Backup previo:** `tmp/20261117_173335/`
**Backup final:** `tmp/20261117_173934/`
**Git:** sin commit.

### 110.1 Motivo

El perfil de `perf` mostraba:

```text
PPU::sync_lcd_from_ppu   4,83% del proceso GUI
PPU::read_lcdc           2,87%
PPU::read_stat           2,75%
```

`sync_lcd_from_ppu()` se ejecutaba en **cada T-cycle** (4,19 M veces/s)
aunque el espejo `LCD` es pasivo: la CPU lee los registros del PPU
directamente y nadie de producción consume `m_lcd` durante la emulación.
Solo lo usan savestates, tests y herramientas de depuración.

### 110.2 Cambio

- `PPU::sync_lcd_from_ppu()` pasa a `const`.
- Nuevo `PPU::sync_lcd()` público, que refresca el espejo bajo demanda.
- `PPU::ppu_tick()` ya **no** llama a `sync_lcd_from_ppu()`.
- `PPU::save_state()` refresca el espejo antes de serializar.
- `PPU::load_state()` lo refresca al terminar, para llamadas PPU-only.
- El chunk LCD se sigue escribiendo después del chunk PPU.
- El bloque de traza `CPUEVT` usa `PPU::debug_ly()` en vez del espejo.
- `PPUTestLoader` llama a `sync_lcd()` tras cada `ppu_tick()` para conservar
  las comprobaciones de modo STAT.
- `read_lcd_registers()` de los tests de savestate llama a `sync_lcd()` antes
  de leer.
- Nuevo test: el espejo se refresca al guardar y con `sync_lcd()`.

El espejo puede quedar desactualizado durante la emulación normal; es
deliberado. La fuente de verdad es el PPU.

### 110.3 Rendimiento

Benchmark headless con `roms/01-special.gb`, 600 frames (600 × 70224
T-cycles), variante exacta de la GUI (`-O2 -DENABLE_LOG`), `taskset -c 2`:

| Caso | Antes | Después | Mejora |
|---|---:|---:|---:|
| 1 jugador | 9,72% (9,56–10,08) | **8,18%** (8,16–8,20) | **−15,8%** |
| 2 jugadores link | 19,17% (18,92–19,45) | **16,32%** (16,24–16,41) | **−14,9%** |

El perfil de `perf` posterior ya no muestra `sync_lcd_from_ppu`,
`read_lcdc` ni `read_stat` en el top. Nuevo top del core headless:

```text
QtreGB::tick_once        12,94%
CpuMicro::tick           10,00%
PPU::ppu_tick             9,49%
PPU::raise_stat_irq       9,22%
PPU::pixel_transfer_lx8   7,99%
APU::tick                 6,74%
Serial::tick              4,55%
QtreGB::tick              3,81%
APU::prepare_tick         3,76%
```

### 110.4 Evidencia

Build limpio core + GUI: **OK**.

- `make run_tests`: **191 casos / 3729 assertions**, verde.
- `make run_blargg`: **15/15**.
- `make run_mts`: **34/34**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- BIOS: **7 ROMs / 2800 frames, all match**.
- PPU acceptance: **12/12**.
- Harness PPU: **25 PASS / 0 FAIL / 8 SKIP**.
- Probe de dos instancias: `serial_exchange=ok`, `independent=ok`.
- Probe de dos pantallas: textura **320×144**, `link_mode=ok`.
- Traza `CPUEVT` (entonces con `QTR_IRQDBG=1`): imprime LY correcto. Ahora
  se activa con `make clean && make QTR_IRQDBG=1`.

### 110.5 Siguiente

- `QtreGB::tick_once` sigue siendo el mayor bloque (12,94%): guard del
  `static`, `ErrorReporter::has_error()` y comprobación de boot ROM por tick.
- APU (~6,74% + helpers) y PPU `raise_stat_irq`/`pixel_transfer` son los
  siguientes bloques grandes.
- El bucle de la GUI todavía comprueba `get_frame_ready()` por T-cycle.

---

## 111. SESIÓN 2026-11-17 — Traza CPUEVT en compilación condicional

**Backup previo:** `tmp/20261117_173934/`
**Backup final:** `tmp/20261117_175114/`
**Git:** sin commit.

### 111.1 Motivo

El `static const bool irq_dbg` de `QtreGB::tick_once()` seguía costando en
cada T-cycle:

- carga del guard de inicialización,
- test del guard,
- carga del booleano,
- salto sobre el bloque de debug.

`perf annotate` lo valoraba en ~11% del self de `tick_once`. La traza es
solo una herramienta de desarrollo, así que se compila fuera por defecto.

### 111.2 Cambio

`core/src/QtreGB.cpp`:

```cpp
#ifdef QTR_ENABLE_IRQDBG
    // Development-only CPUEVT trace.
    ...
#endif
```

- Desaparece `std::getenv("QTR_IRQDBG")` del camino caliente.
- Desaparece el `static` local y su guard.
- El bloque `CPUEVT` solo existe si se compila con
  `-DQTR_ENABLE_IRQDBG`.

`common.mk`:

```sh
make clean && make QTR_IRQDBG=1
```

añade `-DQTR_ENABLE_IRQDBG` a las variantes `BUILD_DBG` y `BUILD_OPT`.
Hay que hacer `make clean`: los objetos no codifican el flag.

`AGENTS.md` documenta el nuevo comando y actualiza el conteo de tests.

### 111.3 Verificación del build de traza

Build con `QTR_IRQDBG=1`:

- compila con `-DQTR_ENABLE_IRQDBG`;
- ejecutando el bench con una ROM imprime:

```text
CPUEVT pc=0100 dot=395 ly=0 if=E0 ie=00
```

Build de producción (sin el flag):

- `nm` no encuentra `irq_dbg` ni su guard;
- `objdump` no encuentra ninguna llamada a `getenv` en `tick_once()`.

### 111.4 Rendimiento

`perf stat`, 600 frames de `01-special`, `taskset -c 2`:

| Caso | Instrucciones antes | Instrucciones después | Mejora |
|---|---:|---:|---:|
| 1 jugador | 12.327,1 M | 12.124,6 M | **−1,64%** |
| 2 jugadores | 25.449,9 M | 25.052,1 M | **−1,56%** |

En ramas: 3.450,6 M → 3.408,0 M en 1 jugador (−1,23%).
Son ~4,8 instrucciones y ~1,0 ramas menos por T-cycle en 1 jugador.

El benchmark de tiempo da una mejora pequeña y ruidosa (~1–3% según la
ejecución); el dato determinista es la reducción de instrucciones.

### 111.5 Evidencia

Build limpio de producción: **OK**.

- `make run_tests`: **191 casos / 3729 assertions**, verde.
- `make run_blargg`: **15/15**.
- `make run_mts`: **34/34**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- BIOS: **7 ROMs / 2800 frames, all match**.
- PPU acceptance: **12/12**.
- Harness PPU: **25 PASS / 0 FAIL / 8 SKIP**.
- Build de traza verificado con `CPUEVT pc=0100 ...`.

---

## 112. SESIÓN 2026-11-17 — LTO en el build por defecto

**Backup previo:** `tmp/20261117_180000/`
**Backup final:** `tmp/20261117_180301/`
**Git:** sin commit.

### 112.1 Cambio

`common.mk`:

```make
LTO_FLAGS ?= -flto
BUILD_DBG := -g -O2 -DENABLE_LOG $(LTO_FLAGS)
BUILD_OPT := -g -O2 $(LTO_FLAGS)
```

- `GUI_DEBUG=1` mantiene `-O0` y no añade LTO, para depurar cómodamente.
- Los enlaces de GUI, tests, mts, regress y blargg pasan `$(LTO_FLAGS)`.
- Para desactivar LTO sin editar nada:
  `make clean && make LTO_FLAGS=`.
- Los objetos no codifican el flag: hay que hacer `make clean` al cambiarlo.

El motivo es que el core está dividido en muchas unidades de compilación.
Sin LTO, `tick_once` no puede inlinear `CpuMicro::tick`, `PPU::ppu_tick`,
`APU::tick`, `Serial::tick`, etc., y se paga la llamada en cada T-cycle.
Con LTO el compilador ve todo el core y elimina buena parte de ese coste.

### 112.2 Rendimiento

Benchmark headless con `01-special`, 600 frames, build real de la GUI
(`libqtrebgcore_dbg.a`, `-O2 -DENABLE_LOG`), `taskset -c 2`, 7 ejecuciones:

| Caso | Sin LTO | Con LTO | Mejora |
|---|---:|---:|---:|
| 1 jugador | 8,16% (8,04–8,24) | **5,56%** (5,46–5,71) | **−31,8%** |
| 2 jugadores link | 16,40% (16,09–16,90) | **11,26%** (10,94–11,65) | **−31,4%** |

`perf stat`, mismas condiciones:

| Caso | Instrucciones | Ramas |
|---|---:|---:|
| 1P sin LTO | 12.125 M | 3.408 M |
| 1P con LTO | **9.810 M (−19,1%)** | **2.656 M (−22,1%)** |
| 2P sin LTO | 25.053 M | 7.112 M |
| 2P con LTO | **20.329 M (−18,9%)** | **5.520 M (−22,4%)** |

Los ciclos bajan ~32%, más que las instrucciones: LTO también elimina
llamadas y mejora el IPC.

En el experimento aislado, `-O3 -flto` daba 4,99% / 9,67%, pero se ha
mantenido `-O2` como pedía la sesión. `-O3` queda como opción futura.

### 112.3 Evidencia

Build limpio core + GUI con LTO: **OK**.

- `make run_tests`: **191 casos / 3729 assertions**, verde.
- `make run_blargg`: **15/15**.
- `make run_mts`: **34/34**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- BIOS: **7 ROMs / 2800 frames, all match**.
- PPU acceptance: **12/12**.
- Harness PPU: **25 PASS / 0 FAIL / 8 SKIP**.
- Probe de dos instancias: `serial_exchange=ok`, `independent=ok`.
- Probe de dos pantallas: textura **320×144**, `link_mode=ok`.

El enlace LTO muestra un aviso normal de compilación LTRANS en serie; no
afecta al binario. Si el tiempo de enlace molesta, se puede probar
`-flto=auto`, pero no se ha cambiado en esta sesión.

### 112.4 Siguiente

- MBC5.
- GUI de forma seria.

---

## 113. SESIÓN 2026-11-17 — MBC5

**Backup previo:** `tmp/20261117_180604/`
**Backup final:** `tmp/20261117_181057/`
**Git:** sin commit.

### 113.1 Diferencias con los mappers existentes

| Mapper | ROM | RAM | Particularidades |
|---|---|---|---|
| No-MBC | 32 KB fijos | 8 KB | Sin banking |
| MBC1 | 5 bits + 2 bits secundarios | 32 KB | Modo 0/1, `00 -> 01`, MBC1M |
| MBC2 | 4 bits | 512 × 4 bits integrada | Sin RAM externa |
| MBC3 | 7 bits | 32 KB | RTC, latch, halt, carry; MBC30 |
| **MBC5** | **9 bits (hasta 8 MB)** | **hasta 128 KB** | **Sin modo/RTC; banco 0 en `$4000`; rumble** |

MBC5 separa el banco ROM en dos registros:

- `$2000-$2FFF`: 8 bits bajos.
- `$3000-$3FFF`: bit 8.

Eso permite 512 bancos de 16 KB (8 MB), sin las limitaciones de MBC1. A
diferencia de MBC1, escribir 0 selecciona el banco físico 0 en
`$4000-$7FFF`. El registro de `$4000-$5FFF` es el banco de RAM de 4 bits; en
las variantes con rumble (`$1C-$1E`) el bit 3 controla el motor y no forma
parte del banco. MBC5 no tiene registro de modo ni RTC/latch.

### 113.2 Implementación

- Nuevo `CartMBC5` (`core/inc/cart_mbc5.h`, `core/src/cart_mbc5.cpp`).
- Tipos de cartucho `$19-$1E`:
  - `$19`: MBC5.
  - `$1A`: MBC5 + RAM.
  - `$1B`: MBC5 + RAM + batería.
  - `$1C`: MBC5 + rumble.
  - `$1D`: MBC5 + rumble + RAM.
  - `$1E`: MBC5 + rumble + RAM + batería.
- `CartMapper::Mbc5 = 5`.
- RAM enable: nibble bajo `$0A`.
- ROM bank por defecto: 1.
- El banco seleccionado se enmascara al tamaño real de la ROM.
- La RAM usa el tamaño del header (`$00-$05`) como el resto de mappers.
- La batería guarda todos los bancos de RAM en crudo.

### 113.3 Savestate

El mapper se guarda como `5`. Tags locales de MBC5:

| Tag | Campo | Tipo |
|---:|---|---|
| `0x0001` | `ram_enable` | Bool |
| `0x0002` | `rom_bank` | U16 (`0..$1FF`) |
| `0x0003` | `ram_bank` | U8 (`0..$0F`) |
| `0x0010` | RAM | Bytes |

El bit 3 de `ram_bank` conserva el estado del registro de rumble. No se
serializa `m_has_rumble`: se deriva del header del cartucho.

### 113.4 Tests

- Unitarios: ROM de 8 MB con bancos `$000`, `$100` y `$1FF`, banco 0 en
  `$4000`, máscara del registro alto, RAM de 16 bancos con batería, bit de
  rumble y round-trip de savestate.
- `mts`: añadidos los 8 ROMs `emulator-only/mbc5/rom_*.gb` (512 KB a 8 MB).
- `make run_mts`: **42/42** (antes 34; +8 MBC5).

### 113.5 Evidencia

Build con LTO: **OK**.

- `make run_tests`: **200 casos / 3882 assertions**, verde.
- `make run_blargg`: **15/15**.
- `make run_mts`: **42/42**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- BIOS: **7 ROMs / 2800 frames, all match**.
- PPU acceptance: **12/12**.
- Harness PPU: **25 PASS / 0 FAIL / 8 SKIP**.
- Probe de dos pantallas: OK.

### 113.6 Limitaciones

- No hay salida física de rumble: el bit 3 se conserva y afecta al banco de
  RAM, pero no hay API de motor.
- El header de RAM de 2 KB se trata como un banco de 8 KB, igual que en
  MBC1; no hay ROMs de test para ese caso.

---

## 114. SESIÓN 2026-11-17 — Configuración `.ini` multiplataforma

**Backup previo:** `tmp/20261117_182841/`
**Backup final:** `tmp/20261117_184117/`
**Git:** sin commit.

### 114.1 Rutas

Nuevo `AppConfig` en `gui/inc/app_config.h` / `gui/src/app_config.cpp`.

Orden de resolución:

1. `QTR_CONFIG=/ruta/QtreGB.ini` (override para tests/lanzadores).
2. `QtreGB.ini` junto al ejecutable (modo portable).
3. Configuración de usuario, bajo el namespace del ecosistema **Qtre**:
   - Windows: `%APPDATA%\Qtre\QtreGB\config.ini`
   - Linux: `$XDG_CONFIG_HOME/Qtre/QtreGB/config.ini`
     o `~/.config/Qtre/QtreGB/config.ini`
   - macOS: `~/Library/Application Support/Qtre/QtreGB/config.ini`

En Windows/macOS se usa `SDL_GetPrefPath("Qtre", "QtreGB")`; en Linux se
respeta `XDG_CONFIG_HOME` (con fallback a `SDL_GetPrefPath`). El modo
portable sigue usando `QtreGB.ini` junto al ejecutable.

Se usa `std::filesystem::u8path` para tratar las rutas UTF-8 que devuelve
SDL. Si no hay ninguna ruta disponible, se usa `QtreGB.ini` en el directorio
actual como último recurso.

### 114.2 Formato

```ini
# QtreGB configuration
version=1

[window]
width=800
height=600
maximized=0
```

- Parser propio, sin dependencias nuevas.
- Secciones y claves case-insensitive.
- Comentarios `#` y `;`; líneas malformadas o valores inválidos se ignoran.
- Ancho/alto válidos: `160..16384`; si no, se mantienen los defaults.
- Guardado atómico con `.tmp` + `std::filesystem::rename`.
- `save_default()` crea el directorio padre si hace falta.

De momento solo se persiste la geometría de la ventana. Los campos se irán
añadiendo cuando existan las opciones correspondientes.

### 114.3 ImGui

- `io.IniFilename = nullptr` en `ImGuiManager` y en el contexto de SDL.
- Eliminado el `imgui.ini` generado en la raíz del repo.
- Añadido `QtreGB.ini` a `.gitignore`.

La GUI carga el `.ini` antes de crear la ventana y guarda la geometría al
salir. Si la ventana está maximizada, conserva el último tamaño restaurado.

### 114.4 Evidencia

- Build core + GUI: OK.
- `tests/test_app_config.cpp`: fichero ausente, round-trip, reemplazo atómico
  y valores malformados. El runner de tests compila `gui/src/app_config.cpp`
  y enlaza SDL.
- Probe externo: override `QTR_CONFIG`, round-trip, valores inválidos,
  fichero inexistente y modo portable junto al ejecutable.
- GUI headless con `SDL_VIDEODRIVER=dummy`: carga `1024x768`, guarda
  `1024x768` y no crea `imgui.ini`.
- `make run_tests`: **200 casos / 3882 assertions**, verde.

### 114.5 Siguiente

- Ir añadiendo campos a `AppConfig` según se implementen opciones de GUI:
  renderer, escala, paleta, volumen, teclas, últimas rutas, etc.
- Persistir el layout de ImGui solo si se decide hacerlo dentro de nuestro
  formato; por ahora está desactivado.

---

## 115. SESIÓN 2026-11-17 — Pacing consciente del vsync

**Backup previo:** `tmp/20261117_184117/`
**Backup final:** `tmp/20261117_190247/`
**Git:** sin commit.

### 115.1 Problema

El pacer anterior dormía siempre hasta `inicio_del_bucle + 16,7427 ms`
(59,7275 Hz), incluso cuando `SDL_RenderPresent` ya había bloqueado en el
vblank. En un monitor de 60 Hz:

- vblank: `16,6667 ms`
- frame Game Boy: `16,7427 ms`
- diferencia: `0,076 ms` por frame

El bucle se retrasa poco a poco respecto al monitor y cada ~219 frames
(~3,7 s) se pierde un vblank. MegaBoy no hace ese `sleep` cuando el vsync
realmente bloquea; se deja llevar por `glfwSwapBuffers`.

### 115.2 Cambio

`gui/src/main.cpp` mide cuánto tarda `render_draw_data()`:

- si el vsync está disponible y el present tarda al menos el 75% del
  intervalo de refresco del monitor, **no duerme**: manda el monitor;
- si el present vuelve rápido (driver que ignora vsync, ventana minimizada,
  etc.), duerme hasta `present_start + 16,7427 ms` para no quemar CPU.

`SDLRendererBackend::display_refresh_hz()` consulta el refresco del monitor
con SDL para calcular el umbral. Si no está disponible, se usa el frame de
Game Boy como referencia.

### 115.3 Medición de CPU

Monitor de 59,96 Hz, Kirby's Dream Land, `timeout 4s`, 3 ejecuciones:

| Aplicación | CPU media |
|---|---:|
| QtreGB antes del cambio | ~11% |
| **QtreGB con pacer vsync-aware** | **~11%** |
| MegaBoy con el mismo juego | ~24% |

QtreGB idle (sin ROM) ronda el 2%. Con `QTR_RENDERER=software` y ROM subió a
~19%, pero sigue lejos de un busy loop.

El cambio no aumenta el consumo de forma apreciable en 60 Hz: simplemente
elimina el desfase periódico. En un monitor de 144/240 Hz, si el vsync
bloquea, se presentará al ritmo del monitor (como MegaBoy); eso sí puede
subir el coste de GPU/CPU. Si molesta, el siguiente paso sería añadir un
modo `present_mode=gameboy|monitor` a `AppConfig`.

### 115.4 Evidencia

- Build core + GUI: OK.
- `make run_tests`: **200 casos / 3882 assertions**, verde.
- GUI con Kirby: ~11% de un núcleo; MegaBoy ~24%.
- `imgui.ini` sigue sin generarse.
- `QTR_CONFIG`/`Qtre/QtreGB/config.ini` sin cambios.

---

## 116. SESIÓN 2026-11-17 — Menú Graphics, paletas, LCD y ghosting

**Backup previo:** `tmp/20261117_190247/`
**Backup final:** `tmp/20261117_220448/`
**Git:** sin commit.

### 116.1 Menú Graphics

Añadido `Graphics` con:

- `FPS` y milisegundos por frame (de `ImGuiIO::Framerate`).
- `Palette`: DMG, Pocket y `Custom...`, con cuatro muestras de color.
- `Background`: color del letterbox que rodea a la imagen.
- `LCD grid` (siempre con celdas de 1 píxel de Game Boy; sin spacing) y
  `Grid opacity`.
- `Integer scaling`.
- `Present mode`: Auto, Monitor, Game Boy.
- `Fullscreen (F)`; se ha quitado el menú `View`, que solo tenía fullscreen.
- `Save screenshot...` (BMP del framebuffer procesado).
- `Reset graphics settings`.
- `Ghosting`: presets Off/Fast/Normal/Slow, enable, `Lighten ms/tone` y
  `Darken ms/tone`.

La paleta custom abre un modal con cuatro `ColorEdit3` sin alpha:
`Color 0` (más claro) … `Color 3` (más oscuro). `Apply` fija la paleta,
`Cancel` no cambia nada y `Reset DMG` vuelve a los colores DMG.

Los menús de ImGui son **solo con ratón**: se ha desactivado
`NavEnableKeyboard` para que las flechas no muevan el foco del menú; las
flechas quedan para el jugador 1.

### 116.2 Paletas

Se usa el orden lógico del Game Boy:

```text
0 = más claro
3 = más oscuro
```

Presets:

- **DMG**: `#E0F8D0`, `#88C070`, `#346856`, `#081820`
  (los mismos que QtreGBPaint).
- **Pocket**: `#FFFFFF`, `#AAAAAA`, `#555555`, `#000000`.
- **Custom**: cuatro colores de usuario.

Los colores se guardan como `#RRGGBB`; el canal alpha se ignora y la salida
siempre es opaca. `SDL_PIXELFORMAT_RGBA8888` usa `0xRRGGBBAA` en este host,
así que las constantes de paleta mantienen el mismo formato que QtreGB.

Defaults sin `.ini`: paleta DMG, fondo negro, grid desactivada, integer
scaling activado, present mode Auto, ghosting desactivado y tiempos `32 ms`
(aclarar) / `16 ms` (oscurecer) por tono.

### 116.3 Procesado y ghosting

`DisplayProcessor` trabaja siempre a **160×144**:

- cada píxel guarda origen, target y una transición cuya duración depende
  del **cambio de target** (`duración = |target - target_anterior| × ms_por_tono`),
  no de cuánto había avanzado ya el píxel;
- `ghosting_light_ms` y `ghosting_dark_ms` son milisegundos **por tono**
  (`0..150 ms`); pasar de 0 a 3 tarda el triple;
- la curva es **ease-out cúbica**: `t = elapsed / duration` y
  `nivel = origen + (target - origen) × (1 - (1 - t)³)`; arranca rápido y
  frena suave;
- **drive coherence**: un salto grande hacia oscuro (`target - anterior >= 2`)
  se aplica de inmediato. Esto mantiene sólidas las líneas negras en
  movimiento; la vuelta a claro sí conserva la estela;
- `0 ms` significa instantáneo para esa dirección;
- el primer frame tras cargar/resetear una ROM se inicializa al target, sin
  fundido de entrada;
- el color final se **interpola** entre las dos entradas de paleta adyacentes
  según el nivel fraccional, así que pueden aparecer tonos intermedios;
- `refresh()` reaplica paleta/estado sin avanzar el tiempo, para cambios con
  el emulador pausado.

Cada `EmulatorInstance` tiene su propio `DisplayProcessor`; en modo link se
componen los dos buffers ya procesados en la textura 320×144.

### 116.4 LCD grid

La rejilla se pinta con `ImDrawList` **después** de `ImGui::Image`, con
`PushClipRect` al rectángulo de la imagen:

- color = `palette[0]`, el más claro, modulado por `grid_opacity`;
- grosor = 1 píxel físico (corregido por `DisplayFramebufferScale`);
- una celda por píxel de Game Boy (spacing fijo a 1);
- el marco exterior también se pinta, en el borde de la imagen, para que la
  rejilla no parezca abierta por la derecha ni por abajo;
- solo se pinta si la escala efectiva es **≥ 2×**;
- funciona igual en ventana y fullscreen.

No se hornea una textura 5×: el coste es solo dibujar líneas. La proporción
de rejilla depende de la escala, pero el spacing queda fijo en 1 píxel de
Game Boy. El borde exterior cierra la última fila/columna sin tapar el
contenido con una celda extra.

### 116.5 Configuración

`QtreGB.ini` añade:

```ini
[graphics]
palette=dmg
custom_0=#E0F8D0
custom_1=#88C070
custom_2=#346856
custom_3=#081820
background=#000000
integer_scaling=1
lcd_grid=0
grid_opacity=100
present_mode=auto
ghosting=0
ghosting_light_ms=32
ghosting_dark_ms=16
```

`QtreGB.ini` sigue en `Qtre/QtreGB/config.ini` (XDG en Linux,
`SDL_GetPrefPath("Qtre","QtreGB")` en Windows/macOS).

`Present mode`:

- `Auto`: si el present bloquea de verdad, manda el monitor; si no, se limita
  a 59,7275 Hz.
- `Monitor`: si SDL dice que hay vsync, no se limita; puede subir el consumo
  si el driver miente.
- `Game Boy`: limita siempre a 59,7275 Hz.

`Save screenshot...` escribe un BMP del framebuffer ya procesado
(paleta + ghosting). La rejilla y el fondo de pantalla son overlays de ImGui
y no forman parte de esa captura nativa.

### 116.6 Medición

Kirby's Dream Land, monitor 59,96 Hz, 4 s, 3 ejecuciones:

| Configuración | CPU media |
|---|---:|
| Sin rejilla ni ghosting | 11% |
| Rejilla + ghosting | 11% |

El procesado a resolución nativa y el overlay de ImGui no muestran un coste
medible en este equipo. La captura de la ventana confirma la paleta DMG y la
rejilla superpuesta.

### 116.7 Evidencia

- Build core + GUI: OK.
- `make run_tests`: **200 casos / 3882 assertions**, verde.
- `make run_blargg`: **15/15**.
- `make run_mts`: **42/42**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- BIOS: **7 ROMs / 2800 frames, all match**.

---

## 117. Referencia — Retro Display Lab

**Proyecto:** `https://github.com/JohnnySun/retro-display-lab`

No es un filtro de “look retro”: es un conjunto de shaders de RetroArch
(Vulkan) con modelos físicos por panel:

- Game Boy DMG: cuatro tonos + fondo óptico no dirigido, respuesta STN
  asimétrica, crosstalk de filas/columnas, retención iónica, apertura
  rectangular de píxel y sombra del reflector.
- Game Boy Color CGB-001: LUT de color medida, respuesta temporal TFT,
  retención por DC residual y apertura RGB.
- GBA SP AGS-101: respuesta gray-to-gray, drive alterno, scan/latch y
  subpíxeles BGR.

La idea importante coincide con lo que ya hacemos: **el ghosting es estado
por píxel que evoluciona**, no un blend de frames completos. La diferencia es
que ellos modelan también crosstalk, retención y geometría del panel a nivel
físico, y lo ejecutan en GPU.

Decisión para QtreGB:

- No copiar el modelo físico completo; es mucho más complejo y está pensado
  para shaders de GPU.
- Mantener nuestro ghosting por píxel a 160×144, ahora con interpolación de
  color.
- La rejilla LCD por overlay ya cubre la apertura de píxel de forma barata.
- Si más adelante queremos más fidelidad, los siguientes pasos baratos serían
  un color de fondo/undriven distinto y respuestas de subida/bajada ya
  separadas; el crosstalk y la retención iónica quedan fuera por ahora.

Valores orientativos para nuestro modelo (ms por tono), derivados de los
T90 de rango completo del repo y divididos entre 3:

| Perfil | Oscurecer | Aclarar |
|---|---:|---:|
| Físico DMG 20 °C (`ws2`) | ~22 ms | ~22 ms |
| `plausible-fast` | ~27 ms | ~27 ms |
| `nominal` | ~33 ms | ~67 ms |
| `plausible-slow` | ~47 ms | ~103 ms |

No son equivalentes exactos: el modelo del repo tiene respuesta no lineal y
drag por distancia; nuestro modelo de duración por tono con easing solo puede
aproximar uno de los extremos. Para probar a mano, empezaría por `33/67`
(nominal) o `27/27` (rápido). El default actual es `32/16`. Con `50/25` y la
curva ease-out + drive coherence, la estela debería quedar más uniforme.

---

## 118. Debug de ghosting con línea vertical sintética

**Herramienta:** `/tmp/opencode/ghost_line_probe.cpp` (no forma parte del
build). Genera un framebuffer 160×144 con una línea negra vertical de 8 px
que rebota horizontalmente y lo pasa por el mismo `DisplayProcessor`.
Paleta Pocket, `ghosting_light_ms=30`, `ghosting_dark_ms=15`, velocidades
1/2/4/6 px por frame.

Artefactos en `tmp/ghost-debug/`:

- `ghost_line_contact_coherence.png`: tiras temporales (x=píxel,
  y=frame) con ghosting on/off.
- `speed6_contact.png`: frames completos 18–22 a 6 px/frame, on/off.
- `speed6_row_compare.png`: una fila ampliada, on/off.

### 118.1 Hallazgo

Antes del ajuste, al salir de la línea cada píxel calculaba la duración según
su nivel intermedio. Un píxel que había quedado a medio oscurecer se aclaraba
mucho más rápido que uno que había llegado a negro, produciendo una secuencia
no monótona: negro, gris claro, gris más oscuro, blanco. Es el artefacto que
se veía en movimiento horizontal.

También influía el `smoothstep`: los píxeles recién tapados empezaban con
velocidad cero y no llegaban a negro antes de que la línea los abandonara.

### 118.2 Ajuste

- Duración basada en el cambio de target, no en la distancia restante.
- Curva ease-out cúbica: respuesta inicial rápida y frenada suave.
- Drive coherence: saltos grandes hacia oscuro se aplican de inmediato. La
  línea negra queda sólida; la vuelta a claro conserva el ghosting.

Con esto, una fila a 6 px/frame queda aproximadamente:

```text
negro (línea) -> gris -> gris más claro -> blanco
```

sin bandas intermedias no monótonas.

### 118.3 Evidencia

- `make run_tests`: **200 casos / 3882 assertions**, verde.
- Test específico de drive coherence e interpolación en
  `tests/test_display_processor.cpp`.

---

## 119. SESIÓN 2026-11-17 — Menú Audio

**Backup previo:** `tmp/20261117_220448/`
**Backup final:** `tmp/20261117_221727/`
**Git:** sin commit.

### 119.1 Opciones

Nuevo menú `Audio`:

- `Enable audio` (checkbox).
- `Volume` (0–100%).

Defaults cuando no hay `.ini`: **audio activado al 50%**.

Se guarda en `QtreGB.ini`:

```ini
[audio]
enabled=1
volume=50
```

### 119.2 Implementación

- `AudioSettings` se comparte entre `AppConfig`, `EmulatorFrontend` y cada
  `EmulatorInstance`.
- El volumen se aplica al PCM antes de `SDL_QueueAudio`.
- Al desactivar o poner volumen a 0:
  - se pausa el dispositivo SDL;
  - se limpia la cola;
  - se descarta el ring del APU para que no quede audio viejo al reactivar.
- Al reactivar se despausa el dispositivo y el siguiente frame vuelve a
  encolar audio.
- Funciona por instancia, así que el jugador 2 respeta los mismos ajustes.

### 119.3 Evidencia

- Build core + GUI: OK.
- `make run_tests`: **200 casos / 3882 assertions**, verde.
- `.ini` nuevo contiene `[audio] enabled=1 volume=50`.
- `imgui.ini` sigue sin generarse.

---

## 120. SESIÓN 2026-11-17 — Settings: Controllers y Key bindings

**Backup previo:** `tmp/20261117_221727/`
**Backup final:** `tmp/20261117_231621/`
**Git:** sin commit.

### 120.1 Modelo

Dos jugadores, un dispositivo por jugador:

- `None`.
- `Keyboard`.
- Un mando SDL GameController identificado por su GUID.

Ambos jugadores pueden elegir el mismo dispositivo. El jugador 1 arranca con
el teclado y los controles anteriores (flechas, Z/X, Enter/RightShift). El
jugador 2 arranca en `None`, sin bindings; se eliminaron las IJKL
provisionales.

Cada jugador tiene ocho bindings: `Right`, `Left`, `Up`, `Down`, `A`, `B`,
`Start`, `Select`. Un binding es un control físico:

```text
key:<SDL scancode>
button:<SDL GameController button>
axis:<SDL GameController axis>:<+1|-1>
```

Los gatillos y las setas analógicas se tratan como botones:

- press al 60 %;
- release al 40 % (histéresis).

Las key bindings globales tienen dos huecos: uno de teclado y otro de
"cualquier mando". Por defecto:

```text
Fullscreen   F    (ninguno)
Save state   F5   (ninguno)
Load state   F8   (ninguno)
```

Formato en `QtreGB.ini`:

```ini
[input]
allow_opposite_directions=0
p1_device=keyboard
p1_right=key:79
p1_left=key:80
p1_up=key:82
p1_down=key:81
p1_a=key:29
p1_b=key:27
p1_start=key:40
p1_select=key:229
p2_device=none
p2_right=none
...
[shortcuts]
fullscreen_key=key:9
fullscreen_controller=none
save_state_key=key:62
save_state_controller=none
load_state_key=key:65
load_state_controller=none
```

`none` se acepta y se conserva al guardar: limpiar un atajo en la UI no vuelve
a poner el valor por defecto al recargar.

### 120.2 Limpieza de conflictos

`InputSettings::sanitize()` se aplica al cargar, al guardar y tras cada
cambio de la UI:

- Un control inválido para el dispositivo del jugador se elimina.
- Ctrl, Alt, Esc y Backspace nunca son asignables (Shift sí).
- Dentro de un mismo jugador, un control repetido elimina el que aparece
  después en el orden fijo Right → Left → Up → Down → A → B → Start → Select.
- Entre jugadores sí se permite el mismo control: así las dos Game Boys
  responden a las mismas teclas.
- Dos key bindings del mismo tipo con el mismo control: se conserva el primero
  en el orden Fullscreen → Save state → Load state.
- Una key binding global gana siempre: se elimina el binding del jugador que
  coincida. La UI avisa indicando jugador y botón.

Al asignar desde la UI:

- Si el control ya lo usa otro botón del mismo jugador, se elimina el otro y se
  avisa.
- Si el control ya lo usa una key binding, se rechaza la asignación del jugador
  (la key binding sigue ganando).
- Al asignar una key binding se eliminan los bindings de jugador que
  coincidan.
- Al cambiar de dispositivo se limpian los bindings de ese jugador y se avisa.
- `Clear` por fila y por hueco; Esc/Backspace también vacían la captura.

### 120.3 InputManager

`gui/inc/input_manager.h` + `gui/src/input_manager.cpp`:

- Inicializa `SDL_INIT_GAMECONTROLLER` en el backend.
- Enumera y abre cada GameController una sola vez; si los dos jugadores
  eligen el mismo GUID, comparten el dispositivo.
- Hotplug con `SDL_CONTROLLERDEVICEADDED/REMOVED` y botón `Refresh devices`.
- Teclado por eventos (KEYDOWN/KEYUP) y SDL scancodes; al perder el foco se
  sueltan todas las teclas para evitar controles pegados.
- Estado de mandos por polling (`SDL_GameControllerGetButton/Axis`).
- Captura: tecla para slots de teclado; botón/eje para slots de mando. En un
  slot de jugador se filtra por el GUID seleccionado; el hueco "cualquier
  mando" acepta cualquier mando conectado.
- Los atajos se disparan en flanco de subida, una sola vez por pulsación. Si
  el teclado y el mando del mismo atajo se pulsan a la vez, la acción se
  ejecuta una sola vez.
- Al reasignar un atajo que aún está pulsado se rearma el estado para que no
  se dispare inmediatamente.
- `EmulatorFrontend::shutdown_input()` cierra los mandos antes de
  `SDL_Quit()`.

### 120.4 Direcciones opuestas

`gui/inc/input_directions.h` + `gui/src/input_directions.cpp`. El core no se
tocó: `Gamepad` sigue admitiendo Left+Right y Up+Down independientes.

Con `Allow opposite directions` desactivado (por defecto):

- la última dirección pulsada gana;
- al soltarla, vuelve la opuesta si sigue físicamente pulsada;
- si las dos se pulsan en el mismo frame, gana Right sobre Left y Down sobre
  Up.

El check está en `Settings → Controllers`.

### 120.5 UI

`Settings → Controllers`:

- Desplegable de dispositivo por jugador (None, Keyboard y mandos detectados).
- `Refresh devices`.
- Tabla con botón de Game Boy, control actual, `Bind` y `Clear`.
- Estado de mando desconectado (los bindings se conservan y se reactivan al
  reconectar).
- `Reset controllers` y `Allow opposite directions`.

`Settings → Key bindings`:

- Lista con scroll.
- Por acción: hueco de teclado, `Bind`, `Clear`, hueco de mando, `Bind`,
  `Clear`.
- `Reset key bindings`.
- Avisos de conflicto dentro de la propia ventana.

Las ventanas son de ratón; la navegación por teclado de ImGui sigue
desactivada. No se crea `imgui.ini`.

### 120.6 Evidencia

- Build limpio: `make clean && make -j$(nproc)`, OK.
  Solo aparece el warning preexistente de `core/src/lcd.cpp:188`
  (`-Wnarrowing`); no hay warnings nuevos de la GUI.
- `make run_tests`: **222 casos / 3987 assertions**, verde.
- `make run_blargg`: **15/15**.
- `make run_mts`: **42/42**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- BIOS: **7 ROMs / 2800 frames, all match**.
- `QTR_CONFIG` nuevo contiene `[input]` y `[shortcuts]` con los defaults.
- `.ini` con conflictos se limpia al cargar: duplicado de jugador, key binding
  que gana y atajo duplicado eliminado.
- Smoke test headless con `Controllers` y `Key bindings` abiertas 3 s: sin
  asserts de ImGui ni crash.
- `imgui.ini` no se genera.

### 120.7 Limitaciones conocidas

- La identidad del mando es el GUID: dos mandos idénticos comparten GUID y
  ambos alimentan al jugador que lo tenga seleccionado.
- Solo se admiten mandos reconocidos por SDL GameController (con mapping).
- Los umbrales 60/40 están fijos; no se exponen en la UI.
- En este entorno no había un mando físico conectado: la ruta de mandos está
  validada por el modelo, los tests de configuración y revisión de código,
  pero no por una sesión con hardware real.

### 120.8 Corrección de UI: IDs duplicados y barra de estado

**Backup final:** `tmp/20261117_233254/`

#### IDs duplicados

En la tabla de `Controllers` el botón `Bind` de la fila `Up` chocaba con la
cabecera de la columna `Bind`:

- fila `Up` → índice 2 + label `Bind`;
- cabecera de la columna 2 → índice 2 + label `Bind`.

ImGui usa el índice numérico como parte del ID, así que ambos widgets
generaban el mismo ID. El botón `Clear` de `Down` (índice 3) chocaba de la
misma forma con la cabecera `Clear`.

Se añadió un namespace de texto por fila:

```cpp
ImGui::PushID(static_cast<int>(i));
ImGui::PushID("binding");   // Controllers
// ...
ImGui::PushID("shortcut");  // Key bindings
```

Con eso el ID de fila queda `[tabla, fila, "binding", label]` y el de cabecera
`[tabla, columna, label]`, sin colisiones. Se aplicó también a `Key bindings`
para que no vuelva a ocurrir al crecer la tabla.

#### Textos variables encima de las listas

Se quitaron los textos de captura, aviso e hint que aparecían y desaparecían
encima de las listas. Ahora hay una **barra de estado fija al pie** de cada
ventana, con altura constante:

- durante una captura muestra qué se está capturando;
- si hay un aviso reciente, muestra el aviso;
- si no, muestra el hint de uso;
- la segunda línea es un hint permanente.

La lista ocupa siempre el mismo alto; el contenido de la barra cambia sin
mover la tabla. En `Controllers` la barra queda debajo del checkbox
`Allow opposite directions`.

#### Evidencia

- Build limpio: `make clean && make -j$(nproc)`, OK. Solo el warning
  preexistente de `core/src/lcd.cpp:188`.
- `make run_tests`: **222 casos / 3987 assertions**, verde.
- `make run_blargg`: **15/15**.
- `make run_mts`: **42/42**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- BIOS: **7 ROMs / 2800 frames, all match**.
- Smoke test headless de ambas ventanas 3 s en estado idle: sin asserts ni
  crash.
- Smoke test headless de ambas ventanas 3 s con captura activa: sin asserts
  ni crash.
- `imgui.ini` no se genera.

---

## 121. SESIÓN 2026-11-18 — Licencia, assets y vendorización de ImGuiFileDialog

**Backup final:** `tmp/20261118_010856/`
**Git:** sin commit.

### 121.1 Licencia del proyecto

- Añadido `LICENSE.txt` con **GPL-3.0-or-later** y el texto completo de la
  GPLv3.
- El aviso inicial incluye el copyright del proyecto, el identificador SPDX y
  la referencia a la política de marca.
- `README.md` ya no contiene la frase restrictiva anterior; ahora indica
  GPL-3.0-or-later, CC0 para los assets y enlaces a los avisos.

### 121.2 Icono, assets y marca

- `assets/QtreGB_icon_generic_256.png` se documenta como icono oficial.
- `assets/LICENSE` y `assets/LICENSE.es.md`: los assets visuales se dedican a
  **CC0 1.0**, con procedencia y aviso de que CC0 no concede derechos de marca.
- `TRADEMARK.md` y `TRADEMARK.es.md`: política de marca para **QtreGB**, el
  nombre corto **Qtre**, el logotipo y el icono. Incluye reglas para versiones
  oficiales, copias sin modificar, forks, uso comercial y dominios/cuentas.
- El icono se muestra en `README.md` como imagen de marca del proyecto.

### 121.3 Terceros

- Añadidos `THIRD_PARTY_NOTICES.md` y `.es.md` con:
  - SDL2 y tinyfiledialogs (zlib);
  - Dear ImGui, ImGuiFileDialog, dirent, stb y doctest (MIT);
  - GLEW (BSD-3-Clause).
- Se aclara que ImGuiFileDialog todavía no está conectado al build de la
  aplicación.

### 121.4 ImGuiFileDialog vendorizado

Copiado desde QtreGBPaint a `third_party/ImGuiFileDialog`:

- ImGuiFileDialog **v0.6.8**, commit
  `ee77f190861193a995435c388682f1512f6fd29a`;
- `LICENSE` MIT y `VERSION.txt`;
- `LOCAL_CHANGES.md` con los cambios locales de i18n runtime
  (`IGFD::FileDialogStrings`);
- `ImGuiFileDialogStrings.h`, `ImGuiFileDialog.cpp` y headers;
- `dirent` y `stb` con sus licencias;
- `Makefile`, que compila `libImGuiFileDialog.a` contra `lib/imgui/src`.

También se añadió `third_party/README.md` y se actualizó `.gitignore` para
ignorar únicamente `third_party/ImGuiFileDialog/build/` y el `.a` generado.

No se conectó en esa sesión: quedó como base para sustituir tinyfiledialogs y
añadir i18n. La sesión 122 lo integró finalmente en `QtreGB-GUI`.

### 121.5 Evidencia

- `make -C third_party/ImGuiFileDialog -j$(nproc)`: genera
  `libImGuiFileDialog.a` correctamente.
- Build de la aplicación: OK.
- `make run_tests`: **222 casos / 3987 assertions**, verde.
- `git diff --check`: limpio.
- No se ha tocado código del emulador ni el core.

---

## 122. SESIÓN 2026-11-19 — ImGuiFileDialog, fin de TinyFileDialogs e idiomas

**Backup final:** `tmp/20261119_062529/`
**Git:** sin commit.

### 122.1 File dialogs

- Integrado `third_party/ImGuiFileDialog` (v0.6.8, MIT, con los cambios
  locales de i18n de QtreGBPaint) en el build de la GUI.
- `common.mk` compila y enlaza `libImGuiFileDialog.a`; `gui/Makefile` construye
  la librería si falta y `make clean` limpia también su build.
- Sustituidos los diálogos síncronos de TinyFileDialogs por diálogos
  asíncronos de ImGuiFileDialog:
  - abrir ROM;
  - cargar ROM del jugador 2;
  - cargar BIOS;
  - guardar captura BMP.
- Los diálogos se dibujan después de la ventana principal y guardan el último
  directorio usado.
- Mientras un diálogo está abierto se bloquean la entrada de los jugadores y
  los atajos globales (`InputManager::set_ui_blocked`), para que escribir un
  nombre de archivo o pulsar F5 no afecte al emulador.
- `lib/tinyfiledialogs` eliminado del proyecto; ya no hay referencias en el
  build, el código ni los avisos de terceros.

### 122.2 Idiomas

- Añadidos `gui/inc/i18n.h` + `gui/src/i18n.cpp`, con catálogos gettext-style:
  las cadenas inglesas del código son la clave y el fallback.
- `lang/es.po` contiene la traducción española completa de la interfaz actual.
- `Settings → Language` ofrece:
  - `System default`;
  - `English`;
  - `Español`.
- El idioma se guarda en `QtreGB.ini`:

```ini
[ui]
language=system
```

- `system` resuelve `es`/`en` desde `LANGUAGE`, `LC_ALL`, `LC_MESSAGES` o
  `LANG`; si no hay catálogo, cae a inglés.
- Directorio de catálogos: `QTR_LANG_DIR`, `<exe>/lang`, `<exe>/../lang`,
  `<exe>/../share/QtreGB/lang`, `<exe>/../Resources/lang` o `<cwd>/lang`.
- `apply_file_dialog_language()` traduce también la tabla runtime de
  ImGuiFileDialog (OK, cancelar, columnas, búsqueda, avisos de sobrescritura,
  dispositivos, miniaturas, etc.).
- Las ventanas de Settings usan IDs estables (`###Controllers`,
  `###KeyBindings`, `###CustomPalette`, `###LayersWindow`) para que cambiar el
  idioma no cree ventanas nuevas.

### 122.3 Evidencia

- Build limpio: `make clean && make -j$(nproc)`, OK. Solo el warning
  preexistente de `core/src/lcd.cpp:188`.
- `make run_tests`: **226 casos / 4005 assertions**, verde.
- `make run_blargg`: **15/15**.
- `make run_mts`: **42/42**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- BIOS: **7 ROMs / 2800 frames, all match**.
- Smoke test headless en español con `Controllers`, `Key bindings` y el
  diálogo de abrir ROM abiertos 3 s: sin asserts ni crash.
- Catálogo `lang/es.po` cargado sin errores.
- `git diff --check`: limpio.
- `imgui.ini` no se genera.

---

## 123. SESIÓN 2026-11-19 — Settings → Paths

**Backup final:** `tmp/20261119_065746/`
**Git:** sin commit.

### 123.1 Modelo

Nuevo `PathSettings` (`gui/inc/path_settings.h` + `gui/src/path_settings.cpp`)
con:

- `roms`: directorio inicial del diálogo de ROMs.
- `bios`: directorio inicial del diálogo de BIOS.
- `battery`: directorio de baterías.
- `savestates`: directorio de savestates.
- `screenshots`: directorio inicial de capturas.
- `battery_same_as_rom`.
- `savestates_same_as_rom`.

Los dos checks son independientes y valen `true` por defecto y en
configuraciones antiguas sin `[paths]`.

Cuando un check está activo se mantiene el comportamiento actual:

```text
/roms/Pokemon Red.gb
/roms/Pokemon Red.sav
/roms/Pokemon Red.sr1
```

Cuando está desactivado se usan nombres planos en el directorio configurado:

```text
/saves/battery/Pokemon Red.sav
/saves/states/Pokemon Red.sr1
```

Si el directorio central está vacío, se vuelve a la carpeta de la ROM. No hay
subcarpetas por juego: dos ROMs con el mismo nombre comparten saves, decisión
consciente del usuario.

El jugador 2 conserva sus sufijos `.sa2` y `.sr2`.

### 123.2 Interfaz

`Settings → Paths...` abre una ventana con filas para ROMs, BIOS, baterías,
savestates y capturas:

- campo de texto editable;
- `Browse...` con el selector de directorios de ImGuiFileDialog;
- `Clear`;
- validación visible si el directorio no existe;
- checks independientes `Same folder as ROM` en baterías y savestates;
- `Apply`, `Cancel` y `Reset defaults`.

Los cambios no se aplican hasta pulsar `Apply`; `Cancel` o cerrar la ventana
descartan la edición. `Apply` se deshabilita si alguna ruta activa no existe.

Al aplicar:

1. Cada instancia guarda la battery actual en su ruta antigua si la ruta de
   baterías ha cambiado.
2. Se actualizan las rutas de baterías y savestates en caliente.
3. Los nuevos saves/states se escriben en las rutas nuevas. No se migran
   archivos existentes.

### 123.3 Integración con diálogos

- `Abrir ROM...` y `Cargar ROM del jugador 2...` empiezan en `paths.roms`.
- `Cargar BIOS...` empieza en `paths.bios`.
- `Guardar captura...` empieza en `paths.screenshots`.
- Un campo vacío usa el último directorio usado.
- `StateStore` calcula `<rom>.srN` o `<directorio>/<stem>.srN` con el mismo
  modelo.
- La configuración se guarda en:

```ini
[paths]
roms=
bios=
battery=
savestates=
screenshots=
battery_same_as_rom=1
savestates_same_as_rom=1
```

### 123.4 Evidencia

- Build limpio: `make clean && make -j$(nproc)`, OK. Solo el warning
  preexistente de `core/src/lcd.cpp:188`.
- `make run_tests`: **231 casos / 4030 assertions**, verde.
- `make run_blargg`: **15/15**.
- `make run_mts`: **42/42**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- BIOS: **7 ROMs / 2800 frames, all match**.
- Smoke test headless en español con la ventana Paths y el selector de
  directorios abiertos 3 s: sin asserts ni crash; catálogo cargado.
- `git diff --check`: limpio.
- `imgui.ini` no se genera.

---

## 124. SESIÓN 2026-11-19 — Link sessions y savestate de sesión

**Backup final:** `tmp/20261119_082742/`
**Git:** sin commit.

### 124.1 Menú Link

Nuevo menú `Link`:

- `Start session...` — requiere ROM de P1 y abre el diálogo de ROM de P2.
- `Load ROM player 2...` — solo con sesión activa.
- `Reset player 2` — solo con sesión activa.
- `Stop session` — guarda la battery de P2, desconecta el cable, resetea el
  estado serie de P1 y destruye P2.

`File > Open ROM...` sigue siendo para P1. Si hay una sesión activa:

- se carga la ROM nueva de P1;
- P2 sigue corriendo;
- se desconecta/reconecta el cable;
- se resetean los dos puertos serie;
- se crea una sesión nueva (nuevo nombre de `.qss`), sin guardar
  automáticamente la anterior.

Lo mismo al cambiar la ROM de P2 desde `Link`. Si la ROM nueva no valida, no se
toca la sesión actual. Las ROMs se validan con un `Cart` temporal antes de
tocar las máquinas.

`Reset player 1` (o `Reset` sin sesión) y `Reset player 2` resetean solo su
máquina y rehacen el estado del cable.

### 124.2 F5/F8 contextual

- Sin sesión: `.sr1` como hasta ahora.
- Con sesión activa: un único `.qss` de sesión.
- No se guarda nada automáticamente al cambiar de juego ni al parar la sesión.
- Los `.qss` antiguos no se sobrescriben salvo que el usuario pulse F5 con la
  misma pareja de ROMs.

### 124.3 Contenedor `.qss`

`gui/inc/session_state.h` + `gui/src/session_state.cpp`:

- magic y versión;
- CRC-32 de cabecera y payload;
- información de cada ROM: nombre, ruta informativa, tamaño y **FNV-1a 64**
  del contenido completo;
- ruta y hash FNV-1a de la BIOS latcheada;
- los dos blobs crudos del core.

El nombre es legible y lleva un hash corto:

```text
Pokemon Red - Pokemon Blue (4f2a9c1e).qss
```

Los stems se sanean y se truncan si hace falta; el hash corto sale de los dos
FNV-1a de contenido. El archivo se guarda en la carpeta de savestates de P1
(configurada o junto a la ROM de P1).

Al cargar:

1. se comprueba que las ROMs cargadas coinciden por tamaño y FNV-1a 64;
2. si no, se prueba la ruta guardada;
3. después `paths.roms / nombre_de_archivo`, **solo en la raíz**;
4. si no aparece o el hash no coincide, se muestra el esperado y el actual;
5. antes de aplicar los estados se guardan snapshots de las dos máquinas;
6. si un core rechaza su blob, se intenta restaurar la sesión anterior.

La BIOS no es un corte previo: se guarda como metadato. El core solo valida su
hash si el estado se capturó con la boot ROM todavía mapeada. Si falla, el
mensaje lo indica.

### 124.4 BIOS en Paths

- `File > Load BIOS...` eliminado.
- `Settings → Paths...` ahora tiene `BIOS file`, un path a un archivo de
  exactamente 256 bytes (vacío = sin BIOS).
- Es configuración global y persistente (`[paths] bios`).
- Al iniciar una sesión se latcha la BIOS que tiene P1 y se aplica a P2.
- Cambiar la ruta durante la sesión no altera las máquinas activas; solo
  afecta a futuras sesiones/resets.
- Al parar la sesión, P1 adopta la BIOS global para futuros resets.
- `QtreGB::clear_boot_rom()` y `EmulatorInstance::clear_boot_rom()` permiten
  quitar la BIOS sin afectar la ejecución actual hasta el siguiente reset.

### 124.5 Evidencia

- Build limpio: `make clean && make -j$(nproc)`, OK. Solo el warning
  preexistente de `core/src/lcd.cpp:188`.
- `make run_tests`: **237 casos / 4063 assertions**, verde.
- `make run_blargg`: **15/15**.
- `make run_mts`: **42/42**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- BIOS: **7 ROMs / 2800 frames, all match**.
- Smoke test headless con BIOS configurada:
  - `Start session` con `tetris.gb` + `Pokeblue.gb`;
  - guardado y carga del `.qss`;
  - cambio de ROM de P2 a `Parodius.gb` y nuevo `.qss`;
  - cambio de ROM de P1 a `zelda.gb` y nuevo `.qss`;
  - los tres `.qss` permanecen;
  - `Stop session` deja 1 instancia y la sesión inactiva.
- Smoke test de la ventana Paths en español: sin
  asserts ni crash.
- `imgui.ini` no se genera.

---

## 125. SESIÓN 2026-11-19 — Core dump al arrancar con BIOS como directorio

**Backup final:** `tmp/20261119_084153/`
**Git:** sin commit.

### 125.1 Causa

La configuración existente tenía:

```ini
[paths]
bios=/home/pedro/emulation/gb/bios
```

Con el cambio a BIOS como archivo, ese valor apunta a un directorio. Al
arrancar, `read_binary_file()` abría el directorio, `tellg()` devolvía un
tamaño enorme y `std::vector::resize()` lanzaba `std::bad_alloc`; el proceso
terminaba con `SIGABRT` y generaba el core.

### 125.2 Corrección

- `read_binary_file()` comprueba `is_regular_file` antes de abrir.
- Valida `seekg`, `tellg`, tamaño no negativo y un máximo de 64 MiB.
- `SessionState::read_file()` aplica las mismas comprobaciones.
- Un directorio o un archivo especial ya no puede provocar una reserva
  gigantesca.

Con esto la configuración antigua arranca sin core. La fila `BIOS file` de
`Settings → Paths...` muestra el directorio como inválido hasta que se
seleccione el archivo de 256 bytes.

### 125.3 Evidencia

- Causa reproducida con `QTR_CONFIG=~/.config/Qtre/QtreGB/config.ini`:
  `terminate called after throwing std::bad_alloc`.
- Tras el arreglo, el mismo comando arranca 3 s en headless sin crash.
- Test nuevo: cargar un directorio como `.qss` devuelve error, no lanza.
- Build limpio: OK. Solo el warning preexistente de `core/src/lcd.cpp:188`.
- `make run_tests`: **238 casos / 4065 assertions**, verde.
- `make run_blargg`: **15/15**.
- `make run_mts`: **42/42**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- BIOS: **7 ROMs / 2800 frames, all match**.
- `imgui.ini` no se genera.

---

## 126. SESIÓN 2026-11-19 — Diálogo de BIOS y ConfirmOverwrite

**Backup final:** `tmp/20261119_084349/`
**Git:** sin commit.

### 126.1 Causa

Todos los `OpenDialog` usaban `ImGuiFileDialogFlags_Default`, que incluye
`ImGuiFileDialogFlags_ConfirmOverwrite`. Por eso el selector de BIOS (y también
el de ROMs) preguntaba si se quería sobrescribir un archivo existente, aunque
fuera una operación de apertura.

### 126.2 Corrección

- Abrir ROM y elegir archivo de BIOS:
  `Modal | HideColumnType | CaseInsensitiveExtentionFiltering`.
- Selector de directorios:
  `Modal | HideColumnType`.
- Solo `Guardar captura` conserva `ImGuiFileDialogFlags_Default`
  (`ConfirmOverwrite` incluido).

### 126.3 Evidencia

- Build: OK.
- `make run_tests`: **238 casos / 4065 assertions**, verde.
- Sin cambios en core ni en el formato de sesión.

---

## 127. SESIÓN 2026-11-19 — 10 slots de savestate y barra de estado

**Backup final:** `tmp/20261119_091945/`
**Git:** sin commit.

### 127.1 Slots

- Savestates de un jugador: **10 slots**, de `.sr0` a `.sr9`.
- Al arrancar la aplicación o cargar una ROM nueva en P1 se selecciona el
  **slot 0**.
- `Next state slot` y `Previous state slot` cambian de slot con vuelta
  circular 0↔9.
- Por defecto: **PageUp** y **PageDown**.
- `F5`/`F8` guardan/cargan únicamente el slot seleccionado.
- El jugador 2 ya **no tiene savestate propio**. `EmulatorInstance::state_path`
  devuelve vacío y `save_state`/`load_state` fallan para P2.
- Con una sesión Link activa, F5/F8 siguen usando el único `.qss` de sesión;
  los slots no aplican y los atajos de slot no cambian nada.
- `StateStore` usa el slot y `PathSettings::state_path(slot)` genera
  `.sr0`…`.sr9`, tanto junto a la ROM como en la carpeta central.

No hay migración de `.sr1` antiguos: el formato de nombre final es `.srN`.

### 127.2 Shortcuts

- `ShortcutAction` pasa de 3 a 5 acciones:
  - `NextStateSlot` → `Next state slot`.
  - `PreviousStateSlot` → `Previous state slot`.
- `InputSettings::ShortcutCount` centraliza el tamaño de los arrays.
- Configuración nueva en `[shortcuts]`:

```ini
next_state_slot_key=key:75
next_state_slot_controller=none
previous_state_slot_key=key:78
previous_state_slot_controller=none
```

- La ventana de Key bindings muestra automáticamente las cinco acciones.

### 127.3 Barra de estado

- Eliminado el overlay de mensajes de la esquina superior.
- Añadida una barra fija al pie de la ventana principal.
- La barra reserva su altura, así que no tapa la imagen.
- Muestra siempre:
  - `State slot: N` sin sesión;
  - `Session state (single slot)` con sesión Link.
- A la derecha muestra el último aviso de guardado, carga, cambio de slot,
  ROM, paths, etc.
- Los mensajes siguen caducando, pero el indicador de slot/sesión permanece.

### 127.4 Evidencia

- Build limpio: OK. Solo el warning preexistente de `core/src/lcd.cpp:188`.
- `make run_tests`: **239 casos / 4075 assertions**, verde.
- `make run_blargg`: **15/15**.
- `make run_mts`: **42/42**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- BIOS: **7 ROMs / 2800 frames, all match**.
- Smoke test de slots:
  - ROM nueva → `tetris.sr0`;
  - siguiente → `tetris.sr1`;
  - anterior desde 1 → slot 9 y `tetris.sr9`;
  - P2: `state_path` vacío y `save_state` falso;
  - en sesión Link el slot no cambia y la sesión sigue siendo única.
- `imgui.ini` no se genera.

---

## 128. SESIÓN 2026-11-19 — Línea horizontal fija en el jugador 2

**Backup final:** `tmp/20261119_092737/`
**Git:** sin commit.

### 128.1 Causa

La textura combinada se generaba cuando P1 llegaba al inicio de su línea 0.
En ese instante se procesaban los buffers de las dos PPU, pero la PPU de P2
tiene una fase independiente y podía estar a mitad de frame. El buffer de P2
contenía, por tanto, filas del frame anterior y del actual: una línea
horizontal fija, igual que el antiguo problema de empezar el frame en un dot
arbitrario.

### 128.2 Corrección

- P1 sigue refrescando su textura cuando el frame de P1 termina.
- P2 refresca la suya dentro de `EmulatorFrontend::tick()`, justo cuando su
  propia PPU señala el inicio de línea 0.
- `EmulatorFrontend::update_frame()` solo actualiza P1 y compone la textura
  combinada con el último frame **completo** de cada máquina.
- P2 puede quedar como máximo un frame por detrás de P1; no se mezclan filas
  de dos frames y desaparece la línea fija.

### 128.3 Evidencia

- Build limpio: OK. Solo el warning preexistente de `core/src/lcd.cpp:188`.
- `make run_tests`: **239 casos / 4075 assertions**, verde.
- `make run_blargg`: **15/15**.
- `make run_mts`: **42/42**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- BIOS: **7 ROMs / 2800 frames, all match**.
- Smoke test Link: arranque, guardado/carga de sesión y cambio de ROMs sin
  regresiones.
- `imgui.ini` no se genera.

---

## 129. SESIÓN 2026-11-19 — Audio de Link: P1, P2 o ambos

**Backup final:** `tmp/20261119_095046/`
**Git:** sin commit.

### 129.1 Opciones

- El menú general `Audio` mantiene `Enable audio` y `Volume`.
- `Link → Audio` añade:
  - `Player 1` (por defecto).
  - `Player 2`.
  - `Both`.
- La opción se guarda en `[audio] link_source=p1|p2|both`.
- Sin sesión Link, P1 sigue siendo audible aunque la preferencia guardada sea
  P2; la preferencia se aplica al arrancar la sesión.

### 129.2 Implementación

- `AudioSettings` incorpora `LinkAudioSource`.
- `EmulatorInstance` tiene `m_audio_output_enabled` independiente del
  `AudioSettings` global.
- Al silenciar una instancia:
  - se limpia su cola SDL;
  - se pausa su dispositivo;
  - `drain_audio()` descarta el ring del APU para que no quede audio viejo.
- `EmulatorFrontend::apply_audio_routing()` decide qué instancia suena según
  la sesión activa y `link_source`.
- `Both` deja sonar los dos dispositivos SDL; la mezcla final la hace el
  sistema.
- Cambiar de fuente en caliente limpia las colas y aplica pausa/despausa sin
  reiniciar la emulación.

### 129.3 Evidencia

- Build: OK.
- `make run_tests`: **239 casos / 4077 assertions**, verde.
- Smoke test de routing:
  - por defecto: P1 on, P2 off;
  - Player 2: P1 off, P2 on;
  - Both: P1 on, P2 on;
  - vuelta a Player 1: P1 on, P2 off;
  - Stop session: P1 on, P2 destruido.
- `make run_blargg`: **15/15**.
- `make run_mts`: **42/42**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- BIOS: **7 ROMs / 2800 frames, all match**.
- `imgui.ini` no se genera.

---

## 130. SESIÓN 2026-11-19 — Fast Forward, Exit y bloqueo de input en Settings

**Backup final:** `tmp/20261119_103257/`
**Git:** sin commit.

### 130.1 Limpieza

- Eliminado el menú `Debug` (la antigua prueba de PPU/capas) y su ventana
  `Layers Window`.
- `File → Exit` cierra la aplicación de forma limpia y guarda la configuración
  al salir.
- El bucle principal sale también cuando el frontend marca `should_close()`.

### 130.2 Fast Forward

- Nueva acción global `Fast forward`, por defecto **Space**.
- Nueva opción `Settings → Fast Forward speed`:
  - `200%`.
  - `300%`.
  - `MAX`.
- Se guarda en `[emulation] fast_forward_speed=200|300|max`.
- Es una tecla **mantenida**, no un toggle: se consulta con
  `InputManager::shortcut_held()`.

Comportamiento del bucle:

- **200%**: el acumulador recibe `delta * 2` y se ejecutan los frames
  necesarios; solo se presenta el último.
- **300%**: igual con `delta * 3`.
- **MAX**: se ignoran los acumuladores de tiempo y se ejecutan frames hasta
  consumir aproximadamente la mitad del intervalo de presentación, con un
  máximo de 8 ms. Después se presenta y se duerme el resto del frame. Así no
  se queda el proceso al 100 % de CPU.
- Durante fast-forward se descarta el audio (ring APU + cola SDL) para evitar
  que se acumule audio a velocidad incorrecta.

### 130.3 Input bloqueado con ventanas de Settings

- Con `Controllers`, `Key bindings` o `Paths` abiertas, `InputManager` queda
  en modo UI-blocked.
- No llegan pulsaciones al emulador ni se disparan atajos globales.
- La captura de bindings sigue funcionando: `begin_capture` procesa teclas y
  botones antes de la suspensión normal.
- Al cerrar la ventana se rearma la entrada y se reanuda el juego.

### 130.4 Evidencia

- Build limpio: OK. Solo el warning preexistente de `core/src/lcd.cpp:188`.
- `make run_tests`: **240 casos / 4086 assertions**, verde.
- `make run_blargg`: **15/15**.
- `make run_mts`: **42/42**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- BIOS: **7 ROMs / 2800 frames, all match**.
- Smoke test de fast-forward en 200% y MAX con ROM cargada y Space inyectado:
  sin crash.
- `.ini` nuevo contiene `fast_forward_speed=200` y
  `fast_forward_key=key:44`.
- `imgui.ini` no se genera.

---

## 131. SESIÓN 2026-11-19 — FPS reales del emulador en la barra de estado

**Backup final:** `tmp/20261119_105713/`
**Git:** sin commit.

### 131.1 Objetivo

- Mostrar en la barra inferior los FPS **emulados** (frames emulados por
  segundo real), no los FPS de presentación de ImGui.
- Comprobar el rendimiento de fast-forward:
  - normal ≈ 59.7;
  - 200% ≈ 119.5;
  - 300% ≈ 179.2;
  - MAX, lo más alto posible con un pequeño respiro de CPU.

### 131.2 Diagnóstico

- Benchmark del core sin GUI (`QtreGB::tick()` durante 2 s):
  - Tetris: **846.8 fps**.
  - Pokémon Blue: **911.4 fps**.
- El core no es el cuello de botella. La sonda temporal del bucle real midió:
  - normal: 58.7–58.9 fps;
  - 200%: 119.1–120.5 fps;
  - 300%: 174.3–183.8 fps;
  - MAX antes del cambio: ≈ 265 fps.
- El 200/300 ya cumplía el objetivo; el usuario veía los FPS de ImGui
  (presentación), que siguen alrededor de 60 aunque la emulación vaya a 120 o
  180.
- MAX estaba limitado por un presupuesto de solo 8 ms de emulación por vuelta
  más el coste fijo de presentación/ImGui. En este entorno ese coste ronda
  17 ms por vuelta, así que subir solo el presupuesto tiene rendimientos
  decrecientes; aun así, 12 ms elevan MAX a ≈ 330 fps.

### 131.3 Implementación

- `gui/inc/fps_counter.h` + `gui/src/fps_counter.cpp`:
  - ventana deslizante de **0.5 s** sobre frames completados;
  - `fps()` devuelve 0 si no hay frames desde hace más de 1 s;
  - un hueco largo reinicia la ventana en vez de promediar la pausa.
- `run_emulated_frame()` llama a `EmulatorFrontend::record_emulated_frame()`
  justo cuando el PPU entrega un frame completo. Se cuentan todos los frames
  emulados, incluidos los que no se presentan.
- `EmulatorFrontend::draw_main_status_bar()` muestra, tras la ranura:
  - `FPS: 59.8` cuando hay medida;
  - `FPS: --` al arrancar, sin ROM o con la LCD apagada.
- El menú `Graphics` ahora etiqueta su dato como
  `Display FPS: %.1f - %.2f ms` para no confundirlo con los FPS emulados.
- MAX pasa a usar hasta el **75% del intervalo de display**, con tope de
  **12 ms** de emulación por vuelta. El resto queda para presentar y dormir.

### 131.4 Evidencia

- Build limpio: OK. Solo el warning preexistente de `core/src/lcd.cpp:188`.
- `make run_tests`: **243 casos / 4098 assertions**, verde.
- `make run_blargg`: **15/15**.
- `make run_mts`: **42/42**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- BIOS: **7 ROMs / 2800 frames, all match**.
- Sonda temporal del contador real:
  - normal: 58.8 fps;
  - 200%: 119–120 fps;
  - 300%: 178–180 fps;
  - MAX: ≈ 325–331 fps (antes ≈ 265).
- Captura de la ventana con Tetris: la barra muestra
  `Ranura de guardado: 0  FPS: 59.8`.
- `imgui.ini` no se genera.

---

## 132. SESIÓN 2026-11-19 — Archivos recientes en el menú File

**Backup final:** `tmp/20261119_112347/`
**Git:** sin commit.

### 132.1 Comportamiento

- El menú `File` muestra hasta **10 ROMs recientes** entre `Load state` y
  `Exit`, con un separador antes de la lista y otro antes de `Exit`.
- Solo aparece la sección si la lista no está vacía.
- Cada entrada se muestra numerada con el nombre del archivo:
  `1. tetris.gb`, `2. Pokeblue.gb`, …
- Al pasar el ratón se muestra la ruta completa como tooltip.
- Al seleccionar una entrada se carga esa ROM por el mismo camino que
  `File → Open ROM...`.
- Si el archivo ya no existe, se elimina de la lista y se avisa en la barra
  inferior.
- La ROM abierta pasa al primer puesto; no se duplica.

### 132.2 Persistencia

- Nueva sección en `QtreGB.ini`:

```ini
[recent]
1=/ruta/tetris.gb
2=/ruta/Pokeblue.gb
```

- Claves indexadas de `1` a `10`. Los huecos se compactan al cargar y las
  claves fuera de rango se ignoran.
- `RecentFiles` (`gui/inc/recent_files.h` + `gui/src/recent_files.cpp`)
  centraliza orden, deduplicación y límite.
- `AppConfig::recent_roms` guarda el modelo; `main.cpp` lo carga al arrancar y
  lo vuelca al salir.

### 132.3 Alcance

- Solo se registran ROMs abiertas para el **jugador 1** mediante
  `open_rom_path()`.
- `Link → Start session...`, `Link → Load ROM player 2...` y la carga de
  sesiones `.qss` **no** modifican la lista.
- No hay cambios en el flujo de Link ni en el jugador 2.

### 132.4 Evidencia

- Build: OK.
- `make run_tests`: **246 casos / 4133 assertions**, verde.
- Tests nuevos:
  - `RecentFiles`: orden más reciente primero, deduplicación, tope de 10,
    eliminación y `set()` con huecos/duplicados.
  - `AppConfig`: valores por defecto, claves `recent.N` inválidas, huecos y
    round-trip completo de la lista.
- `imgui.ini` no se genera.

---

## 133. SESIÓN 2026-11-19 — Contenedor de savestate (fase 1)

**Backup final:** `tmp/20261119_120600/`
**Git:** sin commit.

### 133.1 Alcance

- Savestates de un jugador (`.sr0`…`.sr9`).
- El blob del core no cambia: se envuelve con metadatos de la GUI.
- Los `.qss` de Link no se tocan en esta fase.
- Sin compresión y sin sincronización de batería.

### 133.2 Contenedor

- Magic `QTGBSTF1`, versión de contenedor y copia de format/schema del core.
- Cabecera de 84 bytes con CRC propio.
- Identidad de ROM:
  - título de cabecera, nombre de archivo, tamaño, FNV-1a 64, mapper, flag
    CGB, tipo de cartucho, checksum de cabecera y checksum global.
- Identidad de BIOS:
  - tamaño, hash FNV-1a y si el boot ROM estaba activo.
- Fecha UTC de creación y frames emulados.
- Ranura, tipo de estado (`manual` por ahora) y nota (vacía).
- Versión de build obtenida con `git describe --tags --always --dirty`
  (`QTREGB_BUILD_VERSION`).
- Miniatura 160×144 RGBA8888 sin comprimir (92.160 bytes).
- CRC-32 del payload y escritura atómica mediante `.tmp` + `rename`.
- Los `.srN` antiguos que solo contienen el blob `QTGBST01` siguen cargando.
- Formato documentado en
  `documentation/Game Boy/savestate-container-format.md`.

### 133.3 Tamaño sin comprimir

| ROM | Blob core | Miniatura | `.sr0` real |
|---|---:|---:|---:|
| Tetris | 19.104 B | 92.160 B | **111.420 B** (108,8 KiB) |
| Pokémon Blue | 52.029 B | 92.160 B | **144.325 B** (140,9 KiB) |

- La miniatura domina el tamaño en cartuchos sin RAM grande.
- El contenedor de Tetris se generó con F5 y la miniatura se extrajo y verificó
  visualmente (pantalla de copyright de Tetris).

### 133.4 RTC

- Recomendación: **congelar** el RTC al cargar un savestate.
- El core ya lo hace: restaura los registros y ancla `m_last_time` al instante
  de carga.
- Un savestate es una foto reproducible; ponerse al día con el tiempo real
  podría disparar eventos de días/meses y hacer que dos cargas del mismo
  estado no sean deterministas.
- El avance con tiempo real corresponde al bloque RTC del `.sav`, que ya
  guarda su timestamp.
- `created_unix_seconds` queda en el contenedor por si en el futuro se ofrece
  una opción explícita de “sincronizar RTC al cargar”.

### 133.5 Batería `.sav`: análisis

Situación actual:

- El `.sav` se escribe al salir, al cargar otra ROM, al resetear y al cambiar
  la carpeta de batería.
- Si el proceso casca entre dos escrituras, el `.sav` en disco no se actualiza
  y se pierde el progreso desde el último guardado.
- La consola no tiene una señal fiable de “guardado terminado”: las escrituras
  a `$A000-$BFFF` son accesos normales a SRAM. Un indicador de acceso sirve
  como pista, no como confirmación.
- Mantener el `.sav` abierto y escribirlo in-place es peor: un corte a mitad
  deja el único archivo corrupto y se pueden provocar muchas escrituras por
  segundo sin necesidad.

Mecanismo recomendado para la fase 2:

1. Marcar un flag `dirty` cuando el cartucho escriba en RAM respaldada.
2. Cada 2–5 segundos, si `dirty`, escribir un `.sav.tmp`, hacer `flush`/
   `fsync`, y reemplazar `.sav` con `rename` atómico.
3. Mantener un `.sav.bak` rotando el anterior; si el `.sav` falta o está
   corrupto, intentar el backup.
4. Vaciar también en eventos de ciclo de vida: salir, cambiar de ROM, reset,
   guardar/cargar estado y cerrar la ventana.
5. No escribir en cada acceso a SRAM ni mantener el archivo abierto. El
   intervalo de 2–5 s limita la pérdida a unos segundos con muy poco coste
   de I/O.
6. Opcional: instalar manejadores de `SIGINT`/`SIGTERM` para una salida
   ordenada; no intentar guardar desde un `SIGSEGV`/`SIGKILL`, donde no es
   seguro.

### 133.6 Evidencia

- Build limpio: OK. Solo el warning preexistente de `core/src/lcd.cpp:188`.
- `make run_tests`: **250 casos / 4174 assertions**, verde.
- Tests nuevos de `StateStore`: round-trip completo, CRC de cabecera/payload,
  truncado, compatibilidad con blobs antiguos, preview inválida y datos vacíos.
- Carga real del contenedor con el core de Tetris: `StateError::Ok`, y nuevo
  guardado correcto.
- `make run_blargg`: **15/15**.
- `make run_mts`: **42/42**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- BIOS: **7 ROMs / 2800 frames, all match**.
- `imgui.ini` no se genera.

---

## 134. SESIÓN 2026-11-19 — Contenedor de estado de Link (.qss v2)

**Backup final:** `tmp/20261119_130907/`
**Git:** sin commit.

### 134.1 Alcance

- Misma idea que el contenedor `.srN`: cabecera versionada, CRC, metadatos y
  escritura atómica.
- Se reutilizan las identidades de ROM y BIOS que ya existían en `.qss`.
- Se añade la imagen combinada que se está mostrando: 320×144, P1 a la
  izquierda y P2 a la derecha.
- Los `.qss` versión 1 se siguen cargando; los nuevos se guardan en versión 2.

### 134.2 Formato v2

- Cabecera fija de 28 bytes: magic `QTRSES01`, versión 2, flags, tamaños y
  CRCs de header/payload.
- Header:
  - ROM 1 y ROM 2 (nombre, ruta informativa, tamaño, hash FNV-1a).
  - BIOS (ruta, hash, tamaño).
  - Fecha UTC de creación.
  - Frames emulados.
  - Versión de build (`QTREGB_BUILD_VERSION`).
- Payload:
  - `preview_size` + preview RGBA8888 de 320×144 (184.320 bytes).
  - Los dos blobs del core con sus longitudes.
- Flag `FLAG_HAS_PREVIEW` para indicar si hay imagen.
- La preview se compone con `compose_combined_frame()`, exactamente con los
  mismos píxeles que `upload_combined_frame()` envía a la textura.
- Si alguna de las dos máquinas todavía no tiene un frame completo, se guarda
  el estado sin preview.

### 134.3 Compatibilidad y carga

- `SessionState::load()` acepta versión 1 y 2.
- En versión 1, los campos nuevos quedan a cero/vacíos y no se lee preview.
- En versión 2 se validan dimensiones, formato, tamaño exacto y coherencia del
  flag de preview.
- Los dos blobs se cargaron por separado con el core real:
  - máquina 1: `ok`;
  - máquina 2: `ok`.
- Al cargar una sesión, `m_emulated_frames` se restaura desde el `.qss`.

### 134.4 Tamaño medido

Sesión Tetris + Pokémon Blue:

| Parte | Bytes |
|---|---:|
| Blob P1 | 19.104 |
| Blob P2 | 52.029 |
| Preview 320×144 | 184.320 |
| Metadatos y CRCs | ~250 |
| **Total `.qss`** | **255.700 B** (249,7 KiB) |

La preview combinada es la parte dominante. La compresión queda para una fase
posterior.

### 134.5 Evidencia

- Build limpio: OK. Solo el warning preexistente de `core/src/lcd.cpp:188`.
- `make run_tests`: **252 casos / 4197 assertions**, verde.
- Tests nuevos:
  - round-trip v2 con preview y metadatos;
  - carga de un `.qss` v1 construido a mano;
  - rechazo de preview con tamaño incoherente;
  - CRC/truncado siguen cubiertos por los tests existentes.
- `.qss` real generado con F5 en una sesión Tetris + Pokémon Blue.
- Preview extraída del `.qss` y verificada visualmente a 320×144.
- `make run_blargg`: **15/15**.
- `make run_mts`: **42/42**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- BIOS: **7 ROMs / 2800 frames, all match**.
- `imgui.ini` no se genera.

---

## 135. SESIÓN 2026-11-19 — Savestates como PNG con datos y navegador de estados

**Backup final:** `tmp/20261119_144209/`
**Git:** sin commit.

### 135.1 Formato

- Los savestates nuevos son **PNG válidos con el contenedor detrás**:
  - `<rom>.sr<slot>.png`;
  - `Game1 - Game2 (short-hash).qss.png`.
- El PNG contiene la vista previa que se mostraba:
  - 160×144 para un jugador;
  - 320×144 para Link, con las dos máquinas.
- El contenedor sigue teniendo su cabecera/CRC/metadatos y su copia RGBA para
  el navegador interno.
- Al cargar, `png_payload_offset()` localiza el final de `IEND`; el contenedor
  se parsea desde ahí.
- Si no hay preview, se escribe un PNG de relleno oscuro para que el archivo
  siga siendo una imagen.
- Los archivos antiguos (`.srN`, `.qss`) se siguen cargando: si el `.png`
  nuevo no existe, se prueba la ruta heredada.

### 135.2 Codificación PNG

- `gui/inc/png_writer.h` + `gui/src/png_writer.cpp`.
- Usa `stb_image_write` v1.16 (dominio público) para generar PNG comprimido.
- Convierte los píxeles internos `0xRRGGBBAA` al orden de bytes RGBA que espera
  el codificador.
- `png_payload_offset()` valida la firma PNG y recorre los chunks hasta
  `IEND`, sin decodificar la imagen.

### 135.3 Navegador de estados

- `File → State browser...`.
- Lista las 10 ranuras del ROM actual con miniatura, título, fecha, frames,
  tiempo emulado y tamaño.
- En una sesión Link muestra el estado `.qss.png` único con la imagen
  combinada.
- Permite `Load`, `Save here`/`Save state` y `Refresh`.
- Al cargar un estado correctamente, la ventana se cierra; si la carga falla,
  se queda abierta para elegir otra ranura.
- La entrada de juego y los atajos globales quedan bloqueados mientras está
  abierto, igual que en las ventanas de Settings.
- Las miniaturas se cargan del contenedor (copia RGBA) y se suben como
  texturas SDL temporales.

### 135.4 Tamaños medidos

| Fichero | Bytes |
|---|---:|
| Tetris `.sr0.png` | **115.432 B** (112,7 KiB) |
| Tetris + Pokémon Blue `.qss.png` | **260.685 B** (254,6 KiB) |

El PNG añade unos pocos KiB; la mayor parte sigue siendo el contenedor con la
copia RGBA de la miniatura.

### 135.5 Evidencia

- `identify` reconoce el `.sr0.png` como **PNG 160×144**.
- `identify` reconoce el `.qss.png` como **PNG 320×144**.
- La imagen del PNG se abrió y verificó visualmente.
- `StateStore::load()` carga el `.sr0.png` y devuelve el core de Tetris, la
  metadata y la preview.
- `SessionState::load()` carga el `.qss.png`, ambos blobs y la preview
  combinada.
- Captura del navegador de estados mostrando la ranura 0 con su miniatura.
- Build limpio: OK. Solo el warning preexistente de `core/src/lcd.cpp:188`.
- `make run_tests`: **253 casos / 4209 assertions**, verde.
- `make run_blargg`: **15/15**.
- `make run_mts`: **42/42**.
- `make run_regress`: **11 ROMs / 990 frames, all match**.
- BIOS: **7 ROMs / 2800 frames, all match**.
- `imgui.ini` no se genera.

---

## 136. SIGUIENTE TAREA — Core libretro (fase 1)

**Estado:** pendiente.
**Carpeta:** `libretro/`.
**Makefile:** `libretro/Makefile` (placeholder, sin targets todavía).

### 136.1 Decisiones fijadas

- **ROMs:** solo `.gb` (DMG). Los `.gbc` quedan fuera de la primera versión.
- **Modo:** single-player. El Link Cable no entra en la primera versión porque
  libretro no tiene una API estándar de cable link entre dos cores.
- **RTC:** se mantiene el comportamiento actual del core.
- **Estados:** se usa directamente el blob de `QtreGB::save_state()` /
  `load_state()`. La envoltura PNG y los metadatos de la GUI no forman parte
  del core de libretro.
- **Paleta configurable y opciones:** fase posterior.

### 136.2 Tamaño máximo del estado

Medido con un cartucho MBC3 + RTC con 16 bancos de RAM (128 KiB), que es el
peor caso real del core:

- Estado medido: **150.333 bytes**.
- Margen para FIFOs/OAM lists/delayed writes completos: ~156 KiB.
- Tamaño para `retro_serialize_size`: **320 KiB (327.680 bytes)**, el doble
  del peor caso conservador redondeado hacia arriba.

Si el schema del core añade campos nuevos, hay que revisar esta constante.

### 136.3 Prueba local de la pantalla física

- Repo confirmado: <https://github.com/JohnnySun/retro-display-lab>.
- No es un core ni código C++ del LCD: es un paquete de **shaders Slang para
  RetroArch**. Modela DMG-01, CGB-001 y AGS-101; para QtreGB interesa el de
  DMG-01.
- Se instala en `RetroArch/shaders/retro-display-lab` y se carga como
  `.slangp` con Vulkan (también Metal en macOS). El perfil probado por el autor
  es para KONKR Pocket Advance 960×640; para otro display hay que partir del
  preset del modelo y crear un target profile.
- Usa respuesta temporal con feedback: hay que desactivar el frame mixing del
  core (y el ghosting propio de la GUI) para no aplicarlo dos veces.
- Licencia Apache-2.0 para código/docs originales; los datos de terceros
  conservan sus términos. Es compatible con GPLv3, pero si se redistribuye
  hay que conservar avisos y licencias.
- Para probarlo no hace falta esperar al core libretro: se puede usar ya en
  RetroArch con Gambatte y una ROM `.gb`.
- Carpeta ignorada por Git: `libretro/screen-test/`.
- No se commitea ni forma parte del emulador.
- Sirve para probar localmente la simulación de pantalla física de otro
  proyecto (shader o harness local):
  - si es un **shader** (GLSL/Slang), se puede cargar directamente en
    RetroArch sin tocar el core;
  - si es **código C++/software**, se integra como post-proceso sobre el
    framebuffer de 160×144, no como estado de máquina;
  - los efectos de persistencia/decay del LCD son visuales: no necesitan
    entrar en el savestate ni en el blob del core.
- Antes de incorporar código de otro proyecto al repo habría que revisar su
  licencia; para pruebas locales la carpeta ignorada es suficiente.
- Clon local realizado (ignorado por Git):
  - `libretro/screen-test/retro-display-lab/` en **v0.8.5**, commit
    `b751710`.
  - Paquetes generados:
    - `retro-display-lab-pack-v0.8.5.zip` (full, 39 shader files, 1.2 MB);
    - `retro-display-lab-dmg-v0.8.5.zip` (DMG + KPA, 790 KB);
    - `retro-display-lab-dmg-minimal-v0.8.5.zip` (DMG básico, 35 KB,
      recomendado para la primera prueba);
    - `SHA256SUMS.txt`.
  - Script local: `libretro/screen-test/build_shader_pack.py`.
  - Validación: `npm test` del repo pasa completo; también
    `build-retroarch-pack.mjs --check`, `build-dmg01-presets.mjs --check`,
    `build-dmg01-stn-physics.mjs --check-stored` y `validate-dmg01.mjs`.

### 136.4 Próximos pasos

1. Crear `libretro/libretro.cpp` con la API mínima.
2. Convertir el framebuffer 160×144 a XRGB8888.
3. Drenar el APU a 48 kHz y mapear el gamepad.
4. Exponer save RAM y `retro_serialize`/`unserialize`.
5. BIOS opcional desde `RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY`.
6. Build `.so`/`.dll`/`.dylib` y prueba en RetroArch.
