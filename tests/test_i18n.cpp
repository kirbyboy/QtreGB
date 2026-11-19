#include "doctest.h"
#include "i18n.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace
{

const std::filesystem::path I18N_TEST_DIR =
	std::filesystem::temp_directory_path() / "qtrebg_i18n_test";

void write_catalog(const std::filesystem::path& path, const std::string& text)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	REQUIRE(output.good());
	output << text;
}

} // namespace

TEST_CASE("i18n: Spanish catalog and English fallback")
{
	std::filesystem::remove_all(I18N_TEST_DIR);
	const std::filesystem::path catalog = I18N_TEST_DIR / "es.po";
	write_catalog(catalog,
		"msgid \"\"\n"
		"msgstr \"\"\n"
		"\"Language: es\\n\"\n"
		"\n"
		"msgid \"File\"\n"
		"msgstr \"Archivo\"\n"
		"\n"
		"msgid \"Hello %s\"\n"
		"msgstr \"Hola %s\"\n"
		"\n"
		"msgctxt \"menu\"\n"
		"msgid \"Open\"\n"
		"msgstr \"Abrir\"\n");

	i18n::set_language_directory(I18N_TEST_DIR.string());
	i18n::set_locale("es");
	CHECK(std::string(i18n::tr("File")) == "Archivo");
	CHECK(std::string(i18n::tr("Missing")) == "Missing");
	CHECK(i18n::format("Hello %s", "QtreGB") == "Hola QtreGB");
	CHECK(std::string(i18n::tr("menu", "Open")) == "Abrir");

	i18n::set_locale("en");
	CHECK(std::string(i18n::tr("File")) == "File");

	i18n::set_language_directory({});
	i18n::set_locale("system");
	std::filesystem::remove_all(I18N_TEST_DIR);
}

TEST_CASE("i18n: format falls back to the English source")
{
	i18n::set_language_directory({});
	i18n::set_locale("en");
	CHECK(i18n::format("Value %d", 7) == "Value 7");
	CHECK(i18n::format("Two values: %s, %d", "x", 3) ==
		"Two values: x, 3");
}

TEST_CASE("i18n: bundled Spanish catalog")
{
	const std::filesystem::path source_root =
		std::filesystem::path(__FILE__).parent_path().parent_path();
	const std::filesystem::path lang_dir = source_root / "lang";
	REQUIRE(std::filesystem::exists(lang_dir / "es.po"));

	i18n::set_language_directory(lang_dir.u8string());
	i18n::set_locale("es");
	CHECK(std::string(i18n::tr("File")) == "Archivo");
	CHECK(i18n::format("Player %d", 1) == "Jugador 1");
	CHECK(std::string(i18n::tr("Missing entry")) == "Missing entry");

	i18n::set_language_directory({});
	i18n::set_locale("system");
}
