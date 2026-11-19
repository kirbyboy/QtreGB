#ifndef QTREGB_I18N_H
#define QTREGB_I18N_H

#include <string>

// Minimal gettext-style catalog for the ImGui frontend. English source text is
// the key and the fallback; translations live in <language_directory>/<locale>.po.
// The module is single-threaded like the rest of the UI.
namespace i18n
{
// Directory containing <locale>.po. Call once at startup before set_locale().
void set_language_directory(std::string directory);

// "system", "en" or "es". Missing catalogs fall back to the English source.
// "en_XA" is a test-only pseudo-locale; it needs no .po file.
void set_locale(const std::string& locale);

// The requested setting ("system", "en", "es").
const std::string& locale();

// The concrete catalog in use ("en" or "es").
const std::string& resolved_locale();

// Resolves LANGUAGE/LC_ALL/LC_MESSAGES/LANG to a supported catalog.
std::string detect_system_locale();

// English source text is the key. The returned pointer is owned by the
// catalog and stays valid until the next set_locale()/clear_catalog().
const char* tr(const char* text);

// Optional context overload. Falls back to the plain text if the translated
// entry has no context.
const char* tr(const char* context, const char* text);

// Translates a printf-style format string and formats it. Use this for
// messages that contain player names, paths or other dynamic data.
std::string format(const char* text, ...);

// Catalog management, mainly for tests.
bool load_catalog(const std::string& path);
void clear_catalog();
} // namespace i18n

#endif
