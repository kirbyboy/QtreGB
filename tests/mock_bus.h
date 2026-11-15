#ifndef QTREGB_MOCK_BUS_H
#define QTREGB_MOCK_BUS_H

#include "ibus.h"
#include "ppu.h"

using namespace qtreGB;

class MockBus : public IBus
{
	public:
		MockBus();

		u8 read(u16 address) override;
		void write(u16 address, u8 value) override;

		void connect(PPU *ppu);
		void set_dma_transferring(bool transferring);
	private:
		PPU *m_ppu;
		bool m_dma_transferring;
};

#endif
