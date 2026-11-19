# Core savestate format — schema 5 (core completo)

Fecha: 2026-11-17
Ámbito: `core/` exclusivamente.

Este documento define el **estado de máquina** y el **formato binario** que
produce y consume `QtreGB::save_state()` / `QtreGB::load_state()`. El
contenedor de usuario (metadatos, miniaturas, compresión, slots, rewind) es
responsabilidad de la GUI y no forma parte de este formato.

Implementación:

- `core/inc/state_stream.h`
- `core/src/state_stream.cpp`
- `tests/test_state_stream.cpp`
- `tests/test_savestate.cpp`

---

## 1. Principios

1. **Core y GUI separados.** El core guarda solo estado del emulador. No hay
   rutas, fechas, nombres de slot, miniaturas, compresión ni historial de
   rewind.
2. **Determinismo.** Se guarda todo lo que pueda afectar a la ejecución
   futura. Lo derivado, lo de solo salida y lo que pertenece al host no se
   guarda.
3. **Binario estable.** Little-endian, tipos de ancho fijo, sin `struct` con
   padding, sin punteros, sin `size_t`.
4. **Lectura defensiva.** El lector valida cabecera, tamaños, checksums,
   tipos y longitudes antes de aplicar. Un estado inválido no debe dejar la
   máquina a medias.
5. **Versionado explícito.** `format_version` cambia si cambia el layout
   binario; `schema_version` cambia si cambia el conjunto de campos/chunks
   obligatorios.

---

## 2. Contenedor

### 2.1 Cabecera (36 bytes)

| Offset | Tamaño | Campo | Descripción |
|---:|---:|---|---|
| 0 | 8 | `magic` | ASCII `QTGBST01` |
| 8 | 2 | `format_version` | `u16` LE. Actualmente `1` |
| 10 | 2 | `schema_version` | `u16` LE. Actualmente `1` |
| 12 | 4 | `rom_size` | `u32` LE. Tamaño del ROM cargado |
| 16 | 8 | `rom_hash` | `u64` LE. FNV-1a 64 del ROM completo |
| 24 | 4 | `payload_size` | `u32` LE. Bytes de payload |
| 28 | 4 | `payload_crc32` | `u32` LE. CRC-32 del payload |
| 32 | 4 | `header_crc32` | `u32` LE. CRC-32 de los bytes `[0, 32)` |

Reglas:

- El fichero termina exactamente en `36 + payload_size`. Bytes extra o
  ausentes producen `SizeMismatch` / `Truncated`.
- `payload_crc32` cubre únicamente el payload.
- `header_crc32` cubre la cabecera sin incluirse a sí misma.
- Si no hay cartucho cargado, `rom_size = 0` y `rom_hash` es el FNV-1a 64 de
  una secuencia vacía (`0xCBF29CE484222325`).

### 2.2 CRC-32

CRC-32/ISO-HDLC (el habitual de zip/PNG):

- polinomio reflejado `0xEDB88320`;
- valor inicial `0xFFFFFFFF`;
- XOR final `0xFFFFFFFF`;
- sin tabla, bit a bit, para evitar estado global.

### 2.3 Hash de ROM

FNV-1a de 64 bits sobre la imagen ROM completa:

- offset basis `0xCBF29CE484222325`;
- primo `0x100000001B3`;
- un `xor` y una multiplicación por byte.

El hash **no** es un checksum de integridad del savestate; sirve para atar el
estado a la ROM con la que se creó. Un estado con otro ROM devuelve
`WrongRom` y no se aplica.

---

## 3. Entradas TLV

Cada entrada del payload tiene esta cabecera de 7 bytes:

| Offset | Tamaño | Campo |
|---:|---:|---|
| 0 | 2 | `tag` (`u16` LE) |
| 2 | 1 | `type` (`u8`) |
| 3 | 4 | `length` (`u32` LE) |
| 7 | `length` | valor |

Tipos:

| Código | Nombre | Longitud exigida |
|---:|---|---:|
| 1 | `U8` | 1 |
| 2 | `U16` | 2 |
| 3 | `U32` | 4 |
| 4 | `U64` | 8 |
| 5 | `I8` | 1 |
| 6 | `I16` | 2 |
| 7 | `I32` | 4 |
| 8 | `I64` | 8 |
| 9 | `F32` | 4 (IEEE-754, bits LE) |
| 10 | `F64` | 8 (IEEE-754, bits LE) |
| 11 | `Bool` | 1, solo `0` o `1` |
| 12 | `Bytes` | cualquiera |
| 13 | `Chunk` | cualquiera (entradas anidadas) |

Reglas:

- Los tags son **locales al chunk padre**. El mismo número puede usarse en
  chunks distintos.
- Los tags deben ser únicos dentro de su chunk. El lector se queda con el
  primero que encuentra y `find_chunk()` rechaza chunks de nivel superior
  duplicados.
- Un chunk es una entrada `Chunk` cuyo valor es otra secuencia de entradas.
- Anidamiento máximo: 32 chunks abiertos.
- `Bytes` con longitud 0 es válido.
- `Bool` con un valor distinto de 0/1 produce `BadValue`.
- No se guardan punteros, `size_t`, `long`, `float` de plataforma ni structs
  con padding.

---

## 4. Semántica del lector

### 4.1 Construcción

`StateReader(data, size)` valida, en este orden:

1. tamaño mínimo de cabecera;
2. magic;
3. `format_version` exacta;
4. `schema_version` exacta;
5. CRC de cabecera;
6. `payload_size == size - 36`;
7. CRC del payload.

Cualquier fallo deja el lector en error y no se lee ningún campo.

### 4.2 Búsqueda de campos

- `read_*()` busca el tag desde el cursor actual hacia delante, saltando
  entradas que no coinciden.
