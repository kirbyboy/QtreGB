#include "doctest.h"
#include "QtreGB.h"
#include "io.h"
#include "link_cable.h"
#include "serial.h"

#include <vector>

using namespace qtreGB;

namespace
{

constexpr int BIT_TICKS = 512;
constexpr int BYTE_TICKS = BIT_TICKS * 8;

std::vector<u8> make_nop_rom()
{
	std::vector<u8> rom(0x8000, 0x00);
	rom[0x0100] = 0x18;	// JR -2
	rom[0x0101] = 0xFE;
	rom[0x0147] = 0x00;	// ROM only
	rom[0x0148] = 0x00;	// 32 KiB
	rom[0x0149] = 0x00;	// no RAM
	return rom;
}

} // namespace

TEST_CASE("Serial: internal clock completes with floating input")
{
	IO io;
	Serial serial(&io);
	int interrupts = 0;
	serial.set_interrupt_callback([&](InterruptType type)
	{
		if(type == InterruptType::IT_SERIAL)
		{
			++interrupts;
		}
	});

	io.io_write(0xFF01, 0x42);
	io.io_write(0xFF02, 0x81);
	CHECK(serial.is_active() == false);

	for(int i = 0; i < BYTE_TICKS; ++i)
	{
		serial.tick();
	}

	CHECK(serial.is_active() == false);
	CHECK((io.io_read(0xFF02) & 0x80) == 0);
	CHECK(io.io_read(0xFF01) == 0xFF);
	CHECK(interrupts == 1);
}

TEST_CASE("Serial: external clock waits for the cable")
{
	IO io;
	Serial serial(&io);

	io.io_write(0xFF01, 0x5A);
	io.io_write(0xFF02, 0x80);

	for(int i = 0; i < BYTE_TICKS * 4; ++i)
	{
		serial.tick();
	}

	CHECK(serial.is_active());
	CHECK(io.io_read(0xFF01) == 0x5A);
	CHECK((io.io_read(0xFF02) & 0x80) != 0);

	io.io_write(0xFF02, 0x00);
	serial.tick();
	CHECK(!serial.is_active());
}

TEST_CASE("Serial: LinkCable exchanges bytes and raises serial IRQ")
{
	IO io_a;
	IO io_b;
	Serial a(&io_a);
	Serial b(&io_b);
	LinkCable cable;

	int irq_a = 0;
	int irq_b = 0;
	a.set_interrupt_callback([&](InterruptType type)
	{
		if(type == InterruptType::IT_SERIAL) ++irq_a;
	});
	b.set_interrupt_callback([&](InterruptType type)
	{
		if(type == InterruptType::IT_SERIAL) ++irq_b;
	});

	REQUIRE(cable.connect(a, b));
	CHECK(cable.connected());
	CHECK(cable.left() == &a);
	CHECK(cable.right() == &b);

	io_a.io_write(0xFF01, 0x42);
	io_a.io_write(0xFF02, 0x81);	// master, internal clock
	io_b.io_write(0xFF01, 0x99);
	io_b.io_write(0xFF02, 0x80);	// slave, external clock

	for(int i = 0; i < BYTE_TICKS; ++i)
	{
		a.tick();
		b.tick();
	}

	CHECK(io_a.io_read(0xFF01) == 0x99);
	CHECK(io_b.io_read(0xFF01) == 0x42);
	CHECK((io_a.io_read(0xFF02) & 0x80) == 0);
	CHECK((io_b.io_read(0xFF02) & 0x80) == 0);
	CHECK(irq_a == 1);
	CHECK(irq_b == 1);

	cable.disconnect();
	CHECK(!cable.connected());
	CHECK(a.link() == nullptr);
	CHECK(b.link() == nullptr);
}

TEST_CASE("Serial: QtreGB integration and serial IF")
{
	QtreGB a;
	QtreGB b;
	REQUIRE(a.load_cart(make_nop_rom()));
	REQUIRE(b.load_cart(make_nop_rom()));

	LinkCable cable;
	REQUIRE(cable.connect(a.m_serial, b.m_serial));

	a.m_bus.write(0xFF01, 0x12);
	a.m_bus.write(0xFF02, 0x81);
	b.m_bus.write(0xFF01, 0x34);
	b.m_bus.write(0xFF02, 0x80);

	for(int i = 0; i < BYTE_TICKS; ++i)
	{
		a.tick();
		b.tick();
	}

	CHECK(a.m_bus.read(0xFF01) == 0x34);
	CHECK(b.m_bus.read(0xFF01) == 0x12);
	CHECK((a.m_cpu.read_IF() & 0x08) != 0);
	CHECK((b.m_cpu.read_IF() & 0x08) != 0);
}

TEST_CASE("Serial: savestate keeps an in-flight transfer")
{
	QtreGB machine;
	REQUIRE(machine.load_cart(make_nop_rom()));
	machine.m_bus.write(0xFF01, 0x42);
	machine.m_bus.write(0xFF02, 0x81);

	for(int i = 0; i < 1000; ++i)
	{
		machine.tick();
	}

	std::vector<u8> saved;
	REQUIRE(machine.save_state(saved));

	for(int i = 0; i < 500; ++i)
	{
		machine.tick();
	}
	std::vector<u8> expected;
	REQUIRE(machine.save_state(expected));

	REQUIRE(machine.load_state(saved) == StateError::Ok);
	for(int i = 0; i < 500; ++i)
	{
		machine.tick();
	}
	std::vector<u8> actual;
	REQUIRE(machine.save_state(actual));

	CHECK(actual == expected);
}
