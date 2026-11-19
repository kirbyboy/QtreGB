# QtreGB translations

QtreGB uses English source strings as translation keys and as the fallback.
Spanish lives in `es.po`.

- The runtime looks for `<language_directory>/<locale>.po`.
- `QTR_LANG_DIR` overrides the language directory.
- Otherwise the app tries `lang/` next to the executable, `../lang/`
  (the source-tree layout when running `build/QtreGB-GUI`), and `lang/` in the
  current directory.
- The language setting is stored in `QtreGB.ini` as `[ui] language=system`.
- Supported values are `system`, `en` and `es`. English does not need a
  catalog because the source text is already English.

The `.po` files use the subset of gettext understood by `gui/src/i18n.cpp`:
`msgid`, `msgstr`, quoted continuations and optional `msgctxt`. Entries are
separated by blank lines.
