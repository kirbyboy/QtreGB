#include "io.h"

using namespace qtreGB;

static u8 ly = 0;

u8 IO::io_read(u16 address)
{
	if(address == 0xFF01)
	{
		return m_sb;
	}
	else if(address == 0xFF02)
	{
		return m_sc;
	}


	// for debug:
	//if(address == 0xFF44)
	//{
		//return 0x90;
	//}

	if(address == 0xFF44)
	{
		return ly++;
	}

	return 0xFF;
}

void IO::io_write(u16 address, u8 value)
{
	if(address == 0xFF01)
	{
		m_sb = value;
	}
	else if(address == 0xFF02)
	{
		m_sc = value;
	}
}
