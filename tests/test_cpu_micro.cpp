#include "doctest.h"
#include "cpu_micro.h"

#include <array>
#include <vector>

using namespace qtreGB;

namespace
{

class MicroTestBus : public IBus
{
	public:
		struct Access
		{
			long tick;
			u8 phase;
			bool write;
			u16 addr;
			u8 value;
		};

		MicroTestBus() :
		mem {}
		{
		}

		u8 read(u16 address) override
		{
			log.push_back({tick, cpu != nullptr ? cpu->get_phase() : 0xFF, false, address, mem[address]});
			return mem[address];
		}

		void write(u16 address, u8 value) override
		{
			log.push_back({tick, cpu != nullptr ? cpu->get_phase() : 0xFF, true, address, value});
			mem[address] = value;
		}

		void end_tick()
		{
			tick++;
		}

		std::array<u8, 0x10000> mem;
		std::vector<Access> log;
		long tick = 0;
		CpuMicro* cpu = nullptr;
};

void run_m_cycles(CpuMicro& cpu, MicroTestBus& bus, int count)
{
	for(int i = 0; i < count; i++)
	{
		for(int t = 0; t < 4; t++)
		{
			cpu.tick();
			bus.end_tick();
		}
	}
}

const MicroTestBus::Access* find_access(const MicroTestBus& bus, bool write, u16 addr)
{
	const MicroTestBus::Access* found = nullptr;

	for(const auto& access : bus.log)
	{
		if(access.write == write && access.addr == addr)
		{
			found = &access;
		}
	}

	return found;
}

} // namespace

TEST_CASE("CpuMicro: fetch pipeline and first instruction family")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	// 0x0100: NOP
	// 0x0101: LD B,0x42
	// 0x0103: LD B,B
	// 0x0104: LD C,0x37
	bus.mem[0x0100] = 0x00;
	bus.mem[0x0101] = 0x06;
	bus.mem[0x0102] = 0x42;
	bus.mem[0x0103] = 0x40;
	bus.mem[0x0104] = 0x0E;
	bus.mem[0x0105] = 0x37;

	cpu.reset();

	auto run_m_cycle = [&]()
	{
		for(int i = 0; i < 4; i++)
		{
			cpu.tick();
			bus.end_tick();
		}
	};

	// M0: fetch the reset vector.
	run_m_cycle();
	REQUIRE(bus.log.size() == 1);
	CHECK(bus.log[0].addr == 0x0100);
	CHECK(bus.log[0].phase == 3);
	CHECK(bus.log[0].write == false);
	CHECK(cpu.get_pc() == 0x0101);
	CHECK(cpu.is_end_opcode());

	// M1: NOP, then fetch the next opcode.
	run_m_cycle();
	REQUIRE(bus.log.size() == 2);
	CHECK(bus.log[1].addr == 0x0101);
	CHECK(bus.log[1].phase == 3);
	CHECK(cpu.get_pc() == 0x0102);
	CHECK(cpu.get_b() == 0x00);

	// M2: LD B,d8 reads the immediate operand.
	run_m_cycle();
	REQUIRE(bus.log.size() == 3);
	CHECK(bus.log[2].addr == 0x0102);
	CHECK(bus.log[2].phase == 3);
	CHECK(cpu.get_pc() == 0x0103);
	CHECK(cpu.get_b() == 0x00);

	// M3: LD B,d8 writes B and fetches the next opcode.
	run_m_cycle();
	REQUIRE(bus.log.size() == 4);
	CHECK(bus.log[3].addr == 0x0103);
	CHECK(bus.log[3].phase == 3);
	CHECK(cpu.get_b() == 0x42);
	CHECK(cpu.get_pc() == 0x0104);

	// M4: LD B,B is a single M-cycle.
	run_m_cycle();
	REQUIRE(bus.log.size() == 5);
	CHECK(bus.log[4].addr == 0x0104);
	CHECK(bus.log[4].phase == 3);
	CHECK(cpu.get_b() == 0x42);
	CHECK(cpu.get_pc() == 0x0105);

	// M5: LD C,d8 reads the immediate operand.
	run_m_cycle();
	REQUIRE(bus.log.size() == 6);
	CHECK(bus.log[5].addr == 0x0105);
	CHECK(bus.log[5].phase == 3);
	CHECK(cpu.get_pc() == 0x0106);
	CHECK(cpu.get_c() == 0x13);

	// M6: LD C,d8 writes C and fetches the next opcode.
	run_m_cycle();
	REQUIRE(bus.log.size() == 7);
	CHECK(bus.log[6].addr == 0x0106);
	CHECK(bus.log[6].phase == 3);
	CHECK(cpu.get_c() == 0x37);
	CHECK(cpu.get_pc() == 0x0107);

	for(const auto& access : bus.log)
	{
		CHECK(access.write == false);
	}
}

