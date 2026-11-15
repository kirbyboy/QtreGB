# F1 — Scheduler de 4 fases (guía de implementación)

Objetivo: hacer explícito el reloj global de 4 fases (M-cycle = 4 T-cycles)
**sin cambiar el comportamiento observable**. F1 es andamiaje, no precisión.
La CPU, la PPU, el timer, el DMA y el bus se quedan como están.

> Regla de oro de F1: después de cada paso, el build y los tests deben dar
> exactamente lo mismo que antes. Si un diff del harness cambia, has movido
> comportamiento, no solo el reloj.

---

## 0. Qué es F1 y qué no es

**Sí entra en F1:**

- Un contador de fase global (`PhaseClock`) en `QtreGB`.
- `QtreGB::tick()` sigue avanzando **un T-cycle** (API pública intacta).
- `QtreGB::cycle()` nuevo, avanza **cuatro T-cycles** (un M-cycle).
- `QtreGB::tick_t0()..tick_t3()` privados, inicialmente los cuatro llaman al
  mismo cuerpo (`tick_once()`).
- `QtreGB::get_phase()` para tests y depuración.
- Tests unitarios del contador y de la API.

**No entra en F1:**

- No se toca `Z80GB.cpp` ni la lógica de instrucciones.
- No se separan todavía las lecturas/escrituras del CPU en request/flush.
- No se cambia la PPU (`ppu.cpp`, `fetcher.cpp`, `fifo.cpp`).
- No se cambia el bus (`bus.cpp`, `bus_cpu_view.cpp`).
- No se toca `timer.cpp` (regla del proyecto: es sensible).
- No se cambia el DMA ni su condición `is_end_m_cycle()`.
- No se reordena ningún componente: en F1 las cuatro fases hacen lo mismo.

El objetivo de F1 es que, a partir de F2, exista un sitio claro donde mover
cada cosa sin reescribir el core.

---

## 1. Punto de partida

Ahora mismo, todo el tiempo vive en `QtreGB::tick()`:

```cpp
void QtreGB::tick()
{
    if(m_errors.has_error())
    {
        return;
    }

    if(m_cpu.is_end_opcode() && !m_cpu.is_interruption_time())
    {
        // serial debug (0xFF01/0xFF02)
    }

    m_timer.begin_cpu_phase();
    m_cpu.tick();

    m_timer.timer_tick();
    m_ppu.ppu_tick();

    if(m_cpu.is_end_m_cycle())
    {
        m_dma.dma_tick();
    }
}
```

Detalles que hay que respetar en F1:

- La GUI llama a `tick()` en bucle (`gui/src/main.cpp`). Si `tick()` pasa a
  significar un M-cycle, la emulación iría 4 veces más rápido. **`tick()` debe
  seguir siendo un T-cycle.**
- El `mts_runner` y los tests también llaman a `tick()` en bucle.
- `Z80GB::tick()` ya avanza un T-cycle y lleva su propio contador `m_t_cycle`.
  No lo toques en F1.
- `Timer::begin_cpu_phase()` debe ejecutarse **antes** de `m_cpu.tick()`, y
  `timer_tick()` **después**. No cambies ese orden.
- `m_dma.dma_tick()` se llama con la condición actual `is_end_m_cycle()`.
  No la muevas todavía; ya la ajustaremos en F2/F3.
- `m_ppu.ppu_tick()` sigue siendo un T-cycle; su significado no cambia.

### Nota sobre `m_t_cycle`

La fase de `QtreGB` y el contador interno `m_t_cycle` del CPU son **dos cosas
distintas**. En F1 no se sincronizan: `Phase::T0` es simplemente la primera
llamada de cada cuatro, mientras que el CPU lleva su propio `m_t_cycle` (que
además tiene un offset propio: el primer tick de un NOP deja `m_t_cycle` en 2,
no en 1). No asumas que `Phase::T3` coincide con `m_t_cycle == 4`.

