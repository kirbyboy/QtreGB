#ifndef QTREGB_OPERATIONS_H
#define QTREGB_OPERATIONS_H

#include "common.h"

class Operations
{
	public:		
		static u8 set_bit(u8 value, u8 position, bool bit_value);
		static bool get_bit(u8 value, u8 position);
};

#endif