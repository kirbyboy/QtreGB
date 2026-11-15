# F6 — Registros retardados y STAT IRQ

## Estado

F6 está implementado en su mayor parte:

- Retardos de 1 T-cycle para `LCDC`, `BGP`, `WX`, `LY`, `LYC`:
  - `m_last_lcdc`, `m_last_bgp`, `m_last_wx`, `m_last_ly`, `m_last_lyc`.
  - Se actualizan en `update_delayed_registers()` al final de cada dot.
  - El pixel pipeline usa `last_lcdc` para BG/OBJ enable (excepto `lx == 8`),
    `(bgp | last_bgp)` para el color BG y `last_wx` para activar la ventana.
- Retardo de 1 T-cycle del write a STAT:
  - `LCD::write(0xFF41)` deja el valor pendiente.
  - `LCD::tick()` lo aplica al final del dot; el siguiente dot ve los nuevos
    bits de interrupción.
- STAT IRQ por dot:
  - `raise_stat_irq()` evalúa LYC/HBlank/VBlank en cada dot.
  - `m_pending_stat_irq` eleva `IF` en el dot siguiente (1 T-cycle).
  - La línea interna `m_stat_irq_line` está latchada: mientras siga alta no se
    pide otro IRQ. Solo se limpia al entrar en modo 3, y **no** se limpia si la
    condición LYC sigue activa (`stat_irq_blocking`).
- OAM IRQ solo en transiciones:
  - `update_stat_irq_for_oam_mode()` al entrar en VBlank (quirk DMG) y al
    empezar frame (dot 453 de la línea 153).
  - El quirk de OAM en VBlank es **one-shot al entrar**, no una condición
    continua: el bit OAM no mantiene la línea alta durante todo VBlank.
- VBlank DMG: el IRQ de VBlank se dispara con el bit de VBlank; el bit de OAM
  añade el quirk de entrada a VBlank (`vblank_stat_intr-GS`).
- LYC:
  - `m_enable_lyc_eq_ly_irq` desactiva el IRQ en el dot 453 y en la última
    línea de VBlank (dots 2–6).
  - El flag LYC==LY se actualiza con retardo de 4 dots al ponerse y 2 al
    limpiarse (`schedule_lyc_update`).
  - `set_lyc_flag()` solo dispara IRQ en el borde 0→1; así el encendido de la
    PPU conserva el flag sin generar un IRQ falso.
- Timeline de fin de línea:
  - HBlank: LY++ en el dot 453, cambio de modo en el 454, selector y reset de
    `m_line_ticks` en el 455.
  - VBlank normal: LY++ en el 453 y cambio de modo en el 454.
  - Línea 153: LY se resetea a 0 en el dot 2 y el frame termina en el 455.
    `m_vblank_last_line` recuerda que seguimos en la línea 153 aunque LY ya
    valga 0.
- Encendido/apagado del LCD:
  - `Bus` llama a `ppu_sync_lcd_power()` de forma síncrona en el write a
    `LCDC`, antes del check de interrupciones de fin de opcode.
  - Si el encendido completa la comparación LYC y genera un IRQ, `IF` se eleva
    en ese mismo write (`stat_lyc_onoff` round 4).

## Bugs encontrados y corregidos

- El selector cambiaba a OAM/VBlank en el dot 454, saltándose el reset de línea
  del 455. Se cambia en el 455, después del reset.
- Tras resetear LY a 0 en el dot 2 de la línea 153, el código caía en la rama
  normal de VBlank y volvía a incrementar LY en el 453: el frame empezaba en
  LY=1. Eso rompía `instr_timing.gb`. Se corrigió con `m_vblank_last_line`.
- `raise_stat_irq()` escribía el flag LYC en cada dot y pisaba el flag
  retardado de F6. Se eliminó esa escritura: el flag lo gestiona
  `schedule_lyc_update()`.
- La línea STAT se limpiaba incondicionalmente al entrar en modo 3, lo que
  impedía el bloqueo que comprueba `stat_irq_blocking`. Ahora solo se limpia
  si la condición LYC no sigue activa.
