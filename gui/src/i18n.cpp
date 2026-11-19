#include "i18n.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
std::string trim(std::string_view view)
{
	const std::size_t first = view.find_first_not_of(" \t\r\n");
	if (first == std::string_view::npos)
	{
		return {};
	}
	const std::size_t last = view.find_last_not_of(" \t\r\n");
	return std::string(view.substr(first, last - first + 1));
}

std::string lowercase(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(),
	               [](unsigned char c)
	               {
		               return static_cast<char>(std::tolower(c));
	               });
	return value;
}

// Returns "es" or "en" for supported locales, or an empty string when the
// candidate is not supported.
std::string supported_locale(const std::string& value)
{
	std::string normalized = lowercase(trim(value));
	const std::size_t encoding = normalized.find_first_of(".@");
	if (encoding != std::string::npos)
	{
		normalized.erase(encoding);
	}
	// Test-only pseudo-locale. Check it before stripping the region suffix.
	if (normalized == "en_xa" || normalized == "en-xa")
	{
		return "en_XA";
	}
	const std::size_t separator = normalized.find_first_of("_-");
	if (separator != std::string::npos)
	{
		normalized.erase(separator);
	}
	if (normalized == "es")
	{
		return "es";
	}
	if (normalized == "en" || normalized == "c" || normalized == "posix")
	{
		return "en";
	}
	return {};
}

std::string parse_quoted(const std::string& line)
{
	const std::size_t first = line.find('"');
	if (first == std::string::npos)
	{
		return {};
	}

	std::string out;
	bool escaped = false;
	for (std::size_t index = first + 1; index < line.size(); ++index)
	{
		const char character = line[index];
		if (escaped)
		{
			switch (character)
			{
				case 'n':
					out += '\n';
					break;
				case 't':
					out += '\t';
					break;
				case 'r':
					out += '\r';
					break;
				case '"':
					out += '"';
					break;
				case '\\':
					out += '\\';
					break;
				default:
					out += character;
					break;
			}
			escaped = false;
		}
		else if (character == '\\')
		{
			escaped = true;
		}
		else if (character == '"')
		{
			break;
		}
		else
		{
			out += character;
		}
	}
	return out;
}

std::string pseudolocalize(const std::string& text)
{
	std::string out;
	out.reserve(text.size() + 12);
	out += "[!! ";
	for (const char character : text)
	{
		switch (character)
		{
			case 'a':
				out += "á";
				break;
			case 'e':
				out += "é";
				break;
			case 'i':
				out += "í";
				break;
			case 'o':
				out += "ó";
				break;
			case 'u':
				out += "ú";
				break;
			case 'A':
				out += "Á";
				break;
			case 'E':
				out += "É";
				break;
			case 'I':
				out += "Í";
				break;
			case 'O':
				out += "Ó";
				break;
			case 'U':
				out += "Ú";
				break;
			default:
				out += character;
				break;
		}
	}
	out += " !!]";
	return out;
}

void parse_block(const std::vector<std::string>& lines,
                 std::unordered_map<std::string, std::string>& catalog)
{
	std::string context;
	std::string msgid;
	std::string msgstr;

	enum class Field
	{
		None,
		Context,
		Id,
		Str,
	};
	Field field = Field::None;

	for (const std::string& raw : lines)
	{
		const std::string line = trim(raw);
		if (line.empty() || line.front() == '#')
		{
			continue;
		}

		if (line.rfind("msgctxt", 0) == 0)
		{
			field = Field::Context;
			context = parse_quoted(line);
		}
		else if (line.rfind("msgid_plural", 0) == 0)
		{
			// The project currently has no plural entries; ignore them.
			field = Field::None;
		}
		else if (line.rfind("msgid", 0) == 0)
		{
			field = Field::Id;
			msgid = parse_quoted(line);
		}
		else if (line.rfind("msgstr[", 0) == 0)
		{
			field = Field::None;
		}
		else if (line.rfind("msgstr", 0) == 0)
		{
			field = Field::Str;
			msgstr = parse_quoted(line);
		}
		else if (line.front() == '"')
		{
			const std::string continuation = parse_quoted(line);
			if (field == Field::Context)
			{
				context += continuation;
			}
			else if (field == Field::Id)
			{
				msgid += continuation;
			}
			else if (field == Field::Str)
			{
				msgstr += continuation;
			}
		}
	}

	if (msgid.empty() || msgstr.empty())
	{
		return;
	}

	if (context.empty())
	{
		catalog[msgid] = msgstr;
	}
	else
	{
		catalog[context + '\x1f' + msgid] = msgstr;
		if (catalog.find(msgid) == catalog.end())
		{
			catalog[msgid] = msgstr;
		}
	}
}
} // namespace

