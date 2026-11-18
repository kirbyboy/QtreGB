#include "io.h"
#include "state_stream.h"

using namespace qtreGB;

namespace
{

constexpr u16 IO_SB = 0x0001;
constexpr u16 IO_SC = 0x0002;

} // namespace

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

void IO::save_state(StateWriter& writer) const
{
	writer.write_u8(IO_SB, m_sb);
	writer.write_u8(IO_SC, m_sc);
}

bool IO::load_state(StateReader& reader)
{
	u8 sb = 0;
	u8 sc = 0;
	if(!reader.read_u8(IO_SB, sb) || !reader.read_u8(IO_SC, sc))
	{
		return false;
	}
	m_sb = sb;
	m_sc = sc;
	return true;
}
