#ifndef QTREGB_UTILS_H
#define QTREGB_UTILS_H

#include <string>
#include <sstream>
#include <iomanip>

#include "types.h"

namespace qtreGB
{

inline std::string to_hex(u8 v)
{
	std::ostringstream ss;
	ss 	<< "0x"
		<< std::hex << std::uppercase << std::setw(2) << std::setfill('0')
		<< static_cast<int>(v);
	return ss.str();
}

} // namespace qtreGB

#endif