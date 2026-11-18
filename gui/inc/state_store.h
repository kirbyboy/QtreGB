#ifndef QTREGB_STATE_STORE_H
#define QTREGB_STATE_STORE_H

#include <cstdint>
#include <string>
#include <vector>

#include "types.h"

// Raw core savestate file for one ROM: <rom>.sr1 next to the ROM.
//
// This class deliberately contains no metadata, thumbnails or compression.
// It is the GUI-side file wrapper for the core blob; a future GUI container
// can replace the implementation without touching QtreGB.
class StateStore
{
	public:
		void set_rom_path(const std::string& rom_path);
		void set_player_index(qtreGB::u8 player_index);
		qtreGB::u8 player_index() const { return m_player_index; }
		const std::string& rom_path() const { return m_rom_path; }
		std::string path() const;

		bool save(const std::vector<uint8_t>& data) const;
		bool load(std::vector<uint8_t>& data) const;

	private:
		std::string m_rom_path;
		qtreGB::u8 m_player_index = 1;
};

#endif
