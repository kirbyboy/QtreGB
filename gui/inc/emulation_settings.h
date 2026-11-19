#ifndef QTREGB_EMULATION_SETTINGS_H
#define QTREGB_EMULATION_SETTINGS_H

#include <string>

#include "types.h"

enum class FastForwardSpeed : qtreGB::u8
{
	X200 = 0,
	X300,
	Max,
};

inline const char* fast_forward_speed_name(FastForwardSpeed speed)
{
	switch(speed)
	{
		case FastForwardSpeed::X200: return "200";
		case FastForwardSpeed::X300: return "300";
		case FastForwardSpeed::Max: return "max";
	}
	return "200";
}

inline bool parse_fast_forward_speed(const std::string& text,
	FastForwardSpeed& speed)
{
	if(text == "200" || text == "200%")
	{
		speed = FastForwardSpeed::X200;
		return true;
	}
	if(text == "300" || text == "300%")
	{
		speed = FastForwardSpeed::X300;
		return true;
	}
	if(text == "max")
	{
		speed = FastForwardSpeed::Max;
		return true;
	}
	return false;
}

#endif
