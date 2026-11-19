#include "QtreGB.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace qtreGB;

namespace
{

constexpr int TICKS_PER_FRAME = 70224;
constexpr const char* GOLDEN_TAG = "qtr-regress v1";

struct Options
{
	std::string manifest = "manifest.txt";
	std::string golden = "golden.txt";
	std::string bios;
	bool update = false;
	bool input = false;
	bool allow_missing = false;
	bool verbose = false;
};

struct Entry
{
	std::string name;
	std::string path;
	int frames = 0;
	bool require_audio = false;
};

struct FrameResult
{
	std::string name;
	int frame = 0;
	u64 framebuffer_hash = 0;
	size_t pcm_frames = 0;
	u64 pcm_hash = 0;
	size_t pcm_nonzero = 0;
};

std::string trim(const std::string& text)
{
	const size_t begin = text.find_first_not_of(" \t\r\n");
	if(begin == std::string::npos)
	{
		return {};
	}
	const size_t end = text.find_last_not_of(" \t\r\n");
	return text.substr(begin, end - begin + 1);
}

std::string dirname_of(const std::string& path)
{
	const size_t slash = path.find_last_of("/\\");
	if(slash == std::string::npos)
	{
		return ".";
	}
	if(slash == 0)
	{
		return "/";
	}
	return path.substr(0, slash);
}

bool is_absolute(const std::string& path)
{
	return !path.empty() && (path[0] == '/' || path[0] == '\\');
}

std::string resolve_path(const std::string& base, const std::string& path)
{
	if(is_absolute(path))
	{
		return path;
	}
	if(base.empty() || base == ".")
	{
		return path;
	}
	return base + "/" + path;
}

bool read_file(const std::string& path, std::vector<u8>& out)
{
	std::ifstream in(path, std::ios::binary);
	if(!in)
	{
		return false;
	}
	out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	return in.good() || in.eof();
}

bool parse_int(const std::string& text, int minimum, int& value)
{
	if(text.empty())
	{
		return false;
	}
	char* end = nullptr;
	errno = 0;
	const long parsed = std::strtol(text.c_str(), &end, 10);
	if(errno != 0 || end == text.c_str() || *end != '\0' || parsed < minimum ||
	   parsed > 0x7FFFFFFF)
	{
		return false;
	}
	value = static_cast<int>(parsed);
	return true;
}

bool parse_u64(const std::string& text, u64& value)
{
	if(text.empty())
	{
		return false;
	}
	char* end = nullptr;
	errno = 0;
	const unsigned long long parsed = std::strtoull(text.c_str(), &end, 0);
	if(errno != 0 || end == text.c_str() || *end != '\0')
	{
		return false;
	}
	value = static_cast<u64>(parsed);
	return true;
}

u64 fnv1a(const void* data, size_t size)
{
	const u8* bytes = static_cast<const u8*>(data);
	u64 hash = 0xCBF29CE484222325ull;
	for(size_t i = 0; i < size; ++i)
	{
		hash ^= bytes[i];
		hash *= 0x100000001B3ull;
	}
	return hash;
}

std::string hex64(u64 value)
{
	std::ostringstream out;
	out << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
	return out.str();
}

void usage(const char* argv0)
{
	std::cerr
		<< "usage: " << argv0 << " [options]\n"
		<< "  --manifest PATH    manifest file (default: manifest.txt)\n"
		<< "  --golden PATH      golden file (default: golden.txt)\n"
		<< "  --bios PATH        DMG boot ROM (256 bytes), optional\n"
		<< "  --update           rewrite the golden file from this run\n"
		<< "  --input            apply the fixed input script\n"
		<< "  --allow-missing    skip manifest ROMs that are not present\n"
		<< "  --verbose          per-ROM summary\n"
		<< "  --help             show this help\n";
}

bool parse_args(int argc, char** argv, Options& options)
{
	for(int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];
		auto next = [&](std::string& out) -> bool
		{
			if(i + 1 >= argc)
			{
				std::cerr << "missing value for " << arg << "\n";
				return false;
			}
			out = argv[++i];
			return true;
		};

		if(arg == "--manifest")
		{
			if(!next(options.manifest)) return false;
		}
		else if(arg == "--golden")
		{
			if(!next(options.golden)) return false;
		}
		else if(arg == "--bios")
		{
			if(!next(options.bios)) return false;
		}
		else if(arg == "--update")
		{
			options.update = true;
		}
		else if(arg == "--input")
		{
			options.input = true;
		}
		else if(arg == "--allow-missing")
		{
			options.allow_missing = true;
		}
		else if(arg == "--verbose")
		{
			options.verbose = true;
		}
		else if(arg == "--help" || arg == "-h")
		{
			usage(argv[0]);
			std::exit(0);
		}
		else
		{
			std::cerr << "unknown option: " << arg << "\n";
			usage(argv[0]);
			return false;
		}
	}
	return true;
}

