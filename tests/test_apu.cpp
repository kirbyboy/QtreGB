#include "doctest.h"
#include "apu.h"

using namespace qtreGB;

TEST_CASE("APU: reset leaves the APU powered off")
{
	APU apu;
	apu.reset();

	CHECK(apu.is_powered() == false);
	CHECK(apu.read(0xFF26) == 0x70);
	CHECK(apu.read(0xFF10) == 0x00);
	CHECK(apu.read(0xFF30) == 0x00);
}

TEST_CASE("APU: channel writes are ignored while powered off")
{
	APU apu;
	apu.reset();

	apu.write(0xFF11, 0xAB);
	apu.write(0xFF24, 0x77);
	apu.write(0xFF30, 0x5A);

	CHECK(apu.read(0xFF11) == 0x00);
	CHECK(apu.read(0xFF24) == 0x00);
	// Wave RAM is accessible even with the APU powered off.
	CHECK(apu.read(0xFF30) == 0x5A);
}

TEST_CASE("APU: NR52 bit 7 powers the register file on")
{
	APU apu;
	apu.reset();

	apu.write(0xFF26, 0x80);
	CHECK(apu.is_powered() == true);
	CHECK(apu.read(0xFF26) == 0xF0);

	apu.write(0xFF11, 0xAB);
	apu.write(0xFF12, 0xF3);
	apu.write(0xFF25, 0xFF);

	CHECK(apu.read(0xFF11) == 0xAB);
	CHECK(apu.read(0xFF12) == 0xF3);
	CHECK(apu.read(0xFF25) == 0xFF);
}

TEST_CASE("APU: power off clears the channel registers but keeps wave RAM")
{
	APU apu;
	apu.reset();

	apu.write(0xFF26, 0x80);
	apu.write(0xFF11, 0xAB);
	apu.write(0xFF24, 0x77);
	apu.write(0xFF30, 0x5A);

	apu.write(0xFF26, 0x00);

	CHECK(apu.is_powered() == false);
	CHECK(apu.read(0xFF26) == 0x70);
	CHECK(apu.read(0xFF11) == 0x00);
	CHECK(apu.read(0xFF24) == 0x00);
	CHECK(apu.read(0xFF30) == 0x5A);
}

TEST_CASE("APU: NR52 lower bits are read-only")
{
	APU apu;
	apu.reset();

	apu.write(0xFF26, 0x8F);
	CHECK(apu.read(0xFF26) == 0xF0);
}

TEST_CASE("APU: unused register bits read as one")
{
	APU apu;
	apu.reset();
	apu.write(0xFF26, 0x80);

	apu.write(0xFF10, 0xFF);
	CHECK(apu.read(0xFF10) == 0xFF);	// bit 7 unused

	apu.write(0xFF14, 0xFF);
	CHECK(apu.read(0xFF14) == 0x7F);	// trigger reads 0, bits 5-3 unused

	apu.write(0xFF1A, 0xFF);
	CHECK(apu.read(0xFF1A) == 0xFF);	// bits 6-0 unused

	apu.write(0xFF1C, 0xFF);
	CHECK(apu.read(0xFF1C) == 0xFF);	// bits 7 and 4-0 unused

	apu.write(0xFF20, 0xFF);
	CHECK(apu.read(0xFF20) == 0xFF);	// bits 7-6 unused

	apu.write(0xFF22, 0xFF);
	CHECK(apu.read(0xFF22) == 0xFF);	// bit 3 unused

	apu.write(0xFF23, 0xFF);
	CHECK(apu.read(0xFF23) == 0x7F);	// trigger reads 0, bits 5-0 unused
}

TEST_CASE("APU: tick keeps the register file stable")
{
	APU apu;
	apu.reset();
	apu.write(0xFF26, 0x80);
	apu.write(0xFF11, 0x42);
	apu.write(0xFF13, 0x34);

	for(int i = 0; i < 20000; ++i)
	{
		apu.tick();
	}

	CHECK(apu.read(0xFF11) == 0x42);
	CHECK(apu.read(0xFF13) == 0x34);
	CHECK(apu.read(0xFF26) == 0xF0);
}
