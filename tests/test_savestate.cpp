#include "doctest.h"
#include "QtreGB.h"
#include "cpu_micro.h"
#include "state_stream.h"
#include "timer.h"

#include <algorithm>
#include <array>
#include <vector>

using namespace qtreGB;

namespace
{

class StateTestBus : public IBus
{
	public:
		u8 read(u16 address) override { return mem[address]; }
		void write(u16 address, u8 value) override { mem[address] = value; }

		std::array<u8, 0x10000> mem {};
};

void write_cpu_program(StateTestBus& bus)
{
	// 0x0100: NOP
	// 0x0101: LD A,0x12
	// 0x0103: LD B,0x34
	// 0x0105: LD C,0x56
	// 0x0107: RLC B (CB prefix)
	// 0x0109: INC C
	// 0x010A: JR 0x0100
	bus.mem[0x0100] = 0x00;
	bus.mem[0x0101] = 0x3E;
	bus.mem[0x0102] = 0x12;
	bus.mem[0x0103] = 0x06;
	bus.mem[0x0104] = 0x34;
	bus.mem[0x0105] = 0x0E;
	bus.mem[0x0106] = 0x56;
	bus.mem[0x0107] = 0xCB;
	bus.mem[0x0108] = 0x00;
	bus.mem[0x0109] = 0x0C;
	bus.mem[0x010A] = 0x18;
	bus.mem[0x010B] = 0xF4;	// 0x010C - 12 = 0x0100
}

std::vector<u8> save_cpu_state(const CpuMicro& cpu)
{
	StateWriter writer(0, 0);
	writer.begin_chunk(StateChunk::Cpu);
	cpu.save_state(writer);
	writer.end_chunk();
	writer.finish();
	REQUIRE(writer.ok());
	return writer.take();
}

bool load_cpu_state(CpuMicro& cpu, const std::vector<u8>& blob)
{
	StateReader reader(blob);
	if(!reader.ok() || !reader.find_chunk(StateChunk::Cpu) ||
	   !cpu.load_state(reader) || !reader.end_chunk())
	{
		return false;
	}
	return reader.finish();
}

std::vector<u8> save_timer_state(const Timer& timer)
{
	StateWriter writer(0, 0);
	writer.begin_chunk(StateChunk::Timer);
	timer.save_state(writer);
	writer.end_chunk();
	writer.finish();
	REQUIRE(writer.ok());
	return writer.take();
}

bool load_timer_state(Timer& timer, const std::vector<u8>& blob)
{
	StateReader reader(blob);
	if(!reader.ok() || !reader.find_chunk(StateChunk::Timer) ||
	   !timer.load_state(reader) || !reader.end_chunk())
	{
		return false;
	}
	return reader.finish();
}

std::vector<u8> make_cart_rom(size_t size, u8 type, u8 rom_size, u8 ram_size = 0)
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

std::vector<u8> save_cart_state(const Cart& cart)
{
	StateWriter writer(0, 0);
	writer.begin_chunk(StateChunk::Cart);
	cart.save_state(writer);
	writer.end_chunk();
	writer.finish();
	REQUIRE(writer.ok());
	return writer.take();
}

bool load_cart_state(Cart& cart, const std::vector<u8>& blob)
{
	StateReader reader(blob);
	if(!reader.ok() || !reader.find_chunk(StateChunk::Cart) ||
	   !cart.load_state(reader) || !reader.end_chunk())
	{
		return false;
	}
	return reader.finish();
}

void cart_enable_ram(Cart& cart)
{
	cart.write(0x0000, 0x0A);
}

void cart_latch_rtc(Cart& cart)
{
	cart.write(0x6000, 0x00);
	cart.write(0x6000, 0x01);
}

u8 cart_read_rtc(Cart& cart, u8 reg)
{
	cart.write(0x4000, reg);
	return cart.read(0xA000);
}

void cart_write_rtc(Cart& cart, u8 reg, u8 value)
{
	cart.write(0x4000, reg);
	cart.write(0xA000, value);
}

void setup_ppu(QtreGB& machine)
{
	for(u16 i = 0; i < 0x2000; ++i)
	{
		machine.m_ppu.ppu_vram_write(i, static_cast<u8>((i * 7 + 3) & 0xFF));
	}

	for(int i = 0; i < 40; ++i)
	{
		const u16 base = static_cast<u16>(i * 4);
		machine.m_ppu.ppu_oam_write(base + 0, static_cast<u8>(16 + (i % 8) * 8));
		machine.m_ppu.ppu_oam_write(base + 1, static_cast<u8>((i * 4) % 160));
		machine.m_ppu.ppu_oam_write(base + 2, static_cast<u8>(i * 3));
		machine.m_ppu.ppu_oam_write(base + 3, static_cast<u8>((i & 1) ? 0x20 : 0x00));
	}

	machine.m_ppu.write_lcd_register(7, 0xE4);	// BGP
	machine.m_ppu.write_lcd_register(8, 0xE4);	// OBP0
	machine.m_ppu.write_lcd_register(9, 0x1B);	// OBP1
	machine.m_ppu.write_lcd_register(2, 0x05);	// SCY
	machine.m_ppu.write_lcd_register(3, 0x03);	// SCX
	machine.m_ppu.write_lcd_register(10, 0x00);	// WY
	machine.m_ppu.write_lcd_register(11, 0x17);	// WX = 7 + 16
	machine.m_ppu.write_lcd_register(0, 0xF3);	// LCD on, BG+window+OBJ
}

std::array<u8, 12> read_lcd_registers(QtreGB& machine)
{
	// The LCD is a passive mirror; request a refresh before reading it.
	machine.m_ppu.sync_lcd();

	std::array<u8, 12> values {};
	for(u16 i = 0; i < values.size(); ++i)
	{
		values[i] = machine.m_lcd.read(i);
	}
	return values;
}

std::vector<u8> make_machine_rom()
{
	std::vector<u8> rom(0x8000, 0x00);

	// The program keeps a counter in WRAM and branches on its low bits, so
	// restoring WRAM is required for the post-load execution to match.
	// 0x0100: LD HL,0xC000
	// 0x0103: LD A,(HL)
	// 0x0104: INC A
	// 0x0105: LD (HL),A
	// 0x0106: AND 0x07
	// 0x0108: JR NZ,0x0103
	// 0x010A: JR 0x0100
	rom[0x0100] = 0x21;
	rom[0x0101] = 0x00;
	rom[0x0102] = 0xC0;
	rom[0x0103] = 0x7E;
	rom[0x0104] = 0x3C;
	rom[0x0105] = 0x77;
	rom[0x0106] = 0xE6;
	rom[0x0107] = 0x07;
	rom[0x0108] = 0x20;
	rom[0x0109] = 0xF9;
	rom[0x010A] = 0x18;
	rom[0x010B] = 0xF4;

	rom[0x0147] = 0x00;	// ROM only
	rom[0x0148] = 0x00;	// 32 KiB
	rom[0x0149] = 0x00;	// no RAM
	return rom;
}

} // namespace

