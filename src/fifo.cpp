#include "fifo.h"

using namespace qtreGB;

using namespace std;

FIFOElement::FIFOElement()
{
}

FIFOElement::FIFOElement(u8 color_index, bool palette, bool priority) :
m_color_index(color_index),
m_palette(palette),
m_priority(priority)
{
}

u8 FIFOElement::get_color_index()
{
	return m_color_index;
}

bool FIFOElement::get_palette()
{
	return m_palette;
}

bool FIFOElement::get_priority()
{
	return m_priority;
}

FIFO::FIFO():
m_size(0),
m_head(0),
m_tail(0),
m_elements({FIFOElement(0,false,false)})
{
}

FIFOElement FIFO::pop()
{
	FIFOElement element;
	if(m_size == 0)
	{
		element = FIFOElement();
	}

	element = m_elements[m_head];

	m_head = (m_head + 1) & 0x7;
	m_size--;

	return element;
}

void FIFO::push(FIFOElement element)
{
	if(m_size >= m_elements.size())
	{
		return;
	}

	m_elements[m_tail] = element;
	m_tail = (m_tail + 1) & 0x7;
	m_size++;
}

void FIFO::clear()
{
	m_size = 0;
	m_head = 0;
	m_tail = 0;
}

u8 FIFO::size() const
{
	return m_size;
}

bool FIFO::empty() const
{
	return m_size == 0;
}

bool FIFO::full() const
{
	return m_size == m_elements.size();
}

FIFOElement FIFO::get(u8 index)
{
	return m_elements[(m_head + index) & 0x7];
}

void FIFO::set(u8 index, FIFOElement element)
{
	if(index < m_elements.size())
	{
		m_elements[(m_head + index) & 0x7] = element;
	}
}
