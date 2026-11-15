# F2 — Fase de escritura del CPU: hallazgo y corrección (T3, no T1)

## Resumen

La premisa de F2 era incorrecta. En este CPU, **las escrituras a memoria
ocurren en el último T-cycle del M-cycle (T3), igual que las lecturas**. No hay
que moverlas a T1.

La evidencia son los tests de Blargg:

- `mem_timing-2/02-write_timing.gb`
- `mem_timing-2/03-modify_timing.gb`
- `instr_timing.gb`

Con los writes en T1: `12 passed, 2 failed, 1 no verdict`.
Con los writes en T3 (estado actual): **15 passed, 0 failed, 0 no verdict**.

El código se ha restaurado al estado de `HEAD` (`3a5bf59`). No se usa
`write_next_t1()`, no hay `PendingWrite`, y el test
`tests/test_cpu_write_phase.cpp` se ha eliminado porque medía la hipótesis
descartada.

---

## 1. Qué se probó

Se implementó el patrón `write_next_t1()`:

- El handler registraba el write en T3 del M-cycle anterior.
- `run_pending_write()` lo ejecutaba en T1 del M-cycle siguiente.
- Se aplicó a `in_ld_addr_r`, `in_ld_d16_r16`, `in_push`, `in_call`,
  `in_rst`, los casos directos `0x34/0x35/0x36/0xE0/0xEA` y el RMW de `0xCB`.

Resultados de esa prueba:

- Doctest: 69/69 (incluido el test de fase de escritura que esperaba T1).
- MTS: 34/34.
- Blargg: **12/15**.
  - `02-write_timing.gb` FAIL
  - `03-modify_timing.gb` FAIL
  - `instr_timing.gb` TIMEOUT

Al restaurar el CPU a `HEAD` (writes en T3):

- Doctest: 56/56 (208 assertions).
- Blargg: **15/15**.
- MTS: 34/34.
- Harness PPU: idéntico al baseline (`0 PASS / 25 FAIL / 8 SKIP`).

---

## 2. Evidencia

`roms/blargg/mem_timing-2/readme.txt` lo dice explícitamente:

> "For instructions which either read or write, but not both, the CPU makes
> the access on the last cycle. For instructions which read, modify, then
> write back, the CPU reads on the next-to-last cycle, and writes on the last
> cycle."

La tabla del test (`source/02-write_timing.s`) confirma los ciclos esperados:

| Instrucción | Ciclo del write |
|---|---:|
| `LD (HL),B` (`0x70`) | 2 (M2, el último) |
| `LD (HL),d8` (`0x36`) | 3 (M3, el último) |
| `LDH (a8),A` (`0xE0`) | 3 (M3, el último) |
| `LD (a16),A` (`0xEA`) | 4 (M4, el último) |

Con writes en T1, el test medía un ciclo menos:

```text
36:2-3 70:1-2 71:1-2 72:1-2 73:1-2 74:1-2 75:1-2 77:1-2
02:1-2 12:1-2 22:1-2 32:1-2 E2:1-2 E0:2-3 EA:3-4
```

El formato es `opcode:medido-correcto`, contando **M-cycles** (NOP = 1). El
write en T1 llegaba demasiado pronto y el test lo clasificaba en el M-cycle
anterior. Con T3, todos pasan.

### Ticks correctos (T3)

Medidos con un bus que registra cada write. Estos son los valores que hay que
mantener:

| Instrucción | Ticks totales | Write(s) en |
|---|---:|---|
| `LD (HL),A` | 8 | tick 8 |
| `LD (BC),A` / `LD (DE),A` / `LD (HL+/-),A` | 8 | tick 8 |
| `LDH (a8),A` | 12 | tick 12 |
| `LD (a16),A` | 16 | tick 16 |
| `INC (HL)` / `DEC (HL)` | 12 | tick 12 |
| `LD (HL),d8` | 12 | tick 12 |
| `PUSH rr` | 16 | ticks 12, 16 |
| `CALL nn` | 24 | ticks 20, 24 |
| `RST n` | 16 | ticks 12, 16 |
| `LD (a16),SP` | 20 | ticks 16, 20 |
| `CB` RMW `(HL)` | 16 | tick 16 |

