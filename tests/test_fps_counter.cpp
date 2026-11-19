#include "doctest.h"
#include "fps_counter.h"

TEST_CASE("FPS counter: averages emulated frames over the window")
{
	FpsCounter counter;
	CHECK_FALSE(counter.has_value(0.0));

	// 31 frames at 60 Hz: the first one starts the window and the last one
	// closes it at 0.5 s, so the measured rate is 62 fps.
	for(int i = 0; i <= 30; ++i)
	{
		counter.add_frame(i / 60.0);
	}
	CHECK(counter.has_value(0.5));
	CHECK(counter.fps(0.5) == doctest::Approx(62.0));

	// A second window at the true 60 Hz rate averages to 60 fps.
	for(int i = 1; i <= 30; ++i)
	{
		counter.add_frame(0.5 + i / 60.0);
	}
	CHECK(counter.fps(1.0) == doctest::Approx(60.0));
}

TEST_CASE("FPS counter: stale readings drop to zero")
{
	FpsCounter counter;
	for(int i = 0; i <= 30; ++i)
	{
		counter.add_frame(i / 60.0);
	}
	REQUIRE(counter.has_value(0.5));

	CHECK_FALSE(counter.has_value(2.0));
	CHECK(counter.fps(2.0) == doctest::Approx(0.0));

	// Resuming after a pause starts a new window instead of averaging the
	// idle time into the rate.
	counter.add_frame(2.5);
	CHECK_FALSE(counter.has_value(2.5));
	for(int i = 2; i <= 30; ++i)
	{
		counter.add_frame(2.5 + i / 60.0);
	}
	CHECK(counter.fps(3.0) == doctest::Approx(60.0));
}

TEST_CASE("FPS counter: reset clears the last measurement")
{
	FpsCounter counter;
	for(int i = 0; i <= 30; ++i)
	{
		counter.add_frame(i / 60.0);
	}
	REQUIRE(counter.has_value(0.5));

	counter.reset();
	CHECK_FALSE(counter.has_value(0.5));
	CHECK(counter.fps(0.5) == doctest::Approx(0.0));
}
