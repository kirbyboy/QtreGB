#ifndef DOCBOYINTERRUPTS_H
#define DOCBOYINTERRUPTS_H

#include <functional>

// Interrupt facade used by the DocBoy PPU. The callback is installed by the
// QtreGB wrapper and raises IT_VBLANK / IT_LCD_STAT.
class Interrupts
{
	public:
		enum class InterruptType
		{
			VBlank,
			Stat
		};

		using Callback = std::function<void(InterruptType)>;

		void set_callback(Callback cb) { m_callback = std::move(cb); }

		template <InterruptType Type>
		void raise_interrupt()
		{
			if(m_callback)
			{
				m_callback(Type);
			}
		}

	private:
		Callback m_callback;
};

#endif // DOCBOYINTERRUPTS_H