- En el round 4 de `stat_lyc_onoff`, el IRQ de LYC del encendido se quedaba en
  `m_pending_stat_irq` hasta el siguiente `ppu_tick`, demasiado tarde para el
  check de fin de opcode. Ahora se eleva `IF` de forma síncrona en
  `ppu_sync_lcd_power()`.
- `raise_stat_irq()` trataba el bit OAM como condición continua durante todo
  VBlank (`(VBLANK || OAM) && mode == VBLANK`). Eso mantenía la línea alta y
  bloqueaba el IRQ de OAM de la línea 0, haciendo fallar `intr_1_2_timing-GS`.
  El quirk es one-shot al entrar en VBlank; el IRQ de inicio de frame se lanza
  en el dot 453 de la línea 153.

## Resultados

Tests base:

- `make run_tests`: **60/60, 221 assertions**.
- `make run_blargg`: **15/15**.
- `make run_mts`: **34/34**.
- Guardrails: `halt_ime0_ei.gb` **PASS**; `instr_timing.gb` **PASS**.

Aceptación PPU (`vendor/mts/*/acceptance/ppu/*.gb`): **8/12 PASS** al cerrar
F6. Tras el bloque F5.2 de timing de mode 3 sube a **11/12**; solo queda
`intr_2_mode0_timing_sprites` (ver `documentation/ppu-v3-f5-pipeline.md`).

| Test | Resultado |
|---|---|
| `intr_1_2_timing-GS.gb` | PASS |
| `intr_2_0_timing.gb` | PASS |
| `intr_2_mode3_timing.gb` | PASS |
| `lcdon_timing-GS.gb` | PASS |
| `lcdon_write_timing-GS.gb` | PASS |
| `stat_irq_blocking.gb` | PASS |
| `stat_lyc_onoff.gb` | PASS |
| `vblank_stat_intr-GS.gb` | PASS |
| `hblank_ly_scx_timing-GS.gb` | FAIL |
| `intr_2_mode0_timing.gb` | FAIL (`E` expected 02, obtained 01) |
| `intr_2_mode0_timing_sprites.gb` | FAIL |
| `intr_2_oam_ok_timing.gb` | FAIL (`E` expected 02, obtained 01) |

Los 4 fallos restantes dependen de la duración del modo 3 (pipeline F5): el
STAT ya está bien, pero el final de modo 3 llega un ciclo de bucle antes de lo
esperado. `intr_2_mode0_timing_sprites` añade sprites y
`hblank_ly_scx_timing` depende de cómo `SCX` alarga el modo 3.

Harness visual (`dmg-acid2` y `m3_*`) sigue prácticamente igual que F5. Los
diffs no bajan todavía porque el pipeline de píxeles no consume los cambios de
registro en el dot exacto; F6 arregla el modelo de registros/STAT, no la
longitud exacta de modo 3.

## Lo que falta

1. **Duración exacta del modo 3** (F5.2/F5.3):
   - `intr_2_mode0_timing`, `intr_2_oam_ok_timing`: el bucle de polling llega
     un ciclo antes/después del final de modo 3.
   - `intr_2_mode0_timing_sprites`: lo mismo con sprites.
2. **Fase de HBlank**:
   - `hblank_ly_scx_timing-GS`: el intervalo STAT mode 0 → LY++ debe variar
     con `SCX mod 8`; depende de la longitud de modo 3.
3. **Harness visual**:
   - `dmg-acid2` 130 y los `m3_*` necesitan que el pipeline use el registro
     correcto en el dot correcto (no solo el retardo de 1 T).

## Cómo probar

```sh
make run_tests
make run_blargg
make run_mts
./build/mts_runner vendor/mts/*/acceptance/ppu/*.gb
./build/mts_runner vendor/mts/*/acceptance/halt_ime0_ei.gb
./build/blargg_runner roms/blargg/instr_timing/instr_timing.gb
QUIET=1 REPORT=/tmp/qtrebg_frames/f6.tsv ./mts/compare_ppu_frames.sh
```
