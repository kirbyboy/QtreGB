#include "doctest.h"
#include "timer.h"

using namespace qtreGB;

// Timer: one timer_tick() per M-cycle, divider advances by 4.

TEST_CASE("Timer: init sets DIV to 0xABCC and TIMA/TMA/TAC to 0")
{
	Timer t;
	t.timer_init();

	CHECK(t.read_div() == 0xAB);
	CHECK(t.read_tima() == 0x00);
	CHECK(t.read_tma() == 0x00);
	CHECK(t.read_tac() == 0xF8);
}

TEST_CASE("Timer: DIV advances 4 T-cycles per timer_tick")
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

TEST_CASE("Timer: TIMA overflow reloads TMA and raises IT_TIMER")
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

TEST_CASE("Timer: write_tima during the pending reload cancels it")
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

