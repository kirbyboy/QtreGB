#ifndef DOCBOYOAMBUS_H
#define DOCBOYOAMBUS_H

#include <cstdint>
#include <functional>

#include "docboy/bus/device.h"
#include "docboy/common/macros.h"

class OamBus
{
	DEBUGGABLE_CLASS()

	public:
		struct Word
		{
			uint8_t a = 0;
			uint8_t b = 0;
		};

		template <Device::Type Dev>
		class View
		{
			public:
				explicit View(OamBus& bus) : m_bus(&bus) {}

				void read_word_request(uint16_t address)
				{
					m_request = address;
				}

				Word flush_read_word_request() const
				{
					Word word;
					word.a = m_bus->read_byte(m_request);
					word.b = m_bus->read_byte(static_cast<uint16_t>(m_request + 1));
					return word;
				}

				void acquire()
				{
					m_acquired = true;
					if(m_bus->m_lock)
					{
						m_bus->m_lock(true);
					}
				}

				void release()
				{
					m_acquired = false;
					if(m_bus->m_lock)
					{
						m_bus->m_lock(false);
					}
				}

				bool is_acquired_by_this() const { return m_acquired; }

				template <Device::Type Other>
				bool is_acquired_by() const
				{
					return Other == Device::Dma && m_bus->m_dma_active && m_bus->m_dma_active();
				}

			private:
				OamBus* m_bus;
				uint16_t m_request = 0;
				bool m_acquired = false;
		};

		void bind(std::function<uint8_t(uint16_t)> read,
		          std::function<void(uint16_t, uint8_t)> write,
		          std::function<void(bool)> lock,
		          std::function<bool()> dma_active)
		{
			m_read = std::move(read);
			m_write = std::move(write);
			m_lock = std::move(lock);
			m_dma_active = std::move(dma_active);
		}

		uint8_t read_byte(uint16_t address) const
		{
			return m_read ? m_read(address) : 0xFF;
		}

		void write_byte(uint16_t address, uint8_t value)
		{
			if(m_write)
			{
				m_write(address, value);
			}
		}

	private:
		std::function<uint8_t(uint16_t)> m_read;
		std::function<void(uint16_t, uint8_t)> m_write;
		std::function<void(bool)> m_lock;
		std::function<bool()> m_dma_active;
};

#endif // DOCBOYOAMBUS_H
