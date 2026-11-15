#include "doctest.h"
#include "Z80GB.h"
#include "error_reporter.h"

#include <vector>

using namespace qtreGB;

namespace
{
class CpuTestBus : public IBus
{
	public:
		CpuTestBus() : mem(0x10000, 0x00) {}

		u8 read(u16 address) override
		{
			reads++;
			return mem[address];
		}

		void write(u16 address, u8 value) override
		{
			writes++;
			mem[address] = value;
		}

		std::vector<u8> mem;
		int reads = 0;
		int writes = 0;
};

Z80GB::State read_state(const std::vector<u8>& snapshot)
{
	Z80GB::State state{};
	TaggedSerializer s(TaggedSerializer::Mode::Read, snapshot);
	state.serialize(s);
	return state;
}
}

TEST_CASE("Z80GB: reset sets the documented power-on state")
{
	InterruptController ic;
	Z80GB cpu(&ic);
	cpu.reset();

	CHECK(cpu.read_register16(RegType::RT_PC) == 0x0100);
	CHECK(cpu.read_register16(RegType::RT_SP) == 0xFFFE);
	CHECK(cpu.read_register(RegType::RT_A) == 0x01);
	CHECK(cpu.read_register(RegType::RT_F) == 0xB0);
	CHECK(cpu.read_register(RegType::RT_C) == 0x13);
	CHECK(cpu.read_register(RegType::RT_E) == 0xD8);
	CHECK(cpu.is_end_opcode());
}

TEST_CASE("Z80GB: reset targets the boot ROM entry point when requested")
{
	InterruptController ic;
	Z80GB cpu(&ic);

	cpu.reset(true);
	CHECK(cpu.get_pc() == 0x0000);
	CHECK(cpu.read_register16(RegType::RT_PC) == 0x0000);

	cpu.reset(false);
	CHECK(cpu.get_pc() == 0x0100);
}

TEST_CASE("Z80GB: reset is deterministic after execution")
{
	InterruptController ic;
	Z80GB cpu(&ic);
	CpuTestBus bus;
	cpu.connect_bus(&bus);

	cpu.reset();
	TaggedSerializer before(TaggedSerializer::Mode::Write);
	cpu.serialize(before);

	for(int i = 0; i < 40; i++) cpu.tick();

	cpu.reset();
	TaggedSerializer after(TaggedSerializer::Mode::Write);
	cpu.serialize(after);

	CHECK(before.data() == after.data());
}

TEST_CASE("Z80GB: NOP runs one M-cycle and advances PC by one")
{
	InterruptController ic;
	Z80GB cpu(&ic);
	CpuTestBus bus;
	cpu.connect_bus(&bus);
	cpu.reset();

	CHECK(cpu.is_end_opcode());
	cpu.tick();
	CHECK_FALSE(cpu.is_end_opcode());
	cpu.tick();
	cpu.tick();
	cpu.tick();

	CHECK(cpu.is_end_opcode());
	CHECK(cpu.read_register16(RegType::RT_PC) == 0x0101);
}

TEST_CASE("Z80GB: run without a bus reports CPU_BUS_NOT_CONNECTED and does not crash")
{
	InterruptController ic;
	Z80GB cpu(&ic);
	ErrorReporter errors;
	cpu.connect_error_reporter(&errors);

	cpu.tick();

	CHECK(errors.code() == ErrorCode::CPU_BUS_NOT_CONNECTED);
}

TEST_CASE("Z80GB: a null interrupt controller is reported on connect")
{
	Z80GB cpu(nullptr);
	ErrorReporter errors;
	cpu.connect_error_reporter(&errors);

	CHECK(errors.code() == ErrorCode::CPU_INTERRUPT_CONTROLLER_NULL);
}

TEST_CASE("Z80GB: an unimplemented opcode is reported and freezes the CPU")
{
	InterruptController ic;
	Z80GB cpu(&ic);
	CpuTestBus bus;
	bus.mem[0x0100] = 0xED;
	cpu.connect_bus(&bus);
	ErrorReporter errors;
	cpu.connect_error_reporter(&errors);
	cpu.reset();

	cpu.tick();

	CHECK(errors.code() == cpu_unimplemented_opcode(0xED));
	CHECK(errors.context() == 0xED);
	CHECK(cpu.read_register16(RegType::RT_PC) == 0x0100);

	const int reads = bus.reads;
	for(int i = 0; i < 8; i++) cpu.tick();

	CHECK(bus.reads == reads);
	CHECK(errors.code() == cpu_unimplemented_opcode(0xED));
}

TEST_CASE("Z80GB: savestate round-trip restores the observable registers")
{
	InterruptController ic1;
	Z80GB cpu1(&ic1);
	CpuTestBus bus1;
	cpu1.connect_bus(&bus1);
	cpu1.reset();
	cpu1.write_register(RegType::RT_A, 0x12);
	cpu1.write_register(RegType::RT_B, 0x34);
	cpu1.write_register16(RegType::RT_SP, 0xABCD);

	TaggedSerializer out(TaggedSerializer::Mode::Write);
	cpu1.serialize(out);

	InterruptController ic2;
	Z80GB cpu2(&ic2);
	CpuTestBus bus2;
	cpu2.connect_bus(&bus2);

	TaggedSerializer in(TaggedSerializer::Mode::Read, out.data());
	cpu2.serialize(in);

	CHECK(cpu2.read_register(RegType::RT_A) == 0x12);
	CHECK(cpu2.read_register(RegType::RT_B) == 0x34);
	CHECK(cpu2.read_register16(RegType::RT_SP) == 0xABCD);
	CHECK(cpu2.read_register16(RegType::RT_PC) == 0x0100);
}

TEST_CASE("Z80GB: State prefix reads the first ten snapshot tags")
{
	InterruptController ic;
	Z80GB cpu(&ic);
	CpuTestBus bus;
	cpu.connect_bus(&bus);
	cpu.reset();
	cpu.write_register(RegType::RT_A, 0x7E);
	cpu.write_register16(RegType::RT_SP, 0x1234);

	TaggedSerializer out(TaggedSerializer::Mode::Write);
	cpu.serialize(out);

	const Z80GB::State state = read_state(out.data());

	CHECK(state.a == 0x7E);
	CHECK(state.sp == 0x1234);
	CHECK(state.pc == 0x0100);
	CHECK(state.f == 0xB0);
}
