#include "interrupt_controller.h"

using namespace qtreGB;

void InterruptController::request_interrupt(InterruptType IType)
{
	u8 value = IF_read();
	value |= static_cast<u8>(IType);
	IF_write(value);
}

void InterruptController::set_IF_read_callback(std::function<u8()> callback)
{
	IF_read = callback;
}

void InterruptController::set_IF_write_callback(std::function<void(u8)> callback)
{
	IF_write = callback;
}

void InterruptController::set_IE_read_callback(std::function<u8()> callback)
{
	IE_read = callback;
}

void InterruptController::set_IE_write_callback(std::function<void(u8)> callback)
{
	IE_write = callback;
}
