#include "doctest.h"
#include "input_directions.h"

#include <array>
#include <cstddef>

namespace
{

constexpr size_t RIGHT = 0;
constexpr size_t LEFT = 1;
constexpr size_t UP = 2;
constexpr size_t DOWN = 3;

std::array<bool, 4> directions(bool right, bool left, bool up, bool down)
{
	return {{right, left, up, down}};
}

} // namespace

TEST_CASE("Directions: opposite directions are allowed when enabled")
{
	DirectionResolver resolver;
	resolver.update(directions(true, true, true, true), true);

	CHECK(resolver.resolved()[RIGHT]);
	CHECK(resolver.resolved()[LEFT]);
	CHECK(resolver.resolved()[UP]);
	CHECK(resolver.resolved()[DOWN]);
}

TEST_CASE("Directions: latest press wins and releasing it restores the other")
{
	DirectionResolver resolver;

	resolver.update(directions(false, true, false, false), false);
	CHECK(resolver.resolved()[LEFT]);
	CHECK_FALSE(resolver.resolved()[RIGHT]);

	// Left is already held; Right is pressed later and wins.
	resolver.update(directions(true, true, false, false), false);
	CHECK_FALSE(resolver.resolved()[LEFT]);
	CHECK(resolver.resolved()[RIGHT]);

	// Releasing the latest direction restores the one that is still held.
	resolver.update(directions(false, true, false, false), false);
	CHECK(resolver.resolved()[LEFT]);
	CHECK_FALSE(resolver.resolved()[RIGHT]);
}

TEST_CASE("Directions: same-frame press has a deterministic tie-break")
{
	DirectionResolver resolver;
	resolver.update(directions(true, true, false, false), false);
	CHECK(resolver.resolved()[RIGHT]);
	CHECK_FALSE(resolver.resolved()[LEFT]);

	DirectionResolver vertical;
	vertical.update(directions(false, false, true, true), false);
	CHECK(vertical.resolved()[DOWN]);
	CHECK_FALSE(vertical.resolved()[UP]);
}

TEST_CASE("Directions: reset forgets the press history")
{
	DirectionResolver resolver;
	resolver.update(directions(false, true, false, false), false);
	resolver.reset();
	resolver.update(directions(true, true, false, false), false);

	// After reset both directions are new presses in the same update, so the
	// fixed tie-break applies again.
	CHECK(resolver.resolved()[RIGHT]);
	CHECK_FALSE(resolver.resolved()[LEFT]);
}