bool load_manifest(const std::string& path, std::vector<Entry>& entries,
                   std::string& error)
{
	std::ifstream in(path);
	if(!in)
	{
		error = "cannot open manifest: " + path;
		return false;
	}

	const std::string base = dirname_of(path);
	std::set<std::string> names;
	std::string line;
	int line_number = 0;

	while(std::getline(in, line))
	{
		++line_number;
		const std::string text = trim(line);
		if(text.empty() || text[0] == '#')
		{
			continue;
		}

		size_t separator = text.find('\t');
		if(separator == std::string::npos)
		{
			separator = text.find(' ');
		}
		if(separator == std::string::npos)
		{
			error = path + ":" + std::to_string(line_number) +
				": expected <frames><TAB><rom>";
			return false;
		}

		Entry entry;
		const std::string frames_text = trim(text.substr(0, separator));
		entry.name = trim(text.substr(separator + 1));

		// Optional trailing flag: <frames><TAB><path><TAB>audio
		const size_t flag_separator = entry.name.find_last_of('\t');
		if(flag_separator != std::string::npos)
		{
			const std::string flag = trim(entry.name.substr(flag_separator + 1));
			if(flag != "audio")
			{
				error = path + ":" + std::to_string(line_number) +
					": unknown flag: " + flag;
				return false;
			}
			entry.require_audio = true;
			entry.name = trim(entry.name.substr(0, flag_separator));
		}

		if(!parse_int(frames_text, 1, entry.frames) || entry.name.empty())
		{
			error = path + ":" + std::to_string(line_number) +
				": invalid frames or ROM path";
			return false;
		}
		if(!names.insert(entry.name).second)
		{
			error = path + ":" + std::to_string(line_number) +
				": duplicate ROM entry: " + entry.name;
			return false;
		}

		entry.path = resolve_path(base, entry.name);
		entries.push_back(entry);
	}

	if(entries.empty())
	{
		error = "manifest has no ROM entries: " + path;
		return false;
	}
	return true;
}

bool parse_golden_header(const std::string& line, const Options& options,
                         std::string& error)
{
	std::istringstream in(line);
	std::string hash;
	std::string tag;
	std::string version;
	std::string input_tag;
	std::string bios_tag;
	in >> hash >> tag >> version >> input_tag >> bios_tag;

	const std::string expected_input = options.input ? "input=script" : "input=none";
	const std::string expected_bios = options.bios.empty() ? "bios=0" : "bios=1";

	if(hash != "#" || tag != "qtr-regress" || version != "v1")
	{
		error = "invalid golden header";
		return false;
	}
	if(input_tag != expected_input)
	{
		error = "golden input mode is " + input_tag + ", expected " +
			expected_input + " (regenerate with --update)";
		return false;
	}
	if(bios_tag != expected_bios)
	{
		error = "golden BIOS mode is " + bios_tag + ", expected " +
			expected_bios + " (regenerate with --update)";
		return false;
	}
	return true;
}