- Si el tag no existe antes del final del chunk: `MissingField`.
- Si existe con otro tipo: `BadType`.
- Si existe con otra longitud: `BadLength`.
- Los campos desconocidos se saltan usando su longitud, por lo que un campo
  nuevo en un schema posterior no desalinea a un lector antiguo que no lo
  conoce (siempre que el schema esté permitido).
- Los campos desconocidos al final de un chunk se descartan en
  `end_chunk()`.

### 4.3 Chunks

- `find_chunk(tag)`: busca un chunk de **nivel superior** desde el inicio del
  payload. No depende del orden de escritura. Rechaza duplicados del mismo
  tag con `BadValue`. Es el método que usa la máquina para cargar
  componentes.
- `begin_chunk(tag)`: búsqueda secuencial desde el cursor. Se usa para chunks
  anidados (por ejemplo los canales del APU).
- `end_chunk()`: exige que el cursor no haya superado el final del chunk y
  salta los campos desconocidos restantes.
- `finish()`: exige que no queden chunks abiertos, que todas las entradas de
  nivel superior sean chunks y salta los chunks desconocidos. Permite que un
  estado futuro añada componentes sin romper a un lector que no los conoce.

### 4.4 Carga transaccional

`QtreGB::load_state()`:

1. valida cabecera, checksums y ROM;
2. guarda un blob de seguridad con el estado actual;
3. aplica Clock, CPU, Timer, APU, memoria, bus, IO, Serial, DMA, Gamepad,
   cartucho, PPU y LCD;
4. si cualquier componente falla, reaplica el blob de seguridad;
5. devuelve el error original.

Además, cada componente lee en locales o en una copia y solo confirma al
final, así que un fallo interno tampoco lo deja a medias.

---

## 5. Errores

| Error | Significado |
|---|---|
| `Ok` | Sin error |
| `BadMagic` | No empieza por `QTGBST01` |
| `UnsupportedFormat` | `format_version` distinta |
| `UnsupportedSchema` | `schema_version` distinta |
| `HeaderChecksum` | CRC de cabecera incorrecto |
| `PayloadChecksum` | CRC del payload incorrecto |
| `Truncated` | Faltan bytes para cabecera o campo |
| `SizeMismatch` | `payload_size` no coincide con el tamaño real |
| `BadType` | Tipo distinto del esperado |
| `BadLength` | Longitud distinta de la esperada o fuera del chunk |
| `BadValue` | Valor semánticamente inválido (bool, enum, rango) |
| `MissingField` | Campo obligatorio ausente |
| `MissingChunk` | Chunk obligatorio ausente |
| `UnbalancedChunk` | `end_chunk()`/`finish()` con chunks abiertos |
| `WrongRom` | `rom_size`/`rom_hash` no coinciden |
| `WriterMisuse` | `end_chunk()` sin chunk o `finish()` con chunk abierto |
| `TooLarge` | Longitud o anidamiento fuera de límite |

---

## 6. Chunks de nivel superior

| Tag | Componente | Schema 5 |
|---:|---|---|
| `0x0100` | CPU | Implementado |
| `0x0101` | Timer | Implementado |
| `0x0102` | APU | Implementado |
| `0x0103` | Memoria (WRAM/HRAM) | Implementado |
| `0x0104` | Bus | Implementado |
| `0x0105` | Cartucho/MBC/RTC | Implementado |
| `0x0106` | PPU | Implementado |
| `0x0107` | LCD (espejo/debug) | Implementado |
| `0x0108` | DMA | Implementado |
| `0x0109` | Gamepad | Implementado |
| `0x010A` | Interrupciones | Sin payload propio |
| `0x010B` | PhaseClock | Implementado |
| `0x010C` | IO | Implementado |
| `0x010D` | Locks de bus | Dentro del chunk PPU |
| `0x010E` | Serial | Implementado |

Orden de escritura actual: Clock, CPU, Timer, APU, Memoria, Bus, IO, Serial,
DMA, Gamepad, Cartucho, PPU, LCD. El lector no depende del orden.

---

## 7. Schema 5 — campos implementados

### 7.1 CPU (`0x0100`)

| Tag | Campo | Tipo | Notas |
|---:|---|---|---|
| `0x0001` | `a` | U8 | |
| `0x0002` | `f` | U8 | |
| `0x0003` | `b` | U8 | |
| `0x0004` | `c` | U8 | |
| `0x0005` | `d` | U8 | |
| `0x0006` | `e` | U8 | |
| `0x0007` | `h` | U8 | |
| `0x0008` | `l` | U8 | |
| `0x0009` | `pc` | U16 | |
| `0x000A` | `sp` | U16 | |
| `0x000B` | `ime` | U8 | `0..3` |
| `0x000C` | `ie` | U8 | |
| `0x000D` | `if` | U8 | |
| `0x000E` | `is_halted` | Bool | |
| `0x000F` | `interrupt_state` | U8 | `0..2` |
| `0x0010` | `interrupt_remaining` | U8 | |
| `0x0011` | `phase` | U8 | `0..3` (T-cycle) |
| `0x0012` | `fetching` | Bool | |
| `0x0013` | `fetching_cb` | Bool | |
| `0x0014` | `opcode` | U8 | |
| `0x0015` | `cb_opcode` | U8 | |
| `0x0016` | `io_data` | U8 | |
| `0x0017` | `end_opcode` | Bool | |
| `0x0018` | `unimplemented` | Bool | |
| `0x0019` | `read_pending` | Bool | |
| `0x001A` | `read_addr` | U16 | |
| `0x001B` | `write_pending` | Bool | |
| `0x001C` | `write_addr` | U16 | |
| `0x001D` | `write_value` | U8 | |
| `0x001E` | `addr_lo` | U8 | |
| `0x001F` | `addr_hi` | U8 | |
| `0x0020` | `push_value` | U16 | |
| `0x0021` | `pop_low` | U8 | |
| `0x0022` | `microop_index` | U8 | |
| `0x0023` | `instruction_kind` | U8 | `0` none, `1` table, `2` CB table, `3` ISR |

