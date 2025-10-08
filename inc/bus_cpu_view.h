#ifndef QTREGB_BUS_CPU_VIEW_H
#define QTREGB_BUS_CPU_VIEW_H

#include "ibus.h"
#include "bus.h"
#include "ppu.h"

class BusCpuView : public IBus
{
	public:
		BusCpuView(Bus* real_bus, PPU* ppu);

		u8 read(u16 address) override;
		void write(u16 address, u8 value) override;

	private:
		Bus* m_real_bus;
		PPU* m_ppu;
		u8 m_last_value = 0xFF;
};

#endif