bool load_golden(const Options& options,
                 std::map<std::string, std::vector<FrameResult>>& expected,
                 std::string& error)
{
	std::ifstream in(options.golden);
	if(!in)
	{
		error = "cannot open golden file: " + options.golden +
			" (run with --update to create it)";
		return false;
	}

	std::string line;
	if(!std::getline(in, line))
	{
		error = "empty golden file: " + options.golden;
		return false;
	}
	if(!parse_golden_header(trim(line), options, error))
	{
		error += " (" + options.golden + ")";
		return false;
	}

	int line_number = 1;
	while(std::getline(in, line))
	{
		++line_number;
		const std::string trimmed = trim(line);
		if(trimmed.empty() || trimmed[0] == '#')
		{
			continue;
		}

		std::vector<std::string> fields;
		size_t begin = 0;
		for(;;)
		{
			const size_t tab = line.find('\t', begin);
			fields.push_back(line.substr(begin, tab == std::string::npos ?
				std::string::npos : tab - begin));
			if(tab == std::string::npos)
			{
				break;
			}
			begin = tab + 1;
		}
		if(fields.size() != 5)
		{
			error = options.golden + ":" + std::to_string(line_number) +
				": expected 5 tab-separated fields";
			return false;
		}

		FrameResult result;
		result.name = fields[0];
		if(result.name.empty())
		{
			error = options.golden + ":" + std::to_string(line_number) +
				": empty ROM name";
			return false;
		}
		int frame = 0;
		if(!parse_int(fields[1], 0, frame) || !parse_u64(fields[2], result.framebuffer_hash))
		{
			error = options.golden + ":" + std::to_string(line_number) +
				": invalid frame or framebuffer hash";
			return false;
		}
		int pcm_frames = 0;
		if(!parse_int(fields[3], 0, pcm_frames) || !parse_u64(fields[4], result.pcm_hash))
		{
			error = options.golden + ":" + std::to_string(line_number) +
				": invalid PCM frame count or hash";
			return false;
		}
		result.frame = frame;
		result.pcm_frames = static_cast<size_t>(pcm_frames);

		auto& list = expected[result.name];
		if(result.frame != static_cast<int>(list.size()))
		{
			error = options.golden + ":" + std::to_string(line_number) +
				": frames for " + result.name + " are not contiguous from 0";
			return false;
		}
		list.push_back(result);
	}

	if(expected.empty())
	{
		error = "golden file has no data: " + options.golden;
		return false;
	}
	return true;
}

void release_buttons(QtreGB& machine)
{
	machine.m_gamepad.m_button_start = false;
	machine.m_gamepad.m_button_select = false;
	machine.m_gamepad.m_button_b = false;
	machine.m_gamepad.m_button_a = false;
	machine.m_gamepad.m_button_down = false;
	machine.m_gamepad.m_button_up = false;
	machine.m_gamepad.m_button_left = false;
	machine.m_gamepad.m_button_right = false;
}

void apply_input(QtreGB& machine, int frame, bool script)
{
	release_buttons(machine);
	if(!script)
	{
		return;
	}

	const int phase = frame % 120;
	machine.m_gamepad.m_button_right = phase >= 20 && phase < 24;
	machine.m_gamepad.m_button_a = phase >= 50 && phase < 54;
	machine.m_gamepad.m_button_b = phase >= 70 && phase < 72;
	machine.m_gamepad.m_button_start = phase >= 90 && phase < 94;
}