`m_instruction` es un puntero a una tabla estática y **no se serializa**. Se
guarda su identidad como `instruction_kind` y se reconstruye:

- `1` → `&table()[opcode]`
- `2` → `&cb_table()[cb_opcode]`
- `3` → `isr_table()`
- `0` → `nullptr`

El índice de micro-op se conserva, de modo que el estado se puede capturar a
mitad de instrucción (incluida la ventana de prefijo CB), no solo en un
límite de instrucción.

Validaciones al cargar: `ime <= 3`, `phase <= 3`, `interrupt_state <= 2`,
`instruction_kind <= 3`, índice dentro de la tabla y coherencia entre
`instruction_kind == 3` y `interrupt_state == Serving`.

### 7.2 Timer (`0x0101`)

| Tag | Campo | Tipo | Notas |
|---:|---|---|---|
| `0x0001` | `div` | U16 | Divisor interno de 16 bits |
| `0x0002` | `tima` | U8 | |
| `0x0003` | `tma` | U8 | |
| `0x0004` | `tac` | U8 | Solo bits 0-2 |
| `0x0005` | `timer_state` | U8 | `0` none, `1` reload, `2` pending |
| `0x0006` | `last_div_signal` | Bool | |

El callback de interrupción no se serializa: es cableado del host.

### 7.3 APU (`0x0102`)

Campos directos:

| Tag | Campo | Tipo |
|---:|---|---|
| `0x0001` | `nr10` | U8 |
| `0x0002` | `nr11` | U8 |
| `0x0003` | `nr12` | U8 |
| `0x0004` | `nr13` | U8 |
| `0x0005` | `nr14` | U8 |
| `0x0006` | `nr21` | U8 |
| `0x0007` | `nr22` | U8 |
| `0x0008` | `nr23` | U8 |
| `0x0009` | `nr24` | U8 |
| `0x000A` | `nr30` | U8 |
| `0x000B` | `nr31` | U8 |
| `0x000C` | `nr32` | U8 |
| `0x000D` | `nr33` | U8 |
| `0x000E` | `nr34` | U8 |
| `0x000F` | `nr41` | U8 |
| `0x0010` | `nr42` | U8 |
| `0x0011` | `nr43` | U8 |
| `0x0012` | `nr44` | U8 |
| `0x0013` | `nr50` | U8 |
| `0x0014` | `nr51` | U8 |
| `0x0020` | `wave_ram` | Bytes (16) |
| `0x0021` | `powered` | Bool |
| `0x0022` | `last_div` | U16 |
| `0x0023` | `frame_step` | U8 (`0..7`) |
| `0x0024` | `apu_phase` | U8 (`0..3`) |
| `0x0025` | `cycle` | U32 |
| `0x0026` | `sample_acc` | U32 |
| `0x0027` | `hpf_left` | F32 |
| `0x0028` | `hpf_right` | F32 |

Canales (chunks anidados):

| Tag | Canal |
|---:|---|
| `0x0100` | CH1 (pulse + sweep) |
| `0x0101` | CH2 (pulse) |
| `0x0102` | CH3 (wave) |
| `0x0103` | CH4 (noise) |

#### Pulse (CH1/CH2)

| Tag | Campo | Tipo |
|---:|---|---|
| `0x0001` | `enabled` | Bool |
| `0x0002` | `duty` | U8 |
| `0x0003` | `duty_step` | U8 |
| `0x0004` | `frequency` | U16 |
| `0x0005` | `timer` | I32 |
| `0x0006` | `trigger_delay` | U8 |
| `0x0007` | `volume` | U8 |
| `0x0008` | `initial_volume` | U8 |
| `0x0009` | `env_period` | U8 |
| `0x000A` | `env_timer` | U8 |
| `0x000B` | `env_reloaded` | Bool |
| `0x000C` | `env_pending` | Bool |
| `0x000D` | `env_increase` | Bool |
| `0x000E` | `length` | U16 |
| `0x000F` | `length_enable` | Bool |
| `0x0010` | `shadow_frequency` | U16 |
| `0x0011` | `sweep_period` | U8 |
| `0x0012` | `sweep_shift` | U8 |
| `0x0013` | `sweep_timer` | U8 (`0..8`) |
| `0x0014` | `sweep_decrease` | Bool |
| `0x0015` | `sweep_enabled` | Bool |
| `0x0016` | `sweep_negate_used` | Bool |

#### Wave (CH3)

| Tag | Campo | Tipo |
|---:|---|---|
| `0x0001` | `enabled` | Bool |
| `0x0002` | `frequency` | U16 |
| `0x0003` | `timer` | I32 |
| `0x0004` | `trigger_delay` | U8 |
| `0x0005` | `sample_index` | U8 |
| `0x0006` | `sample` | U8 |
| `0x0007` | `level` | U8 |
| `0x0008` | `length` | U16 |
| `0x0009` | `length_enable` | Bool |
| `0x000A` | `sampled_byte` | U8 |
| `0x000B` | `window_ttl` | U8 |
| `0x000C` | `last_sample_cycle` | U32 |
| `0x000D` | `sample_valid` | Bool |

`access_window` y `access_byte` son estado derivado del T-cycle actual: no se
serializan y se recalculan en `prepare_tick()`.

#### Noise (CH4)

| Tag | Campo | Tipo |
|---:|---|---|
| `0x0001` | `enabled` | Bool |
| `0x0002` | `timer` | I32 |
| `0x0003` | `lfsr` | U16 |
| `0x0004` | `short_mode` | Bool |
| `0x0005` | `volume` | U8 |
| `0x0006` | `initial_volume` | U8 |
| `0x0007` | `env_period` | U8 |
| `0x0008` | `env_timer` | U8 |
| `0x0009` | `env_reloaded` | Bool |
| `0x000A` | `env_pending` | Bool |
| `0x000B` | `env_increase` | Bool |
| `0x000C` | `length` | U16 |
| `0x000D` | `length_enable` | Bool |
| `0x000E` | `clock_shift` | U8 |
| `0x000F` | `divisor_code` | U8 |