TEST_CASE("CpuMicro: savestate restores mid-instruction state")
{
	// Saving at every T-cycle phase (and every micro-op position) and then
	// running the same number of ticks must produce the exact same state.
	for(int start = 0; start < 80; ++start)
	{
		StateTestBus bus;
		write_cpu_program(bus);
		CpuMicro cpu(&bus);
		cpu.reset();

		for(int i = 0; i < start; ++i)
		{
			cpu.tick();
		}

		const std::array<u8, 0x10000> memory_at_save = bus.mem;
		const std::vector<u8> saved = save_cpu_state(cpu);

		for(int i = 0; i < 700; ++i)
		{
			cpu.tick();
		}
		const std::vector<u8> expected = save_cpu_state(cpu);

		REQUIRE(load_cpu_state(cpu, saved));
		bus.mem = memory_at_save;

		for(int i = 0; i < 700; ++i)
		{
			cpu.tick();
		}
		const std::vector<u8> actual = save_cpu_state(cpu);

		CHECK(expected == actual);
	}
}

TEST_CASE("CpuMicro: savestate rejects an out-of-range phase")
{
	StateTestBus bus;
	CpuMicro cpu(&bus);
	cpu.reset();

	const std::vector<u8> valid = save_cpu_state(cpu);

	// Rebuild the same chunk with a deliberately invalid phase value.
	StateWriter writer(0, 0);
	writer.begin_chunk(StateChunk::Cpu);
	writer.write_u8(0x0001, cpu.get_a());
	writer.write_u8(0x0002, cpu.get_f());
	writer.write_u8(0x0003, cpu.get_b());
	writer.write_u8(0x0004, cpu.get_c());
	writer.write_u8(0x0005, cpu.get_d());
	writer.write_u8(0x0006, cpu.get_e());
	writer.write_u8(0x0007, cpu.get_h());
	writer.write_u8(0x0008, cpu.get_l());
	writer.write_u16(0x0009, cpu.get_pc());
	writer.write_u16(0x000A, cpu.get_sp());
	writer.write_u8(0x000B, 0);	// ime
	writer.write_u8(0x000C, 0);	// ie
	writer.write_u8(0x000D, 0);	// if
	writer.write_bool(0x000E, false);	// halted
	writer.write_u8(0x000F, 0);	// interrupt state
	writer.write_u8(0x0010, 0);	// interrupt remaining
	writer.write_u8(0x0011, 9);	// invalid phase
	writer.write_bool(0x0012, false);
	writer.write_bool(0x0013, false);
	writer.write_u8(0x0014, 0);
	writer.write_u8(0x0015, 0);
	writer.write_u8(0x0016, 0);
	writer.write_bool(0x0017, true);
	writer.write_bool(0x0018, false);
	writer.write_bool(0x0019, false);
	writer.write_u16(0x001A, 0);
	writer.write_bool(0x001B, false);
	writer.write_u16(0x001C, 0);
	writer.write_u8(0x001D, 0);
	writer.write_u8(0x001E, 0);
	writer.write_u8(0x001F, 0);
	writer.write_u16(0x0020, 0);
	writer.write_u8(0x0021, 0);
	writer.write_u8(0x0022, 0);
	writer.write_u8(0x0023, 0);
	writer.end_chunk();
	writer.finish();
	REQUIRE(writer.ok());

	StateReader reader(writer.data());
	REQUIRE(reader.find_chunk(StateChunk::Cpu));
	CHECK(!cpu.load_state(reader));
	CHECK(reader.error() == StateError::BadValue);

	// The CPU keeps its previous state.
	CHECK(save_cpu_state(cpu) == valid);
}

