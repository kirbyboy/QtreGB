#ifndef QTREGB_MEMORY_H
#define QTREGB_MEMORY_H

#include <vector>

#include "common.h"

class Memory
{
	public:
		Memory(u32 size);

		u8 read(u16 address);
		void write(u16 address, u8 value);

	private:
		std::vector<u8> m_ram;
};

#endif