El **ring PCM** (`m_ring`, `m_ring_read/write/count`) es solo etapa de
salida: no se serializa. Al cargar se vacía para no reproducir audio viejo.

### 7.4 PhaseClock (`0x010B`)

| Tag | Campo | Tipo | Notas |
|---:|---|---|---|
| `0x0001` | `phase` | U8 | `0..3` |

`QtreGB::tick()` elige `tick_t0..tick_t3` según esta fase. Sin este chunk,
cargar en una fase distinta ejecutaría otro tick del M-cycle y el estado
divergiría aunque CPU, Timer y APU fueran correctos.

### 7.5 Memoria (`0x0103`)

| Tag | Campo | Tipo | Notas |
|---:|---|---|---|
| `0x0001` | `wram` | Bytes | 8192 B (`0xC000-0xDFFF`) |
| `0x0002` | `hram` | Bytes | 128 B; el bus mapea `0xFF80-0xFFFE` |

Echo RAM (`0xE000-0xFDFF`) es un espejo de WRAM: no se duplica. Al cargar se
exige el tamaño exacto de cada vector; si no coincide, `BadLength`.

### 7.6 Bus (`0x0104`)

| Tag | Campo | Tipo | Notas |
|---:|---|---|---|
| `0x0001` | `boot_rom_enabled` | Bool | |
| `0x0002` | `boot_rom_hash` | U64 | FNV-1a 64 de los 256 bytes |

El contenido del boot ROM es una entrada del host. No se incrusta en el
savestate: si el estado se creó con el boot ROM activo y el host no tiene el
mismo (mismo hash), la carga falla con `BadValue`. Esto evita distribuir la
ROM de arranque de Nintendo dentro de los estados. Si el estado tiene el boot
ROM desactivado, el hash no se exige.

### 7.7 IO (`0x010C`)

| Tag | Campo | Tipo |
|---:|---|---|
| `0x0001` | `sb` (FF01) | U8 |
| `0x0002` | `sc` (FF02) | U8 |

El resto de registros de IO los poseen Timer, APU, PPU y CPU. El contador
`static ly` de `io.cpp` es una ruta muerta de debug y no forma parte del
estado.

> SB/SC son los registros del puerto serie. El estado de transferencia
> (activo, bits restantes y contador de clock) está en el chunk `Serial`; el
> `LinkCable` que conecta dos máquinas es externo al estado de una sola
> máquina.

### 7.8 DMA (`0x0108`)

| Tag | Campo | Tipo | Notas |
|---:|---|---|---|
| `0x0001` | `active` | Bool | |
| `0x0002` | `pending` | Bool | |
| `0x0003` | `byte` | U8 | `0..0xA0` |
| `0x0004` | `value` | U8 | Fuente `XX00` |
| `0x0005` | `start_delay` | U8 | `0..2` |

Validaciones: `start_delay <= 2`, `byte <= 0xA0`, `active` implica
`pending`. Al cargar se actualiza el flag `m_dma_active` del PPU. Los
punteros `m_bus`/`m_ppu` son cableado y no se serializan.

### 7.9 Gamepad (`0x0109`)

| Tag | Campo | Tipo |
|---:|---|---|
| `0x0001` | `button_start` | Bool |
| `0x0002` | `button_select` | Bool |
| `0x0003` | `button_b` | Bool |
| `0x0004` | `button_a` | Bool |
| `0x0005` | `button_down` | Bool |
| `0x0006` | `button_up` | Bool |
| `0x0007` | `button_left` | Bool |
| `0x0008` | `button_right` | Bool |
| `0x0009` | `select_directions` | Bool |
| `0x000A` | `select_buttons` | Bool |

Los botones y la selección están inicializados en el constructor por defecto
para que dos ejecuciones idénticas produzcan el mismo estado.

### 7.10 Interrupciones (`0x010A`)

`IF` e `IE` viven en la CPU y ya se serializan allí. `InterruptController`
solo contiene callbacks de host, por lo que este chunk no necesita payload.

### 7.11 Cartucho / MBC / RTC (`0x0105`)

Chunk `Cart`:

| Tag | Campo | Tipo | Notas |
|---:|---|---|---|
| `0x0001` | `mapper` | U8 | `0` none, `1` No-MBC, `2` MBC1, `3` MBC2, `4` MBC3, `5` MBC5 |
| `0x0002` | `data` | Chunk | Estado específico del mapper |

Tags dentro de `data` (locales al chunk):

| Tag | Campo | Tipo | Mappers |
|---:|---|---|---|
| `0x0001` | `ram_enable` | Bool | MBC1/MBC2/MBC3/MBC5 |
| `0x0002` | `rom_bank` | U8 / U16 | MBC1/MBC2/MBC3 (U8); MBC5 (U16) |
| `0x0003` | `ram_bank` | U8 | MBC1 (`m_rom_ram_bank`), MBC5 |
| `0x0004` | `banking_mode` | Bool | MBC1 |
| `0x0005` | `mbc1m` | Bool | MBC1 (variante detectada) |
| `0x0003` | `selected` | U8 | MBC3 |
| `0x0004` | `latch_armed` | Bool | MBC3 |
| `0x0005` | `mbc30` | Bool | MBC3 (variante detectada) |
| `0x0006` | `has_rtc` | Bool | MBC3 (derivado del header) |
| `0x0010` | RAM | Bytes | No-MBC/MBC1/MBC2/MBC5 |
| `0x0040` | RAM | Bytes | MBC3 |