TEST_CASE("Timer: savestate restores the divider and pending reload")
{
	Timer timer;
	timer.timer_init();
	timer.write_tima(0xFE);
	timer.write_tma(0x42);
	timer.write_tac(0x05);

	for(int i = 0; i < 100; ++i)
	{
		timer.timer_tick();
	}

	const std::vector<u8> saved = save_timer_state(timer);

	for(int i = 0; i < 500; ++i)
	{
		timer.timer_tick();
	}
	const std::vector<u8> expected = save_timer_state(timer);

	REQUIRE(load_timer_state(timer, saved));
	for(int i = 0; i < 500; ++i)
	{
		timer.timer_tick();
	}
	const std::vector<u8> actual = save_timer_state(timer);

	CHECK(expected == actual);
}

TEST_CASE("Machine state: save/load is deterministic with WRAM-dependent code")
{
	QtreGB machine;
	REQUIRE(machine.load_cart(make_machine_rom()));

	// Run past a frame boundary plus one T-cycle: the saved machine clock is
	// not at phase 0, and the number of ticks between save and load is not a
	// multiple of four either.
	for(int i = 0; i < 70225; ++i)
	{
		machine.tick();
	}

	std::vector<u8> saved;
	REQUIRE(machine.save_state(saved));
	REQUIRE(!saved.empty());

	for(int i = 0; i < 1999; ++i)
	{
		machine.tick();
	}
	std::vector<u8> expected;
	REQUIRE(machine.save_state(expected));

	CHECK(machine.load_state(saved) == StateError::Ok);

	for(int i = 0; i < 1999; ++i)
	{
		machine.tick();
	}
	std::vector<u8> actual;
	REQUIRE(machine.save_state(actual));

	CHECK(expected == actual);
}

