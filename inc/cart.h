#ifndef QTREGB_CART_H
#define QTREGB_CART_H

#include <vector>

#include "types.h"
#include "memory.h"
#include "internal_cart.h"
#include "error_reporter.h"

namespace qtreGB
{

class Cart
{
	public:
		Cart();
		~Cart();

		u8 load_cart(std::vector<u8> rom);
		void connect_error_reporter(ErrorReporter* reporter);

		u8 read(u16 address);
		void write(u16 address, u8 value);
	private:
		void report(ErrorCode code);

		InternalCart* m_internal;
		ErrorReporter* m_errors = nullptr;
		std::vector<Memory> m_ram;

};


} // namespace qtreGB
#endif
