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

// Mooneye verdict protocol (common/lib/quit.s):
//   success -> B/C/D/E/H/L = 3, 5, 8, 13, 21, 34 (Fibonacci)
//   failure -> B/C/D/E/H/L = 0x42
// They must be read at the first "magic_breakpoint" (LD B, B = 0x40),
// which the test suite emits right before dumping over the serial port.
enum class Verdict
{
	Pending,
	Pass,
	Fail,
};

constexpr u8 FAIL_MARKER = 0x42;
constexpr u8 PASS_MARKER[6] = {3, 5, 8, 13, 21, 34};

constexpr long DEFAULT_MAX_TCYCLES = 30000000;
// The real DMG boot ROM spends ~23 M T-cycles before entering the cartridge,
// so BIOS runs need a larger budget.
constexpr long BIOS_MAX_TCYCLES = 60000000;
constexpr u8 MAGIC_BREAKPOINT = 0x40;

// HRAM map left by setup_assertions (check_asserts_cb.s). The symbols come
// from the .sym file shipped with each ROM: setup_assertions pushes hl/de/bc/af
// with SP = hram.regs_save + 8, so the map is fixed for the whole suite.
constexpr u16 HRAM_FLAGS = 0xFF88;
constexpr u16 HRAM_TIMA = 0xFF05;

struct AssertReg
{
	char name;
	u16 actual;
	u16 expected;
};

constexpr AssertReg ASSERT_REGS[8] = {
	{'A', 0xFF81, 0xFF8A},
	{'F', 0xFF80, 0xFF89},
	{'B', 0xFF83, 0xFF8C},
	{'C', 0xFF82, 0xFF8B},
	{'D', 0xFF85, 0xFF8E},
	{'E', 0xFF84, 0xFF8D},
	{'H', 0xFF87, 0xFF90},
	{'L', 0xFF86, 0xFF8F},
};

struct Assertion
{
	char name;
	u8 expected;
	u8 actual;
};

struct TestResult
{
	Verdict verdict = Verdict::Pending;
	long cycles = 0;
	std::vector<Assertion> assertions;
	int failures = 0;
	int fail_round = -1;
	u8 fail_expect = 0;
	u8 fail_actual = 0;
};

const char* to_string(Verdict verdict)
{
	switch(verdict)
	{
		case Verdict::Pass:	return "PASS";
		case Verdict::Fail:	return "FAIL";
		default:		return "TIMEOUT";
	}
}

// Writes the PPU framebuffer as a binary PGM (P5) with the DMG palette.
// The core stores color indices 0..3, so the harness maps them to the
// grayscale values used by the reference screenshots (0 -> white).
bool dump_framebuffer_pgm(QtreGB& emu, const std::string& path)
{
	const std::vector<u8>& fb = emu.m_ppu.get_video_buffer();
	if(fb.size() != static_cast<size_t>(PPU::XRES * PPU::YRES))
	{
		return false;
	}

	std::ofstream out(path, std::ios::binary);
	if(!out)
	{
		return false;
	}

	static const u8 palette[4] = {255, 170, 85, 0};

	out << "P5\n" << PPU::XRES << " " << PPU::YRES << "\n255\n";
	for(u8 index : fb)
	{
		out.put(static_cast<char>(palette[index & 0x03]));
	}

	return out.good();
}

CpuState read_cpu_state(QtreGB& emu)
{
	CpuState state;
	state.a = emu.m_cpu.get_a();
	state.f = emu.m_cpu.get_f();
	state.b = emu.m_cpu.get_b();
	state.c = emu.m_cpu.get_c();
	state.d = emu.m_cpu.get_d();
	state.e = emu.m_cpu.get_e();
	state.h = emu.m_cpu.get_h();
	state.l = emu.m_cpu.get_l();
	state.pc = emu.m_cpu.get_pc();
	state.sp = emu.m_cpu.get_sp();
	return state;
}

Verdict read_verdict(const CpuState& state)
{
	const u8 regs[6] = {state.b, state.c, state.d,
	                    state.e, state.h, state.l};

	bool all_pass = true;
	bool all_fail = true;

	for(int i = 0; i < 6; i++)
	{
		if(regs[i] != PASS_MARKER[i])	all_pass = false;
		if(regs[i] != FAIL_MARKER)	all_fail = false;
	}

	if(all_pass)	return Verdict::Pass;
	if(all_fail)	return Verdict::Fail;
	return Verdict::Pending;
}

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

	// Pre-validate the cartridge: QtreGB::load_cart does not expose the failure
	// and a null cart would crash (segfault) on the first fetch. main() reports ERROR.
	switch(Cart().load_cart(rom))
	{
		case 1: error = "cartridge: ROM too small"; return false;
		case 2: error = "cartridge: invalid ROM size"; return false;
		case 3: error = "cartridge: size does not match header"; return false;
		case 4: error = "cartridge: unsupported type"; return false;
		default: break;
	}

	return true;
}

