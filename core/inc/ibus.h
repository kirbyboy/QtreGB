#ifndef QTREGB_IBUS_H
#define QTREGB_IBUS_H

#include "types.h"

namespace qtreGB
{

class IBus
{
	public:
		virtual ~IBus() = default;
		virtual u8 read(u16 addr) = 0;
		virtual void write(u16 addr, u8 value) = 0;
};


} // namespace qtreGB
#endif
