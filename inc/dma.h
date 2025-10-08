#ifndef QTREGB_DMA_H
#define QTREGB_DMA_H

#include "common.h"
#include "ppu.h"
#include "ibus.h"

class DMA
{
	public:
		void dma_start(u8 start);
		void dma_tick();
		bool dma_transferring();
		void connect(IBus* bus, PPU* ppu);
		u8 get_value();


	private:
		bool m_active;
		u8 m_byte;
		u8 m_value;
		u8 m_start_delay;

		IBus *m_bus;
		PPU *m_ppu;

};

#endif
