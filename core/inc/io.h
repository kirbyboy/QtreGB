#ifndef QTREGB_IO_H
#define QTREGB_IO_H

#include "types.h"

namespace qtreGB
{

class StateReader;
class StateWriter;

class IO
{
	public:
		u8 io_read(u16 address);
		void io_write(u16 address, u8 value);

		void save_state(StateWriter& writer) const;
		bool load_state(StateReader& reader);
	private:
		u8 m_sb = 0;	// Serial transfer data (FF01)
		u8 m_sc = 0;	// Serial transfer control (FF02)
		//u8 m_
};


} // namespace qtreGB
#endif