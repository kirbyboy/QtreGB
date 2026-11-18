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

namespace qtreGB
{

std::vector<u8> cart_ram_bytes(const std::vector<Memory>& ram)
{
	size_t total = 0;
	for(const Memory& bank : ram)
	{
		total += bank.size();
	}

	std::vector<u8> data;
	data.reserve(total);
	for(const Memory& bank : ram)
	{
		data.insert(data.end(), bank.data().begin(), bank.data().end());
	}
	return data;
}

bool cart_ram_load(const std::vector<u8>& data, std::vector<Memory>& ram)
{
	size_t total = 0;
	for(const Memory& bank : ram)
	{
		total += bank.size();
	}
	if(data.size() != total)
	{
		return false;
	}

	size_t offset = 0;
	for(Memory& bank : ram)
	{
		std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(offset),
			static_cast<std::ptrdiff_t>(bank.size()), bank.data().begin());
		offset += bank.size();
	}
	return true;
}

} // namespace qtreGB
