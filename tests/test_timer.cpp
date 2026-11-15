 #include "doctest.h"
#include "timer.h"

using namespace qtreGB;

#ifndef QTR_CPU_MICRO

// Reference values: Pan Docs - "Timer Obscure Behaviour", DIV = 0xAC00 after
// power-on. Register 0xFF04 exposes only the high byte of the divider.

TEST_CASE("Timer: init sets DIV to 0xAC00 (read as 0xAC via 0xFF04)")
{
	Timer t;
	t.timer_init();

	CHECK(t.read_div() == 0xAC);
}

TEST_CASE("Timer: write_div resets the divider and DIV advances every 256 ticks")
{
	Timer t;
	t.timer_init();
	t.write_div();

	CHECK(t.read_div() == 0x00);

	// The DIV-write cycle itself absorbs that T-cycle's increment, so the
	// divider reaches 0x0100 on tick 257, not 256.
	for(int i = 0; i < 257; ++i)
	{
		t.timer_tick();
	}
	CHECK(t.read_div() == 0x01);

	t.write_div();
	CHECK(t.read_div() == 0x00);
}

TEST_CASE("Timer: with TAC disabled TIMA does not advance")
{
	Timer t;
	t.timer_init();

	t.write_tac(0x00);	// interrupt enable (bit 2) off
	t.write_tima(0);

	for(int i = 0; i < 1024; ++i)	// 1024 ticks = one full cycle of bit 9
	{
		t.timer_tick();
	}

	CHECK(t.read_tima() == 0);
}

TEST_CASE("Timer: mode 0 (bit 9) increments TIMA on the falling edge")
{
	Timer t;
	t.timer_init();		// DIV = 0xAC00 -> bit 9 at 0

	t.write_tac(0x04);	// mode 0 (4096Hz) + enabled
	t.write_tima(0);

	// 511 ticks: DIV 0xAC00..0xADFF, bit 9 at 0 -> TIMA unchanged
	for(int i = 0; i < 511; ++i)
	{
		t.timer_tick();
	}
	CHECK(t.read_tima() == 0);

	// Tick 512: DIV 0xADFF -> 0xAE00, bit 9 rises (0->1) -> does NOT count
	t.timer_tick();
	CHECK(t.read_tima() == 0);

	// 511 more ticks: DIV 0xAE00..0xAFFF, bit 9 at 1 -> no edge
	for(int i = 0; i < 511; ++i)
	{
		t.timer_tick();
	}
	CHECK(t.read_tima() == 0);

	// Tick 1024: DIV 0xAFFF -> 0xB000, bit 9 falls (1->0) -> TIMA++
	t.timer_tick();
	CHECK(t.read_tima() == 1);
}

TEST_CASE("Timer: mode 1 (bit 3) increments TIMA on the falling edge")
{
	Timer t;
	t.timer_init();		// DIV = 0xAC00 -> bit 3 at 0

	t.write_tac(0x05);	// mode 1 (262144Hz) + enabled
	t.write_tima(0);

	// 7 ticks: DIV 0xAC00..0xAC07, bit 3 at 0 -> TIMA unchanged
	for(int i = 0; i < 7; ++i)
	{
		t.timer_tick();
	}
	CHECK(t.read_tima() == 0);

	// Tick 8: DIV 0xAC07 -> 0xAC08, bit 3 rises (0->1) -> does NOT count
	t.timer_tick();
	CHECK(t.read_tima() == 0);

	// 7 more ticks: DIV 0xAC08..0xAC0F, bit 3 at 1 -> no edge
	for(int i = 0; i < 7; ++i)
	{
		t.timer_tick();
	}
	CHECK(t.read_tima() == 0);

	// Tick 16: DIV 0xAC0F -> 0xAC10, bit 3 falls (1->0) -> TIMA++
	t.timer_tick();
	CHECK(t.read_tima() == 1);
}