bool run_entry(const Options& options, const Entry& entry,
               std::vector<FrameResult>& results, std::string& error)
{
	std::vector<u8> rom;
	if(!read_file(entry.path, rom))
	{
		error = "cannot read ROM: " + entry.path;
		return false;
	}

	QtreGB machine;
	if(!options.bios.empty())
	{
		std::vector<u8> bios;
		if(!read_file(options.bios, bios) || bios.size() != 0x100)
		{
			error = "cannot read a 256-byte DMG boot ROM: " + options.bios;
			return false;
		}
		if(!machine.load_boot_rom(std::move(bios)))
		{
			error = "load_boot_rom failed";
			return false;
		}
	}

	if(!machine.load_cart(std::move(rom)))
	{
		error = "load_cart failed (unsupported or invalid ROM): " + entry.path;
		return false;
	}

	std::vector<int16_t> pcm(APU::RING_FRAMES * 2);
	std::vector<u8> pcm_bytes;
	pcm_bytes.reserve(APU::RING_FRAMES * 4);
	size_t total_nonzero = 0;

	for(int frame = 0; frame < entry.frames; ++frame)
	{
		apply_input(machine, frame, options.input);

		for(int tick = 0; tick < TICKS_PER_FRAME; ++tick)
		{
			machine.tick();
		}

		const std::vector<u8>& framebuffer = machine.m_ppu.get_video_buffer();
		if(framebuffer.size() != static_cast<size_t>(PPU::XRES * PPU::YRES))
		{
			error = "PPU framebuffer has an unexpected size";
			return false;
		}

		FrameResult result;
		result.name = entry.name;
		result.frame = frame;
		result.framebuffer_hash = fnv1a(framebuffer.data(), framebuffer.size());

		pcm_bytes.clear();
		for(;;)
		{
			const size_t frames = machine.m_apu.read_samples(
				pcm.data(), APU::RING_FRAMES);
			if(frames == 0)
			{
				break;
			}
			for(size_t i = 0; i < frames * 2; ++i)
			{
				const u16 sample = static_cast<u16>(pcm[i]);
				pcm_bytes.push_back(static_cast<u8>(sample & 0xFF));
				pcm_bytes.push_back(static_cast<u8>((sample >> 8) & 0xFF));
				if(pcm[i] != 0)
				{
					++result.pcm_nonzero;
					++total_nonzero;
				}
			}
			result.pcm_frames += frames;
			if(frames < APU::RING_FRAMES)
			{
				break;
			}
		}
		result.pcm_hash = fnv1a(pcm_bytes.data(), pcm_bytes.size());
		results.push_back(result);
	}

	if(entry.require_audio && total_nonzero == 0)
	{
		error = "expected non-silent PCM from " + entry.name +
			" (APU powered off or not producing samples)";
		return false;
	}
	return true;
}

bool write_golden(const Options& options,
                  const std::vector<Entry>& entries,
                  const std::map<std::string, std::vector<FrameResult>>& results,
                  std::string& error)
{
	std::ofstream out(options.golden, std::ios::trunc);
	if(!out)
	{
		error = "cannot write golden file: " + options.golden;
		return false;
	}

	out << "# " << GOLDEN_TAG
	    << " input=" << (options.input ? "script" : "none")
	    << " bios=" << (options.bios.empty() ? "0" : "1") << "\n";

	size_t lines = 0;
	for(const Entry& entry : entries)
	{
		const auto found = results.find(entry.name);
		if(found == results.end())
		{
			continue;
		}
		for(const FrameResult& result : found->second)
		{
			out << result.name << '\t' << result.frame << '\t'
			    << hex64(result.framebuffer_hash) << '\t'
			    << result.pcm_frames << '\t'
			    << hex64(result.pcm_hash) << '\n';
			++lines;
		}
	}

	if(!out.good())
	{
		error = "error while writing golden file: " + options.golden;
		return false;
	}
	std::cout << "updated golden: " << options.golden
	          << " (" << lines << " frame lines)\n";
	return true;
}