En F2, cuando separes los accesos del CPU, tendrás que decidir el mapeo exacto
(por ejemplo, `flush_read` en T3) y validarlo con `intr_timing`, `pop_timing` y
los tests de blargg. F1 no necesita ese mapeo: solo etiqueta las llamadas.

---

## 2. Modelo objetivo (para saber hacia dónde vamos)

F1 solo crea el esqueleto. La matriz completa se rellena en F2+:

| Fase | F1 (todas iguales) | Objetivo F2+ |
|---|---|---|
| `T0` | `tick_once()` | CPU micro-op / emisión de bus + dot PPU + LCD |
| `T1` | `tick_once()` | CPU `flush_write` + dot PPU + LCD + DMA |
| `T2` | `tick_once()` | CPU comprobación de interrupción + dot PPU + LCD |
| `T3` | `tick_once()` | CPU `flush_read` + timer + dot PPU + LCD + DMA + IRQ |

La idea es la de la guía de arquitectura:

- Un M-cycle son 4 T-cycles.
- La PPU avanza un dot por T-cycle (4 dots por M-cycle).
- El CPU avanza una micro-operación por M-cycle, pero reparte el acceso a
  memoria entre fases (petición en T0, write en T1, read en T3).
- El timer se evalúa una vez por M-cycle en T3 (más adelante).
- El DMA va en T1 y T3 (más adelante).

En F1 **no** se implementa nada de eso: solo se etiquetan las cuatro llamadas.

---

## 3. Cambios de arquitectura concretos

### 3.1 `Phase` y `PhaseClock`

Archivo nuevo: `core/inc/phase_clock.h` (header-only, sin `.cpp`).

```cpp
#ifndef QTREGB_PHASE_CLOCK_H
#define QTREGB_PHASE_CLOCK_H

#include "types.h"

namespace qtreGB
{

enum class Phase : u8
{
    T0 = 0,
    T1,
    T2,
    T3,
};

class PhaseClock
{
    public:
        Phase get() const
        {
            return static_cast<Phase>(m_value);
        }

        Phase advance()
        {
            m_value = static_cast<u8>((m_value + 1) & 0x03);
            return get();
        }

        void reset()
        {
            m_value = 0;
        }

    private:
        u8 m_value = 0;
};

} // namespace qtreGB

#endif
```

Por qué un objeto y no un `u8` suelto: se puede probar aislado, no depende del
emulador y deja claro que la fase es una pieza de reloj, no un flag cualquiera.

### 3.2 `QtreGB`: API nueva

En `core/inc/QtreGB.h`:

```cpp
#include "phase_clock.h"

class QtreGB
{
    public:
        // ...
        void tick();          // un T-cycle (la API de siempre)
        void cycle();         // cuatro T-cycles (un M-cycle)

        Phase get_phase() const { return m_clock.get(); }

    private:
        void tick_t0();
        void tick_t1();
        void tick_t2();
        void tick_t3();
        void tick_once();

        PhaseClock m_clock;
};
```

No cambies la firma de `tick()`. No cambies `load_cart()`, `get_frame_ready()`
ni el resto de la API.

### 3.3 `QtreGB.cpp`: despacho por fase

```cpp
void QtreGB::tick()
{
    switch(m_clock.get())
    {
        case Phase::T0: tick_t0(); break;
        case Phase::T1: tick_t1(); break;
        case Phase::T2: tick_t2(); break;
        case Phase::T3: tick_t3(); break;
        default: break;
    }

    m_clock.advance();
}

void QtreGB::cycle()
{
    // Un M-cycle = cuatro T-cycles. tick() sigue siendo un T-cycle para no
    // romper la GUI, el runner ni los tests.
    tick();
    tick();
    tick();
    tick();
}

void QtreGB::tick_t0() { tick_once(); }
void QtreGB::tick_t1() { tick_once(); }
void QtreGB::tick_t2() { tick_once(); }
void QtreGB::tick_t3() { tick_once(); }

void QtreGB::tick_once()
{
    // Aquí va, tal cual, el cuerpo actual de QtreGB::tick().
    // ...
}
```

