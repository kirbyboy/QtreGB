#ifndef QTREGB_DMA_H
#define QTREGB_DMA_H

#include "types.h"
#include "ppu.h"
#include "ibus.h"

namespace qtreGB
{

class DMA
{
	public:
		void dma_start(u8 start);
		void dma_tick();
		bool dma_transferring();
		void connect(IBus* bus, PPU* ppu);
		u8 get_value();


	private:
		// m_active: a byte copy is actually in progress, so OAM/the source bus
		//           are owned by the DMA (the CPU sees them blocked).
		// m_pending: a transfer has been requested but has not started/finished
		//            yet (covers the 2-machine-cycle start delay).
		bool m_active = false;
		bool m_pending = false;
		u8 m_byte = 0;
		u8 m_value = 0;
		u8 m_start_delay = 0;

		IBus *m_bus;
		PPU *m_ppu;

};


} // namespace qtreGB
#endif
