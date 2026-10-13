#ifndef QTREGB_INTERRUPT_CONTROLLER_H
#define QTREGB_INTERRUPT_CONTROLLER_H

#include <functional>
#include "types.h"

namespace qtreGB
{

class InterruptController
{
	public:
		void request_interrupt(InterruptType IType);

		void set_IF_read_callback(std::function<u8()> callback);
		void set_IF_write_callback(std::function<void(u8)> callback);
		void set_IE_read_callback(std::function<u8()> callback);
		void set_IE_write_callback(std::function<void(u8)> callback);

	private:
		std::function<u8()> IF_read;
		std::function<void(u8)> IF_write;
		std::function<u8()> IE_read;
		std::function<void(u8)> IE_write;
};


} // namespace qtreGB
#endif