TEST_CASE("CpuMicro: LD (HL),B writes at t1")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x26;	// LD H,0xC0
	bus.mem[0x0101] = 0xC0;
	bus.mem[0x0102] = 0x2E;	// LD L,0x00
	bus.mem[0x0103] = 0x00;
	bus.mem[0x0104] = 0x06;	// LD B,0x5A
	bus.mem[0x0105] = 0x5A;
	bus.mem[0x0106] = 0x70;	// LD (HL),B
	bus.mem[0x0107] = 0x00;

	cpu.reset();
	run_m_cycles(cpu, bus, 9);

	const auto* write = find_access(bus, true, 0xC000);
	REQUIRE(write != nullptr);
	CHECK(write->phase == 1);
	CHECK(write->value == 0x5A);
	CHECK(write->tick == 29);	// M7 t1: 7*4 + 1
	CHECK(cpu.get_b() == 0x5A);
}

TEST_CASE("CpuMicro: LD B,(HL) reads at t3")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x26;	// LD H,0xC0
	bus.mem[0x0101] = 0xC0;
	bus.mem[0x0102] = 0x2E;	// LD L,0x00
	bus.mem[0x0103] = 0x00;
	bus.mem[0x0104] = 0x46;	// LD B,(HL)
	bus.mem[0x0105] = 0x00;
	bus.mem[0xC000] = 0x99;

	cpu.reset();
	run_m_cycles(cpu, bus, 7);

	const auto* read = find_access(bus, false, 0xC000);
	REQUIRE(read != nullptr);
	CHECK(read->phase == 3);
	CHECK(read->value == 0x99);
	CHECK(read->tick == 23);	// M5 t3: 5*4 + 3
	CHECK(cpu.get_b() == 0x99);
}

TEST_CASE("CpuMicro: LD (HL),d8 phase order")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x26;	// LD H,0xC0
	bus.mem[0x0101] = 0xC0;
	bus.mem[0x0102] = 0x2E;	// LD L,0x00
	bus.mem[0x0103] = 0x00;
	bus.mem[0x0104] = 0x36;	// LD (HL),0x99
	bus.mem[0x0105] = 0x99;
	bus.mem[0x0106] = 0x00;

	cpu.reset();
	run_m_cycles(cpu, bus, 8);

	const auto* imm = find_access(bus, false, 0x0105);
	REQUIRE(imm != nullptr);
	CHECK(imm->phase == 3);
	CHECK(imm->value == 0x99);

	const auto* write = find_access(bus, true, 0xC000);
	REQUIRE(write != nullptr);
	CHECK(write->phase == 1);
	CHECK(write->value == 0x99);
}

TEST_CASE("CpuMicro: LDH (a8),A writes at t1")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x3E;	// LD A,0x5A
	bus.mem[0x0101] = 0x5A;
	bus.mem[0x0102] = 0xE0;	// LDH (0x47),A
	bus.mem[0x0103] = 0x47;
	bus.mem[0x0104] = 0x00;

	cpu.reset();
	run_m_cycles(cpu, bus, 6);

	const auto* write = find_access(bus, true, 0xFF47);
	REQUIRE(write != nullptr);
	CHECK(write->phase == 1);
	CHECK(write->value == 0x5A);
}

TEST_CASE("CpuMicro: LDH A,(a8) reads at t3")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0xF0;	// LDH A,(0x44)
	bus.mem[0x0101] = 0x44;
	bus.mem[0x0102] = 0x00;
	bus.mem[0xFF44] = 0x90;

	cpu.reset();
	run_m_cycles(cpu, bus, 4);

	const auto* read = find_access(bus, false, 0xFF44);
	REQUIRE(read != nullptr);
	CHECK(read->phase == 3);
	CHECK(read->value == 0x90);
	CHECK(cpu.get_a() == 0x90);
}

TEST_CASE("CpuMicro: LD (a16),A writes at t1")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x3E;	// LD A,0x5A
	bus.mem[0x0101] = 0x5A;
	bus.mem[0x0102] = 0xEA;	// LD (0xC000),A
	bus.mem[0x0103] = 0x00;
	bus.mem[0x0104] = 0xC0;
	bus.mem[0x0105] = 0x00;

	cpu.reset();
	run_m_cycles(cpu, bus, 7);

	const auto* write = find_access(bus, true, 0xC000);
	REQUIRE(write != nullptr);
	CHECK(write->phase == 1);
	CHECK(write->value == 0x5A);
}

