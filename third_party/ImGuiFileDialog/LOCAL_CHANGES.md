# Local changes to ImGuiFileDialog

This directory is a vendored copy of **ImGuiFileDialog v0.6.8**, upstream commit
`ee77f190861193a995435c388682f1512f6fd29a`
(<https://github.com/aiekick/ImGuiFileDialog>).

It is **not QtreGB code**. The original library and these local changes are
distributed under the MIT license in `LICENSE`; the original copyright notice is
kept in the source files.

## Why it is vendored

The upstream library defines its UI strings as compile-time macros. QtreGB
needs to switch the whole UI language at runtime, including the file dialog
chrome. The source is vendored so the complete modified copy is visible and
editable, and it stays pinned until the project decides to update it.

## Changes

- Added `ImGuiFileDialogStrings.h` with the `IGFD::FileDialogStrings` table.
- Added `IGFD::FileDialog::SetStrings()` / `GetStrings()` in
  `ImGuiFileDialog.h` and `ImGuiFileDialog.cpp`.
- Noted the runtime API in `ImGuiFileDialogConfig.h`; the compile-time macros
  remain as optional overrides.
- The string macros in `ImGuiFileDialog.cpp` now resolve to the runtime table
  instead of string literals. The existing `#ifndef` guards are kept so a
  compile-time override still wins.
- Labels that used literal concatenation (`okButtonString "##..."`,
  `OverWriteDialogTitleString "##"`, places buttons, etc.) now build a
  `std::string` label; the `##` stable IDs are preserved.
- `ImGui::Text`/`ImGui::SetTooltip` calls that receive a runtime string use
  `TextUnformatted` or `"%s"` so translated text is never used as a format
  string.

The application side belongs in QtreGB's i18n layer; it will fill the table
from `i18n::tr()` whenever the language changes.

## Updating

There is no automatic upstream sync. To update:

1. Replace this directory with the new upstream release.
2. Reapply the changes above (or merge them manually).
3. Keep `LICENSE`, this file and the upstream version/commit visible.
4. Run the i18n check and the file-dialog smoke.
