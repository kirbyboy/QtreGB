#ifndef QTREGB_FIFO_H
#define QTREGB_FIFO_H

#include <array>

#include "types.h"

namespace qtreGB
{

class FIFOElement
{
	public:
		FIFOElement();
		FIFOElement(u8 color_index, bool palette, bool priority);

		u8 get_color_index();
		bool get_palette();
		bool get_priority();

	private:
		u8 m_color_index;	// The index of the color, combined with palette we get actual color
		bool m_palette;		// For OAM elements(sprites), one of each palette
		bool m_priority;	// If this element has priority over background or not, only for OAM elements
};

class FIFO
{
	public:
		FIFO();

		FIFOElement pop();
		void push(FIFOElement element);
		void clear();
		u8 size() const;
		bool empty() const;
		bool full() const;
		FIFOElement get(u8 index);
		void set(u8 index, FIFOElement element);

	private:
		u8 m_size;
		u8 m_head;
		u8 m_tail;

		std::array<FIFOElement, 8> m_elements;	// Actual elements of this FIFO
};


} // namespace qtreGB
#endif
