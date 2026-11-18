#include "doctest.h"
#include "QtreGB.h"

#include <vector>

using namespace qtreGB;

namespace
{
std::vector<u8> make_nop_rom()
{
	std::vector<u8> rom(0x8000, 0x00);
	rom[0x0147] = 0x00; // ROM only
	rom[0x0148] = 0x00; // 32 KiB
	rom[0x0149] = 0x00; // no RAM
	return rom;
}
}

TEST_CASE("QtreGB: tick advances one phase and cycle advances four")
{
	QtreGB emu;
	REQUIRE(emu.load_cart(make_nop_rom()));

	CHECK(emu.get_phase() == Phase::T0);
	emu.tick();
	CHECK(emu.get_phase() == Phase::T1);
	emu.tick();
	CHECK(emu.get_phase() == Phase::T2);
	emu.tick();
	CHECK(emu.get_phase() == Phase::T3);
	emu.tick();
	CHECK(emu.get_phase() == Phase::T0);

	emu.cycle();
	CHECK(emu.get_phase() == Phase::T0);
}
