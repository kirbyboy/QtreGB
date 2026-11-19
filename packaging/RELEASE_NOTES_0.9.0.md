# QtreGB 0.9.0

Fecha: 2026-11-19
Licencia: GPL-3.0-or-later

Primera versión empaquetada de QtreGB, un emulador de Game Boy DMG escrito en
C++17 con SDL2 e ImGui.

## Contenido

- CPU de micro-ops (`CpuMicro`) y PPU nativa dirigida por dots.
- APU completo: CH1/CH2/CH3/CH4, frame sequencer, mezcla estéreo y HPF.
- Cartuchos no-MBC, MBC1, MBC2, MBC3/MBC30 y MBC5, con RAM de batería y RTC.
- BIOS opcional de 256 bytes.
- Guardado de batería atómico y autoguardado cada 60 segundos cuando hay
  cambios.
- 10 ranuras de savestate (`.sr0.png`…`.sr9.png`) y navegador de estados con
  miniaturas.
- Estados de sesión Link (`.qss.png`) con imagen combinada 320×144.
- Link cable, audio P1/P2/Both y renderizado independiente de las dos máquinas.
- Controles de teclado y mando, atajos globales configurables.
- Fast forward 200%, 300% y MAX.
- Archivos recientes en `File`.
- Interfaz en inglés y español.
- Modo de presentación por defecto: `Monitor` (vblank del monitor).
- Arranque con ROM por argumento y asociación `.gb` (`%f`).
- About con icono, versión, licencia y enlaces.
- Paquetes `.deb` y AppImage.

## Instalación

### Debian/Ubuntu

```sh
sudo apt install ./qtrebg_0.9.0_amd64.deb
```

### AppImage

```sh
chmod +x QtreGB-0.9.0-x86_64.AppImage
./QtreGB-0.9.0-x86_64.AppImage
```

También se puede abrir una ROM directamente:

```sh
QtreGB juego.gb
```

## Limitaciones conocidas

- Solo Game Boy DMG; los juegos CGB-only no están soportados.
- El core libretro es single-player y no incluye Link Cable.
- Los paquetes de esta versión son para Linux x86_64.
- El shader de pantalla física de Retro Display Lab es una prueba local y no
  forma parte del paquete.

## Verificación

- `make run_tests`: 254 casos / 4227 assertions.
- `make run_blargg`: 15/15.
- `make run_mts`: 42/42.
- `make run_regress`: 11 ROMs / 990 frames.
- BIOS: 7 ROMs / 2800 frames.

Los checksums de los artefactos están en `SHA256SUMS-0.9.0`.
