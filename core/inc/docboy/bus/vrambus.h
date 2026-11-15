#ifndef DOCBOYVRAMBUS_H
#define DOCBOYVRAMBUS_H

#include <cstddef>
#include <cstdint>
#include <functional>

#include "docboy/bus/device.h"
#include "docboy/common/macros.h"

class VramBus
{
	DEBUGGABLE_CLASS()

	public:
		template <Device::Type Dev>
		class View
		{
			public:
				explicit View(VramBus& bus) : m_bus(&bus) {}

				template <uint8_t Bank>
				uint8_t read(uint16_t address) const
				{
					(void)Bank;
					return m_bus->m_read ? m_bus->m_read(address) : 0xFF;
				}

				uint8_t read(uint16_t address) const
				{
					return m_bus->m_read ? m_bus->m_read(address) : 0xFF;
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
				bool is_acquired_by() const { return false; }

			private:
				VramBus* m_bus;
				bool m_acquired = false;
		};

		void bind(std::function<uint8_t(uint16_t)> read, std::function<void(bool)> lock)
		{
			m_read = std::move(read);
			m_lock = std::move(lock);
		}

	private:
		std::function<uint8_t(uint16_t)> m_read;
		std::function<void(bool)> m_lock;
};

#endif // DOCBOYVRAMBUS_H
