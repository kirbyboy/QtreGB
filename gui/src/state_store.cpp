#include "state_store.h"

#include <fstream>
#include <iterator>

void StateStore::set_rom_path(const std::string& rom_path)
{
	m_rom_path = rom_path;
}

void StateStore::set_player_index(qtreGB::u8 player_index)
{
	m_player_index = player_index == 0 ? 1 : player_index;
}

std::string StateStore::path() const
{
	if(m_rom_path.empty())
	{
		return {};
	}

	const size_t slash = m_rom_path.find_last_of("/\\");
	const size_t dot = m_rom_path.find_last_of('.');
	const std::string extension = ".sr" + std::to_string(m_player_index);
	if(dot != std::string::npos && (slash == std::string::npos || dot > slash))
	{
		return m_rom_path.substr(0, dot) + extension;
	}
	return m_rom_path + extension;
}

bool StateStore::save(const std::vector<uint8_t>& data) const
{
	const std::string state_path = path();
	if(state_path.empty() || data.empty())
	{
		return false;
	}

	std::ofstream out(state_path, std::ios::binary | std::ios::trunc);
	if(!out)
	{
		return false;
	}
	out.write(reinterpret_cast<const char*>(data.data()),
		static_cast<std::streamsize>(data.size()));
	return out.good();
}

bool StateStore::load(std::vector<uint8_t>& data) const
{
	const std::string state_path = path();
	if(state_path.empty())
	{
		return false;
	}

	std::ifstream in(state_path, std::ios::binary);
	if(!in)
	{
		return false;
	}
	data.assign(std::istreambuf_iterator<char>(in),
		std::istreambuf_iterator<char>());
	return !data.empty();
}
