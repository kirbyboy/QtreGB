#ifndef QTREGB_OAM_ENTRY_H
#define QTREGB_OAM_ENTRY_H

#include "common.h"

class OAMEntry
{
	public:
		OAMEntry();

		u8 m_y;
		u8 m_x;
		u8 m_tile;
		u8 m_flags;
};

#endif
