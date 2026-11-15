#include "doctest.h"
#include "phase_clock.h"

using namespace qtreGB;

TEST_CASE("PhaseClock: T0 -> T1 -> T2 -> T3 -> T0")
{
	PhaseClock clock;
	CHECK(clock.get() == Phase::T0);
	CHECK(clock.advance() == Phase::T1);
	CHECK(clock.advance() == Phase::T2);
	CHECK(clock.advance() == Phase::T3);
	CHECK(clock.advance() == Phase::T0);
}

TEST_CASE("PhaseClock: reset returns to T0")
{
	PhaseClock clock;
	clock.advance();
	clock.advance();
	clock.reset();
	CHECK(clock.get() == Phase::T0);
}