TEST_CASE("CpuMicro: LD A,(a16) reads at t3")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0xFA;	// LD A,(0xC000)
	bus.mem[0x0101] = 0x00;
	bus.mem[0x0102] = 0xC0;
	bus.mem[0x0103] = 0x00;
	bus.mem[0xC000] = 0x77;

	cpu.reset();
	run_m_cycles(cpu, bus, 5);

	const auto* read = find_access(bus, false, 0xC000);
	REQUIRE(read != nullptr);
	CHECK(read->phase == 3);
	CHECK(read->value == 0x77);
	CHECK(cpu.get_a() == 0x77);
}

TEST_CASE("CpuMicro: PUSH BC writes high then low at t1")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x06;	// LD B,0x12
	bus.mem[0x0101] = 0x12;
	bus.mem[0x0102] = 0x0E;	// LD C,0x34
	bus.mem[0x0103] = 0x34;
	bus.mem[0x0104] = 0xC5;	// PUSH BC
	bus.mem[0x0105] = 0x00;

	cpu.reset();
	run_m_cycles(cpu, bus, 9);

	const auto* high = find_access(bus, true, 0xFFFD);
	REQUIRE(high != nullptr);
	CHECK(high->phase == 1);
	CHECK(high->value == 0x12);

	const auto* low = find_access(bus, true, 0xFFFC);
	REQUIRE(low != nullptr);
	CHECK(low->phase == 1);
	CHECK(low->value == 0x34);

	CHECK(cpu.get_sp() == 0xFFFC);
}

TEST_CASE("CpuMicro: POP BC reads low then high at t3")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0xC1;	// POP BC
	bus.mem[0x0101] = 0x00;
	bus.mem[0xFFFE] = 0x34;	// low -> C
	bus.mem[0xFFFF] = 0x12;	// high -> B

	cpu.reset();
	run_m_cycles(cpu, bus, 4);

	const auto* low = find_access(bus, false, 0xFFFE);
	REQUIRE(low != nullptr);
	CHECK(low->phase == 3);
	CHECK(low->value == 0x34);

	const auto* high = find_access(bus, false, 0xFFFF);
	REQUIRE(high != nullptr);
	CHECK(high->phase == 3);
	CHECK(high->value == 0x12);

	CHECK(cpu.get_b() == 0x12);
	CHECK(cpu.get_c() == 0x34);
	CHECK(cpu.get_sp() == 0x0000);
}

TEST_CASE("CpuMicro: LD (BC),A and LD A,(BC) phases")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x06;	// LD B,0xC1
	bus.mem[0x0101] = 0xC1;
	bus.mem[0x0102] = 0x0E;	// LD C,0x00
	bus.mem[0x0103] = 0x00;
	bus.mem[0x0104] = 0x3E;	// LD A,0x5A
	bus.mem[0x0105] = 0x5A;
	bus.mem[0x0106] = 0x02;	// LD (BC),A
	bus.mem[0x0107] = 0x00;

	cpu.reset();
	run_m_cycles(cpu, bus, 9);

	const auto* write = find_access(bus, true, 0xC100);
	REQUIRE(write != nullptr);
	CHECK(write->phase == 1);
	CHECK(write->value == 0x5A);
}

TEST_CASE("CpuMicro: JP a16")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0xC3;	// JP 0x0200
	bus.mem[0x0101] = 0x00;
	bus.mem[0x0102] = 0x02;
	bus.mem[0x0200] = 0x00;

	cpu.reset();
	run_m_cycles(cpu, bus, 5);

	CHECK(cpu.get_pc() == 0x0201);

	const auto* low = find_access(bus, false, 0x0101);
	REQUIRE(low != nullptr);
	CHECK(low->phase == 3);

	const auto* high = find_access(bus, false, 0x0102);
	REQUIRE(high != nullptr);
	CHECK(high->phase == 3);
}

TEST_CASE("CpuMicro: JP Z taken")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0xF1;	// POP AF -> Z=1
	bus.mem[0x0101] = 0xCA;	// JP Z,0x0200
	bus.mem[0x0102] = 0x00;
	bus.mem[0x0103] = 0x02;
	bus.mem[0x0200] = 0x00;
	bus.mem[0xFFFE] = 0x80;	// F: Z
	bus.mem[0xFFFF] = 0x00;	// A

	cpu.reset();
	run_m_cycles(cpu, bus, 8);

	CHECK(cpu.get_pc() == 0x0201);
}

TEST_CASE("CpuMicro: JP Z not taken ends after three M-cycles")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0xF1;	// POP AF -> Z=0
	bus.mem[0x0101] = 0xCA;	// JP Z,0x0200
	bus.mem[0x0102] = 0x00;
	bus.mem[0x0103] = 0x02;
	bus.mem[0x0104] = 0x00;
	bus.mem[0xFFFE] = 0x00;	// F: no flags
	bus.mem[0xFFFF] = 0x00;	// A

	cpu.reset();
	run_m_cycles(cpu, bus, 7);

	CHECK(cpu.get_pc() == 0x0105);
	CHECK(find_access(bus, false, 0x0200) == nullptr);
}