std::string basename_of(const std::string& path)
{
	const size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Optional DMG boot ROM supplied through QTR_BIOS. It is not part of the
// repository; the runner only reports an error when the environment variable
// is set but the file is invalid.
bool load_boot_rom(const std::string& path, std::vector<u8>& boot_rom, std::string& error)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if(!file)
	{
		error = "could not open";
		return false;
	}

	const std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);
	if(size != 0x100)
	{
		error = "expected a 256-byte DMG boot ROM";
		return false;
	}

	boot_rom.resize(static_cast<size_t>(size));
	if(!file.read(reinterpret_cast<char*>(boot_rom.data()), size))
	{
		error = "could not read";
		return false;
	}

	return true;
}

// A ROM uses assertions only if its .sym file declares assert_*/
// setup_assertions symbols (check_asserts_cb mechanism). The others do not
// populate the HRAM map 0xFF80-0xFF90, and scanning it yields false
// positives: e.g. reg_read copies its dma_proc code right at 0xFF80,
// and opcode 0x20 (jr nz) acts as the "assertion flag E".
bool sym_has_assertions(const std::string& path)
{
	std::string sym = path.substr(0, path.find_last_of('.'));
	sym += ".sym";

	std::ifstream file(sym);
	if(!file)
	{
		return false;
	}

	std::string line;
	while(std::getline(file, line))
	{
		if(line.find("setup_assertions") != std::string::npos ||
		   line.find("assert_") != std::string::npos ||
		   line.find("check_asserts_cb") != std::string::npos)
		{
			return true;
		}
	}

	return false;
}

// The OAM/VRAM access tests (lcdon_timing-GS, lcdon_write_timing-GS) leave the
// failed iteration index and the expected/obtained value in HRAM. The address
// of hram.fail_round varies per test (0xFF98 read / 0xFF80 write), so it is read
// from the .sym. One line format: "61:01:ff98 hram.fail_round" (index:bank:addr sym).
int sym_fail_round_address(const std::string& path)
{
	std::string sym = path.substr(0, path.find_last_of('.'));
	sym += ".sym";

	std::ifstream file(sym);
	if(!file)
	{
		return -1;
	}

	std::string line;
	while(std::getline(file, line))
	{
		if(line.find("fail_round") == std::string::npos ||
		   line.find("_sizeof_") != std::string::npos)
		{
			continue;
		}
		const size_t space = line.find(' ');
		const std::string fields = (space == std::string::npos) ? line : line.substr(0, space);
		const size_t last_colon = fields.find_last_of(':');
		if(last_colon == std::string::npos)
		{
			return -1;
		}
		try
		{
			return std::stoi(fields.substr(last_colon + 1), nullptr, 16);
		}
		catch(...)
		{
			return -1;
		}
	}

	return -1;
}