TEST_CASE("Machine state: memory, IO, DMA and gamepad round-trip")
{
	QtreGB machine;
	REQUIRE(machine.load_cart(make_machine_rom()));

	machine.m_wram.data()[0x0123] = 0x5A;
	machine.m_wram.data()[0x1FFF] = 0xA5;
	machine.m_hram.data()[0x0010] = 0x3C;
	machine.m_bus.write(0xFF01, 0x11);
	machine.m_bus.write(0xFF02, 0x22);

	machine.m_gamepad.m_button_a = true;
	machine.m_gamepad.m_button_down = true;
	machine.m_gamepad.set_selection(0x10);	// directions selected, buttons not

	machine.m_dma.dma_start(0xC0);
	for(int i = 0; i < 16; ++i)
	{
		machine.tick();
	}
	REQUIRE(machine.m_dma.dma_transferring());

	const u8 pad_before = machine.m_gamepad.get_state();
	std::vector<u8> saved;
	REQUIRE(machine.save_state(saved));

	// Change every schema 2 component before loading.
	machine.m_wram.data()[0x0123] = 0;
	machine.m_wram.data()[0x1FFF] = 0;
	machine.m_hram.data()[0x0010] = 0;
	machine.m_bus.write(0xFF01, 0);
	machine.m_bus.write(0xFF02, 0);
	machine.m_gamepad.m_button_a = false;
	machine.m_gamepad.m_button_down = false;
	machine.m_gamepad.set_selection(0x30);
	machine.m_dma.dma_start(0x00);

	REQUIRE(machine.load_state(saved) == StateError::Ok);

	CHECK(machine.m_wram.data()[0x0123] == 0x5A);
	CHECK(machine.m_wram.data()[0x1FFF] == 0xA5);
	CHECK(machine.m_hram.data()[0x0010] == 0x3C);
	CHECK(machine.m_bus.read(0xFF01) == 0x11);
	CHECK(machine.m_bus.read(0xFF02) == 0x22);
	CHECK(machine.m_gamepad.get_state() == pad_before);
	CHECK(machine.m_dma.dma_transferring());

	std::vector<u8> after;
	REQUIRE(machine.save_state(after));
	CHECK(saved == after);
}

TEST_CASE("Machine state: boot ROM is identified by hash")
{
	std::vector<u8> boot(0x100);
	for(int i = 0; i < 0x100; ++i)
	{
		boot[static_cast<size_t>(i)] = static_cast<u8>(i * 7 + 3);
	}

	QtreGB machine;
	REQUIRE(machine.load_boot_rom(boot));
	REQUIRE(machine.load_cart(make_machine_rom()));
	REQUIRE(machine.m_bus.is_boot_rom_enabled());

	std::vector<u8> saved;
	REQUIRE(machine.save_state(saved));

	QtreGB same;
	REQUIRE(same.load_boot_rom(boot));
	REQUIRE(same.load_cart(make_machine_rom()));
	CHECK(same.load_state(saved) == StateError::Ok);
	CHECK(same.m_bus.is_boot_rom_enabled());

	QtreGB missing;
	REQUIRE(missing.load_cart(make_machine_rom()));
	REQUIRE(!missing.m_bus.is_boot_rom_enabled());

	std::vector<u8> before_missing;
	REQUIRE(missing.save_state(before_missing));
	CHECK(missing.load_state(saved) == StateError::BadValue);
	CHECK(!missing.m_bus.is_boot_rom_enabled());

	std::vector<u8> after_missing;
	REQUIRE(missing.save_state(after_missing));
	CHECK(before_missing == after_missing);
}

TEST_CASE("Cart state: MBC1 registers and RAM round-trip")
{
	std::vector<u8> rom = make_cart_rom(0x10000, 0x03, 0x01, 0x03);
	rom[0x4010] = 0x33;
	rom[0x8010] = 0x44;
	rom[0xC010] = 0x55;

	Cart cart;
	REQUIRE(cart.load_cart(rom) == 0);

	cart_enable_ram(cart);
	cart.write(0x2000, 0x03);	// ROM bank 3
	cart.write(0x4000, 0x02);	// RAM bank 2
	cart.write(0x6000, 0x01);	// advanced banking mode
	cart.write(0xA000, 0x11);
	cart.write(0xB000, 0x22);

	CHECK(cart.read(0x4010) == 0x55);
	const std::vector<u8> saved = save_cart_state(cart);

	cart.write(0x0000, 0x00);	// RAM off
	cart.write(0x2000, 0x01);
	cart.write(0x4000, 0x00);
	cart.write(0x6000, 0x00);
	cart.write(0xA000, 0x00);	// ignored

	REQUIRE(load_cart_state(cart, saved));
	CHECK(cart.read(0x4010) == 0x55);
	CHECK(cart.read(0xA000) == 0x11);
	CHECK(cart.read(0xB000) == 0x22);
	CHECK(save_cart_state(cart) == saved);
}

