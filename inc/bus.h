#ifndef QTREGB_BUS_H
#define QTREGB_BUS_H

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

class Bus : public IBus
{
	public:

		Bus(Cart *cart, Timer *timer, DMA *dma, Gamepad* gamepad, Memory* wram, Memory* hram, IO* io, LCD* lcd, PPU* ppu, ICPURegisters *cpu_registers);

		u8 read(u16 address) override;
		void write(u16 address, u8 value) override;

	private:
		Cart *m_cart;
		Timer *m_timer;
		DMA *m_dma;
		Gamepad *m_gamepad;
		Memory *m_wram;
		Memory *m_hram;
		IO *m_io;
		LCD *m_lcd;
		PPU *m_ppu;
		ICPURegisters *m_cpu_registers;
};


} // namespace qtreGB
#endif