TEST_CASE("Timer: overflow sets TIMA to 0x00 and reloads TMA + IT_TIMER 4 T-cycles later")
{
	Timer t;
	t.timer_init();

	t.write_tac(0x04);	// mode 0 + enabled
	t.write_tima(0xFF);
	t.write_tma(0x42);

	int interrupts = 0;
	t.set_interrupt_callback([&](InterruptType type)
	{
		++interrupts;
		CHECK(type == InterruptType::IT_TIMER);
	});

	// 1023 ticks: no falling edge yet -> TIMA still 0xFF
	for(int i = 0; i < 1023; ++i)
	{
		t.timer_tick();
	}
	CHECK(t.read_tima() == 0xFF);
	CHECK(interrupts == 0);

	// Tick 1024: falling edge with TIMA == 0xFF -> overflow: reads 0x00
	t.timer_tick();
	CHECK(t.read_tima() == 0x00);
	CHECK(interrupts == 0);

	// Ticks 1025-1027: the reload is still pending -> TIMA still 0x00
	// (Pan Docs: "TIMA is equal to $00 for the M-cycle after it overflows")
	t.timer_tick();
	t.timer_tick();
	t.timer_tick();
	CHECK(t.read_tima() == 0x00);
	CHECK(interrupts == 0);

	// Tick 1028 (4 T-cycles after the overflow): TIMA = TMA and the interrupt fires
	t.timer_tick();
	CHECK(t.read_tima() == 0x42);
	CHECK(interrupts == 1);
}

TEST_CASE("Timer: writing TIMA in the overflow cycle cancels the increment")
{
	Timer t;
	t.timer_init();

	t.write_tac(0x04);	// mode 0 + enabled
	t.write_tima(0xFF);
	t.write_tma(0x42);

	int interrupts = 0;
	t.set_interrupt_callback([&](InterruptType type)
	{
		++interrupts;
		CHECK(type == InterruptType::IT_TIMER);
	});

	// 1023 ticks: the next one would overflow. m_div is 45055 = 0xB000 - 1,
	// and the incoming tick takes it to 45056, which is a multiple of 4: that is,
	// this is the last T-cycle of the M-cycle in which the overflow happens.
	for(int i = 0; i < 1023; ++i)
	{
		t.timer_tick();
	}
	CHECK(t.read_tima() == 0xFF);
	CHECK(interrupts == 0);

	// Write in that M-cycle: it blocks the edge, so there is no increment,
	// no overflow and no interrupt, and the written value stays.
	t.write_tima(0x7F);
	t.timer_tick();
	CHECK(t.read_tima() == 0x7F);
	CHECK(interrupts == 0);

	// The block does not survive its M-cycle: the next bit 9 edge falls
	// 1024 T-cycles later (45056 -> 46080) and increments again.
	for(int i = 0; i < 1024; ++i)
	{
		t.timer_tick();
	}
	CHECK(t.read_tima() == 0x80);
}

TEST_CASE("Timer: writing TIMA in cycle A cancels the reload and the interrupt")
{
	Timer t;
	t.timer_init();

	t.write_tac(0x04);	// mode 0 + enabled
	t.write_tima(0xFF);
	t.write_tma(0x42);

	int interrupts = 0;
	t.set_interrupt_callback([&](InterruptType type)
	{
		++interrupts;
		CHECK(type == InterruptType::IT_TIMER);
	});

	// Tick 1024: overflow
	for(int i = 0; i < 1024; ++i)
	{
		t.timer_tick();
	}
	CHECK(t.read_tima() == 0x00);
	CHECK(interrupts == 0);

	t.timer_tick();		// tick 1025
	t.timer_tick();		// tick 1026

	// Pan Docs: writing TIMA during cycle A acts as if the overflow never
	// happened: the written value stays, TMA is not copied and the IF bit
	// is not set.
	t.write_tima(0x7F);
	CHECK(t.read_tima() == 0x7F);
	CHECK(interrupts == 0);

	// The reload was cancelled: no later tick copies TMA nor re-raises the
	// interrupt.
	t.timer_tick();
	t.timer_tick();
	t.timer_tick();
	t.timer_tick();
	CHECK(t.read_tima() == 0x7F);
	CHECK(interrupts == 0);
}

