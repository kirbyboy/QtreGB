#include "doctest.h"
#include "error_reporter.h"

using namespace qtreGB;

TEST_CASE("ErrorReporter: starts clean and latches the first error only")
{
	ErrorReporter reporter;

	CHECK_FALSE(reporter.has_error());
	CHECK(reporter.code() == ErrorCode::OK);

	reporter.report(ErrorCode::CPU_BUS_NOT_CONNECTED);
	CHECK(reporter.has_error());
	CHECK(reporter.code() == ErrorCode::CPU_BUS_NOT_CONNECTED);

	reporter.report(ErrorCode::CART_ROM_TOO_SMALL, 0x1234);
	CHECK(reporter.code() == ErrorCode::CPU_BUS_NOT_CONNECTED);
	CHECK(reporter.context() == 0);
}

TEST_CASE("ErrorReporter: clear resets the latched error")
{
	ErrorReporter reporter;
	reporter.report(ErrorCode::CPU_INTERRUPT_CONTROLLER_NULL);
	CHECK(reporter.has_error());

	reporter.clear();
	CHECK_FALSE(reporter.has_error());
	CHECK(reporter.code() == ErrorCode::OK);
	CHECK(reporter.context() == 0);
}

TEST_CASE("ErrorReporter: opcode helper encodes the opcode")
{
	CHECK(static_cast<u16>(cpu_unimplemented_opcode(0xED)) == 0x10ED);
}

TEST_CASE("ErrorReporter: message names the error and shows the context")
{
	ErrorReporter reporter;
	reporter.report(cpu_unimplemented_opcode(0xED), 0xED);

	CHECK(reporter.message() == "CPU: unimplemented opcode (0x00ED)");
}

TEST_CASE("ErrorReporter: report(OK) is ignored")
{
	ErrorReporter reporter;
	reporter.report(ErrorCode::OK);
	CHECK_FALSE(reporter.has_error());
}
