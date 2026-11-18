#include <fstream>

#include "ppu_test_loader.h"

using namespace qtreGB;

PPUTestLoader::PPUTestLoader(PPU *ppu, LCD *lcd) :
m_ppu(ppu),
m_lcd(lcd)
{}

bool PPUTestLoader::load_from_file(const std::string &path)
{

	std::ifstream ifs(path, std::ios::binary);
	if (!ifs) {
		std::cerr << "PPUTestLoader: cannot open file " << path << "\n";
		return false;
	}

	if(!ifs.read(reinterpret_cast<char*>(&header), sizeof(header)))
	{
		std::cerr << "PPUTestLoader: failed to read header\n";
		return false;
	}


	if (strncmp(header.magic, "PPUT", 4) != 0) {
		std::cerr << "PPUTestLoader: bad magic\n";
		return false;
	}

	// parse blocks until EOF
	while(ifs && ifs.peek() != EOF)
	{
		BlockHeader bh;
		if(!ifs.read(reinterpret_cast<char*>(&bh), sizeof(bh)))
		{
			break;
		}

		switch(bh.type)
		{
			case BLOCK_LCD_REGS:
			{
				if(bh.size < sizeof(LCDRegsBlock))
				{
					std::cerr << "Bad LCD_REGS block size\n";
					return false;
				}
				LCDRegsBlock regs;
				ifs.read(reinterpret_cast<char*>(&regs), sizeof(regs));
				// copy into lcd (store temporarily in vram/oam dumps? we'll apply later)
				// We'll reuse vram_dump/oam_dump only for memory dumps; for regs we apply immediately:
				if (m_lcd)
				{
					m_lcd->m_lcdc = regs.lcdc;
					m_lcd->m_lcds = regs.stat;
					m_lcd->m_scroll_y = regs.scy;
					m_lcd->m_scroll_x = regs.scx;
					m_lcd->m_ly = regs.ly;
					m_lcd->m_lyc = regs.lyc;
					m_lcd->m_bg_palette = regs.bgp;
					m_lcd->m_obj_palette_0 = regs.obp0;
					m_lcd->m_obj_palette_1 = regs.obp1;
					m_lcd->m_win_y = regs.wy;
					m_lcd->m_win_x = regs.wx;
				}
				// skip any extra bytes in block
				if((int)bh.size > (int)sizeof(regs))
				{
					ifs.seekg(bh.size - sizeof(regs), std::ios::cur);
				}
				break;
			}
			case BLOCK_VRAM_DUMP:
			{
				vram_dump.resize(bh.size);
				ifs.read(reinterpret_cast<char*>(vram_dump.data()), bh.size);
				break;
			}
			case BLOCK_OAM_DUMP:
			{
				oam_dump.resize(bh.size);
				ifs.read(reinterpret_cast<char*>(oam_dump.data()), bh.size);
				break;
			}
			case BLOCK_EVENTS:
			{
				// events are fixed size Event
				size_t n = bh.size / sizeof(Event);
				events.resize(n);
				ifs.read(reinterpret_cast<char*>(events.data()), n * sizeof(Event));
				// populate map
				for(Event &e : events)
				{
					events_map[e.dot].push_back(e);
				}
				break;
			}
			case BLOCK_EXPECT_MODE3:
			{
				size_t n = bh.size / sizeof(Mode3Check);
				mode3_checks.resize(n);
				ifs.read(reinterpret_cast<char*>(mode3_checks.data()), n * sizeof(Mode3Check));
				break;
			}
			default:
				// unknown block — skip
				ifs.seekg(bh.size, std::ios::cur);
				break;
		}
	}

	return true;
}

void PPUTestLoader::apply_initial_state()
{
	// Apply VRAM dump
	if(!vram_dump.empty() && m_ppu)
	{
		size_t max = std::min<size_t>(vram_dump.size(), 0x2000);
		for(size_t i = 0; i < max; ++i)
		{
			// address in real map starts at 0x8000
			m_ppu->ppu_vram_write(0x8000 + static_cast<uint16_t>(i), vram_dump[i]);
		}
	}

	// Apply OAM dump
	if(!oam_dump.empty() && m_ppu)
	{
		size_t max = std::min<size_t>(oam_dump.size(), 0xA0);
		for(size_t i = 0; i < max; ++i)
		{
			m_ppu->ppu_oam_write(0xFE00 + static_cast<uint16_t>(i), oam_dump[i]);
		}
	}
}