MBC3 usa tags propios para el RTC actual y el latcheado:

| Tag | Campo | Tipo |
|---:|---|---|
| `0x0010` | RTC `seconds` | U8 (`0..63`) |
| `0x0011` | RTC `minutes` | U8 (`0..63`) |
| `0x0012` | RTC `hours` | U8 (`0..31`) |
| `0x0013` | RTC `days` | U16 (`0..511`) |
| `0x0014` | RTC `halt` | Bool |
| `0x0015` | RTC `carry` | Bool |
| `0x0020` | Latched `seconds` | U8 |
| `0x0021` | Latched `minutes` | U8 |
| `0x0022` | Latched `hours` | U8 |
| `0x0023` | Latched `days` | U16 |
| `0x0024` | Latched `halt` | Bool |
| `0x0025` | Latched `carry` | Bool |
| `0x0030` | `subsecond` | U32 (`0..999999`) |

La RAM del cartucho se guarda como un único `Bytes` con todos los bancos
concatenados en orden. Al cargar se exige el tamaño total exacto.

Validaciones:

- `mapper` debe coincidir con el cartucho cargado; `None` solo vale para una
  máquina sin cartucho.
- MBC1: `mbc1m` debe coincidir con la detección actual, `rom_bank <= 31`,
  `ram_bank <= 3`.
- MBC2: `rom_bank <= 15`; RAM de 512 B.
- MBC3: `mbc30` y `has_rtc` deben coincidir con la detección actual;
  `rom_bank <= 0x7F` salvo MBC30; `selected` válido (`0x00-0x03` o
  `0x00-0x07` en MBC30, `0x08-0x0C`, `0xFF`); rangos de RTC y
  `subsecond <= 999999`.
- MBC5: `rom_bank <= 0x1FF`; `ram_bank <= 0x0F` (el bit 3 es rumble en las
  variantes `$1C-$1E`).

**Política de RTC:** se guardan los contadores y el subsegundo tal como están
en el estado interno (no se consulta el reloj del host al guardar), pero
**no** el epoch (`m_last_time`). Al cargar, `m_last_time` se rebasa al reloj
actual: el tiempo transcurrido entre guardar y cargar no se aplica. Si el RTC
estaba halted se mantiene halted; al quitar halt vuelve a contar desde el
instante de carga. El `.sav` de batería es un formato distinto y conserva su
propia política de tiempo.

---

### 7.12 PPU (`0x0106`)

El PPU se guarda en el estado exacto por dot: registros, latches DMG,
máquina de estados, OAM scan, fetchers, FIFOs, memorias y locks de bus.

Registros y estado de la máquina:

| Tag | Campo | Tipo |
|---:|---|---|
| `0x0001` | LCDC empaquetado (bits 7-0) | U8 |
| `0x0002` | STAT empaquetado (bit 7 ignorado al cargar) | U8 |
| `0x0003` | modo real (`m_mode`) | U8 (`0..3`) |
| `0x0004` | SCY | U8 |
| `0x0005` | SCX | U8 |
| `0x0006` | LY | U8 |
| `0x0007` | LYC | U8 |
| `0x0008` | DMA | U8 |
| `0x0009` | BGP | U8 |
| `0x000A` | OBP0 | U8 |
| `0x000B` | OBP1 | U8 |
| `0x000C` | WY | U8 |
| `0x000D` | WX | U8 |
| `0x000E` | selector de tick | U8 (ID estable) |
| `0x000F` | selector del fetcher | U8 (ID estable) |
| `0x0010` | dots | U16 (`0..455`) |
| `0x0011` | LX | U8 (`0..168`) |
| `0x0012` | `last_stat_irq` | Bool |
| `0x0013` | `enable_lyc_eq_ly_irq` | Bool |
| `0x0014` | `pending_stat_irq` | Bool |
| `0x0015` | `last_bgp` | U8 |
| `0x0016` | `last_wx` | U8 |
| `0x0017` | `last_ly` | U8 |
| `0x0018` | `last_lyc` | U8 |
| `0x0019` | `last_lcdc` empaquetado | U8 |
| `0x001A` | `pending_stat.countdown` | U8 (`0..1`) |
| `0x001B` | `pending_stat.value` | U8 |
| `0x001C` | `is_glitched_line_0` | Bool |
| `0x001D` | `glitched_line_0_hblank_delay` | U8 |
| `0x001E` | `vblank_last_line_stat_mode_hblank_glitch` | Bool |

OAM scan:

| Tag | Campo | Tipo |
|---:|---|---|
| `0x0020` | `oam_scan.count` | U8 (`0..10`) |
| `0x0021` | `oam_scan.index` | U8 (`0..40`) |
| `0x0022` | `oam_scan.request` | U8 (`0..40`) |
| `0x0023` | `oam_scan_registers.m_y` | U8 |
| `0x0024` | `oam_scan_registers.m_x` | U8 |
| `0x0025` | `oam_scan_registers.m_tile` | U8 |
| `0x0026` | `oam_scan_registers.m_flags` | U8 |

Ventana y transferencia:

| Tag | Campo | Tipo |
|---:|---|---|
| `0x0030` | `w_active_for_frame` | Bool |
| `0x0031` | `w_wly` | U8 |
| `0x0032` | `w_activating` | Bool |
| `0x0033` | `w_active` | Bool |
| `0x0034` | `w_just_activated` | Bool |
| `0x0035` | `wf_tilemap_x` | U8 |
| `0x0036` | `initial_scx.to_discard` | U8 (`0..7`) |
| `0x0037` | `initial_scx.discarded` | U8 (`0..7`) |
| `0x0038` | `bwf.lx` | U16 |
| `0x0039` | `bwf.tilemap_tile_vram_addr` | U16 (`<= 0x1FFF`) |
| `0x003A` | `bwf.tile_number` | U8 |
| `0x003B` | `bwf.interrupted_fetch_has_data` | Bool |
| `0x003C` | `bwf.interrupted_tile_data_low` | U8 |
| `0x003D` | `bwf.interrupted_tile_data_high` | U8 |
| `0x003E` | `of.entry.y` | U8 |
| `0x003F` | `of.entry.x` | U8 (`< 168`) |
| `0x0040` | `of.tile_number` | U8 |
| `0x0041` | `of.attributes` | U8 |
| `0x0042` | `of.entry.number` | U8 (`< 40`) |
| `0x0044` | `psf.tile_data_vram_address` | U16 (`<= 0x1FFE`) |
| `0x0045` | `psf.tile_data_low` | U8 |
| `0x0046` | `psf.tile_data_high` | U8 |
| `0x0047` | `pixel_slice_fetcher_push_enabled` | Bool |
| `0x0048` | `pending_bg_fifo_fill` | Bool |
| `0x0049` | `fetching_sprite` | Bool |

Memorias, FIFOs y locks:

| Tag | Campo | Tipo | Formato |
|---:|---|---|---|
| `0x0050` | BG FIFO | Bytes | 1 byte por píxel (`color_index 0..3`) |
| `0x0051` | OBJ FIFO | Bytes | 4 bytes por píxel: color, atributos, número, X |
| `0x0052` | OAM lists | Bytes | 168 listas: count + `count` × (número, Y, X) |
| `0x0060` | VRAM | Bytes | 8192 B |
| `0x0061` | OAM RAM | Bytes | 160 B (Y, X, tile, flags por entrada) |
| `0x0062` | OAM bus read lock | Bool | |
| `0x0063` | OAM bus write lock | Bool | |
| `0x0064` | VRAM bus read lock | Bool | |
| `0x0065` | VRAM bus write lock | Bool | |

Empaquetados:

- **LCDC**: bit 7 enable, 6 win tile map, 5 win enable, 4 bg/win data,
  3 bg tile map, 2 obj size, 1 obj enable, 0 bg/win enable.
- **STAT**: bit 7 se ignora; bits 6-3 habilitaciones LYC/OAM/VBlank/HBlank,
  bit 2 LYC=LY, bits 1-0 `m_stat.mode`.
- **OAM lists**: count `0..10`; número de entrada `< 40`; X `< 168`.
- **BG/OBJ FIFOs**: tamaños máximos aceptados 256 y 64 píxeles.

Selectores de tick (IDs estables, no reutilizar):

```text
 1 oam_scan_even
 2 oam_scan_odd
 3 oam_scan_77
 4 oam_scan_done
 5 oam_scan_after_turn_on
 6 oam_scan_after_turn_on_78
 7 oam_scan_after_turn_on_79
 8 pixel_transfer_dummy_lx0
 9 pixel_transfer_discard_lx0
10 pixel_transfer_discard_lx0_wx0_scx
11 pixel_transfer_lx0
12 pixel_transfer_lx8
13 hblank
14 hblank_452
15 hblank_453
16 hblank_454
17 hblank_455
18 hblank_last_line
19 hblank_last_line_453
20 hblank_last_line_454
21 hblank_last_line_455
22 hblank_first_line_after_turn_on
23 vblank
24 vblank_453
25 vblank_454
26 vblank_455
27 vblank_last_line
28 vblank_last_line_2
29 vblank_last_line_3
30 vblank_last_line_4
31 vblank_last_line_7
32 vblank_last_line_453
33 vblank_last_line_454
34 vblank_last_line_455
```

Selectores del fetcher:

```text
 1 pixel_slice_fetcher_idle
 2 bgwin_prefetcher_get_tile_0
 3 bg_prefetcher_get_tile_0
 4 bg_prefetcher_get_tile_1
 5 bg_pixel_slice_fetcher_get_tile_data_low_0
 6 bg_pixel_slice_fetcher_get_tile_data_low_1
 7 bg_pixel_slice_fetcher_get_tile_data_high_0
 8 bg_pixel_slice_fetcher_get_tile_data_high_1
 9 win_prefetcher_get_tile_1
10 win_pixel_slice_fetcher_get_tile_data_low_0
11 win_pixel_slice_fetcher_get_tile_data_low_1
12 win_pixel_slice_fetcher_get_tile_data_high_0
13 win_pixel_slice_fetcher_get_tile_data_high_1
14 obj_prefetcher_get_tile_0
15 obj_prefetcher_get_tile_1
16 obj_pixel_slice_fetcher_get_tile_data_low_0
17 obj_pixel_slice_fetcher_get_tile_data_low_1
18 obj_pixel_slice_fetcher_get_tile_data_high_0
19 obj_pixel_slice_fetcher_get_tile_data_high_1_and_merge_with_obj_fifo
```

Los selectores son punteros a método en el código, pero se serializan como
estos IDs. Un ID desconocido al cargar devuelve `BadValue`; un selector no
registrado al guardar devuelve `WriterMisuse`.

Exclusiones del chunk PPU:

- `m_video_buffer`: salida derivada; no afecta a la ejecución futura.
- `m_dma_active`: lo restaura el chunk del DMA.
- `m_boot_rom`: solo influye en el reset; es configuración del host.
- `m_bus`, `m_lcd`, callbacks: cableado del host.

### 7.13 LCD (`0x0107`)

El LCD nativo es un espejo de debug/GUI; el PPU es la fuente de verdad. Se
serializa para conservar el objeto exacto y las escrituras diferidas. El
espejo no se actualiza en cada dot: `PPU::save_state()` lo refresca antes de
escribir los chunks PPU/LCD, y `PPU::sync_lcd()` permite refrescarlo bajo
demanda (tests y herramientas de depuración).