// Runs a ROM until the first magic_breakpoint with a recognizable verdict.
// check_assertions: scan the HRAM assertion map (only if the ROM's .sym
// declares setup_assertions).
TestResult run_rom(const std::vector<u8>& rom, bool check_assertions, int fail_round_addr, const std::vector<u8>& boot_rom, long max_cycles)
{
	TestResult result;
	QtreGB emu;
	if(!boot_rom.empty())
	{
		emu.load_boot_rom(boot_rom);
	}
	emu.load_cart(rom);

	const bool trace_env = (std::getenv("MTS_TRACE") != nullptr);
	u8 last_tima = emu.m_bus.read(HRAM_TIMA);
	u8 last_div  = emu.m_bus.read(0xFF04);
	u8 last_tac  = emu.m_bus.read(0xFF07);
	bool last_bit9 = (last_div >> 1) & 1;   // counter bit 9 = DIV register bit 1
	bool last_intr = emu.m_cpu.is_interruption_time();

	for(long t = 0; t < max_cycles; t++)
	{
		// is_end_opcode() == true means the CPU is free: the fetch happens on the
		// next tick, so the opcode at m_pc is the one about to run now,
		// and the registers still hold the test verdict.
		if(emu.m_cpu.is_end_opcode())
		{
			const CpuState state = read_cpu_state(emu);

			// Ignore 0x40 bytes in the low vectors / boot area: the PPU frame
			// harness must stop at the test's real LD B,B breakpoint.
			if(emu.m_cpu.peek_next_opcode() == MAGIC_BREAKPOINT && state.pc >= 0x0100)
			{
				// Frame-dump mode: the PPU acceptance ROMs do not leave a
				// verdict in the registers, they only hit the magic
				// breakpoint. Dump the framebuffer and finish successfully.
				if(const char* frame_out = std::getenv("MTS_FRAME_OUT"))
				{
					// The breakpoint can fire mid-frame. The PPU updates the
					// framebuffer line by line and does not clear it between
					// frames, so an immediate dump can mix the current and the
					// previous frame. MTS_FRAME_DELAY advances N extra
					// T-cycles before the dump (70224 T-cycles = one frame).
					if(const char* delay_env = std::getenv("MTS_FRAME_DELAY"))
					{
						long delay = std::strtol(delay_env, nullptr, 10);
						if(delay < 0)
						{
							delay = 0;
						}
						for(long k = 0; k < delay; k++)
						{
							emu.tick();
						}
					}

					if(std::getenv("QTR_PPUDBG"))
					{
						const std::vector<u8>& fb = emu.m_ppu.get_video_buffer();
						int nonwhite = 0;
						for(u8 p : fb) if((p & 0x03) != 0) nonwhite++;
						std::fprintf(stderr, "PPUDBG PC=%04X LCDC=%02X STAT=%02X BGP=%02X LY=%02X mode=%d dots=%u nonwhite=%d"
							" vram=%02X,%02X,%02X,%02X map=%02X,%02X,%02X,%02X\n",
							static_cast<unsigned>(emu.m_cpu.get_pc()),
							static_cast<unsigned>(emu.m_lcd.m_lcdc),
							static_cast<unsigned>(emu.m_lcd.read(1)),
							static_cast<unsigned>(emu.m_lcd.m_bg_palette),
							static_cast<unsigned>(emu.m_lcd.m_ly),
							static_cast<int>(emu.m_lcd.get_lcds_mode()),
							static_cast<unsigned>(emu.m_ppu.debug_dots()),
							nonwhite,
							static_cast<unsigned>(emu.m_bus.read(0x8000)),
							static_cast<unsigned>(emu.m_bus.read(0x8010)),
							static_cast<unsigned>(emu.m_bus.read(0x8020)),
							static_cast<unsigned>(emu.m_bus.read(0x8030)),
							static_cast<unsigned>(emu.m_bus.read(0x9800)),
							static_cast<unsigned>(emu.m_bus.read(0x9801)),
							static_cast<unsigned>(emu.m_bus.read(0x9810)),
							static_cast<unsigned>(emu.m_bus.read(0x9900)));
					}
					if(!dump_framebuffer_pgm(emu, frame_out))
					{
						std::cerr << "could not write frame: " << frame_out << "\n";
						return result;
					}

					result.verdict = Verdict::Pass;
					result.cycles = t;
					return result;
				}

				const Verdict verdict = read_verdict(state);
				if(verdict != Verdict::Pending)
				{
					result.verdict = verdict;
					result.cycles = t;

					if(std::getenv("MTS_DUMP"))
					{
						std::cout << "  DUMP PC=" << std::hex << state.pc
						          << " SP=" << state.sp
						          << " AF=" << unsigned(state.a) << unsigned(state.f)
						          << " BC=" << unsigned(state.b) << unsigned(state.c)
						          << " DE=" << unsigned(state.d) << unsigned(state.e)
						          << " HL=" << unsigned(state.h) << unsigned(state.l)
						          << std::dec << "\n  HRAM:";
						for(u16 a = 0xFF80; a <= 0xFF90; a++)
						{
							std::cout << " " << std::hex << std::setw(2) << std::setfill('0')
							          << unsigned(emu.m_bus.read(a));
						}
						std::cout << std::dec << "\n";
					}

					if(verdict == Verdict::Fail && fail_round_addr >= 0)
					{
						result.fail_round = emu.m_bus.read(fail_round_addr);
						result.fail_expect = emu.m_bus.read(fail_round_addr + 1);
						result.fail_actual = emu.m_bus.read(fail_round_addr + 2);
					}

					if(check_assertions)
					{
						const u8 flags = emu.m_bus.read(HRAM_FLAGS);
						for(int i = 0; i < 8; i++)
						{
							// The bit is set when the assertion is requested, not when it passes.
							if(!(flags & (1u << i))) continue;

							const Assertion assertion{
								ASSERT_REGS[i].name,
								emu.m_bus.read(ASSERT_REGS[i].expected),
								emu.m_bus.read(ASSERT_REGS[i].actual)};
							if(assertion.expected != assertion.actual) result.failures++;
							result.assertions.push_back(assertion);
						}
					}
					return result;
				}
			}
		}
		emu.tick();

		if(trace_env)
		{
			const u8 tima = emu.m_bus.read(HRAM_TIMA);
			const u8 div  = emu.m_bus.read(0xFF04);
			const u8 tac  = emu.m_bus.read(0xFF07);
			const bool bit9 = (div >> 1) & 1;
			const bool intr = emu.m_cpu.is_interruption_time();

			if(tac != last_tac)
			{
				std::cout << "    t=" << std::setw(8) << t
				          << "  TAC=0x" << std::hex << std::setw(2)
				          << unsigned(tac) << std::dec
				          << "  bit9=" << unsigned(bit9)
				          << "  (" << (tac & 0x04 ? "enable" : "disable") << ")\n";
				last_tac = tac;
			}
			if(bit9 != last_bit9)
			{
				std::cout << "    t=" << std::setw(8) << t
				          << "  bit9=" << unsigned(bit9)
				          << "  DIV=0x" << std::hex << std::setw(2)
				          << unsigned(div) << std::dec << "\n";
				last_bit9 = bit9;
			}
			if(tima != last_tima)
			{
				std::cout << "    t=" << std::setw(8) << t
				          << "  TIMA=0x" << std::hex << std::setw(2)
				          << unsigned(tima) << std::dec << "\n";
				last_tima = tima;
			}
			if(div == 0x00 && last_div != 0x00 && last_div != 0xFF)
			{
				std::cout << "    t=" << std::setw(8) << t
				          << "  DIV reset\n";
			}
			if(intr && !last_intr)
			{
				const CpuState state = read_cpu_state(emu);
				std::cout << "    t=" << std::setw(8) << t
				          << "  INTR dispatch  B=0x" << std::hex << std::setw(2)
				          << unsigned(state.b) << "  C=0x" << std::setw(2)
				          << unsigned(state.c) << std::dec << "\n";
			}
			last_div = div;
			last_intr = intr;
		}
	}

	result.verdict = Verdict::Pending;
	result.cycles = max_cycles;
	return result;
}

} // namespace

