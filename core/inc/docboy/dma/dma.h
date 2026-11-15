#ifndef DOCBOYDMA_H
#define DOCBOYDMA_H

#include <cstdint>

// Minimal DMA facade. QtreGB's DMA controller is driven by the bus; the PPU
// only needs to know whether a transfer is in progress for OAM arbitration.
class Dma
{
	public:
		bool is_active() const { return m_active; }
		void set_active(bool active) { m_active = active; }
		void start_transfer(uint8_t) {}

	private:
		bool m_active = false;
};

#endif // DOCBOYDMA_H
