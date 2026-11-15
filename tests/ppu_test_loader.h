#ifndef QTREGB_PPU_TEST_H
#define QTREGB_PPU_TEST_H

#include <cstdint>
#include <vector>
#include <map>
#include <string>
#include <iostream>

#include "ppu.h"
#include "lcd.h"
#include "tagged_serializer.h"

using namespace qtreGB;

struct PPUTestHeader
{
	char magic[4];	// "PPUT"
	uint16_t version;
	uint32_t total_dots;
	uint16_t lines_per_frame;
	uint16_t ticks_per_line;

	void serialize(TaggedSerializer& s)
	{
		s.process("magic", magic);
		s.process("version", version);
		s.process("total_dots", total_dots);
		s.process("lines_per_frame", lines_per_frame);
		s.process("ticks_per_line", ticks_per_line);
	}
};

struct BlockHeader
{
	uint32_t type;
	uint32_t size;

	void serialize(TaggedSerializer& s)
	{
		s.process("type", type);
		s.process("size", size);
	}
};

struct LCDRegsBlock
{
	uint8_t lcdc;
	uint8_t stat;
	uint8_t scy;
	uint8_t scx;
	uint8_t ly;
	uint8_t lyc;
	uint8_t dma;
	uint8_t bgp;
	uint8_t obp0;
	uint8_t obp1;
	uint8_t wy;
	uint8_t wx;

	void serialize(TaggedSerializer& s)
	{
		s.process("lcdc", lcdc);
		s.process("stat", stat);
		s.process("scy", scy);
		s.process("scx", scx);
		s.process("ly", ly);
		s.process("lyc", lyc);
		s.process("dma", dma);
		s.process("bgp", bgp);
		s.process("obp0", obp0);
		s.process("obp1", obp1);
		s.process("wy", wy);
		s.process("wx", wx);
	}
};

struct Event
{
	uint32_t dot;
	uint16_t address;
	uint8_t value;

	void serialize(TaggedSerializer& s)
	{
		s.process("dot", dot);
		s.process("address", address);
		s.process("value", value);
	}
};

struct Mode3Check
{
	uint16_t line;
	uint16_t expected_ticks;

	void serialize(TaggedSerializer& s)
	{
		s.process("line", line);
		s.process("expected_ticks", expected_ticks);
	}
};

// ---------------------- Block types ----------------------
static constexpr uint32_t BLOCK_LCD_REGS = 1;
static constexpr uint32_t BLOCK_VRAM_DUMP = 2;
static constexpr uint32_t BLOCK_OAM_DUMP = 3;
static constexpr uint32_t BLOCK_EVENTS = 4;
static constexpr uint32_t BLOCK_EXPECT_MODE3 = 5;

class PPUTestLoader
{
	public:
		PPUTestLoader(PPU* ppu, LCD* lcd);
	    bool load_from_file(const std::string &path);
	    void apply_initial_state();
	    bool run_simulation(bool check_mode3 = true, bool verbose = false);

		// getters
		uint32_t get_total_dots() const { return header.total_dots; }
		uint16_t get_ticks_per_line() const { return header.ticks_per_line; }
		uint16_t get_lines_per_frame() const { return header.lines_per_frame; }

		// ---------------------- Serialization ----------------------
		void serialize(TaggedSerializer& s);

	private:
		PPU* m_ppu = nullptr;
		LCD* m_lcd = nullptr;

		PPUTestHeader header;

		std::vector<uint8_t> vram_dump;	// should be 0x2000 if present
		std::vector<uint8_t> oam_dump;	// should be 0xA0 if present
		std::vector<Event> events;
		std::vector<Mode3Check> mode3_checks;

		// Fast lookup map of dot -> list of events
		std::map<uint32_t, std::vector<Event>> events_map;
};

#endif
