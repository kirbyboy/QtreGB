#include "doctest.h"
#include "videobus.h"

using namespace qtreGB;

TEST_CASE("VideoBus: starts free")
{
	VideoBus bus;
	CHECK_FALSE(bus.is_acquired(VideoAccess::Read));
	CHECK_FALSE(bus.is_acquired(VideoAccess::Write));
}

TEST_CASE("VideoBus: PPU acquisition is per access and device")
{
	VideoBus bus;
	bus.acquire(VideoDevice::Ppu, VideoAccess::Read);

	CHECK(bus.is_acquired(VideoAccess::Read));
	CHECK_FALSE(bus.is_acquired(VideoAccess::Write));
	CHECK(bus.is_acquired_by(VideoDevice::Ppu, VideoAccess::Read));
	CHECK_FALSE(bus.is_acquired_by(VideoDevice::Cpu, VideoAccess::Read));

	bus.release(VideoDevice::Ppu, VideoAccess::Read);
	CHECK_FALSE(bus.is_acquired(VideoAccess::Read));
}

TEST_CASE("VideoBus: multiple devices keep the bus acquired until all release")
{
	VideoBus bus;
	bus.acquire(VideoDevice::Ppu, VideoAccess::Write);
	bus.acquire(VideoDevice::Dma, VideoAccess::Write);

	CHECK(bus.is_acquired(VideoAccess::Write));

	bus.release(VideoDevice::Ppu, VideoAccess::Write);
	CHECK(bus.is_acquired(VideoAccess::Write));
	CHECK(bus.is_acquired_by(VideoDevice::Dma, VideoAccess::Write));

	bus.release(VideoDevice::Dma, VideoAccess::Write);
	CHECK_FALSE(bus.is_acquired(VideoAccess::Write));
}

TEST_CASE("VideoBus: reset clears both directions")
{
	VideoBus bus;
	bus.acquire(VideoDevice::Ppu, VideoAccess::Read);
	bus.acquire(VideoDevice::Ppu, VideoAccess::Write);

	bus.reset();

	CHECK_FALSE(bus.is_acquired(VideoAccess::Read));
	CHECK_FALSE(bus.is_acquired(VideoAccess::Write));
}