TEST_CASE("CpuMicro: JR e")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x18;	// JR +2
	bus.mem[0x0101] = 0x02;
	bus.mem[0x0104] = 0x00;

	cpu.reset();
	run_m_cycles(cpu, bus, 4);

	CHECK(cpu.get_pc() == 0x0105);

	const auto* offset = find_access(bus, false, 0x0101);
	REQUIRE(offset != nullptr);
	CHECK(offset->phase == 3);
}

TEST_CASE("CpuMicro: JR NZ taken")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0xF1;	// POP AF -> Z=0
	bus.mem[0x0101] = 0x20;	// JR NZ,+2
	bus.mem[0x0102] = 0x02;
	bus.mem[0x0105] = 0x00;
	bus.mem[0xFFFE] = 0x00;
	bus.mem[0xFFFF] = 0x00;

	cpu.reset();
	run_m_cycles(cpu, bus, 7);

	CHECK(cpu.get_pc() == 0x0106);
}

TEST_CASE("CpuMicro: JR NZ not taken ends after two M-cycles")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0xF1;	// POP AF -> Z=1
	bus.mem[0x0101] = 0x20;	// JR NZ,+2
	bus.mem[0x0102] = 0x02;
	bus.mem[0x0103] = 0x00;
	bus.mem[0xFFFE] = 0x80;
	bus.mem[0xFFFF] = 0x00;

	cpu.reset();
	run_m_cycles(cpu, bus, 6);

	CHECK(cpu.get_pc() == 0x0104);
}

TEST_CASE("CpuMicro: CALL a16 pushes the return address")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0xCD;	// CALL 0x0200
	bus.mem[0x0101] = 0x00;
	bus.mem[0x0102] = 0x02;
	bus.mem[0x0200] = 0x00;

	cpu.reset();
	run_m_cycles(cpu, bus, 7);

	CHECK(cpu.get_pc() == 0x0201);
	CHECK(cpu.get_sp() == 0xFFFC);

	const auto* high = find_access(bus, true, 0xFFFD);
	REQUIRE(high != nullptr);
	CHECK(high->phase == 1);
	CHECK(high->value == 0x01);

	const auto* low = find_access(bus, true, 0xFFFC);
	REQUIRE(low != nullptr);
	CHECK(low->phase == 1);
	CHECK(low->value == 0x03);
}

TEST_CASE("CpuMicro: CALL Z not taken ends after three M-cycles")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0xF1;	// POP AF -> Z=0
	bus.mem[0x0101] = 0xCC;	// CALL Z,0x0200
	bus.mem[0x0102] = 0x00;
	bus.mem[0x0103] = 0x02;
	bus.mem[0x0104] = 0x00;
	bus.mem[0xFFFE] = 0x00;
	bus.mem[0xFFFF] = 0x00;

	cpu.reset();
	run_m_cycles(cpu, bus, 7);

	CHECK(cpu.get_pc() == 0x0105);
	CHECK(cpu.get_sp() == 0x0000);
	CHECK(find_access(bus, true, 0xFFFD) == nullptr);
	CHECK(find_access(bus, true, 0xFFFC) == nullptr);
}

TEST_CASE("CpuMicro: RET pops the return address")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0xC9;	// RET
	bus.mem[0x0200] = 0x00;
	bus.mem[0xFFFE] = 0x00;	// low
	bus.mem[0xFFFF] = 0x02;	// high

	cpu.reset();
	run_m_cycles(cpu, bus, 5);

	CHECK(cpu.get_pc() == 0x0201);
	CHECK(cpu.get_sp() == 0x0000);

	const auto* low = find_access(bus, false, 0xFFFE);
	REQUIRE(low != nullptr);
	CHECK(low->phase == 3);

	const auto* high = find_access(bus, false, 0xFFFF);
	REQUIRE(high != nullptr);
	CHECK(high->phase == 3);
}

TEST_CASE("CpuMicro: RET Z taken")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0xF1;	// POP AF -> Z=1
	bus.mem[0x0101] = 0xC8;	// RET Z
	bus.mem[0x0200] = 0x00;
	bus.mem[0xFFFE] = 0x80;
	bus.mem[0xFFFF] = 0x00;
	bus.mem[0x0000] = 0x00;	// low return address
	bus.mem[0x0001] = 0x02;	// high return address

	cpu.reset();
	run_m_cycles(cpu, bus, 9);

	CHECK(cpu.get_pc() == 0x0201);
	CHECK(cpu.get_sp() == 0x0002);

	const auto* low = find_access(bus, false, 0x0000);
	REQUIRE(low != nullptr);
	CHECK(low->phase == 3);

	const auto* high = find_access(bus, false, 0x0001);
	REQUIRE(high != nullptr);
	CHECK(high->phase == 3);
}

