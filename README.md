<img src="assets/QtreGB_icon_generic_256.png" width="96" align="right" alt="QtreGB icon">

# QtreGB

Game Boy emulator in C++ using SDL2 and ImGui. The goal is to keep the core
independent from the GUI.

## License

QtreGB is free software licensed under **GPL-3.0-or-later**. See
[`LICENSE.txt`](LICENSE.txt).

The project-original visual assets, including the application icon, are
dedicated to the public domain under **CC0 1.0**. See
[`assets/LICENSE`](assets/LICENSE).

The QtreGB name and icon are unregistered trademarks. Their use is governed by
[`TRADEMARK.md`](TRADEMARK.md).

Third-party components and their licenses are listed in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

## Dependencies

QtreGB currently uses SDL2, Dear ImGui, tinyfiledialogs and doctest. A vendored
copy of ImGuiFileDialog is prepared under `third_party/ImGuiFileDialog` for the
future in-app file dialogs.