| Tag | Campo | Tipo |
|---:|---|---|
| `0x0001` | LCDC | U8 |
| `0x0002` | STAT | U8 |
| `0x0003` | SCY | U8 |
| `0x0004` | SCX | U8 |
| `0x0005` | LY | U8 |
| `0x0006` | LYC | U8 |
| `0x0007` | BGP | U8 |
| `0x0008` | OBP0 | U8 |
| `0x0009` | OBP1 | U8 |
| `0x000A` | WY | U8 |
| `0x000B` | WX | U8 |
| `0x0010` | `m_pending_stat` | U8 |
| `0x0011` | `m_pending_stat_value` | U8 |
| `0x0012` | `m_delayed_write_count` | U8 (`0..8`) |
| `0x0013` | escrituras diferidas | Bytes |

Cada escritura diferida son 3 bytes: dirección, valor y countdown. Las
direcciones válidas son `0,2,3,5,7,8,9,10,11`.

No se serializan `m_ppu_write_delay`, `m_reg_write_delay[]` ni
`m_bgp_write_delay_ly0`: son configuración del host leída de variables de
entorno al inicializar el LCD, no estado de emulación.

### 7.14 Serial (`0x010E`)

El puerto serie DMG usa SB/SC del chunk IO más este estado de transferencia:

| Tag | Campo | Tipo | Notas |
|---:|---|---|---|
| `0x0001` | `active` | Bool | Transferencia en curso |
| `0x0002` | `bits_remaining` | U8 | `0..8`; si `active`, `1..8` |
| `0x0003` | `clock_counter` | U16 | `0..512` T-cycles hasta el próximo bit |

Protocolo implementado:

- `SC.7` = transferencia solicitada/en curso; se limpia al completar.
- `SC.0` = clock select: `1` interno (master), `0` externo (slave).
- DMG: clock interno de 8192 Hz, 512 T-cycles por bit, 4096 por byte.
- SB se desplaza **MSB primero** (bit 7 sale, bit entrante por bit 0).
- Al completar 8 bits: `SC.7=0` y se pide `IF` bit 3 (Serial).
- Sin peer, el master recibe `0xFF` (línea flotante alta).
- El slave externo no avanza hasta que el `LinkCable` le entrega un flanco.
- El clock es independiente del Timer/DIV; no se avanza desde `timer_tick()`.

El `LinkCable` no se serializa en un estado de máquina: conecta dos `Serial`
externos. Un estado de sesión link contiene los dos estados de máquina y
reconecta el cable al cargar; cargar una sola máquina mientras está linkeada
puede dejar al slave esperando clock o al master completando con `0xFF`.

---

## 8. Cobertura y exclusiones

Con el schema 5 el core guarda **todo el estado de emulación** necesario para
continuar de forma determinista:

- Clock, CPU, Timer y APU;
- WRAM/HRAM, bus, IO, DMA y Gamepad;
- cartucho/MBC/RTC;
- PPU completo y espejo LCD.

Exclusiones deliberadas:

| Dato | Motivo |
|---|---|
| Ring PCM del APU | Etapa de salida; se vacía al cargar |
| `m_video_buffer` | Salida derivada; se regenera al renderizar |
| `m_dma_active` | Duplicado; lo restaura el chunk DMA |
| Contenido del boot ROM | Entrada del host; se guarda su hash |
| Imagen ROM | Entrada del host; se guarda tamaño + hash |
| `m_boot_rom` del PPU | Solo afecta al reset |
| Callbacks y punteros | Cableado del host |
| `m_frame_ready`, `m_errors` | Estado de staging/UI |
| `m_last_time` del RTC | Se rebasa al reloj actual (política congelada) |
| Delays del LCD | Configuración de host por variables de entorno |
| `LinkCable` / peer | Estado compartido de sesión; no hay cable en un estado de una máquina |

El estado se puede capturar en **cualquier dot**: CPU, Timer, APU y PPU
tienen estado intermedio suficiente. Para savestates de usuario se recomienda
igualmente el frame boundary por simplicidad. El framebuffer y el ring PCM no
se restauran; la GUI puede renderizar un frame nuevo tras cargar y gestionar
la continuidad de audio.

La GUI ya puede envolver este blob con su propio contenedor: metadatos,
miniaturas, compresión, slots y rewind quedan fuera del core.

### 8.1 Uso actual desde la GUI

- **F5/F8** actúan sobre el jugador 1 y el slot seleccionado:
  `<rom>.sr0` … `<rom>.sr9`.
- El jugador 2 no tiene savestates independientes. Con una sesión Link,
  F5/F8 usan un único `.qss` de sesión con los dos blobs y el cable.
- Los slots se cambian con los atajos globales `Next state slot` y
  `Previous state slot` (PageUp/PageDown por defecto), con vuelta circular
  0↔9.
- Al arrancar la aplicación o cargar un juego nuevo se selecciona el slot 0.
- Los archivos van junto a la ROM o en el directorio de savestates configurado
  en `Settings → Paths`.
- El `.srN` de un jugador es exactamente el blob descrito en este documento.
- El `.qss` de sesión es un contenedor de GUI: metadatos de ROM/BIOS, CRC y
  los dos blobs crudos.
- Al cargar, la GUI vacía la cola de audio y reaplica el input actual del
  host: evita botones pegados del estado y conserva teclas mantenidas. El
  framebuffer y el ring PCM se regeneran.
- Batería: jugador 1 usa `.sav`; jugador 2 usa `.sa2`.
- Un estado de otro ROM o de otro boot ROM se rechaza; la GUI muestra un
  aviso genérico.

## 9. Punto de captura

Para savestates de usuario se recomienda **frame boundary** (70224 T-cycles),
que es determinista y barato. No es una limitación del formato: CPU, Timer,
APU y PPU guardan su estado intermedio, así que se puede capturar en
cualquier T-cycle/dot, incluso a mitad de instrucción o de un fetch.