TEST_CASE("CpuMicro: RET Z not taken ends after two M-cycles")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0xF1;	// POP AF -> Z=0
	bus.mem[0x0101] = 0xC8;	// RET Z
	bus.mem[0x0102] = 0x00;
	bus.mem[0xFFFE] = 0x00;
	bus.mem[0xFFFF] = 0x00;

	cpu.reset();
	run_m_cycles(cpu, bus, 6);

	CHECK(cpu.get_pc() == 0x0103);
	CHECK(cpu.get_sp() == 0x0000);
	CHECK(find_access(bus, false, 0x0000) == nullptr);
}

TEST_CASE("CpuMicro: RST 38h pushes the return address")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0xFF;	// RST 0x38
	bus.mem[0x0038] = 0x00;

	cpu.reset();
	run_m_cycles(cpu, bus, 5);

	CHECK(cpu.get_pc() == 0x0039);
	CHECK(cpu.get_sp() == 0xFFFC);

	const auto* high = find_access(bus, true, 0xFFFD);
	REQUIRE(high != nullptr);
	CHECK(high->phase == 1);
	CHECK(high->value == 0x01);

	const auto* low = find_access(bus, true, 0xFFFC);
	REQUIRE(low != nullptr);
	CHECK(low->phase == 1);
	CHECK(low->value == 0x01);
}

TEST_CASE("CpuMicro: RETI enables IME")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0xD9;	// RETI
	bus.mem[0x0200] = 0x00;
	bus.mem[0xFFFE] = 0x00;
	bus.mem[0xFFFF] = 0x02;

	cpu.reset();
	run_m_cycles(cpu, bus, 5);

	CHECK(cpu.get_pc() == 0x0201);
	CHECK(cpu.get_ime() == true);
}

TEST_CASE("CpuMicro: JP (HL) and LD SP")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x26;	// LD H,0x02
	bus.mem[0x0101] = 0x02;
	bus.mem[0x0102] = 0x2E;	// LD L,0x00
	bus.mem[0x0103] = 0x00;
	bus.mem[0x0104] = 0xE9;	// JP (HL)
	bus.mem[0x0200] = 0x00;

	cpu.reset();
	run_m_cycles(cpu, bus, 6);

	CHECK(cpu.get_pc() == 0x0201);
}

TEST_CASE("CpuMicro: LD SP,d16 and LD SP,HL")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x31;	// LD SP,0xC000
	bus.mem[0x0101] = 0x00;
	bus.mem[0x0102] = 0xC0;
	bus.mem[0x0103] = 0x26;	// LD H,0xC0
	bus.mem[0x0104] = 0xC0;
	bus.mem[0x0105] = 0x2E;	// LD L,0x00
	bus.mem[0x0106] = 0x00;
	bus.mem[0x0107] = 0xF9;	// LD SP,HL

	cpu.reset();
	run_m_cycles(cpu, bus, 4);

	CHECK(cpu.get_sp() == 0xC000);

	run_m_cycles(cpu, bus, 6);

	CHECK(cpu.get_sp() == 0xC000);
}

TEST_CASE("CpuMicro: CB register SWAP A")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x3E;	// LD A,0xF0
	bus.mem[0x0101] = 0xF0;
	bus.mem[0x0102] = 0xCB;	// SWAP A
	bus.mem[0x0103] = 0x37;
	bus.mem[0x0104] = 0x00;

	cpu.reset();
	run_m_cycles(cpu, bus, 5);

	CHECK(cpu.get_a() == 0x0F);
	CHECK((cpu.get_f() & 0x80) == 0);	// Z=0
	CHECK((cpu.get_f() & 0x40) == 0);	// N=0
	CHECK((cpu.get_f() & 0x20) == 0);	// H=0
	CHECK((cpu.get_f() & 0x10) == 0);	// C=0

	const auto* cb = find_access(bus, false, 0x0103);
	REQUIRE(cb != nullptr);
	CHECK(cb->phase == 3);
}

TEST_CASE("CpuMicro: CB (HL) SWAP reads t3 and writes t1")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x26;	// LD H,0xC0
	bus.mem[0x0101] = 0xC0;
	bus.mem[0x0102] = 0x2E;	// LD L,0x00
	bus.mem[0x0103] = 0x00;
	bus.mem[0x0104] = 0xCB;	// SWAP (HL)
	bus.mem[0x0105] = 0x36;
	bus.mem[0x0106] = 0x00;
	bus.mem[0xC000] = 0xF0;

	cpu.reset();
	run_m_cycles(cpu, bus, 9);

	const auto* read = find_access(bus, false, 0xC000);
	REQUIRE(read != nullptr);
	CHECK(read->phase == 3);
	CHECK(read->value == 0xF0);

	const auto* write = find_access(bus, true, 0xC000);
	REQUIRE(write != nullptr);
	CHECK(write->phase == 1);
	CHECK(write->value == 0x0F);

	CHECK(bus.mem[0xC000] == 0x0F);
	CHECK(cpu.get_pc() == 0x0107);
}

