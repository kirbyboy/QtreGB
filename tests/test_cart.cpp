#include "doctest.h"
#include "cart.h"
#include "error_reporter.h"

#include <vector>

using namespace qtreGB;

namespace
{
std::vector<u8> make_rom(size_t size, u8 type, u8 rom_size)
{
	std::vector<u8> rom(size, 0);
	if(size > 0x149)
	{
		rom[0x0147] = type;
		rom[0x0148] = rom_size;
		rom[0x0149] = 0;
	}
	return rom;
}
}

TEST_CASE("Cart: valid 32KB ROM-only loads and returns 0")
{
	Cart cart;
	ErrorReporter errors;
	cart.connect_error_reporter(&errors);

	CHECK(cart.load_cart(make_rom(0x8000, 0x00, 0x00)) == 0);
	CHECK_FALSE(errors.has_error());
}

TEST_CASE("Cart: ROM smaller than the header is rejected as too small")
{
	Cart cart;
	ErrorReporter errors;
	cart.connect_error_reporter(&errors);

	CHECK(cart.load_cart(make_rom(0x100, 0x00, 0x00)) == 1);
	CHECK(errors.code() == ErrorCode::CART_ROM_TOO_SMALL);
}

TEST_CASE("Cart: invalid ROM-size header value is rejected")
{
	Cart cart;
	ErrorReporter errors;
	cart.connect_error_reporter(&errors);

	CHECK(cart.load_cart(make_rom(0x8000, 0x00, 0x09)) == 2);
	CHECK(errors.code() == ErrorCode::CART_ROM_SIZE_INVALID);
}

TEST_CASE("Cart: file size not matching the header is rejected")
{
	Cart cart;
	ErrorReporter errors;
	cart.connect_error_reporter(&errors);

	CHECK(cart.load_cart(make_rom(0x8000, 0x00, 0x01)) == 3);
	CHECK(errors.code() == ErrorCode::CART_ROM_SIZE_MISMATCH);
}

TEST_CASE("Cart: unsupported cartridge type is rejected without crashing")
{
	Cart cart;
	ErrorReporter errors;
	cart.connect_error_reporter(&errors);

	CHECK(cart.load_cart(make_rom(0x8000, 0x99, 0x00)) == 4);
	CHECK(errors.code() == ErrorCode::CART_TYPE_UNSUPPORTED);
}

TEST_CASE("Cart: read with no cartridge loaded returns 0xFF without crashing")
{
	Cart cart;

	CHECK(cart.read(0x0000) == 0xFF);
	CHECK(cart.read(0x4000) == 0xFF);
}

TEST_CASE("Cart: write with no cartridge loaded is a no-op without crashing")
{
	Cart cart;

	CHECK_NOTHROW(cart.write(0x0000, 0x00));
}