Para TAS/rewind exacto por dot no hace falta otro formato: basta capturar en
el dot deseado. El estado de máquina ya incluye PPU, DMA, bus, memoria y
cartucho.

---

## 10. Compatibilidad y versionado

- `format_version`: layout binario (cabecera, tipos, TLV). Un cambio
  incompatible la incrementa.
- `schema_version`: conjunto de chunks/campos obligatorios. Un componente
  nuevo o un campo obligatorio nuevo la incrementa.
- El lector actual exige coincidencia **exacta** de ambas versiones. Es la
  política adecuada mientras el emulador está en desarrollo: los estados
  pre-release son desechables.
- Cuando haya estados de usuario que conservar, se podrá aceptar
  `schema_version <= actual` y leer los campos nuevos como opcionales. Eso
  requiere una API de lectura opcional; no se implementa todavía.
- Los campos desconocidos de un chunk conocido se saltan. Los chunks
  desconocidos de nivel superior también. Esto permite avanzar el schema sin
  romper la lectura de estados antiguos cuando se active la compatibilidad
  hacia atrás.
- Los tags no se reutilizan ni se renumeran. Un campo nuevo se añade con un
  tag nuevo.

Plan de schemas:

| Schema | Contenido | Estado |
|---:|---|---|
| 1 | Clock + CPU + Timer + APU | Hecho |
| 2 | + WRAM/HRAM, Bus, IO, DMA, Gamepad | Hecho |
| 3 | + Cartucho/MBC/RTC | Hecho |
| 4 | + PPU/LCD | Hecho |
| 5 | + Serial | Hecho (estado completo del core) |

---

## 11. Checklist para añadir un componente

1. Elegir un `StateChunk` no usado.
2. Definir tags nuevos y estables en el `.cpp` del componente.
3. Implementar `save_state(StateWriter&) const` y
   `load_state(StateReader&)`.
4. Leer en locales/copia y confirmar solo al final.
5. Validar rangos de enums y valores imposibles con `mark_error(BadValue)`.
6. No serializar punteros, callbacks ni buffers de salida.
7. Añadir round-trip y determinismo en `tests/test_savestate.cpp`.
8. Actualizar este documento y subir `schema_version`.
9. Ejecutar `make run_tests`, `make run_mts`, `make run_blargg`,
   `make run_regress` y el harness de PPU si se ha tocado el PPU.

---

## 12. Evidencia de la implementación actual

- `make run_tests`: **200 casos / 3882 assertions**, todo verde.
- `make run_mts`: **42/42**. `make run_blargg`: **15/15**.
- PPU acceptance: **12/12**. Harness con BIOS: **25 PASS / 0 FAIL / 8 SKIP**.
- `make run_regress`: **11 ROMs / 990 frames**, golden sin cambios; el
  schema de estado no altera el comportamiento normal.
- El test de CPU guarda en 80 instantes distintos (fases T y posiciones de
  micro-op, incluida la ventana del prefijo CB), ejecuta 700 ticks, restaura
  y vuelve a ejecutar: el estado serializado coincide exactamente.
- El test de máquina guarda en un T-cycle no múltiplo de 4, ejecuta 1999
  ticks, restaura y repite. El programa de prueba mantiene un contador en
  WRAM y ramifica según su valor, así que el determinismo exige restaurar la
  memoria.
- Round-trip específico de WRAM/HRAM, IO, DMA y Gamepad: se cambian todos
  los componentes tras guardar, se carga y se comparan valores y blob.
- El boot ROM se identifica por hash FNV-1a: cargar un estado con boot ROM
  activo sin la misma imagen devuelve `BadValue` y revierte la máquina.
- La carga de un estado sin el chunk del APU devuelve `MissingChunk` tras
  aplicar Clock/CPU/Timer, y el rollback deja el blob exactamente como
  estaba.
- Round-trip de MBC1 (bancos ROM/RAM y modo de banking), MBC2 (RAM de
  nibbles), MBC5 (bancos ROM de 9 bits, RAM y rumble) y no-MBC (RAM), y de la
  RAM de cartucho a través de `QtreGB::save_state/load_state`.
- RTC MBC3: se guarda a los 42 s, pasan 100 s con el estado en disco, se
  carga y sigue en 42 s; 10 s después de cargar marca 52 s.
- RTC halted: permanece congelado tras cargar; al quitar halt vuelve a contar
  desde el instante de carga.
- Un estado de otro mapper se rechaza con `BadValue`.
- Un estado con otra ROM devuelve `WrongRom`.
- Serial: transferencia interna sin peer, espera de clock externo,
  intercambio de bytes con `LinkCable`, IF3 y round-trip de un estado con
  transferencia en curso.
- GUI: dos instancias con `LinkSession` intercambian 0x42/0x99, usan
  `.sav/.sa2` para batería y `.qss` para el estado de sesión; P2 no tiene
  savestate independiente.
- PPU: se guarda en 161 instantes distintos (incluye dots 0-100,
  transiciones HBlank/VBlank, LY 153, frontera de frame y dots arbitrarios),
  se ejecuta un frame completo tras cargar y se comparan el blob, el
  framebuffer completo, los registros del LCD y los cuatro locks OAM/VRAM.
- Todos los selectores de tick y fetcher están registrados con IDs estables;
  un ID desconocido devuelve `BadValue` y un selector no registrado al
  guardar devuelve `WriterMisuse`.

> El schema 5 cubre todo el estado de emulación del core: Clock, CPU, Timer,
> APU, WRAM/HRAM, bus, IO, DMA, Gamepad, cartucho/MBC/RTC, PPU y LCD. Se
> excluyen solo buffers de salida (framebuffer, ring PCM), cableado del host
> y configuración (boot ROM/ROM, delays del LCD, callbacks). El blob ya es
> apto para que la GUI lo envuelva con metadatos, miniaturas, compresión,
> slots y rewind.
