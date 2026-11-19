# Savestate container — fase 1

Fecha: 2026-11-19
Ámbito: `gui/` (el estado de máquina sigue siendo el blob del core).

Este documento define el contenedor de un savestate de un solo jugador,
`<rom>.sr<slot>`, producido por `StateStore`. El blob interno es exactamente el
que genera `QtreGB::save_state()`; el contenedor añade identidad, metadatos,
miniatura y checksums.

Implementación:

- `gui/inc/state_store.h`
- `gui/src/state_store.cpp`
- `tests/test_state_store.cpp`

---

## 1. Principios

1. **El estado de máquina no se duplica.** CPU, PPU, APU, timer, DMA, serial,
   gamepad, WRAM/HRAM, IO, cartucho/MBC/RTC, PhaseClock y boot ROM viven en el
   blob del core.
2. **El contenedor es de la GUI.** Rutas, fechas, miniaturas, slots y
   diagnósticos no entran en el blob del core.
3. **Binario estable y little-endian.** Sin `struct`, padding, punteros ni
   `size_t`.
4. **Lectura defensiva.** Se validan magic, versión, tamaños, CRC y límites
   antes de aceptar nada.
5. **Escritura atómica.** Fichero temporal + `rename`; un fallo a mitad de
   escritura no rompe el estado anterior.
6. **Compatibilidad.** Los `.srN` antiguos que contienen solo el blob del core
   (`QTGBST01`) se siguen cargando.

---

## 2. Layout

```text
[header 84 B][metadata][preview opcional][blob del core]
```

`payload_size = metadata_size + preview_size + core_size`.

### 2.1 Cabecera (84 bytes)

| Offset | Tamaño | Campo | Notas |
|---:|---:|---|---|
| 0 | 8 | `magic` | ASCII `QTGBSTF1` |
| 8 | 2 | `container_version` | `1` |
| 10 | 2 | `header_size` | `84` |
| 12 | 2 | `core_format_version` | Copia de `STATE_FORMAT_VERSION` |
| 14 | 2 | `core_schema_version` | Copia de `STATE_SCHEMA_VERSION` |
| 16 | 8 | `created_unix_seconds` | UTC, segundos desde epoch |
| 24 | 8 | `emulated_frames` | Frames completados al guardar |
| 32 | 4 | `slot` | `0..9` |
| 36 | 1 | `kind` | `0` manual, `1` rápido, `2` automático |
| 37 | 1 | `flags` | bit 0 preview, bit 1 BIOS configurada, bit 2 boot ROM activo |
| 38 | 2 | `reserved` | Cero |
| 40 | 4 | `rom_size` | Tamaño de la imagen ROM |
| 44 | 8 | `rom_hash` | FNV-1a 64 igual que el core |
| 52 | 4 | `bios_size` | 0 si no hay BIOS configurada |
| 56 | 8 | `bios_hash` | FNV-1a 64 de la BIOS |
| 64 | 4 | `metadata_size` | Bytes del bloque de metadatos |
| 68 | 4 | `preview_size` | Bytes del bloque de miniatura |
| 72 | 4 | `core_size` | Bytes del blob del core |
| 76 | 4 | `payload_crc32` | CRC-32 de metadata+preview+core |
| 80 | 4 | `header_crc32` | CRC-32 de los bytes `[0, 80)` |

### 2.2 Metadatos

Campos en orden, sin padding:

| Campo | Tipo |
|---|---|
| `rom_title` | `u16` longitud + bytes (máx. 64) |
| `rom_filename` | `u16` longitud + bytes (máx. 1024) |
| `app_version` | `u16` longitud + bytes (máx. 256) |
| `note` | `u16` longitud + bytes (máx. 4096) |
| `mapper` | `u8` |
| `cgb_flag` | `u8` |
| `cart_type` | `u8` |
| `header_checksum` | `u8` |
| `global_checksum` | `u16` |

`app_version` sale de `git describe --tags --always --dirty` en tiempo de
compilación (`QTREGB_BUILD_VERSION`). `note` queda vacío en la fase 1.

### 2.3 Miniatura

| Campo | Tipo | Notas |
|---|---|---|
| `width` | `u16` | 160 en la fase 1 |
| `height` | `u16` | 144 en la fase 1 |
| `format` | `u8` | `0` = RGBA8888 |
| `reserved` | 3 × `u8` | Cero |
| `pixels` | `width × height × 4` | R, G, B, A por píxel, fila superior primero |

La miniatura se guarda **sin comprimir** (92.160 bytes para 160×144). Es el
formato directo que usa `DisplayProcessor` para texturas, así que no hace falta
un decodificador de imágenes. La compresión genérica del contenedor queda para
la fase 2.

Si no hay frame renderizado todavía, `preview_size = 0` y el flag de preview
queda a cero.

### 2.4 Blob del core

Bytes restantes del fichero. Su cabecera interna (`QTGBST01`) mantiene sus
propios CRC y la comprobación de ROM/BIOS.

---

## 3. Validación

Al cargar:

1. tamaño mínimo y magic;
2. `container_version`, `header_size`, format/schema del core;
3. CRC de cabecera;
4. suma de tamaños y tamaño exacto del fichero;
5. CRC del payload;
6. longitudes y límites de strings, preview y blob;
7. `slot` en `0..9` y `kind` en `0..2`.

Si algo falla, no se modifica el estado actual de la máquina.

Límites actuales: preview ≤ 64 MiB, blob del core ≤ 64 MiB, payload ≤ 128 MiB,
fichero ≤ 192 MiB.