TEST_CASE("Cart state: MBC5 registers and RAM round-trip")
{
	std::vector<u8> rom = make_cart_rom(0x100000, 0x1B, 0x05, 0x03);
	rom[0x4010] = 0x33;
	rom[0x8010] = 0x44;
	rom[0xC010] = 0x55;

	Cart cart;
	REQUIRE(cart.load_cart(rom) == 0);

	cart_enable_ram(cart);
	cart.write(0x2000, 0x03);	// ROM bank 3
	cart.write(0x3000, 0x00);	// high bank bit
	cart.write(0x4000, 0x02);	// RAM bank 2
	cart.write(0xA000, 0x11);
	cart.write(0xB000, 0x22);

	CHECK(cart.read(0x4010) == 0x55);
	const std::vector<u8> saved = save_cart_state(cart);

	cart.write(0x0000, 0x00);	// RAM off
	cart.write(0x2000, 0x01);
	cart.write(0x3000, 0x00);
	cart.write(0x4000, 0x00);
	cart.write(0xA000, 0x00);	// ignored

	REQUIRE(load_cart_state(cart, saved));
	CHECK(cart.read(0x4010) == 0x55);
	CHECK(cart.read(0xA000) == 0x11);
	CHECK(cart.read(0xB000) == 0x22);
	CHECK(save_cart_state(cart) == saved);
}

TEST_CASE("Cart state: MBC2 nibble RAM round-trip")
{
	Cart cart;
	REQUIRE(cart.load_cart(make_cart_rom(0x8000, 0x06, 0x00)) == 0);

	cart_enable_ram(cart);
	cart.write(0x2100, 0x03);
	cart.write(0xA000, 0x05);
	cart.write(0xA1FF, 0x0A);

	const std::vector<u8> saved = save_cart_state(cart);

	cart.write(0x0000, 0x00);
	cart.write(0x2100, 0x01);
	cart.write(0xA000, 0x00);	// ignored

	REQUIRE(load_cart_state(cart, saved));
	CHECK(cart.read(0xA000) == 0xF5);
	CHECK(cart.read(0xA1FF) == 0xFA);
	CHECK(save_cart_state(cart) == saved);
}

TEST_CASE("Cart state: no-MBC RAM round-trip")
{
	Cart cart;
	REQUIRE(cart.load_cart(make_cart_rom(0x8000, 0x09, 0x00, 0x02)) == 0);

	cart.write(0xA000, 0x5A);
	cart.write(0xA123, 0xA5);

	const std::vector<u8> saved = save_cart_state(cart);

	cart.write(0xA000, 0x00);
	cart.write(0xA123, 0x00);

	REQUIRE(load_cart_state(cart, saved));
	CHECK(cart.read(0xA000) == 0x5A);
	CHECK(cart.read(0xA123) == 0xA5);
	CHECK(save_cart_state(cart) == saved);
}

TEST_CASE("Cart state: MBC3 RTC freezes at save and resumes at load")
{
	int64_t now = 1000 * 1000000;
	Cart cart;
	cart.set_rtc_clock([&now] { return now; });
	REQUIRE(cart.load_cart(make_cart_rom(0x8000, 0x10, 0x00, 0x02)) == 0);

	cart_enable_ram(cart);
	cart_write_rtc(cart, 0x08, 42);	// seconds
	cart_write_rtc(cart, 0x09, 7);	// minutes
	cart_write_rtc(cart, 0x0A, 3);	// hours
	cart_write_rtc(cart, 0x0B, 1);	// days low
	cart_write_rtc(cart, 0x0C, 0x00);	// day high 0, halt off, carry off
	cart_latch_rtc(cart);

	CHECK(cart_read_rtc(cart, 0x08) == 42);
	CHECK(cart_read_rtc(cart, 0x09) == 7);
	CHECK(cart_read_rtc(cart, 0x0A) == 3);
	CHECK(cart_read_rtc(cart, 0x0B) == 1);

	const std::vector<u8> saved = save_cart_state(cart);

	// Time passes while the savestate is on disk. The RTC must not apply it.
	now += 100 * 1000000;
	REQUIRE(load_cart_state(cart, saved));
	CHECK(save_cart_state(cart) == saved);
	cart_latch_rtc(cart);
	CHECK(cart_read_rtc(cart, 0x08) == 42);
	CHECK(cart_read_rtc(cart, 0x09) == 7);
	CHECK(cart_read_rtc(cart, 0x0A) == 3);
	CHECK(cart_read_rtc(cart, 0x0B) == 1);

	// Once loaded, it resumes from the load instant.
	now += 10 * 1000000;
	cart_latch_rtc(cart);
	CHECK(cart_read_rtc(cart, 0x08) == 52);
	CHECK(cart_read_rtc(cart, 0x09) == 7);
}

