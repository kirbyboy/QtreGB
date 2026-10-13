#include "memory.h"

using namespace qtreGB;

using namespace std;

Memory::Memory(u32 size) : m_ram(vector<u8>(size))
{
}

u8 Memory::read(u16 address)
{
	return m_ram[address];
}

void Memory::write(u16 address, u8 value)
{
	m_ram[address] = value;
}
