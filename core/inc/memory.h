#ifndef QTREGB_MEMORY_H
#define QTREGB_MEMORY_H

#include <cstddef>
#include <vector>

#include "types.h"

namespace qtreGB
{

class Memory
{
	public:
		Memory(u32 size);

		u8 read(u16 address);
		void write(u16 address, u8 value);

		const std::vector<u8>& data() const { return m_ram; }
		std::vector<u8>& data() { return m_ram; }
		size_t size() const { return m_ram.size(); }

	private:
		std::vector<u8> m_ram;
};


} // namespace qtreGB
#endif