TEST_CASE("Cart state: MBC3 halted RTC stays frozen after load")
{
	int64_t now = 2000 * 1000000;
	Cart cart;
	cart.set_rtc_clock([&now] { return now; });
	REQUIRE(cart.load_cart(make_cart_rom(0x8000, 0x10, 0x00, 0x02)) == 0);

	cart_enable_ram(cart);
	cart_write_rtc(cart, 0x08, 10);
	cart_write_rtc(cart, 0x0C, 0x40);	// halt
	cart_latch_rtc(cart);

	const std::vector<u8> saved = save_cart_state(cart);
	now += 100 * 1000000;

	REQUIRE(load_cart_state(cart, saved));
	CHECK(save_cart_state(cart) == saved);
	cart_latch_rtc(cart);
	CHECK(cart_read_rtc(cart, 0x08) == 10);
	CHECK((cart_read_rtc(cart, 0x0C) & 0x40) != 0);

	// Clearing halt restarts from the load instant, not from the save time.
	cart_write_rtc(cart, 0x0C, 0x00);
	now += 5 * 1000000;
	cart_latch_rtc(cart);
	CHECK(cart_read_rtc(cart, 0x08) == 15);
}

TEST_CASE("Cart state: a different mapper is rejected")
{
	Cart mbc1;
	REQUIRE(mbc1.load_cart(make_cart_rom(0x8000, 0x03, 0x00, 0x03)) == 0);

	Cart mbc3;
	REQUIRE(mbc3.load_cart(make_cart_rom(0x8000, 0x10, 0x00, 0x02)) == 0);

	const std::vector<u8> saved = save_cart_state(mbc1);
	CHECK(!load_cart_state(mbc3, saved));
}

TEST_CASE("Machine state: cartridge RAM round-trips through QtreGB")
{
	QtreGB machine;
	REQUIRE(machine.load_cart(make_cart_rom(0x8000, 0x03, 0x00, 0x03)));

	machine.m_cart.write(0x0000, 0x0A);	// RAM enable
	machine.m_cart.write(0x4000, 0x02);	// RAM bank 2
	machine.m_cart.write(0xA000, 0x77);

	std::vector<u8> saved;
	REQUIRE(machine.save_state(saved));

	machine.m_cart.write(0xA000, 0x00);
	REQUIRE(machine.load_state(saved) == StateError::Ok);
	CHECK(machine.m_cart.read(0xA000) == 0x77);

	std::vector<u8> after;
	REQUIRE(machine.save_state(after));
	CHECK(saved == after);
}

TEST_CASE("PPU state: round-trip at many dots and full-frame determinism")
{
	std::vector<int> offsets;
	for(int i = 0; i <= 100; ++i)
	{
		offsets.push_back(i);
	}
	for(int i = 440; i <= 460; ++i)
	{
		offsets.push_back(i);
	}
	for(int i = 0; i <= 22; ++i)
	{
		offsets.push_back(69768 + i);	// LY 153, VBlank last-line glitches
	}
	for(int i = 0; i <= 10; ++i)
	{
		offsets.push_back(70220 + i);	// frame boundary
	}
	offsets.push_back(1000);
	offsets.push_back(5000);
	offsets.push_back(20000);
	offsets.push_back(40000);
	offsets.push_back(50000);

	std::sort(offsets.begin(), offsets.end());
	offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());

	constexpr int SETTLE_TICKS = 70224 + 2000;

	for(const int start : offsets)
	{
		QtreGB machine;
		REQUIRE(machine.load_cart(make_machine_rom()));
		setup_ppu(machine);

		for(int i = 0; i < start; ++i)
		{
			machine.tick();
		}

		const std::array<u8, 12> lcd_at_save = read_lcd_registers(machine);
		const bool oam_read = machine.m_ppu.is_oam_read_locked();
		const bool oam_write = machine.m_ppu.is_oam_write_locked();
		const bool vram_read = machine.m_ppu.is_vram_read_locked();
		const bool vram_write = machine.m_ppu.is_vram_write_locked();

		std::vector<u8> saved;
		REQUIRE(machine.save_state(saved));

		for(int i = 0; i < SETTLE_TICKS; ++i)
		{
			machine.tick();
		}
		std::vector<u8> expected;
		REQUIRE(machine.save_state(expected));
		const std::vector<u8> expected_frame = machine.m_ppu.get_video_buffer();

		REQUIRE(machine.load_state(saved) == StateError::Ok);

		std::vector<u8> restored;
		REQUIRE(machine.save_state(restored));
		CHECK(restored == saved);
		CHECK(read_lcd_registers(machine) == lcd_at_save);
		CHECK(machine.m_ppu.is_oam_read_locked() == oam_read);
		CHECK(machine.m_ppu.is_oam_write_locked() == oam_write);
		CHECK(machine.m_ppu.is_vram_read_locked() == vram_read);
		CHECK(machine.m_ppu.is_vram_write_locked() == vram_write);

		for(int i = 0; i < SETTLE_TICKS; ++i)
		{
			machine.tick();
		}
		std::vector<u8> actual;
		REQUIRE(machine.save_state(actual));
		const std::vector<u8> actual_frame = machine.m_ppu.get_video_buffer();

		CHECK(actual == expected);
		CHECK(actual_frame == expected_frame);
	}
}

