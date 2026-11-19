# QtreGB libretro core (first version)

Build from the repository root:

```sh
make libretro
```

The result is `build/qtrebg_libretro.so`.

Or build only this directory:

```sh
make -C libretro
```

Install the core and its `.info` next to each other in RetroArch's core
directory:

```sh
make -C libretro install
```

The default destination is `$(HOME)/.config/retroarch/cores`; override it with
`RETROARCH_CORES=/path/to/cores`.

## RetroArch

The `.info` file is metadata, not part of the ABI:

- To load the core directly: `Main Menu → Load Core → Load Core from file`,
  then select `qtrebg_libretro.so`. No `.info` is required for this.
- To see it in RetroArch's core list and associate `.gb` content:
  - put `qtrebg_libretro.so` and `qtrebg_libretro.info` in the directory set
    by `Settings → Directory → Core`;
  - the core-info directory must be the same directory, or contain the
    `.info` file. In `retroarch.cfg` that is `libretro_directory` and
    `libretro_info_path`.
- The `.info` and `.so` basenames must match exactly.

If your core directory is the system one (for example
`/usr/lib/x86_64-linux-gnu/libretro`), copying there may need root. The
per-user `~/.config/retroarch/cores` directory is usually easier.

## Scope

- DMG only, `.gb` ROMs, single player.
- Video: 160×144, XRGB8888.
- Audio: 48 kHz stereo.
- Input: RetroPad A/B/Start/Select and D-pad.
- Save states: the core blob, with a 320 KiB `retro_serialize_size` buffer.
- Save RAM: exposed through `RETRO_MEMORY_SAVE_RAM`.
- BIOS: optional `dmg_boot.bin` / `dmg_bios.bin` in the frontend's system
  directory.
- RTC: current QtreGB behavior.
- Input descriptors and controller info are published to the frontend, so
  RetroArch's Quick Menu → Controls shows D-pad, A, B, Start and Select.
- Link cable and configurable palette are not included yet.

The core is tested with a local libretro host; RetroArch is not required to
build it. Installing/copying the `.so` into a frontend is left to the user.
