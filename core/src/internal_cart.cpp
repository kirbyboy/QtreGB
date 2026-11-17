#include "internal_cart.h"
#include "memory.h"

#include <algorithm>

using namespace qtreGB;

InternalCart::InternalCart()
{
}

InternalCart::~InternalCart()
{
}

void InternalCart::append_battery_data(std::vector<u8>& data)
{
	for(const Memory& bank : get_ram())
	{
		data.insert(data.end(), bank.data().begin(), bank.data().end());
	}
}

void InternalCart::load_battery_data(const std::vector<u8>& data)
{
	size_t offset = 0;
	for(Memory& bank : get_ram())
	{
		if(offset >= data.size())
		{
			break;
		}
		const size_t count = std::min(bank.size(), data.size() - offset);
		std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(offset),
			static_cast<std::ptrdiff_t>(count), bank.data().begin());
		offset += count;
	}
}