TEST_CASE("Timer: writing TIMA in cycle B is ignored and TMA wins")
{
	Timer t;
	t.timer_init();

	t.write_tac(0x04);	// mode 0 + enabled
	t.write_tima(0xFF);
	t.write_tma(0x42);

	int interrupts = 0;
	t.set_interrupt_callback([&](InterruptType type)
	{
		++interrupts;
		CHECK(type == InterruptType::IT_TIMER);
	});

	// Tick 1024: overflow
	for(int i = 0; i < 1024; ++i)
	{
		t.timer_tick();
	}
	CHECK(t.read_tima() == 0x00);
	CHECK(interrupts == 0);

	// Ticks 1025-1028: the reload happens on 1028
	t.timer_tick();
	t.timer_tick();
	t.timer_tick();
	t.timer_tick();
	CHECK(t.read_tima() == 0x42);
	CHECK(interrupts == 1);

	// Cycle B: the Load signal stays asserted for the whole M-cycle, so the
	// write goes through but is immediately overwritten by TMA.
	t.write_tima(0x7F);
	CHECK(t.read_tima() == 0x42);

	t.timer_tick();
	t.timer_tick();
	t.timer_tick();
	t.timer_tick();
	CHECK(t.read_tima() == 0x42);
	CHECK(interrupts == 1);

	// Outside cycle B the write applies again.
	t.write_tima(0x7F);
	CHECK(t.read_tima() == 0x7F);
}

TEST_CASE("Timer: writing TMA copies the value into TIMA if it falls in cycle B")
{
	Timer t;
	t.timer_init();

	t.write_tac(0x04);	// mode 0 + enabled
	t.write_tima(0xFF);
	t.write_tma(0x42);

	int interrupts = 0;
	t.set_interrupt_callback([&](InterruptType type)
	{
		++interrupts;
		CHECK(type == InterruptType::IT_TIMER);
	});

	// Tick 1024: overflow
	for(int i = 0; i < 1024; ++i)
	{
		t.timer_tick();
	}
	CHECK(t.read_tima() == 0x00);
	CHECK(interrupts == 0);

	t.timer_tick();		// tick 1025
	t.timer_tick();		// tick 1026

	// TMA written during cycle A: TIMA stays 0x00 and the reload will copy
	// the new value.
	t.write_tma(0x7F);
	CHECK(t.read_tima() == 0x00);
	CHECK(t.read_tma() == 0x7F);

	t.timer_tick();		// tick 1027
	t.timer_tick();		// tick 1028: reload
	CHECK(t.read_tima() == 0x7F);
	CHECK(interrupts == 1);

	// TMA written during cycle B: it is copied into TIMA in the same cycle.
	t.write_tma(0x99);
	CHECK(t.read_tima() == 0x99);
	CHECK(t.read_tma() == 0x99);
}

TEST_CASE("Timer: write_div with the selected bit at 1 fires an immediate tick")
{
	Timer t;
	t.timer_init();		// DIV = 0xAC00, bit 9 at 0

	t.write_tac(0x04);	// mode 0 + enabled
	t.write_tima(0);

	// 512 ticks: bit 9 rises (0->1) -> no tick
	for(int i = 0; i < 512; ++i)
	{
		t.timer_tick();
	}
	CHECK(t.read_tima() == 0);
	CHECK(t.read_div() == 0xAE);	// 0xAC00 + 512 = 0xAE00

	// write_div: bit 9 falls (1->0) -> the tick fires immediately
	t.write_div();
	CHECK(t.read_tima() == 1);
	CHECK(t.read_div() == 0x00);

	// With the bit at 0, write_div fires nothing
	t.write_div();
	CHECK(t.read_tima() == 1);
}