---

## 4. Compatibilidad

Un fichero que empieza por el magic del core (`QTGBST01`) se trata como un
`.srN` antiguo: se pasa tal cual a `QtreGB::load_state()` y los metadatos quedan
vacíos. Al volver a guardar se escribe el contenedor nuevo.

---

## 5. Tamaño del contenedor sin PNG (medido)

| ROM | Blob del core | Miniatura | Contenedor |
|---|---:|---:|---:|
| Tetris | 19.104 B | 92.160 B | **111.420 B** (108,8 KiB) |
| Pokémon Blue | 52.029 B | 92.160 B | **144.325 B** (140,9 KiB) |

La miniatura domina el tamaño en cartuchos sin RAM grande. En cartuchos con
128 KiB de RAM el blob crece y la proporción cambia. A estos bytes se suman
los del PNG que envuelve el contenedor (unos pocos KiB).

---

## 6. RTC y batería

- **RTC:** un savestate es una foto. Al cargar, el RTC de MBC3 se ancla al
  instante de carga y continúa desde el valor guardado; no se pone al día con
  el tiempo real. Es el comportamiento recomendado para reproducibilidad.
  El bloque RTC del `.sav` sigue siendo el que avanza con el tiempo real entre
  sesiones.
- **Batería:** este contenedor no sincroniza el `.sav`. El guardado periódico
  atómico de la batería es un trabajo de fase 2.

---

## 7. Variante Link (`.qss.png`, versión 2)

El contenedor de sesión reutiliza el mismo esquema de cabecera fija + CRC +
payload + escritura atómica, pero mantiene la estructura propia de `.qss`.

### 7.1 Cabecera fija (28 bytes)

| Offset | Tamaño | Campo |
|---:|---:|---|
| 0 | 8 | magic `QTRSES01` |
| 8 | 2 | `version` = `2` |
| 10 | 2 | `flags` (bit 0 sesión conectada, bit 1 preview) |
| 12 | 4 | `header_size` |
| 16 | 4 | `header_crc32` |
| 20 | 4 | `payload_size` |
| 24 | 4 | `payload_crc32` |

### 7.2 Header v2

En orden:

1. ROM 1: `name`, `path`, `size`, `hash`.
2. ROM 2: `name`, `path`, `size`, `hash`.
3. `bios_path`.
4. `bios_hash`.
5. `bios_size` (`u32`).
6. `created_unix_seconds` (`u64`).
7. `emulated_frames` (`u64`).
8. `app_version` (string).

Las cadenas usan `u32` longitud + bytes, como en la versión 1.

### 7.3 Payload v2

```text
u32 preview_size
[preview: u16 width, u16 height, u8 format=0, 3 reserved, RGBA pixels]
u32 state0_size + state0
u32 state1_size + state1
```

- La preview es exactamente la imagen combinada que se está mostrando:
  320×144, máquina 1 a la izquierda y máquina 2 a la derecha.
- Se guarda como RGBA8888 sin comprimir (184.320 bytes).
- Si todavía no hay frames de ambas máquinas, `preview_size = 0` y el flag
  de preview queda a cero.
- El CRC de payload cubre preview + los dos blobs.

### 7.4 Compatibilidad

- Los `.qss` versión 1 se siguen cargando: header sin los campos nuevos,
  payload sin preview. Los campos nuevos quedan a cero/vacíos.
- Los `.qss` se guardan siempre en versión 2 y con la extensión `.qss.png`.

### 7.5 Tamaño medido

Sesión Tetris + Pokémon Blue:

| Parte | Bytes |
|---|---:|
| Blob P1 | 19.104 |
| Blob P2 | 52.029 |
| Preview 320×144 | 184.320 |
| Metadatos y CRCs | ~250 |
| **Total `.qss`** | **255.700 B** (249,7 KiB) |

La preview combinada domina el tamaño; la compresión genérica queda para una
fase posterior.


---

## 8. Envoltura PNG

Los savestates nuevos se guardan con extensión `.png`:

- `<rom>.sr<slot>.png` para un jugador.
- `Game1 - Game2 (short-hash).qss.png` para una sesión Link.

Estructura del fichero:

```text
[PNG válido: vista previa de la pantalla]
[contenedor descrito en las secciones anteriores]
```

- El PNG se genera con `stb_image_write` y contiene la misma imagen que la
  miniatura guardada en el contenedor.
- El contenedor empieza inmediatamente después del chunk `IEND`.
- Al cargar, `png_payload_offset()` localiza el final de `IEND` y el lector
  procesa el contenedor desde ahí.
- Los gestores de archivos muestran el fichero como una imagen normal, porque
  empieza por la firma PNG y tiene extensión `.png`; el contenido adicional
  después de `IEND` se ignora al dibujar la miniatura.
- Si no hay preview disponible, se escribe un PNG de relleno oscuro para que
  el fichero siga siendo una imagen válida.
- Los ficheros antiguos (`<rom>.srN`, `.qss`) se siguen cargando: si el nuevo
  `.png` no existe, se prueba la ruta heredada.

Tamaños medidos con la envoltura PNG:

| Fichero | Bytes |
|---|---:|
| Tetris `.sr0.png` | **115.432 B** (112,7 KiB) |
| Tetris + Pokémon Blue `.qss.png` | **260.685 B** (254,6 KiB) |

El PNG añade unos pocos KiB porque `stb_image_write` comprime la imagen; el
contenedor interno sigue llevando su copia RGBA para que el navegador de
estados no dependa de un decodificador PNG.