TEST_CASE("CpuMicro: CB BIT 7,A flags")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x3E;	// LD A,0x80
	bus.mem[0x0101] = 0x80;
	bus.mem[0x0102] = 0xCB;	// BIT 7,A
	bus.mem[0x0103] = 0x7F;
	bus.mem[0x0104] = 0x00;

	cpu.reset();
	run_m_cycles(cpu, bus, 5);

	CHECK(cpu.get_a() == 0x80);
	CHECK((cpu.get_f() & 0x80) == 0);	// Z=0: bit was set
	CHECK((cpu.get_f() & 0x40) == 0);	// N=0
	CHECK((cpu.get_f() & 0x20) != 0);	// H=1
}

TEST_CASE("CpuMicro: CB BIT 0,(HL) reads t3 and does not write")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x26;	// LD H,0xC0
	bus.mem[0x0101] = 0xC0;
	bus.mem[0x0102] = 0x2E;	// LD L,0x00
	bus.mem[0x0103] = 0x00;
	bus.mem[0x0104] = 0xCB;	// BIT 0,(HL)
	bus.mem[0x0105] = 0x46;
	bus.mem[0x0106] = 0x00;
	bus.mem[0xC000] = 0x01;

	cpu.reset();
	run_m_cycles(cpu, bus, 8);

	const auto* read = find_access(bus, false, 0xC000);
	REQUIRE(read != nullptr);
	CHECK(read->phase == 3);
	CHECK(read->value == 0x01);

	CHECK(find_access(bus, true, 0xC000) == nullptr);
	CHECK((cpu.get_f() & 0x80) == 0);	// Z=0: bit 0 was set
	CHECK(cpu.get_pc() == 0x0107);
}

TEST_CASE("CpuMicro: CB SET 3,B leaves flags untouched")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x06;	// LD B,0x00
	bus.mem[0x0101] = 0x00;
	bus.mem[0x0102] = 0xCB;	// SET 3,B
	bus.mem[0x0103] = 0xD8;
	bus.mem[0x0104] = 0x00;

	cpu.reset();
	run_m_cycles(cpu, bus, 5);

	CHECK(cpu.get_b() == 0x08);
	CHECK(cpu.get_f() == 0xB0);	// initial flags, SET does not change them
}

TEST_CASE("CpuMicro: VBlank ISR timing and return address")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0xFB;	// EI
	bus.mem[0x0101] = 0x00;	// NOP
	bus.mem[0x0102] = 0x00;	// NOP (interrupted before execution)
	bus.mem[0x0040] = 0x00;	// VBlank handler

	cpu.reset();
	cpu.write_ie(0x01);
	cpu.request_interrupt(InterruptType::IT_VBLANK);

	run_m_cycles(cpu, bus, 8);

	CHECK(cpu.get_pc() == 0x0041);
	CHECK(cpu.get_sp() == 0xFFFC);
	CHECK(bus.mem[0xFFFD] == 0x01);	// high return address
	CHECK(bus.mem[0xFFFC] == 0x02);	// low return address (0x0102)
	CHECK((cpu.read_if() & 0x01) == 0);
	CHECK(cpu.get_ime() == false);

	const auto* high = find_access(bus, true, 0xFFFD);
	REQUIRE(high != nullptr);
	CHECK(high->phase == 1);

	const auto* low = find_access(bus, true, 0xFFFC);
	REQUIRE(low != nullptr);
	CHECK(low->phase == 1);

	const auto* vector = find_access(bus, false, 0x0040);
	REQUIRE(vector != nullptr);
	CHECK(vector->phase == 3);
}

TEST_CASE("CpuMicro: ISR priority VBlank over STAT")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0xFB;	// EI
	bus.mem[0x0101] = 0x00;	// NOP
	bus.mem[0x0102] = 0x00;	// NOP
	bus.mem[0x0040] = 0x00;	// VBlank handler

	cpu.reset();
	cpu.write_ie(0x03);	// VBlank + STAT
	cpu.request_interrupt(InterruptType::IT_VBLANK);
	cpu.request_interrupt(InterruptType::IT_LCD_STAT);

	run_m_cycles(cpu, bus, 8);

	CHECK(cpu.get_pc() == 0x0041);
	CHECK((cpu.read_if() & 0x03) == 0x02);	// STAT still pending
	CHECK(bus.mem[0xFFFD] == 0x01);
	CHECK(bus.mem[0xFFFC] == 0x02);
}