int main(int argc, char** argv)
{
	if(argc < 2)
	{
		std::cerr << "usage: " << argv[0] << " <rom.gb> [rom.gb ...]\n";
		return 2;
	}

	int passed = 0;
	int failed = 0;
	int pending = 0;

	std::vector<u8> boot_rom;
	if(const char* bios_path = std::getenv("QTR_BIOS"))
	{
		std::string bios_error;
		if(!load_boot_rom(bios_path, boot_rom, bios_error))
		{
			std::cerr << "BIOS ERROR (" << bios_error << "): " << bios_path << "\n";
			return 1;
		}
	}

	long max_cycles = boot_rom.empty() ? DEFAULT_MAX_TCYCLES : BIOS_MAX_TCYCLES;
	if(const char* env = std::getenv("MTS_MAX_T"))
	{
		max_cycles = std::strtol(env, nullptr, 10);
	}

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

		const bool check_assertions = sym_has_assertions(path) || (std::getenv("MTS_FORCE_ASSERT") != nullptr);
		const int fail_round_addr = sym_fail_round_address(path);
		const TestResult result = run_rom(rom, check_assertions, fail_round_addr, boot_rom, max_cycles);

		std::cout << std::dec << std::setfill(' ') << std::setw(28) << std::left
		          << name << std::right << std::setw(8) << to_string(result.verdict)
		          << "  " << result.cycles << " T-cycles";

		if(result.failures > 0)
		{
			std::cout << "  [" << result.failures << " assertion(s) failed]";
		}
		std::cout << "\n";

		for(const Assertion& assertion : result.assertions)
		{
			const bool ok = assertion.expected == assertion.actual;
			std::cout << "      " << assertion.name << " expected "
			          << std::setfill('0') << std::hex << std::setw(2)
			          << unsigned(assertion.expected)
			          << std::dec << std::setfill(' ') << "  obtained "
			          << std::setfill('0') << std::hex << std::setw(2)
			          << unsigned(assertion.actual)
			          << std::dec << std::setfill(' ') << (ok ? "   OK" : "   <-- BAD")
			          << "\n";
		}

		if(result.verdict == Verdict::Fail && result.fail_round >= 0)
		{
			std::cout << "      fail_round=" << std::dec << result.fail_round
			          << "  expected " << std::setfill('0') << std::hex << std::setw(2)
			          << unsigned(result.fail_expect)
			          << std::dec << std::setfill(' ') << "  obtained "
			          << std::setfill('0') << std::hex << std::setw(2)
			          << unsigned(result.fail_actual)
			          << std::dec << std::setfill(' ') << "\n";
		}

		switch(result.verdict)
		{
			case Verdict::Pass:	passed++;	break;
			case Verdict::Fail:	failed++;	break;
			default:		pending++;	break;
		}
	}

	std::cout << "\n" << passed << " passed, " << failed << " failed, "
	          << pending << " no verdict\n";

	return failed == 0 && pending == 0 ? 0 : 1;
}