int compare_results(
	const std::vector<Entry>& entries,
	const std::map<std::string, std::vector<FrameResult>>& expected,
	const std::map<std::string, std::vector<FrameResult>>& actual,
	const std::set<std::string>& skipped)
{
	int errors = 0;
	size_t frames = 0;

	for(const Entry& entry : entries)
	{
		if(skipped.count(entry.name) != 0)
		{
			continue;
		}
		const auto actual_it = actual.find(entry.name);
		if(actual_it == actual.end())
		{
			continue;
		}
		const auto expected_it = expected.find(entry.name);
		if(expected_it == expected.end())
		{
			std::cerr << "FAIL " << entry.name
			          << ": no golden reference (run --update)\n";
			++errors;
			continue;
		}

		const std::vector<FrameResult>& expected_frames = expected_it->second;
		const std::vector<FrameResult>& actual_frames = actual_it->second;
		if(expected_frames.size() != actual_frames.size())
		{
			std::cerr << "FAIL " << entry.name << ": expected "
			          << expected_frames.size() << " frames, got "
			          << actual_frames.size() << "\n";
			++errors;
			continue;
		}

		for(size_t i = 0; i < expected_frames.size(); ++i)
		{
			const FrameResult& want = expected_frames[i];
			const FrameResult& got = actual_frames[i];
			++frames;
			if(want.frame != got.frame)
			{
				std::cerr << "FAIL " << entry.name << ": frame order mismatch at "
				          << i << "\n";
				++errors;
				break;
			}
			if(want.framebuffer_hash != got.framebuffer_hash ||
			   want.pcm_frames != got.pcm_frames || want.pcm_hash != got.pcm_hash)
			{
				std::cerr << "FAIL " << entry.name << " frame " << got.frame << ":\n"
				          << "  fb  expected " << hex64(want.framebuffer_hash)
				          << " got " << hex64(got.framebuffer_hash) << "\n"
				          << "  pcm expected " << want.pcm_frames << " samples "
				          << hex64(want.pcm_hash) << "\n"
				          << "  pcm got      " << got.pcm_frames << " samples "
				          << hex64(got.pcm_hash) << "\n";
				++errors;
				break;
			}
		}
	}

	for(const auto& item : expected)
	{
		if(actual.count(item.first) == 0 && skipped.count(item.first) == 0)
		{
			std::cerr << "FAIL " << item.first
			          << ": present in golden but not run\n";
			++errors;
		}
	}

	if(errors == 0)
	{
		std::cout << "regress: " << actual.size()
		          << " ROMs, " << frames << " frames, all match\n";
	}
	else
	{
		std::cout << "regress: " << errors << " mismatch(es)\n";
	}
	return errors == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
	Options options;
	if(!parse_args(argc, argv, options))
	{
		return 2;
	}

	std::vector<Entry> entries;
	std::string error;
	if(!load_manifest(options.manifest, entries, error))
	{
		std::cerr << "regress: " << error << "\n";
		return 2;
	}

	std::map<std::string, std::vector<FrameResult>> actual;
	std::set<std::string> skipped;

	for(const Entry& entry : entries)
	{
		std::ifstream check(entry.path, std::ios::binary);
		if(!check)
		{
			if(options.allow_missing)
			{
				std::cout << "skip  " << entry.name << " (not found)\n";
				skipped.insert(entry.name);
				continue;
			}
			std::cerr << "regress: ROM not found: " << entry.path
			          << " (use --allow-missing to skip)\n";
			return 2;
		}

		std::vector<FrameResult> results;
		if(!run_entry(options, entry, results, error))
		{
			std::cerr << "regress: " << error << "\n";
			return 2;
		}
		actual[entry.name] = std::move(results);

		if(options.verbose)
		{
			const std::vector<FrameResult>& list = actual[entry.name];
			size_t pcm_total = 0;
			size_t pcm_nonzero = 0;
			for(const FrameResult& result : list)
			{
				pcm_total += result.pcm_frames;
				pcm_nonzero += result.pcm_nonzero;
			}
			std::cout << "run   " << entry.name << ": " << entry.frames
			          << " frames, " << pcm_total << " PCM samples ("
			          << pcm_nonzero << " nonzero), last fb="
			          << hex64(list.back().framebuffer_hash) << ", last pcm="
			          << hex64(list.back().pcm_hash) << "\n";
		}
	}

	if(options.update)
	{
		if(!write_golden(options, entries, actual, error))
		{
			std::cerr << "regress: " << error << "\n";
			return 2;
		}
		return 0;
	}

	std::map<std::string, std::vector<FrameResult>> expected;
	if(!load_golden(options, expected, error))
	{
		std::cerr << "regress: " << error << "\n";
		return 2;
	}

	return compare_results(entries, expected, actual, skipped);
}