TEST_CASE("PPU: the LCD mirror is refreshed on demand and by save_state")
{
	QtreGB machine;
	REQUIRE(machine.load_cart(make_machine_rom()));
	setup_ppu(machine);

	for(int i = 0; i < 500; ++i)
	{
		machine.tick();
	}

	// Saving the machine refreshes the passive mirror before the LCD chunk
	// is written.
	std::vector<u8> saved;
	REQUIRE(machine.save_state(saved));
	for(u16 i = 0; i < 12; ++i)
	{
		if(i == 6)
		{
			continue;	// LCD has no DMA mirror; the PPU owns FF46.
		}
		CHECK(machine.m_lcd.read(i) == machine.m_ppu.read_lcd_register(i));
	}

	// The mirror can also be refreshed explicitly without saving.
	for(int i = 0; i < 500; ++i)
	{
		machine.tick();
	}
	machine.m_ppu.sync_lcd();
	for(u16 i = 0; i < 12; ++i)
	{
		if(i == 6)
		{
			continue;
		}
		CHECK(machine.m_lcd.read(i) == machine.m_ppu.read_lcd_register(i));
	}
}

TEST_CASE("Machine state: load rejects a different ROM")
{
	QtreGB machine;
	REQUIRE(machine.load_cart(make_machine_rom()));

	for(int i = 0; i < 1000; ++i)
	{
		machine.tick();
	}
	std::vector<u8> blob;
	REQUIRE(machine.save_state(blob));

	std::vector<u8> other_rom = make_machine_rom();
	other_rom[0x0200] ^= 0xFF;
	QtreGB other;
	REQUIRE(other.load_cart(other_rom));

	CHECK(other.load_state(blob) == StateError::WrongRom);
}

TEST_CASE("Machine state: failed load rolls back the components already applied")
{
	QtreGB machine;
	REQUIRE(machine.load_cart(make_machine_rom()));

	for(int i = 0; i < 5000; ++i)
	{
		machine.tick();
	}

	std::vector<u8> before;
	REQUIRE(machine.save_state(before));

	// Valid header and valid Clock/CPU/Timer chunks, but the APU chunk is
	// missing. Clock/CPU/Timer are applied before the failure, so this also
	// exercises the rollback of components already loaded.
	const std::vector<u8>& rom = machine.m_cart.rom();
	StateWriter writer(static_cast<u32>(rom.size()), state_hash_rom(rom));
	writer.begin_chunk(StateChunk::Clock);
	writer.write_u8(0x0001, static_cast<u8>(machine.get_phase()));
	writer.end_chunk();
	writer.begin_chunk(StateChunk::Cpu);
	machine.m_cpu.save_state(writer);
	writer.end_chunk();
	writer.begin_chunk(StateChunk::Timer);
	machine.m_timer.save_state(writer);
	writer.end_chunk();
	writer.finish();
	REQUIRE(writer.ok());

	CHECK(machine.load_state(writer.data()) == StateError::MissingChunk);

	std::vector<u8> after;
	REQUIRE(machine.save_state(after));
	CHECK(before == after);
}