TEST_CASE("CpuMicro: ALU register/immediate flags")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x3E;	// LD A,0x0F
	bus.mem[0x0101] = 0x0F;
	bus.mem[0x0102] = 0x06;	// LD B,0x01
	bus.mem[0x0103] = 0x01;
	bus.mem[0x0104] = 0x80;	// ADD A,B
	bus.mem[0x0105] = 0xFE;	// CP 0x10
	bus.mem[0x0106] = 0x10;
	bus.mem[0x0107] = 0xAF;	// XOR A
	bus.mem[0x0108] = 0xA7;	// AND A

	cpu.reset();

	run_m_cycles(cpu, bus, 6);
	CHECK(cpu.get_a() == 0x10);
	CHECK((cpu.get_f() & 0x80) == 0);	// Z=0
	CHECK((cpu.get_f() & 0x40) == 0);	// N=0
	CHECK((cpu.get_f() & 0x20) != 0);	// H=1
	CHECK((cpu.get_f() & 0x10) == 0);	// C=0

	run_m_cycles(cpu, bus, 2);
	CHECK((cpu.get_f() & 0x80) != 0);	// Z=1
	CHECK((cpu.get_f() & 0x40) != 0);	// N=1
	CHECK((cpu.get_f() & 0x10) == 0);	// C=0

	run_m_cycles(cpu, bus, 1);
	CHECK(cpu.get_a() == 0x00);
	CHECK((cpu.get_f() & 0x80) != 0);	// Z=1
	CHECK((cpu.get_f() & 0x40) == 0);	// N=0
	CHECK((cpu.get_f() & 0x20) == 0);	// H=0

	run_m_cycles(cpu, bus, 1);
	CHECK(cpu.get_a() == 0x00);
	CHECK((cpu.get_f() & 0x80) != 0);	// Z=1
	CHECK((cpu.get_f() & 0x20) != 0);	// H=1
}

TEST_CASE("CpuMicro: LD rr,d16 and 16-bit arithmetic")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x21;	// LD HL,0xC000
	bus.mem[0x0101] = 0x00;
	bus.mem[0x0102] = 0xC0;
	bus.mem[0x0103] = 0x11;	// LD DE,0x1234
	bus.mem[0x0104] = 0x34;
	bus.mem[0x0105] = 0x12;
	bus.mem[0x0106] = 0x19;	// ADD HL,DE
	bus.mem[0x0107] = 0x03;	// INC BC
	bus.mem[0x0108] = 0x0B;	// DEC BC

	cpu.reset();

	run_m_cycles(cpu, bus, 7);
	CHECK(cpu.get_h() == 0xC0);
	CHECK(cpu.get_l() == 0x00);
	CHECK(cpu.get_d() == 0x12);
	CHECK(cpu.get_e() == 0x34);
	CHECK(cpu.get_pc() == 0x0107);

	const auto* hl_low = find_access(bus, false, 0x0101);
	REQUIRE(hl_low != nullptr);
	CHECK(hl_low->phase == 3);

	run_m_cycles(cpu, bus, 2);
	CHECK(cpu.get_h() == 0xD2);
	CHECK(cpu.get_l() == 0x34);
	CHECK((cpu.get_f() & 0x40) == 0);	// N=0
	CHECK((cpu.get_f() & 0x20) == 0);	// H=0
	CHECK((cpu.get_f() & 0x10) == 0);	// C=0

	run_m_cycles(cpu, bus, 2);
	CHECK(cpu.get_b() == 0x00);
	CHECK(cpu.get_c() == 0x14);	// 0x0013 + 1

	run_m_cycles(cpu, bus, 2);
	CHECK(cpu.get_c() == 0x13);	// back to the initial BC
}

TEST_CASE("CpuMicro: LD (HL+),A and LD A,(HL-)")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x21;	// LD HL,0xC000
	bus.mem[0x0101] = 0x00;
	bus.mem[0x0102] = 0xC0;
	bus.mem[0x0103] = 0x3E;	// LD A,0x5A
	bus.mem[0x0104] = 0x5A;
	bus.mem[0x0105] = 0x22;	// LD (HL+),A
	bus.mem[0x0106] = 0x3A;	// LD A,(HL-)
	bus.mem[0x0107] = 0x00;
	bus.mem[0xC001] = 0x99;

	cpu.reset();
	run_m_cycles(cpu, bus, 10);

	const auto* write = find_access(bus, true, 0xC000);
	REQUIRE(write != nullptr);
	CHECK(write->phase == 1);
	CHECK(write->value == 0x5A);

	const auto* read = find_access(bus, false, 0xC001);
	REQUIRE(read != nullptr);
	CHECK(read->phase == 3);
	CHECK(read->value == 0x99);

	CHECK(cpu.get_a() == 0x99);
	CHECK(cpu.get_h() == 0xC0);
	CHECK(cpu.get_l() == 0x00);
}

