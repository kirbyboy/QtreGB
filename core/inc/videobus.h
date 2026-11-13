#ifndef QTREGB_VIDEOBUS_H
#define QTREGB_VIDEOBUS_H

#include "types.h"

namespace qtreGB
{

enum class VideoDevice : u8
{
	Cpu = 0,
	Ppu,
	Dma,
};

enum class VideoAccess : u8
{
	Read = 0,
	Write,
};

// Ownership of the VRAM/OAM buses. The PPU owns its memory; this class only
// tracks which device is currently using the bus so the CPU view can resolve
// contention at the access T-cycle.
class VideoBus
{
	public:
		void acquire(VideoDevice device, VideoAccess access)
		{
			acquirers(access) |= static_cast<u8>(1u << static_cast<u8>(device));
		}

		void release(VideoDevice device, VideoAccess access)
		{
			acquirers(access) &= static_cast<u8>(~(1u << static_cast<u8>(device)));
		}

		bool is_acquired(VideoAccess access) const
		{
			return acquirers(access) != 0;
		}

		bool is_acquired_by(VideoDevice device, VideoAccess access) const
		{
			return (acquirers(access) & static_cast<u8>(1u << static_cast<u8>(device))) != 0;
		}

		void reset()
		{
			m_read_acquirers = 0;
			m_write_acquirers = 0;
		}

	private:
		u8& acquirers(VideoAccess access)
		{
			return access == VideoAccess::Read ? m_read_acquirers : m_write_acquirers;
		}

		u8 acquirers(VideoAccess access) const
		{
			return access == VideoAccess::Read ? m_read_acquirers : m_write_acquirers;
		}

		u8 m_read_acquirers = 0;
		u8 m_write_acquirers = 0;
};

} // namespace qtreGB

#endif
