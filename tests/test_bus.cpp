#include "doctest.h"
#include "bus.h"
#include "apu.h"
#include "error_reporter.h"

#include <vector>

using namespace qtreGB;

namespace
{

std::vector<u8> make_rom(size_t size, u8 type, u8 rom_size, u8 ram_size)
{
	std::vector<u8> rom(size, 0);
	if(size > 0x149)
	{
		rom[0x0147] = type;
		rom[0x0148] = rom_size;
		rom[0x0149] = ram_size;
	}
	return rom;
}

// Stand-in for the CPU register block so the Bus can route IF/IE.
class CpuRegsStub : public ICPURegisters
{
	public:
		u8 read_IF() const override { return m_if; }
		void write_IF(u8 value) override { m_if = value; }
		u8 read_IE() const override { return m_ie; }
		void write_IE(u8 value) override { m_ie = value; }
	private:
		u8 m_if = 0;
		u8 m_ie = 0;
};

// Real components wired the same way QtreGB wires the emulated machine.
struct BusEnv
{
	ErrorReporter errors;
	Cart cart;
	Timer timer;
	DMA dma;
	Gamepad gamepad;
	Memory wram;
	Memory hram;
	IO io;
	LCD lcd;
	PPU ppu;
	APU apu;
	CpuRegsStub cpu_regs;
	Bus bus;

	BusEnv(std::vector<u8> rom) :
		wram(0x2000),
		hram(0x80),
		ppu(&lcd),
		bus(&cart, &timer, &dma, &gamepad, &wram, &hram, &io, &lcd, &ppu, &apu, &cpu_regs)
	{
		cart.connect_error_reporter(&errors);
		CHECK(cart.load_cart(rom) == 0);
		timer.timer_init();
		apu.reset();
		ppu.ppu_init();	// mirrors the QtreGB constructor initialization

		gamepad.m_button_start = false;
		gamepad.m_button_select = false;
		gamepad.m_button_b = false;
		gamepad.m_button_a = false;
		gamepad.m_button_down = false;
		gamepad.m_button_up = false;
		gamepad.m_button_left = false;
		gamepad.m_button_right = false;

		// Two start-delay ticks plus 160 copy ticks drain the 0xA0-byte
		// transfer, leaving the DMA idle so OAM is reachable by default.
		dma.connect(&bus, &ppu);
		dma.dma_start(0xFE);
		for(int i = 0; i < 162; ++i)
		{
			dma.dma_tick();
		}
	}

	BusEnv(u8 ram_size) : BusEnv(make_rom(0x8000, 0x00, 0x00, ram_size)) {}
};

}

TEST_CASE("Bus: 0000-7FFF routes reads to cartridge ROM")
{
	std::vector<u8> rom = make_rom(0x8000, 0x00, 0x00, 0x00);
	rom[0x0000] = 0x12;
	rom[0x3FFF] = 0x34;
	rom[0x4000] = 0x56;
	rom[0x7FFF] = 0x78;
	BusEnv env(rom);

	CHECK(env.bus.read(0x0000) == 0x12);
	CHECK(env.bus.read(0x3FFF) == 0x34);
	CHECK(env.bus.read(0x4000) == 0x56);
	CHECK(env.bus.read(0x7FFF) == 0x78);
}

TEST_CASE("Bus: 8000-9FFF routes to PPU VRAM")
{
	BusEnv env(0);

	env.bus.write(0x8000, 0xAB);
	CHECK(env.bus.read(0x8000) == 0xAB);
	env.bus.write(0x9FFF, 0xCD);
	CHECK(env.bus.read(0x9FFF) == 0xCD);
}

TEST_CASE("Bus: A000-BFFF routes to cartridge RAM")
{
	BusEnv env(3);

	env.bus.write(0xA000, 0x5A);
	CHECK(env.bus.read(0xA000) == 0x5A);
	env.bus.write(0xBFFF, 0x6B);
	CHECK(env.bus.read(0xBFFF) == 0x6B);
}

TEST_CASE("Bus: C000-DFFF is WRAM")
{
	BusEnv env(0);

	env.bus.write(0xC000, 0x11);
	CHECK(env.bus.read(0xC000) == 0x11);
	env.bus.write(0xDFFF, 0x22);
	CHECK(env.bus.read(0xDFFF) == 0x22);
}

TEST_CASE("Bus: echo RAM mirrors 0xC000-0xDDFF for reads")
{
	BusEnv env(0);

	env.bus.write(0xC123, 0x5A);
	CHECK(env.bus.read(0xE123) == 0x5A);
	env.bus.write(0xDDFF, 0x77);
	CHECK(env.bus.read(0xFDFF) == 0x77);
}

TEST_CASE("Bus: echo RAM mirrors 0xC000-0xDDFF for writes")
{
	BusEnv env(0);

	env.bus.write(0xE123, 0xA5);
	CHECK(env.bus.read(0xC123) == 0xA5);
	env.bus.write(0xFDFF, 0x88);
	CHECK(env.bus.read(0xDDFF) == 0x88);
}

TEST_CASE("Bus: echo window excludes 0xDE00-0xDFFF")
{
	BusEnv env(0);

	env.bus.write(0xDF00, 0x33);
	CHECK(env.bus.read(0xDF00) == 0x33);
	CHECK(env.bus.read(0xFD00) == env.bus.read(0xDD00));
	CHECK_FALSE(env.bus.read(0xFD00) == 0x33);
}

TEST_CASE("Bus: OAM read/write when DMA is idle")
{
	BusEnv env(0);

	env.bus.write(0xFE00, 0x50);
	CHECK(env.bus.read(0xFE00) == 0x50);
	env.bus.write(0xFE9F, 0x51);
	CHECK(env.bus.read(0xFE9F) == 0x51);
}