TEST_CASE("CpuMicro: LD (C),A and LD A,(C)")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x0E;	// LD C,0x47
	bus.mem[0x0101] = 0x47;
	bus.mem[0x0102] = 0x3E;	// LD A,0x5A
	bus.mem[0x0103] = 0x5A;
	bus.mem[0x0104] = 0xE2;	// LD (C),A
	bus.mem[0x0105] = 0xF2;	// LD A,(C)
	bus.mem[0x0106] = 0x00;

	cpu.reset();

	run_m_cycles(cpu, bus, 6);
	CHECK(bus.mem[0xFF47] == 0x5A);
	const auto* write = find_access(bus, true, 0xFF47);
	REQUIRE(write != nullptr);
	CHECK(write->phase == 1);

	bus.mem[0xFF47] = 0x99;

	run_m_cycles(cpu, bus, 3);
	CHECK(cpu.get_a() == 0x99);
	const auto* read = find_access(bus, false, 0xFF47);
	REQUIRE(read != nullptr);
	CHECK(read->phase == 3);
}

TEST_CASE("CpuMicro: DEC (HL) flags and phases")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x21;	// LD HL,0xC000
	bus.mem[0x0101] = 0x00;
	bus.mem[0x0102] = 0xC0;
	bus.mem[0x0103] = 0x35;	// DEC (HL)
	bus.mem[0x0104] = 0x00;
	bus.mem[0xC000] = 0x01;

	cpu.reset();
	run_m_cycles(cpu, bus, 7);

	const auto* read = find_access(bus, false, 0xC000);
	REQUIRE(read != nullptr);
	CHECK(read->phase == 3);
	CHECK(read->value == 0x01);

	const auto* write = find_access(bus, true, 0xC000);
	REQUIRE(write != nullptr);
	CHECK(write->phase == 1);
	CHECK(write->value == 0x00);

	CHECK(bus.mem[0xC000] == 0x00);
	CHECK((cpu.get_f() & 0x80) != 0);	// Z=1
	CHECK((cpu.get_f() & 0x40) != 0);	// N=1
	CHECK((cpu.get_f() & 0x20) == 0);	// H=0
}

TEST_CASE("CpuMicro: RLCA and DAA")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x3E;	// LD A,0x80
	bus.mem[0x0101] = 0x80;
	bus.mem[0x0102] = 0x07;	// RLCA
	bus.mem[0x0103] = 0x00;

	cpu.reset();
	run_m_cycles(cpu, bus, 4);

	CHECK(cpu.get_a() == 0x01);
	CHECK((cpu.get_f() & 0x80) == 0);	// Z=0
	CHECK((cpu.get_f() & 0x10) != 0);	// C=1

	MicroTestBus bus2;
	CpuMicro cpu2(&bus2);
	bus2.cpu = &cpu2;

	bus2.mem[0x0100] = 0xAF;	// XOR A: clear flags
	bus2.mem[0x0101] = 0x3E;	// LD A,0x0A
	bus2.mem[0x0102] = 0x0A;
	bus2.mem[0x0103] = 0x27;	// DAA
	bus2.mem[0x0104] = 0x00;

	cpu2.reset();
	run_m_cycles(cpu2, bus2, 5);

	CHECK(cpu2.get_a() == 0x10);
	CHECK((cpu2.get_f() & 0x80) == 0);	// Z=0
	CHECK((cpu2.get_f() & 0x20) == 0);	// H=0
	CHECK((cpu2.get_f() & 0x10) == 0);	// C=0
}

TEST_CASE("CpuMicro: HALT wakes without servicing when IME is off")
{
	MicroTestBus bus;
	CpuMicro cpu(&bus);
	bus.cpu = &cpu;

	bus.mem[0x0100] = 0x76;	// HALT
	bus.mem[0x0101] = 0x00;	// NOP
	bus.mem[0x0102] = 0x00;

	cpu.reset();
	cpu.write_ie(0x01);

	run_m_cycles(cpu, bus, 2);
	CHECK(cpu.is_halted());

	cpu.request_interrupt(InterruptType::IT_VBLANK);
	run_m_cycles(cpu, bus, 2);

	CHECK(cpu.is_halted() == false);
	CHECK(cpu.get_pc() == 0x0103);
	CHECK((cpu.read_if() & 0x01) != 0);	// not serviced
	CHECK(cpu.get_ime() == false);
}