En F1 las cuatro fases llaman a `tick_once()`. Es intencionado: el
comportamiento es idéntico. La diferencia es que ya existe el punto de
extensión para F2.

Detalle: si `tick_once()` sale antes de tiempo (por ejemplo,
`m_errors.has_error()`), la fase debe avanzar igualmente. El reloj no se
detiene porque un componente haya reportado un error; solo se deja de ejecutar
trabajo.

### 3.4 Reset de la fase

El constructor de `QtreGB` ya llama a `m_cpu.reset()`, `m_timer.timer_init()` y
`m_ppu.ppu_init()`. Añade `m_clock.reset()` ahí (o confía en el inicializador
`m_value = 0`). Si más adelante añades un `reset()` público, resetea también la
fase.

### 3.5 Lo que **no** hay que tocar

- `Z80GB::tick()`: sigue recibiendo una llamada por T-cycle y usando su
  `m_t_cycle` interno.
- `PPU::ppu_tick()`: sigue recibiendo una llamada por T-cycle y usando su
  `m_line_ticks`.
- `Timer::begin_cpu_phase()` / `timer_tick()`: mismo orden.
- `DMA::dma_tick()`: misma condición.
- `BusCpuView`, `LCD`, `InterruptController`: sin cambios.

---

## 4. Pasos pequeños, con build y tests después de cada uno

Haz los pasos en este orden. No mezcles dos pasos en un mismo cambio.

### Paso 0 — Baseline local

Antes de tocar nada, guarda el baseline del harness en esta rama:

```sh
make -j$(nproc)
make run_tests
QUIET=1 REPORT=/tmp/qtrebg_frames/f1-before.tsv ./mts/compare_ppu_frames.sh
```

Apunta el resultado. Debe ser:

```text
0 passed, 25 failed, 8 skipped
```

Guarda `f1-before.tsv`; lo compararás al final.

### Paso 1 — `PhaseClock` + test unitario

1. Crea `core/inc/phase_clock.h` con el contenido de 3.1.
2. Crea `tests/test_phase_clock.cpp`:

```cpp
#include "doctest.h"
#include "phase_clock.h"

using namespace qtreGB;

TEST_CASE("PhaseClock: T0 -> T1 -> T2 -> T3 -> T0")
{
    PhaseClock clock;
    CHECK(clock.get() == Phase::T0);
    CHECK(clock.advance() == Phase::T1);
    CHECK(clock.advance() == Phase::T2);
    CHECK(clock.advance() == Phase::T3);
    CHECK(clock.advance() == Phase::T0);
}

TEST_CASE("PhaseClock: reset returns to T0")
{
    PhaseClock clock;
    clock.advance();
    clock.advance();
    clock.reset();
    CHECK(clock.get() == Phase::T0);
}
```

3. `make run_tests` → los tests nuevos pasan y los 53 antiguos siguen verdes.
4. `git diff --stat`: solo debe aparecer el header y el test.

**Criterio:** ningún cambio en el emulador. Solo una pieza de reloj aislada.

### Paso 2 — Extraer `tick_once()` sin fases

Antes de añadir el `switch`, separa el cuerpo actual:

1. Declara `void tick_once();` en la sección privada de `QtreGB.h`.
2. Renombra el cuerpo de `QtreGB::tick()` a `void QtreGB::tick_once()`.
3. Deja:

```cpp
void QtreGB::tick()
{
    tick_once();
}
```

4. Build + `make run_tests`.
5. Harness:

```sh
QUIET=1 REPORT=/tmp/qtrebg_frames/f1-extract.tsv ./mts/compare_ppu_frames.sh
diff -u /tmp/qtrebg_frames/f1-before.tsv /tmp/qtrebg_frames/f1-extract.tsv
```