TEST_CASE("Timer: write_tac with a frequency change fires an immediate tick")
{
	Timer t;
	t.timer_init();		// DIV = 0xAC00, bit 9 at 0

	t.write_tac(0x04);	// mode 0 (bit 9) + enabled
	t.write_tima(0);

	for(int i = 0; i < 512; ++i)
	{
		t.timer_tick();	// DIV = 0xAE00: bit 9 at 1
	}
	CHECK(t.read_tima() == 0);

	// Change to mode 1 (bit 3): at 0xAE00 bit 3 is at 0 -> falls -> tick
	t.write_tac(0x05);
	CHECK(t.read_tima() == 1);

	// Back to mode 0: bit 3 at 0 -> bit 9 at 1 (rising) -> no tick
	t.write_tac(0x04);
	CHECK(t.read_tima() == 1);

	// With DIV = 0xAF08, bit 9 and bit 3 at 1: changing mode does not fall -> no tick
	Timer t2;
	t2.timer_init();
	t2.write_tac(0x04);
	t2.write_tima(0);
	for(int i = 0; i < 776; ++i)	// 0xAC00 + 776 = 0xAF08
	{
		t2.timer_tick();
	}
	CHECK(t2.read_tima() == 0);
	t2.write_tac(0x05);	// bit 9 (1) -> bit 3 (1): no edge
	CHECK(t2.read_tima() == 0);
}

TEST_CASE("Timer: disabling with the bit at 1 fires a tick on DMG")
{
	Timer t;
	t.timer_init();
	t.write_tac(0x04);
	t.write_tima(0);

	for(int i = 0; i < 512; ++i)
	{
		t.timer_tick();	// bit 9 at 1
	}
	CHECK(t.read_tima() == 0);

	t.write_tac(0x00);	// disabling with bit 9 at 1 -> falls -> tick
	CHECK(t.read_tima() == 1);
	CHECK(t.read_tac() == 0xF8);	// upper 5 bits always read high

	// Disabling with the bit at 0 fires nothing
	Timer t2;
	t2.timer_init();	// bit 9 at 0
	t2.write_tac(0x04);
	t2.write_tima(0);
	t2.write_tac(0x00);	// disabled without a fall
	CHECK(t2.read_tima() == 0);
}

TEST_CASE("Timer: MMIO register round-trip")
{
	Timer t;
	t.timer_init();

	t.write_tima(0x12);
	t.write_tma(0x34);
	t.write_tac(0x07);

	CHECK(t.read_tima() == 0x12);
	CHECK(t.read_tma() == 0x34);
	CHECK(t.read_tac() == 0xFF);	// 0x07 | 0xF8: not a round-trip
}

TEST_CASE("Timer: TAC discards the unmapped upper bits on write")
{
	Timer t;
	t.timer_init();

	// Bits 3-7 are not wired to the write bus, so they are dropped and only
	// the lower 3 bits reach the register.
	t.write_tac(0xFF);
	CHECK(t.read_tac() == 0xFF);

	// The enable bit (bit 2) is what matters: 0xFF enables the timer, and
	// so does 0x04. Both land on the same stored value.
	t.write_tac(0x00);
	CHECK(t.read_tac() == 0xF8);

	t.write_tac(0x04);
	CHECK(t.read_tac() == 0xFC);

	// A write of 0x80 must not be able to affect the enable bit.
	t.write_tac(0x80);
	CHECK(t.read_tac() == 0xF8);

	// read_tac() ORs 0xF8, so it cannot observe whether the upper bits were
	// masked off on write. The savestate can, because it stores m_tac raw.
	Timer masked; masked.timer_init(); masked.write_tac(0xFF);
	Timer unmasked; unmasked.timer_init(); unmasked.write_tac(0x07);
	TaggedSerializer sm(TaggedSerializer::Mode::Write); masked.serialize(sm);
	TaggedSerializer su(TaggedSerializer::Mode::Write); unmasked.serialize(su);
	CHECK(sm.data() == su.data());
}

