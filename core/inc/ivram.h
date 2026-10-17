#ifndef QTREGB_IVRAM_H
#define QTREGB_IVRAM_H

#include "types.h"

namespace qtreGB
{

// Read-only VRAM view for the fetcher. The addresses are the full 0x8000-0x9FFF
// range, so the implementer decides how they are mapped to its own storage.
class IVram
{
	public:
		virtual ~IVram() = default;
		virtual u8 read(u16 address) = 0;
};

} // namespace qtreGB
#endif
