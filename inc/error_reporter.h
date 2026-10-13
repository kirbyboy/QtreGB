#ifndef QTREGB_ERROR_REPORTER_H
#define QTREGB_ERROR_REPORTER_H

#include <string>

#include "types.h"

namespace qtreGB
{

// Centralised error codes. The high part of the u16 identifies the module and
// the low part the detail. Errors tied to a value encode that value in the low
// part: 0x10E0 = CPU, opcode 0xE0 not implemented.
// Reserved families: 0x1xxx CPU, 0x2xxx PPU, 0x3xxx TIMER, 0x4xxx CART,
// 0x5xxx BUS/RAM, 0x6xxx INPUT, 0x7xxx SERIAL.
enum class ErrorCode : u16
{
	OK = 0x0000,

	// 0x1xxx CPU
	CPU_UNIMPLEMENTED_OPCODE = 0x1000,	// 0x1000 | opcode
	CPU_BUS_NOT_CONNECTED = 0x1100,
	CPU_INTERRUPT_CONTROLLER_NULL = 0x1101,

	// 0x4xxx Cartucho / ROM
	CART_ROM_TOO_SMALL = 0x4000,
	CART_ROM_SIZE_INVALID = 0x4001,
	CART_ROM_SIZE_MISMATCH = 0x4002,
	CART_TYPE_UNSUPPORTED = 0x4003,
};

ErrorCode cpu_unimplemented_opcode(u8 opcode);

class ErrorReporter
{
	public:
		void report(ErrorCode code, u16 context = 0);
		void clear();

		bool has_error() const;
		ErrorCode code() const;
		u16 context() const;
		std::string message() const;

	private:
		ErrorCode m_code = ErrorCode::OK;
		u16 m_context = 0;
};

const char* error_name(ErrorCode code);

} // namespace qtreGB

#endif
