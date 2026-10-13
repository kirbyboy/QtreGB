#include "QtreGB.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace qtreGB;

namespace
{

// The blargg tests print their result over the serial port as text
// ("Passed all tests" / "Failed #xx") instead of Mooneye's Fibonacci
// protocol, so the verdict is searched for in the serial capture.
constexpr long DEFAULT_MAX_TCYCLES = 100'000'000;
constexpr u16 SB_ADDR = 0xFF01;
constexpr u16 SC_ADDR = 0xFF02;

struct TestResult
{
	const char* verdict = "TIMEOUT";
	long cycles = 0;
	std::string serial;
	std::string diagnostics;
};

bool load_rom(const std::string& path, std::vector<u8>& rom, std::string& error)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);

	if(!file)
	{
		error = "could not open";
		return false;
	}

	const std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);

	rom.resize(static_cast<size_t>(size));
	if(size > 0 && !file.read(reinterpret_cast<char*>(rom.data()), size))
	{
		error = "could not read";
		return false;
	}

	return true;
}

std::string basename_of(const std::string& path)
{
	const size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

// On timeout the blargg shell leaves the state in memory: signature $DE,$B0,$A1
// at $A001-$A003, final code at $A000 and text at $A004 (test readme).
std::string build_diagnostics(QtreGB& emu)
{
	TaggedSerializer state_out(TaggedSerializer::Mode::Write);
	emu.m_cpu.serialize(state_out);
	Z80GB::State state;
	TaggedSerializer state_in(TaggedSerializer::Mode::Read, state_out.data());
	state.serialize(state_in);

	std::ostringstream out;
	out << "PC=0x" << std::hex << std::setw(4) << std::setfill('0')
	    << state.pc
	    << " IF=0x" << std::setw(2) << unsigned(emu.m_bus.read(0xFF0F) & 0x1F)
	    << " IE=0x" << std::setw(2) << unsigned(emu.m_bus.read(0xFFFF))
	    << " TIMA=0x" << std::setw(2) << unsigned(emu.m_bus.read(0xFF05))
	    << " DIV=0x" << std::setw(2) << unsigned(emu.m_bus.read(0xFF04))
	    << " TAC=0x" << std::setw(2) << unsigned(emu.m_bus.read(0xFF07));

	if(emu.m_bus.read(0xA001) == 0xDE && emu.m_bus.read(0xA002) == 0xB0 &&
	   emu.m_bus.read(0xA003) == 0x61)
	{
		out << " A000=0x" << std::setw(2) << unsigned(emu.m_bus.read(0xA000))
		    << " text=\"";
		for(int i = 0; i < 160; i++)
		{
			const char c = static_cast<char>(emu.m_bus.read(0xA004 + i));
			if(c == '\0') break;
			out << c;
		}
		out << "\"";
	}
	return out.str();
}

// Besides the serial output, the blargg shell leaves signature $DE,$B0,$A1 at
// $A001-$A003, status at $A000 ($80 = running, 0 = pass) and text at $A004.
std::string read_memory_text(QtreGB& emu)
{
	std::string text;
	for(int i = 0; i < 400; i++)
	{
		const char c = static_cast<char>(emu.m_bus.read(0xA004 + i));
		if(c == '\0') break;
		text.push_back(c);
	}
	return text;
}

TestResult run_rom(const std::vector<u8>& rom, long max_cycles)
{
	TestResult result;
	QtreGB emu;
	emu.load_cart(rom);
	u8 prev_sc = 0;

	for(long t = 0; t < max_cycles; t++)
	{
		// Capture only on the 0 -> 0x81 edge: QtreGB::tick takes until the end
		// of the instruction to consume SC, so polling "SC==0x81" on
		// every tick would duplicate the byte if the write and the end separate.
		const u8 sc = emu.m_bus.read(SC_ADDR);
		if(prev_sc != 0x81 && sc == 0x81)
		{
			result.serial.push_back(static_cast<char>(emu.m_bus.read(SB_ADDR)));

			if(result.serial.find("Passed") != std::string::npos)
			{
				result.verdict = "PASS";
				result.cycles = t;
				return result;
			}
			if(result.serial.find("Failed") != std::string::npos)
			{
				result.verdict = "FAIL";
				result.cycles = t;
				return result;
			}
		}
		prev_sc = sc;

		// Some singles (mem_timing-2) do not emit serial: their result only
		// appears at $A000 when they finish; polling is cheap and nothing is lost.
		if((t & 0xFFF) == 0 &&
		   emu.m_bus.read(0xA001) == 0xDE && emu.m_bus.read(0xA002) == 0xB0 &&
		   emu.m_bus.read(0xA003) == 0x61)
		{
			const u8 status = emu.m_bus.read(0xA000);
			if(status != 0x80)
			{
				result.verdict = status == 0 ? "PASS" : "FAIL";
				result.cycles = t;
				result.serial = read_memory_text(emu);
				return result;
			}
		}

		emu.tick();
	}

	result.cycles = max_cycles;
	result.diagnostics = build_diagnostics(emu);
	return result;
}

void print_serial(const std::string& serial)
{
	size_t start = 0;
	while(start < serial.size())
	{
		size_t end = serial.find('\n', start);
		if(end == std::string::npos) end = serial.size();
		std::cout << "      | " << serial.substr(start, end - start) << "\n";
		start = end + 1;
	}
}

} // namespace

int main(int argc, char** argv)
{
	if(argc < 2)
	{
		std::cerr << "usage: " << argv[0] << " <rom.gb> [rom.gb ...]\n";
		return 2;
	}

	long max_cycles = DEFAULT_MAX_TCYCLES;
	if(const char* env = std::getenv("BLARGG_MAX_T"))
	{
		max_cycles = std::strtol(env, nullptr, 10);
	}

	int passed = 0;
	int failed = 0;
	int pending = 0;

	std::cout << std::uppercase << std::setfill('0');

	for(int i = 1; i < argc; i++)
	{
		const std::string path = argv[i];
		const std::string name = basename_of(path);

		std::vector<u8> rom;
		std::string error;
		if(!load_rom(path, rom, error))
		{
			std::cout << std::dec << name << "  ERROR (" << error << ")\n";
			pending++;
			continue;
		}

		const TestResult result = run_rom(rom, max_cycles);

		std::cout << std::dec << std::setfill(' ') << std::setw(28) << std::left
		          << name << std::right << std::setw(8) << result.verdict
		          << "  " << result.cycles << " T-cycles\n";

		if(!result.diagnostics.empty())
		{
			std::cout << "      " << result.diagnostics << "\n";
		}
		print_serial(result.serial);

		if(std::string(result.verdict) == "PASS")	passed++;
		else if(std::string(result.verdict) == "FAIL")	failed++;
		else						pending++;
	}

	std::cout << "\n" << passed << " passed, " << failed << " failed, "
	          << pending << " no verdict\n";

	return failed == 0 && pending == 0 ? 0 : 1;
}