bool PPUTestLoader::run_simulation(bool check_mode3, bool verbose)
{
	if(!m_ppu || !m_lcd)
	{
		std::cerr << "PPUTestLoader: PPU or LCD not set\n";
		return false;
	}

	uint32_t total = header.total_dots;
	if(total == 0)
	{
		// Default to one frame if header total_dots == 0
		total = static_cast<uint32_t>(header.lines_per_frame) * header.ticks_per_line;
	}

	// Prepare mode3 expected map: line -> expected ticks
	std::map<uint16_t, uint16_t> mode3_expect_map;
	for(auto &mc : mode3_checks)
	{
		mode3_expect_map[mc.line] = mc.expected_ticks;
	}

	uint32_t global_dot = 0;
	uint32_t ticks_per_line = header.ticks_per_line;
	if(ticks_per_line == 0)
	{
		ticks_per_line = PPU::TICKS_PER_LINE;
	}
	uint32_t line = 0;
	uint32_t mode3_counter = 0;

	// Apply initial state prior to first dot
	apply_initial_state();

	for (global_dot = 0; global_dot < total; ++global_dot)
	{
		// Apply events for this dot (writes to bus)
		auto it = events_map.find(global_dot);
		if(it != events_map.end())
		{
			for(auto &ev : it->second)
			{
				// redirect writes into PPU memory if they are VRAM/OAM ranges, else ignore
				if(ev.address >= 0x8000 && ev.address <= 0x9FFF)
				{
					m_ppu->ppu_vram_write(ev.address, ev.value);
					if(verbose)
					{
						std::cout << "[dot " << global_dot << "] VRAM write " << std::hex << ev.address << " = " << (int)ev.value << std::dec << "\n";
					}
				}
				else if (ev.address >= 0xFE00 && ev.address <= 0xFE9F)
				{
					m_ppu->ppu_oam_write(ev.address, ev.value);
					if(verbose)
					{
						std::cout << "[dot " << global_dot << "] OAM write " << std::hex << ev.address << " = " << (int)ev.value << std::dec << "\n";
					}
				}
				else
				{
					// Non-VRAM/OAM writes could be applied to LCD registers (example)
					// Map some IO addresses to LCD regs if needed (user may extend)
					if (ev.address == 0xFF40) m_lcd->m_lcdc = ev.value;
					else if (ev.address == 0xFF41) m_lcd->m_lcds = ev.value;
					else if (ev.address == 0xFF42) m_lcd->m_scroll_y = ev.value;
					else if (ev.address == 0xFF43) m_lcd->m_scroll_x = ev.value;
					else if (ev.address == 0xFF44) m_lcd->m_ly = ev.value;
					else if (ev.address == 0xFF45) m_lcd->m_lyc = ev.value;
					// else ignore or extend as needed
				}
			}
		}

		// Tick the PPU. The LCD is a passive mirror now, so refresh it before
		// inspecting the STAT mode.
		m_ppu->ppu_tick();
		m_ppu->sync_lcd();

		// Count Mode3 ticks
		if(m_lcd->get_lcds_mode() == LCDMode::MODE_XFER)
		{
			mode3_counter++;
		}

		// End of line?
		if((global_dot + 1) % ticks_per_line == 0)
		{
			// Check Mode3 expectation for this line if any
			if(check_mode3)
			{
				auto it2 = mode3_expect_map.find(static_cast<uint16_t>(line));
				if(it2 != mode3_expect_map.end())
				{
					uint16_t expected = it2->second;
					if(mode3_counter != expected)
					{
						std::cerr << "Mode3 mismatch on line " << line << ": expected " << expected << " ticks, got " << mode3_counter << "\n";
						return false;
					}
					else if(verbose)
					{
						std::cout << "Line " << line << ": Mode3 ticks OK (" << mode3_counter << ")\n";
					}
				}
			}
			// next line
			++line;
			mode3_counter = 0;
		}
	}

	// If we reach here, all checks passed
	return true;
}

void PPUTestLoader::serialize(TaggedSerializer &s)
{
	header.serialize(s);

	// VRAM & OAM dumps
	s.process("vram_dump", vram_dump);
	s.process("oam_dump", oam_dump);

	// Events
	for (auto& e : events)
	{
		e.serialize(s);
	}

	// Mode3 checks
	for(auto& m : mode3_checks)
	{
		m.serialize(s);
	}
}