**Criterio:** `diff` vacío, 53/53 tests. Si algo cambia, has movido código de
sitio sin querer.

### Paso 3 — Añadir `PhaseClock` y el despacho por fases

1. Incluye `phase_clock.h` en `QtreGB.h`.
2. Añade `PhaseClock m_clock;` y `get_phase()`.
3. Añade `tick_t0..tick_t3` y `cycle()` como en 3.3.
4. Cambia `tick()` para que despache según `m_clock.get()` y luego
   `m_clock.advance()`.
5. Build + `make run_tests`.
6. Harness y diff contra `f1-before.tsv`.

**Criterio:** 53/53 tests y `diff` vacío. El comportamiento no ha cambiado; solo
hemos etiquetado las cuatro llamadas.

### Paso 4 — Test de integración de la API

Crea `tests/test_scheduler.cpp`:

```cpp
#include "doctest.h"
#include "QtreGB.h"

#include <vector>

using namespace qtreGB;

namespace
{
std::vector<u8> make_nop_rom()
{
    std::vector<u8> rom(0x8000, 0x00);
    rom[0x0147] = 0x00; // ROM only
    rom[0x0148] = 0x00; // 32 KiB
    rom[0x0149] = 0x00; // no RAM
    return rom;
}
}

TEST_CASE("QtreGB: tick advances one phase and cycle advances four")
{
    QtreGB emu;
    REQUIRE(emu.load_cart(make_nop_rom()));

    // IO has no constructor yet and m_sb/m_sc start uninitialized. Writing
    // them avoids reading indeterminate values from QtreGB::tick()'s serial
    // debug block. This is test setup, not a core change.
    emu.m_io.io_write(0xFF01, 0x00);
    emu.m_io.io_write(0xFF02, 0x00);

    CHECK(emu.get_phase() == Phase::T0);
    emu.tick();
    CHECK(emu.get_phase() == Phase::T1);
    emu.tick();
    CHECK(emu.get_phase() == Phase::T2);
    emu.tick();
    CHECK(emu.get_phase() == Phase::T3);
    emu.tick();
    CHECK(emu.get_phase() == Phase::T0);

    emu.cycle();
    CHECK(emu.get_phase() == Phase::T0);
}
```

`make run_tests` → todo verde. Este test fija el contrato público: `tick()` =
un T-cycle, `cycle()` = cuatro.

### Paso 5 — Regresión completa

```sh
make -j$(nproc)
make run_tests
make run_blargg
make run_mts
QUIET=1 REPORT=/tmp/qtrebg_frames/f1-after.tsv ./mts/compare_ppu_frames.sh
diff -u /tmp/qtrebg_frames/f1-before.tsv /tmp/qtrebg_frames/f1-after.tsv
```

Resultados esperados:

- `run_tests`: todos los test cases (53 + los nuevos) en verde.
- `run_blargg`: 15/15.
- `run_mts`: 34/34.
- Harness: `0 passed, 25 failed, 8 skipped` y `diff` vacío.
- `git diff --stat`: solo `QtreGB.h`, `QtreGB.cpp`, `phase_clock.h` y los tests.

**Criterio de aceptación de F1:** todo lo anterior se cumple. Si el harness
cambia un solo diff, F1 no está terminado.

### Paso 6 — Actualizar el handoff

Añade en `documentation/ppu-v3-handoff.md`:

- F1 completado, fecha y comandos ejecutados.
- Resultado de doctest/blargg/mts.
- Confirmación de que `f1-before.tsv` y `f1-after.tsv` son idénticos.
- Siguiente paso: F2.

---

## 5. Tests y comandos de referencia

