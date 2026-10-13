#ifndef QTREGB_IO_H
#define QTREGB_IO_H

#include "types.h"

namespace qtreGB
{

class IO
{
	public:
		u8 io_read(u16 address);
		void io_write(u16 address, u8 value);
	private:
		u8 m_sb;	// Serial transfer data
		u8 m_sc;	// Serial transfer control
		//u8 m_
};


} // namespace qtreGB
#endif