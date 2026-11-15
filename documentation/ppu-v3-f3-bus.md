# F3 — Bus de vídeo y propiedad de VRAM/OAM

## Objetivo

Centralizar la propiedad de VRAM/OAM en un solo sitio y dejar claro quién
puede acceder en cada momento:

- La PPU accede a su propia VRAM/OAM sin bloquearse a sí misma.
- El CPU accede a través de una vista (`BusCpuView`) que resuelve la
  contención: si la PPU tiene el bus, el CPU lee `0xFF` y sus writes se
  descartan.
- El DMA sigue ganando el bus: su camino es explícito y no lo bloquean los
  locks de la PPU.

F3 es un **refactor de propiedad**, no un cambio de ventanas de timing. Las
adquisiciones de la PPU siguen ocurriendo en los mismos puntos que los
antiguos booleanos (transiciones de modo y schedule especial de LCD-on). El
paso a dots exactos es F4.

---

## Diseño

### `VideoBus`

Archivo: `core/inc/videobus.h`.

```cpp
enum class VideoDevice : u8 { Cpu = 0, Ppu, Dma };
enum class VideoAccess : u8 { Read = 0, Write };

class VideoBus
{
    public:
        void acquire(VideoDevice device, VideoAccess access);
        void release(VideoDevice device, VideoAccess access);
        bool is_acquired(VideoAccess access) const;
        bool is_acquired_by(VideoDevice device, VideoAccess access) const;
        void reset();

    private:
        u8 m_read_acquirers = 0;
        u8 m_write_acquirers = 0;
};
```

Cada acceso (lectura/escritura) tiene su propia máscara de dispositivos. Esto
permite reproducir las ventanas distintas de `lcdon_write_timing`, donde una
lectura y una escritura del CPU pueden estar bloqueadas en momentos diferentes.

### `PPU`

- Tiene dos buses: `m_oam_bus` y `m_vram_bus`.
- Los antiguos `m_oam_read_locked`, `m_oam_write_locked`,
  `m_vram_read_locked`, `m_vram_write_locked` han desaparecido.
- Los getters `is_oam_read_locked()`, etc. consultan el `VideoBus`
  correspondiente.
- La PPU adquiere/suelta en los mismos puntos que antes:
  - OAM scan / HBlank / VBlank.
  - Schedule especial de las tres primeras líneas tras encender el LCD.
  - `ppu_init`, `ppu_power_off`, `ppu_power_on` resetean ambos buses.

### Acceso de la PPU a su memoria

`PPU::ppu_vram_read/write` y `PPU::ppu_oam_read/write` son **raw**: ya no
comprueban locks. La PPU es la dueña de la memoria y debe poder leerla para su
fetcher aunque el CPU esté bloqueado.

- El fetcher usa el `Bus` real, que llama a esas funciones raw.
- El CPU usa `BusCpuView`, que es quien comprueba los locks antes de llegar al
  `Bus`.
- `ppu_oam_write_dma` sigue siendo raw: el DMA gana el bus.

### `BusCpuView`

Es la vista del CPU. Orden de comprobación:

1. DMA activo → `0xFF` / write descartado (comportamiento ya existente).
2. VRAM (`0x8000-0x9FFF`) con `is_vram_read_locked()` / `is_vram_write_locked()`.
3. OAM (`0xFE00-0xFE9F`) con `is_oam_read_locked()` / `is_oam_write_locked()`.
4. Acceso normal al `Bus`.

En una lectura bloqueada, `m_last_value` se pone a `0xFF`, igual que antes.

### DMA

El DMA sigue accediendo por su camino actual:

- `BusCpuView` detecta `dma_transferring()` y bloquea al CPU.
- `DMA::dma_tick` escribe OAM con `ppu_oam_write_dma`, que no pasa por los
  locks de la PPU.
- `VideoDevice::Dma` está reservado en `VideoBus` para cuando queramos modelar
  la adquisición explícita del DMA; en F3 no hace falta porque el
  comportamiento actual ya es correcto y está cubierto por los tests.

---

## Cambios de código

| Archivo | Cambio |
|---|---|
| `core/inc/videobus.h` | Nuevo: `VideoDevice`, `VideoAccess`, `VideoBus` |
| `core/inc/ppu.h` | Incluye `videobus.h`; `m_oam_bus`/`m_vram_bus`; getters |
| `core/src/ppu.cpp` | Sustituye los cuatro booleanos por `acquire/release`; acceso raw a VRAM/OAM |
| `core/src/bus_cpu_view.cpp` | Comprueba los locks de la PPU para VRAM/OAM |
| `tests/test_videobus.cpp` | Tests de adquisición, liberación, múltiples dispositivos y reset |

No se ha tocado el CPU, el timer, el DMA ni la lógica de la PPU.

---

## Comportamiento conservado

- Las ventanas de acceso del CPU son las mismas que antes: los flags se
  activan y desactivan en los mismos `m_line_ticks`/modos.
- El schedule especial de `lcdon_timing` / `lcdon_write_timing` se mantiene.
- El fetcher de la PPU ya no depende de los locks del CPU.
- El harness de frames es idéntico al baseline.

**Nota:** la V2 no bloquea VRAM durante el modo 3 normal; solo lo hacía en el
schedule especial de LCD-on. F3 conserva ese comportamiento. Añadir la ventana
real de VRAM en modo 3 es parte de F4/F5, cuando la PPU tenga dots exactos.

---

## Resultados

- `make run_tests`: **60/60 test cases, 221 assertions**.
- `make run_blargg`: **15/15**.
- `make run_mts`: **34/34**.
- Harness PPU: **0 PASS / 25 FAIL / 8 SKIP**, idéntico al baseline
  (`documentation/baselines/ppu-v2-baseline-2026-11-12.tsv`).

---

## Siguiente: F4

F4 sustituye `m_line_ticks` + `switch(mode)` por `dots` + `tick_selector` y
mueve las adquisiciones de VRAM/OAM a los dots exactos del OAM scan, pixel
transfer, HBlank y VBlank. `VideoBus` ya está preparado para recibir
`acquire`/`release` desde esas funciones.
