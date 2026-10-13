#ifndef QTREGB_OAM_ENTRY_H
#define QTREGB_OAM_ENTRY_H

#include "types.h"

namespace qtreGB
{

class OAMEntry
{
	public:
		OAMEntry();

		u8 m_y;
		u8 m_x;
		u8 m_tile;
		u8 m_flags;
};


} // namespace qtreGB
#endif