| Comando | Qué valida |
|---|---|
| `make -j$(nproc)` | Compila core + GUI con `-Wall -Wextra -Wpedantic` |
| `make run_tests` | Doctest: CPU, bus, cart, timer, PPU, scheduler |
| `make run_blargg` | 15 ROMs de instrucciones/timing de CPU |
| `make run_mts` | 34 ROMs de Mooneye (timer, interrupts, intr_timing, OAM DMA...) |
| `./mts/compare_ppu_frames.sh` | 25 frames PPU + dmg-acid2 |
| `diff -u before.tsv after.tsv` | Prueba de que F1 no cambió la PPU |

Consejo: ejecuta `make run_tests` después de **cada** paso, no solo al final.
Es barato y localiza el fallo al instante.

---

## 6. Errores típicos y cómo detectarlos

| Síntoma | Causa probable |
|---|---|
| La GUI va 4 veces más rápida o el harness tarda 1/4 | `tick()` pasó a hacer 4 T-cycles. Debe seguir siendo 1. |
| El harness tarda 4 veces más | `cycle()` se llama desde la GUI o el runner sin querer. |
| `run_tests` falla en CPU/timer | Moviste `begin_cpu_phase()`/`timer_tick()` de orden. |
| Cambian los diffs del harness | Moviste trabajo de la PPU o del bus, no solo el reloj. |
| El DMA deja de funcionar | Cambiaste `is_end_m_cycle()` o el orden de `dma_tick()`. |
| El primer test de fase falla tras `load_cart` | No reseteaste la fase al construir/resetear. |
| `Phase` no compila en `QtreGB.h` | Falta `#include "phase_clock.h"` o `types.h`. |

---

## 7. Qué viene después de F1 (mapa, no implementar aún)

F1 deja el reloj etiquetado. A partir de ahí:

- **F2 — CPU y bus:** `Z80GB::tick_t0..t3`, `read/write` → request + flush,
  write en T1 y read en T3, `check_interrupt<T>` por fase. La lógica de
  instrucciones **no se toca**; solo cambia *cuándo* se confirma el acceso.
- **F3 — VideoBus:** `acquirers`, `View<Device>`, CPU/DMA/PPU. La comprobación
  va en el flush del CPU.
- **F4 — PPU por dots:** `dots` + `tick_selector`, OAM scan, pixel transfer,
  HBlank/VBlank, turn on/off.
- **F5 — Fetcher:** cuatro prefetchers, `oam_entries[168]`, ventana y sprites.
- **F6 — Retardos:** `last_lcdc`, `last_bgp`, `last_wx`, `last_ly`, `last_lyc`,
  `pending_write`, STAT IRQ con flanco.
- **F7 — LCD:** row buffer y copia en el dot 452, apagado/encendido.
- **F8 — Validación:** 24 `m3_*` + `dmg-acid2` a diff=0.

F1 es la base: si no queda perfectamente neutra, F2-F7 se construyen sobre arena.

---

## 8. Checklist final de F1

- [ ] `core/inc/phase_clock.h` creado.
- [ ] `QtreGB` incluye `PhaseClock`, `get_phase()`, `tick_t0..t3`, `cycle()`.
- [ ] `tick()` sigue siendo un T-cycle y despacha por fase.
- [ ] `tick_once()` contiene el cuerpo original sin cambios de orden.
- [ ] `tests/test_phase_clock.cpp` en verde.
- [ ] `tests/test_scheduler.cpp` en verde.
- [ ] `make run_tests` todo verde.
- [ ] `make run_blargg` 15/15.
- [ ] `make run_mts` 34/34.
- [ ] Harness: mismos 25 FAIL / 8 SKIP / 0 PASS.
- [ ] `diff` entre TSV before/after vacío.
- [ ] Handoff actualizado.
- [ ] Sin cambios en `Z80GB.cpp`, `ppu.cpp`, `fetcher.cpp`, `fifo.cpp`,
      `bus.cpp`, `bus_cpu_view.cpp`, `timer.cpp`, `dma.cpp`, `lcd.cpp`.
