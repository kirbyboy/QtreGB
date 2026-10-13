#include "operations.h"

using namespace qtreGB;

u8 Operations::set_bit(u8 value, u8 position, bool bit_value)
{
	if(position < 8)
	{
		if(bit_value)
		{
			return (value | (1 << position));
		}
		else
		{
			return (value & (~(1 << position)));
		}
	}
	return value;
}

bool Operations::get_bit(u8 value, u8 position)
{
	if(position < 8)
	{
		return (value & (1 << position)) != 0;
	}
	return false;
}
