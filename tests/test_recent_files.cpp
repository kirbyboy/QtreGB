#include "doctest.h"
#include "recent_files.h"

#include <algorithm>
#include <string>
#include <vector>

TEST_CASE("Recent files: most recent first, unique and capped")
{
	RecentFiles recent;
	CHECK(recent.empty());

	for(int i = 0; i < 12; ++i)
	{
		recent.remember("/games/rom" + std::to_string(i) + ".gb");
	}
	REQUIRE(recent.size() == RecentFiles::MaxEntries);
	CHECK(recent.paths().front() == "/games/rom11.gb");
	CHECK(recent.paths().back() == "/games/rom2.gb");

	// Opening an existing entry moves it to the front without duplicating it.
	recent.remember("/games/rom5.gb");
	CHECK(recent.paths().front() == "/games/rom5.gb");
	CHECK(recent.size() == RecentFiles::MaxEntries);
	CHECK(std::count(recent.paths().begin(), recent.paths().end(),
		"/games/rom5.gb") == 1);

	// Re-adding a path that fell off the cap puts it back and evicts the
	// oldest entry.
	recent.remember("/games/rom1.gb");
	CHECK(recent.paths().front() == "/games/rom1.gb");
	CHECK(recent.size() == RecentFiles::MaxEntries);
	CHECK(std::find(recent.paths().begin(), recent.paths().end(),
		"/games/rom2.gb") == recent.paths().end());

	recent.remove("/games/rom1.gb");
	CHECK(std::find(recent.paths().begin(), recent.paths().end(),
		"/games/rom1.gb") == recent.paths().end());

	// Passing references into the list itself must be safe.
	const std::string front = recent.paths().front();
	recent.remember(recent.paths().front());
	CHECK(recent.paths().front() == front);
	recent.remove(recent.paths().front());
	REQUIRE_FALSE(recent.empty());
	CHECK(recent.paths().front() != front);

	recent.clear();
	CHECK(recent.empty());
}

TEST_CASE("Recent files: load order is preserved and invalid entries dropped")
{
	RecentFiles recent;
	recent.set({"/a.gb", "", "/b.gb", "/a.gb", "/c.gb"});
	REQUIRE(recent.size() == 3);
	CHECK(recent.paths()[0] == "/a.gb");
	CHECK(recent.paths()[1] == "/b.gb");
	CHECK(recent.paths()[2] == "/c.gb");

	std::vector<std::string> too_many;
	for(int i = 0; i < 15; ++i)
	{
		too_many.push_back("/rom" + std::to_string(i) + ".gb");
	}
	recent.set(too_many);
	CHECK(recent.size() == RecentFiles::MaxEntries);
	CHECK(recent.paths().front() == "/rom0.gb");
	CHECK(recent.paths().back() == "/rom9.gb");
}
