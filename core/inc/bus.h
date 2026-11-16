#ifndef QTREGB_BUS_H
#define QTREGB_BUS_H

#include <array>
#include <vector>

#include "types.h"
#include "ibus.h"
#include "cart.h"
#include "timer.h"
#include "interrupt_controller.h"
#include "icpu_registers.h"
#include "dma.h"
#include "gamepad.h"
#include "memory.h"
#include "io.h"
#include "lcd.h"
#include "ppu.h"

namespace qtreGB
{

class QtreGB;
class APU;

class Bus : public IBus
{
	public:

		Bus(Cart *cart, Timer *timer, DMA *dma, Gamepad* gamepad, Memory* wram, Memory* hram, IO* io, LCD* lcd, PPU* ppu, APU* apu, ICPURegisters *cpu_registers);

		u8 read(u16 address) override;
		void write(u16 address, u8 value) override;

		void set_boot_rom(const std::vector<u8>& rom);
		void set_boot_rom_enabled(bool enabled) { m_boot_rom_enabled = enabled; }
		bool is_boot_rom_enabled() const { return m_boot_rom_enabled; }
		void disable_boot_rom() { m_boot_rom_enabled = false; }

	private:
		std::array<u8, 0x100> m_boot_rom{};
		bool m_boot_rom_enabled = false;
		Cart *m_cart;
		Timer *m_timer;
		DMA *m_dma;
		Gamepad *m_gamepad;
		Memory *m_wram;
		Memory *m_hram;
		IO *m_io;
		LCD *m_lcd;
		PPU *m_ppu;
		APU *m_apu;
		ICPURegisters *m_cpu_registers;
};


} // namespace qtreGB
#endif