TEST_CASE("Bus: OAM is blocked while DMA transfers")
{
	BusEnv env(0);
	env.dma.dma_start(0x80);
	env.dma.dma_tick(); // leave the start-delay window: DMA now owns OAM

	CHECK(env.bus.read(0xFE00) == 0xFF);
	env.bus.write(0xFE00, 0xAB);
	CHECK(env.bus.read(0xFE00) == 0xFF);
	CHECK(env.bus.read(0xFF46) == 0x80);
}

TEST_CASE("Bus: FEA0-FEFF is not usable")
{
	BusEnv env(0);

	env.bus.write(0xFEA0, 0x12);
	CHECK(env.bus.read(0xFEA0) == 0xFF);
	CHECK(env.bus.read(0xFEFF) == 0xFF);
}

TEST_CASE("Bus: FF00 routes to the gamepad and reacts to selection")
{
	BusEnv env(0);

	env.bus.write(0xFF00, 0x00);
	CHECK(env.bus.read(0xFF00) == 0xCF); // idle: P14/P15 pulled low
	env.gamepad.m_button_a = true;
	CHECK(env.bus.read(0xFF00) == 0xCE); // A clears bit 0
	env.bus.write(0xFF00, 0x20);
	CHECK(env.bus.read(0xFF00) == 0xEF); // button row deselected: A hidden
}

TEST_CASE("Bus: FF10-FF3F route to the APU")
{
	BusEnv env(0);

	CHECK(env.bus.read(0xFF26) == 0x70);	// APU off after reset
	env.bus.write(0xFF26, 0x80);
	CHECK(env.bus.read(0xFF26) == 0xF0);

	env.bus.write(0xFF11, 0x80);
	CHECK(env.bus.read(0xFF11) == 0xBF);

	env.bus.write(0xFF30, 0xA5);
	CHECK(env.bus.read(0xFF30) == 0xA5);

	env.bus.write(0xFF26, 0x00);
	CHECK(env.bus.read(0xFF11) == 0x3F);
	CHECK(env.bus.read(0xFF26) == 0x70);
}

TEST_CASE("Bus: FF04-FF07 route to the timer")
{
	BusEnv env(0);

	CHECK(env.bus.read(0xFF04) == 0xAB);
	env.bus.write(0xFF04, 0xFF);
	CHECK(env.bus.read(0xFF04) == 0x00); // any DIV write resets it
	env.bus.write(0xFF05, 0x64);
	CHECK(env.bus.read(0xFF05) == 0x64);
	env.bus.write(0xFF06, 0x51);
	CHECK(env.bus.read(0xFF06) == 0x51);
	env.bus.write(0xFF07, 0x03);
	CHECK(env.bus.read(0xFF07) == 0xFB); // TAC bits 3-7 always read back 1
}

TEST_CASE("Bus: FF40-FF4B routes LCD registers and FF46 starts OAM DMA")
{
	BusEnv env(0);

	env.bus.write(0xFF40, 0x91);
	CHECK(env.bus.read(0xFF40) == 0x91);
	env.bus.write(0xFF47, 0xE4);
	CHECK(env.bus.read(0xFF47) == 0xE4);
	env.bus.write(0xFF4B, 0x07);
	CHECK(env.bus.read(0xFF4B) == 0x07);

	env.bus.write(0xFF46, 0x9C);
	CHECK(env.bus.read(0xFF46) == 0x9C);
	CHECK(env.dma.get_value() == 0x9C);
}

TEST_CASE("Bus: FF0F and FFFF route to the interrupt registers")
{
	BusEnv env(0);

	env.bus.write(0xFF0F, 0x0E);
	CHECK(env.bus.read(0xFF0F) == 0x0E);
	env.bus.write(0xFFFF, 0x1F);
	CHECK(env.bus.read(0xFFFF) == 0x1F);
}

TEST_CASE("Bus: serial and unmapped IO land in the IO controller")
{
	BusEnv env(0);

	env.bus.write(0xFF01, 0x42);
	CHECK(env.bus.read(0xFF01) == 0x42);

	env.bus.write(0xFF4C, 0x99);
	CHECK(env.bus.read(0xFF4C) == 0xFF);
	CHECK(env.bus.read(0xFF51) == 0xFF);
}

TEST_CASE("Bus: FF80-FFFE is high RAM")
{
	BusEnv env(0);

	env.bus.write(0xFF80, 0x71);
	CHECK(env.bus.read(0xFF80) == 0x71);
	env.bus.write(0xFFFE, 0x72);
	CHECK(env.bus.read(0xFFFE) == 0x72);
}

TEST_CASE("Bus: boot ROM maps 0000-00FF until FF50 is written")
{
	std::vector<u8> rom = make_rom(0x8000, 0x00, 0x00, 0x00);
	rom[0x0000] = 0x12;
	rom[0x00FF] = 0x34;
	rom[0x0100] = 0x56;
	BusEnv env(rom);

	std::vector<u8> boot(0x100, 0xAA);
	boot[0x00] = 0x78;
	boot[0xFF] = 0x9A;
	env.bus.set_boot_rom(boot);
	env.bus.set_boot_rom_enabled(true);

	CHECK(env.bus.is_boot_rom_enabled());
	CHECK(env.bus.read(0x0000) == 0x78);
	CHECK(env.bus.read(0x00FF) == 0x9A);
	CHECK(env.bus.read(0x0100) == 0x56);

	env.bus.write(0xFF50, 0x01);
	CHECK_FALSE(env.bus.is_boot_rom_enabled());
	CHECK(env.bus.read(0x0000) == 0x12);
	CHECK(env.bus.read(0x00FF) == 0x34);
}