---

## 3. Por qué nos equivocamos

Se tomó el `flush_write()` en `t1` de DocBoy como si fuera el T1 del LR35902.
No lo es: las etiquetas de fase de DocBoy son su propio modelo de CPU, y el
LR35902 confirma la escritura en el **último** T-state del M-cycle.

La idea general de DocBoy sigue siendo válida: separar el efecto de escritura
del resto del handler y tenerlo localizado. Pero **la fase concreta es
específica de cada CPU y la deciden los tests**, no una analogía con otro core.
En este caso, `mem_timing-2` es el árbitro y dice T3.

La tabla de fases útil queda así:

```text
m_t_cycle == 1 -> T0  fetch de opcode / primer T-cycle
m_t_cycle == 2 -> T1
m_t_cycle == 3 -> T2
m_t_cycle == 4 -> T3  lectura/escritura y fin del M-cycle
```

Las lecturas y las escrituras comparten T3. No hay que separar unas de otras
para el timing del CPU.

---

## 4. Estado del código

- `core/inc/Z80GB.h` y `core/src/Z80GB.cpp` están en el estado de `HEAD`
  (`3a5bf59`), sin `PendingWrite` ni `write_next_t1()`.
- Se mantiene el comentario de fases, corregido para indicar que T3 contiene
  lecturas **y** escrituras.
- `tests/test_cpu_write_phase.cpp` eliminado: medía una hipótesis descartada.
- Los tests de F1 (`test_phase_clock.cpp`, `test_scheduler.cpp`) siguen.
- No se ha tocado la PPU, el bus, el timer ni el DMA.

Comprobación final:

```sh
make -C core
make run_tests   # 56/56, 208 assertions
make run_blargg  # 15/15
make run_mts     # 34/34
QUIET=1 REPORT=/tmp/qtrebg_frames/f2-after.tsv ./mts/compare_ppu_frames.sh
# 0 PASS / 25 FAIL / 8 SKIP, idéntico al baseline
```

---

## 5. Qué significa para el plan

- **F2, tal como estaba planteado (mover writes a T1), queda cancelado.**
- El acceso del CPU (lecturas y escrituras) ya está en el T-cycle correcto:
  T3. No hay que cambiarlo para la PPU.
- El problema de píxeles está en el resto del modelo, no en la fase de escritura
  del CPU:
  - **F3**: propiedad del bus VRAM/OAM y contención por T-cycle.
  - **F4**: PPU por dots (`dots` + `tick_selector`, OAM scan, pixel transfer,
    HBlank/VBlank).
  - **F5**: fetcher, FIFOs, ventana y sprites.
  - **F6**: registros retardados (`last_lcdc`, `last_bgp`, `last_wx`,
    `last_ly`, `last_lyc`), `pending_write` y STAT IRQ con flanco.
- El contrato `request`/`flush` del bus puede seguir siendo útil en F3 para el
  DMA y el open bus, pero **sin cambiar el tick final del acceso**: T3.

---

## 6. Lecciones y cómo probar

- **Blargg es el árbitro del timing del CPU.** Si un cambio de fase rompe
  `mem_timing-2` o `instr_timing`, el cambio es incorrecto para este CPU.
- El formato de fallo `opcode:medido-correcto` cuenta **M-cycles**, no
  T-cycles.
- Un `instr_timing` que hace timeout es síntoma de un timing de acceso
  incorrecto, no necesariamente de un bug del timer.
- Cuidado con la librería `core-opt`: después de editar `Z80GB.cpp`, asegúrate
  de que `build/libqtrebgcore_opt.a` se ha reconstruido. Un objeto viejo puede
  hacer que los tests midan el código anterior.
  ```sh
  make -C core
  ```
- No fuerces una fase “elegante” si los tests de hardware dicen lo contrario.
  La fase es una propiedad del CPU, no del diseño que nos gustaría.

---

## 7. Referencias

- `roms/blargg/mem_timing-2/readme.txt`
- `roms/blargg/mem_timing-2/source/02-write_timing.s`
- `roms/blargg/mem_timing-2/source/03-modify_timing.s`
- `roms/blargg/instr_timing/readme.txt`
- `documentation/ppu-v3-handoff.md`
