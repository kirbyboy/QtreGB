#include "error_reporter.h"

#include <cstdio>

namespace qtreGB
{

ErrorCode cpu_unimplemented_opcode(u8 opcode)
{
	return static_cast<ErrorCode>(0x1000u | opcode);
}

void ErrorReporter::report(ErrorCode code, u16 context)
{
	if(code == ErrorCode::OK || has_error())
	{
		return;
	}

	m_code = code;
	m_context = context;
	printf("Error: %s\n", message().c_str());
}

void ErrorReporter::clear()
{
	m_code = ErrorCode::OK;
	m_context = 0;
}

bool ErrorReporter::has_error() const
{
	return m_code != ErrorCode::OK;
}

ErrorCode ErrorReporter::code() const
{
	return m_code;
}

u16 ErrorReporter::context() const
{
	return m_context;
}

std::string ErrorReporter::message() const
{
	std::string text = error_name(m_code);

	if(m_context != 0)
	{
		char buffer[16];
		snprintf(buffer, sizeof(buffer), " (0x%04X)", m_context);
		text += buffer;
	}

	return text;
}

const char* error_name(ErrorCode code)
{
	switch(code)
	{
		case ErrorCode::OK: return "OK";
		case ErrorCode::CPU_BUS_NOT_CONNECTED: return "CPU: bus not connected";
		case ErrorCode::CPU_INTERRUPT_CONTROLLER_NULL: return "CPU: interrupt controller is null";
		case ErrorCode::CART_ROM_TOO_SMALL: return "Cart: ROM too small";
		case ErrorCode::CART_ROM_SIZE_INVALID: return "Cart: invalid ROM size";
		case ErrorCode::CART_ROM_SIZE_MISMATCH: return "Cart: ROM size does not match";
		case ErrorCode::CART_TYPE_UNSUPPORTED: return "Cart: unsupported cartridge type";
		default: break;
	}

	u16 raw = static_cast<u16>(code);
	if(raw >= 0x1000 && raw <= 0x10FF)
	{
		return "CPU: unimplemented opcode";
	}

	return "Unknown error";
}

} // namespace qtreGB