namespace i18n
{
namespace
{
std::string g_directory;
std::string g_locale = "system";
std::string g_resolved = "en";
bool g_pseudo = false;
std::unordered_map<std::string, std::string> g_catalog;
std::unordered_map<std::string, std::string> g_pseudo_cache;
} // namespace

void set_language_directory(std::string directory)
{
	g_directory = std::move(directory);
}

void set_locale(const std::string& locale)
{
	g_locale = locale.empty() ? "system" : locale;
	g_resolved = g_locale == "system"
		? detect_system_locale()
		: supported_locale(g_locale);
	if (g_resolved.empty())
	{
		g_resolved = "en";
	}
	g_pseudo = g_resolved == "en_XA";

	clear_catalog();

	if (g_pseudo || g_resolved == "en" || g_directory.empty())
	{
		return;
	}

	const std::filesystem::path path =
		std::filesystem::path(g_directory) / (g_resolved + ".po");
	if (!load_catalog(path.string()))
	{
		std::fprintf(stderr, "i18n: could not load %s\n", path.c_str());
	}
}

const std::string& locale()
{
	return g_locale;
}

const std::string& resolved_locale()
{
	return g_resolved;
}

std::string detect_system_locale()
{
	if (const char* language = std::getenv("LANGUAGE"))
	{
		std::string list(language);
		std::size_t start = 0;
		while (start <= list.size())
		{
			const std::size_t end = list.find(':', start);
			const std::string candidate = list.substr(
				start, end == std::string::npos ? std::string::npos : end - start);
			const std::string supported = supported_locale(candidate);
			if (!supported.empty())
			{
				return supported;
			}
			if (end == std::string::npos)
			{
				break;
			}
			start = end + 1;
		}
	}

	for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"})
	{
		if (const char* value = std::getenv(name))
		{
			const std::string supported = supported_locale(value);
			if (!supported.empty())
			{
				return supported;
			}
		}
	}

	return "en";
}

const char* tr(const char* text)
{
	if (!text)
	{
		return "";
	}
	if (g_pseudo)
	{
		const auto existing = g_pseudo_cache.find(text);
		if (existing != g_pseudo_cache.end())
		{
			return existing->second.c_str();
		}
		return g_pseudo_cache.emplace(text, pseudolocalize(text))
			.first->second.c_str();
	}
	const auto entry = g_catalog.find(text);
	return entry == g_catalog.end() ? text : entry->second.c_str();
}

const char* tr(const char* context, const char* text)
{
	if (context && *context && text)
	{
		const std::string key = std::string(context) + '\x1f' + text;
		const auto entry = g_catalog.find(key);
		if (entry != g_catalog.end())
		{
			return entry->second.c_str();
		}
	}
	return tr(text);
}

std::string format(const char* text, ...)
{
	const char* translated = tr(text);
	if (translated == nullptr)
	{
		return {};
	}

	char stack_buffer[512];
	va_list args;
	va_start(args, text);
	va_list copy;
	va_copy(copy, args);
	const int needed = std::vsnprintf(stack_buffer, sizeof(stack_buffer),
		translated, copy);
	va_end(copy);
	if (needed < 0)
	{
		va_end(args);
		return translated;
	}
	if (static_cast<size_t>(needed) < sizeof(stack_buffer))
	{
		va_end(args);
		return std::string(stack_buffer, static_cast<size_t>(needed));
	}

	std::vector<char> buffer(static_cast<size_t>(needed) + 1);
	std::vsnprintf(buffer.data(), buffer.size(), translated, args);
	va_end(args);
	return std::string(buffer.data(), static_cast<size_t>(needed));
}

bool load_catalog(const std::string& path)
{
	std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
	if (!input)
	{
		clear_catalog();
		return false;
	}

	std::unordered_map<std::string, std::string> catalog;
	std::vector<std::string> block;
	std::string line;
	bool first_line = true;

	while (std::getline(input, line))
	{
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}
		if (first_line && line.size() >= 3 &&
		    static_cast<unsigned char>(line[0]) == 0xef &&
		    static_cast<unsigned char>(line[1]) == 0xbb &&
		    static_cast<unsigned char>(line[2]) == 0xbf)
		{
			line.erase(0, 3);
		}
		first_line = false;

		if (line.empty())
		{
			parse_block(block, catalog);
			block.clear();
		}
		else
		{
			block.push_back(line);
		}
	}
	parse_block(block, catalog);

	g_catalog = std::move(catalog);
	return true;
}

void clear_catalog()
{
	g_catalog.clear();
	g_pseudo_cache.clear();
}
} // namespace i18n