TEST_CASE("Timer: timer_init() defines TIMA, TMA and TAC regardless of prior state")
{
	Timer t;

	// Dirty the registers, then reset. If timer_init() did not establish a
	// defined state for them, these values would survive the reset.
	t.write_tima(0xAB);
	t.write_tma(0xCD);
	t.write_tac(0x07);

	t.timer_init();

	CHECK(t.read_tima() == 0x00);
	CHECK(t.read_tma() == 0x00);
	CHECK(t.read_tac() == 0xF8);
	CHECK(t.read_div() == 0xAC);
}

TEST_CASE("Timer: savestate with TaggedSerializer (write and read)")
{
	Timer t;
	t.timer_init();
	t.write_div();		// divider to 0: after the burn-in it will be 300
	t.write_tac(0x00);	// disabled: ticks only move DIV
	t.write_tima(0x11);
	t.write_tma(0x22);

	for(int i = 0; i < 300; ++i)	// leaves the divider at 300 (high byte = 0x01)
	{
		t.timer_tick();
	}

	t.write_tac(0x05);

	TaggedSerializer out(TaggedSerializer::Mode::Write);
	t.serialize(out);
	CHECK(!out.data().empty());

	TaggedSerializer in(TaggedSerializer::Mode::Read, out.data());
	Timer t2;
	t2.serialize(in);

	CHECK(t2.read_div() == 0x01);
	CHECK(t2.read_tima() == 0x11);
	CHECK(t2.read_tma() == 0x22);
	CHECK(t2.read_tac() == 0xFD);	// 0x05 | 0xF8
}

#else // QTR_CPU_MICRO

// DocBoy-style timer: one timer_tick() per M-cycle, divider advances by 4.

TEST_CASE("Timer (DocBoy): init sets DIV to 0xABCC and TIMA/TMA/TAC to 0")
{
	Timer t;
	t.timer_init();

	CHECK(t.read_div() == 0xAB);
	CHECK(t.read_tima() == 0x00);
	CHECK(t.read_tma() == 0x00);
	CHECK(t.read_tac() == 0xF8);
}

TEST_CASE("Timer (DocBoy): DIV advances 4 T-cycles per timer_tick")
{
	Timer t;
	t.timer_init();
	t.write_div();
	CHECK(t.read_div() == 0x00);

	for(int i = 0; i < 64; ++i)
	{
		t.timer_tick();
	}
	CHECK(t.read_div() == 0x01);
}

TEST_CASE("Timer (DocBoy): TIMA overflow reloads TMA and raises IT_TIMER")
{
	Timer t;
	t.timer_init();

	int interrupts = 0;
	t.set_interrupt_callback([&](InterruptType type)
	{
		if(type == InterruptType::IT_TIMER)
		{
			++interrupts;
		}
	});

	t.write_tma(0x42);
	t.write_tima(0xFF);
	t.write_tac(0x05);	// enable, mode 1 (DIV bit 3)

	for(int i = 0; i < 16 && interrupts == 0; ++i)
	{
		t.timer_tick();
	}

	CHECK(interrupts == 1);
	CHECK(t.read_tima() == 0x42);
}

TEST_CASE("Timer (DocBoy): write_tima during the pending reload cancels it")
{
	Timer t;
	t.timer_init();

	int interrupts = 0;
	t.set_interrupt_callback([&](InterruptType type)
	{
		if(type == InterruptType::IT_TIMER)
		{
			++interrupts;
		}
	});

	t.write_tma(0x55);
	t.write_tima(0xFF);
	t.write_tac(0x05);

	// Run until the overflow has been latched (TIMA becomes 0x00).
	for(int i = 0; i < 16 && t.read_tima() != 0x00; ++i)
	{
		t.timer_tick();
	}
	REQUIRE(t.read_tima() == 0x00);

	t.write_tima(0x11);
	t.timer_tick();

	CHECK(t.read_tima() == 0x11);
	CHECK(interrupts == 0);
}

#endif // QTR_CPU_MICRO
