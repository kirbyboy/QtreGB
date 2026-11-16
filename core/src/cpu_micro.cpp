#include "cpu_micro.h"

#include <array>
#include <cstdio>
#include <cstdlib>

using namespace qtreGB;

CpuMicro::CpuMicro(IBus* bus) :
m_bus(bus)
{
}

void CpuMicro::reset()
{
	m_a = 0x01;
	m_f = 0xB0;
	m_b = 0x00;
	m_c = 0x13;
	m_d = 0x00;
	m_e = 0xD8;
	m_h = 0x01;
	m_l = 0x4D;
	m_pc = 0x0100;
	m_sp = 0xFFFE;

	m_phase = 0;
	m_fetching = false;
	m_fetching_cb = false;
	m_opcode = 0;
	m_cb_opcode = 0;
	m_io_data = 0;
	m_end_opcode = true;
	m_unimplemented = false;

	m_read_pending = false;
	m_write_pending = false;

	m_addr_lo = 0;
	m_addr_hi = 0;
	m_push_value = 0;
	m_pop_low = 0;

	m_ime = ImeState::Disabled;
	m_ie = 0;
	m_if = 0;
	m_is_halted = false;
	m_interrupt_state = InterruptState::None;
	m_interrupt_remaining = 0;

	m_instruction = nullptr;
	m_microop_index = 0;
}

void CpuMicro::State::serialize(TaggedSerializer& s)
{
	s.process("a", a);
	s.process("f", f);
	s.process("b", b);
	s.process("c", c);
	s.process("d", d);
	s.process("e", e);
	s.process("h", h);
	s.process("l", l);
	s.process("pc", pc);
	s.process("sp", sp);
}

void CpuMicro::serialize(TaggedSerializer& s)
{
	s.process("a", m_a);
	s.process("f", m_f);
	s.process("b", m_b);
	s.process("c", m_c);
	s.process("d", m_d);
	s.process("e", m_e);
	s.process("h", m_h);
	s.process("l", m_l);
	s.process("pc", m_pc);
	s.process("sp", m_sp);

	u8 ime = static_cast<u8>(m_ime);
	s.process("ime", ime);
	m_ime = static_cast<ImeState>(ime & 0x03);

	s.process("if", m_if);
	s.process("ie", m_ie);
	s.process("is_halted", m_is_halted);

	u8 interrupt_state = static_cast<u8>(m_interrupt_state);
	s.process("interrupt_state", interrupt_state);
	m_interrupt_state = static_cast<InterruptState>(interrupt_state & 0x03);

	s.process("interrupt_remaining", m_interrupt_remaining);
	s.process("phase", m_phase);
	s.process("fetching", m_fetching);
	s.process("fetching_cb", m_fetching_cb);
	s.process("opcode", m_opcode);
	s.process("cb_opcode", m_cb_opcode);
	s.process("io_data", m_io_data);
	s.process("end_opcode", m_end_opcode);
	s.process("unimplemented", m_unimplemented);
	s.process("read_pending", m_read_pending);
	s.process("read_addr", m_read_addr);
	s.process("write_pending", m_write_pending);
	s.process("write_addr", m_write_addr);
	s.process("write_value", m_write_value);
	s.process("addr_lo", m_addr_lo);
	s.process("addr_hi", m_addr_hi);
	s.process("push_value", m_push_value);
	s.process("pop_low", m_pop_low);
	s.process("microop_index", m_microop_index);

	// m_instruction points into a static table and is not serialized. The
	// runners read the state at instruction boundaries, where it is null.
}

u8 CpuMicro::read_register(RegType type) const
{
	switch(type)
	{
		case RegType::RT_A: return m_a;
		case RegType::RT_F: return m_f;
		case RegType::RT_B: return m_b;
		case RegType::RT_C: return m_c;
		case RegType::RT_D: return m_d;
		case RegType::RT_E: return m_e;
		case RegType::RT_H: return m_h;
		case RegType::RT_L: return m_l;
		default: return 0;
	}
}

u16 CpuMicro::read_register16(RegType type) const
{
	switch(type)
	{
		case RegType::RT_AF: return static_cast<u16>((static_cast<u16>(m_a) << 8) | m_f);
		case RegType::RT_BC: return static_cast<u16>((static_cast<u16>(m_b) << 8) | m_c);
		case RegType::RT_DE: return static_cast<u16>((static_cast<u16>(m_d) << 8) | m_e);
		case RegType::RT_HL: return hl();
		case RegType::RT_SP: return m_sp;
		case RegType::RT_PC: return m_pc;
		default: return 0;
	}
}

u8 CpuMicro::peek_next_opcode() const
{
	if(m_fetching)
	{
		// The opcode fetch is still in flight: m_io_data holds the result of
		// the previous bus read until T3. Read the pending fetch address so
		// the harness never mistakes stale data (e.g. LY=0x40) for an opcode.
		return (m_read_pending && m_bus != nullptr) ? m_bus->read(m_read_addr) : m_io_data;
	}
	if(m_bus == nullptr)
	{
		return 0xFF;
	}
	return m_bus->read(m_pc);
}

void CpuMicro::tick()
{
	switch(m_phase)
	{
		case 0: tick_t0(); break;
		case 1: tick_t1(); break;
		case 2: tick_t2(); break;
		case 3: tick_t3(); break;
	}

	m_phase = static_cast<u8>((m_phase + 1) & 0x03);
}

void CpuMicro::tick_m_cycle()
{
	for(int i = 0; i < 4; i++)
	{
		tick();
	}
}

void CpuMicro::tick_t0()
{
	check_interrupt(3);
	tick_microop();
}

void CpuMicro::tick_t1()
{
	check_interrupt(0);
	flush_write();
}

void CpuMicro::tick_t2()
{
	check_interrupt(1);
}

void CpuMicro::tick_t3()
{
	check_interrupt(2);
	flush_read();
}

void CpuMicro::tick_microop()
{
	if(m_interrupt_state == InterruptState::Pending)
	{
		if(m_interrupt_remaining > 0)
		{
			m_interrupt_remaining--;
		}

		if(m_interrupt_remaining == 0 && m_microop_index == 0 && m_end_opcode)
		{
			m_is_halted = false;

			if(m_ime == ImeState::Enabled && (m_ie & m_if & 0x1F) != 0)
			{
				serve_interrupt();
			}
			else
			{
				m_interrupt_state = InterruptState::None;
			}
		}
	}

	if(m_is_halted)
	{
		return;
	}

	if(m_fetching)
	{
		m_fetching = false;
		m_end_opcode = false;
		m_opcode = m_io_data;
		m_instruction = &table()[m_opcode];
		m_microop_index = 0;

	}
	else if(m_fetching_cb)
	{
		m_fetching_cb = false;
		m_end_opcode = false;
		m_cb_opcode = m_io_data;
		m_instruction = &cb_table()[m_cb_opcode];
		m_microop_index = 0;
	}

	if(m_instruction == nullptr)
	{
		// Start-of-run fetch: the reset vector is read before the first
		// instruction's first micro-op.
		fetch();
		finish_instruction();
		return;
	}

	if(m_instruction->count == 0)
	{
		m_unimplemented = true;
		m_instruction = nullptr;
		finish_instruction();
		return;
	}

	const Instruction* instruction = m_instruction;
	const MicroOp op = instruction->ops[m_microop_index];
	m_microop_index++;
	(this->*op.fn)();

	// The CB prefix micro-op only reads the second opcode byte; the CB
	// instruction continues in the next M-cycle, so this is not an
	// instruction boundary.
	if(m_fetching_cb)
	{
		return;
	}

	// A conditional micro-op may call fetch() early. In that case the
	// instruction ends here and the remaining micro-ops are skipped.
	if(m_instruction == nullptr)
	{
		finish_instruction();
		return;
	}

	if(m_microop_index >= instruction->count)
	{
		m_instruction = nullptr;
		finish_instruction();
	}
}

void CpuMicro::finish_instruction()
{
	m_end_opcode = true;

	// EI requests IME. The state advances one step at the end of each
	// instruction (Requested -> Pending -> Enabled), so the interrupt is
	// accepted only after the instruction following EI.
	if(m_ime == ImeState::Requested)
	{
		m_ime = ImeState::Pending;
	}
	else if(m_ime == ImeState::Pending)
	{
		m_ime = ImeState::Enabled;
	}
}

void CpuMicro::fetch()
{
	m_fetching = true;
	m_instruction = nullptr;
	m_microop_index = 0;
	read_request(m_pc);
	m_pc++;
}

void CpuMicro::read_request(u16 addr)
{
	m_read_addr = addr;
	m_read_pending = true;
}

void CpuMicro::flush_read()
{
	if(m_read_pending && m_bus != nullptr)
	{
		m_io_data = m_bus->read(m_read_addr);
		m_read_pending = false;
	}
}

void CpuMicro::write_request(u16 addr, u8 value)
{
	m_write_addr = addr;
	m_write_value = value;
	m_write_pending = true;
}

void CpuMicro::flush_write()
{
	if(m_write_pending && m_bus != nullptr)
	{
		m_bus->write(m_write_addr, m_write_value);
		m_write_pending = false;
	}
}

u8 CpuMicro::read_reg(u8 reg) const
{
	switch(reg)
	{
		case 0: return m_b;
		case 1: return m_c;
		case 2: return m_d;
		case 3: return m_e;
		case 4: return m_h;
		case 5: return m_l;
		case 7: return m_a;
		default: return 0xFF;	// (HL) belongs to the memory family.
	}
}

void CpuMicro::write_reg(u8 reg, u8 value)
{
	switch(reg)
	{
		case 0: m_b = value; break;
		case 1: m_c = value; break;
		case 2: m_d = value; break;
		case 3: m_e = value; break;
		case 4: m_h = value; break;
		case 5: m_l = value; break;
		case 7: m_a = value; break;
		default: break;
	}
}

void CpuMicro::nop_fetch()
{
	fetch();
}

void CpuMicro::ld_r_r_fetch()
{
	const u8 dst = static_cast<u8>((m_opcode >> 3) & 0x07);
	const u8 src = static_cast<u8>(m_opcode & 0x07);

	write_reg(dst, read_reg(src));
	fetch();
}

void CpuMicro::ld_r_d8_read()
{
	read_request(m_pc);
	m_pc++;
}

void CpuMicro::ld_r_d8_fetch()
{
	const u8 dst = static_cast<u8>((m_opcode >> 3) & 0x07);

	write_reg(dst, m_io_data);
	fetch();
}

u16 CpuMicro::read_reg16(u8 rr) const
{
	switch(rr)
	{
		case 0: return static_cast<u16>((static_cast<u16>(m_b) << 8) | m_c);
		case 1: return static_cast<u16>((static_cast<u16>(m_d) << 8) | m_e);
		case 2: return hl();
		case 3: return static_cast<u16>((static_cast<u16>(m_a) << 8) | m_f);
		default: return 0;
	}
}

void CpuMicro::write_reg16(u8 rr, u16 value)
{
	switch(rr)
	{
		case 0:
			m_b = static_cast<u8>((value >> 8) & 0xFF);
			m_c = static_cast<u8>(value & 0xFF);
			break;
		case 1:
			m_d = static_cast<u8>((value >> 8) & 0xFF);
			m_e = static_cast<u8>(value & 0xFF);
			break;
		case 2:
			m_h = static_cast<u8>((value >> 8) & 0xFF);
			m_l = static_cast<u8>(value & 0xFF);
			break;
		case 3:
			m_a = static_cast<u8>((value >> 8) & 0xFF);
			m_f = static_cast<u8>(value & 0xF0);
			break;
		default:
			break;
	}
}

u16 CpuMicro::read_rr16_alu(u8 rr) const
{
	// For INC/DEC rr and ADD HL,rr the encoding is BC/DE/HL/SP.
	return rr == 3 ? m_sp : read_reg16(rr);
}

void CpuMicro::write_rr16_alu(u8 rr, u16 value)
{
	if(rr == 3)
	{
		m_sp = value;
	}
	else
	{
		write_reg16(rr, value);
	}
}

void CpuMicro::ld_addr_r_write()
{
	u16 addr;
	u8 src;

	if(m_opcode == 0x02)
	{
		addr = read_reg16(0);	// BC
		src = 7;	// A
	}
	else if(m_opcode == 0x12)
	{
		addr = read_reg16(1);	// DE
		src = 7;	// A
	}
	else
	{
		addr = read_reg16(2);	// HL
		src = static_cast<u8>(m_opcode & 0x07);
	}

	write_request(addr, read_reg(src));
}

void CpuMicro::ld_r_addr_read()
{
	u16 addr;

	if(m_opcode == 0x0A)
	{
		addr = read_reg16(0);	// BC
	}
	else if(m_opcode == 0x1A)
	{
		addr = read_reg16(1);	// DE
	}
	else
	{
		addr = read_reg16(2);	// HL
	}

	read_request(addr);
}

void CpuMicro::ld_r_addr_fetch()
{
	const u8 dst = (m_opcode == 0x0A || m_opcode == 0x1A)
		? 7
		: static_cast<u8>((m_opcode >> 3) & 0x07);

	write_reg(dst, m_io_data);
	fetch();
}

void CpuMicro::ld_addr_d8_read()
{
	read_request(m_pc);
	m_pc++;
}

void CpuMicro::ld_addr_d8_write()
{
	write_request(hl(), m_io_data);
}

void CpuMicro::ldh_an_read()
{
	read_request(m_pc);
	m_pc++;
}

void CpuMicro::ldh_an_write()
{
	write_request(static_cast<u16>(0xFF00 | m_io_data), m_a);
}

void CpuMicro::ldh_r_an_read()
{
	read_request(static_cast<u16>(0xFF00 | m_io_data));
}

void CpuMicro::ldh_r_an_fetch()
{
	m_a = m_io_data;
	fetch();
}

void CpuMicro::ld_ann_lo_read()
{
	read_request(m_pc);
	m_pc++;
}

void CpuMicro::ld_ann_hi_read()
{
	m_addr_lo = m_io_data;
	read_request(m_pc);
	m_pc++;
}

void CpuMicro::ld_ann_write()
{
	const u16 addr = static_cast<u16>((static_cast<u16>(m_io_data) << 8) | m_addr_lo);
	write_request(addr, m_a);
}

void CpuMicro::ld_ann_read()
{
	const u16 addr = static_cast<u16>((static_cast<u16>(m_io_data) << 8) | m_addr_lo);
	read_request(addr);
}

void CpuMicro::ld_ann_fetch()
{
	m_a = m_io_data;
	fetch();
}

void CpuMicro::push_begin()
{
	m_push_value = read_reg16(static_cast<u8>((m_opcode >> 4) & 0x03));
	m_sp--;
}

void CpuMicro::push_write_hi()
{
	write_request(m_sp, static_cast<u8>((m_push_value >> 8) & 0xFF));
	m_sp--;
}

void CpuMicro::push_write_lo()
{
	write_request(m_sp, static_cast<u8>(m_push_value & 0xFF));
}

void CpuMicro::pop_begin()
{
	read_request(m_sp);
	m_sp++;
}

void CpuMicro::pop_read()
{
	m_pop_low = m_io_data;
	read_request(m_sp);
	m_sp++;
}

void CpuMicro::pop_fetch()
{
	const u16 value = static_cast<u16>((static_cast<u16>(m_io_data) << 8) | m_pop_low);
	write_reg16(static_cast<u8>((m_opcode >> 4) & 0x03), value);
	fetch();
}

bool CpuMicro::check_condition() const
{
	const u8 condition = static_cast<u8>((m_opcode >> 3) & 0x03);

	switch(condition)
	{
		case 0: return (m_f & 0x80) == 0;	// NZ
		case 1: return (m_f & 0x80) != 0;	// Z
		case 2: return (m_f & 0x10) == 0;	// NC
		case 3: return (m_f & 0x10) != 0;	// C
		default: return false;
	}
}

void CpuMicro::jp_hl_fetch()
{
	m_pc = hl();
	fetch();
}

void CpuMicro::jp_read_lo()
{
	read_request(m_pc);
	m_pc++;
}

void CpuMicro::jp_read_hi()
{
	m_addr_lo = m_io_data;
	read_request(m_pc);
	m_pc++;
}

void CpuMicro::jp_set_pc()
{
	m_pc = static_cast<u16>((static_cast<u16>(m_io_data) << 8) | m_addr_lo);
}

void CpuMicro::jp_cc()
{
	if(check_condition())
	{
		m_pc = static_cast<u16>((static_cast<u16>(m_io_data) << 8) | m_addr_lo);
	}
	else
	{
		fetch();
	}
}

void CpuMicro::jr_read()
{
	read_request(m_pc);
	m_pc++;
}

void CpuMicro::jr_add()
{
	m_pc = static_cast<u16>(static_cast<int16_t>(m_pc) + static_cast<int8_t>(m_io_data));
}

void CpuMicro::jr_cc()
{
	if(check_condition())
	{
		m_pc = static_cast<u16>(static_cast<int16_t>(m_pc) + static_cast<int8_t>(m_io_data));
	}
	else
	{
		fetch();
	}
}

void CpuMicro::call_read_lo()
{
	read_request(m_pc);
	m_pc++;
}

void CpuMicro::call_read_hi()
{
	m_addr_lo = m_io_data;
	read_request(m_pc);
	m_pc++;
}

void CpuMicro::call_sp()
{
	m_addr_hi = m_io_data;
	m_sp--;
}

void CpuMicro::call_cc()
{
	if(!check_condition())
	{
		fetch();
	}
	else
	{
		m_addr_hi = m_io_data;
		m_sp--;
	}
}

void CpuMicro::call_write_hi()
{
	write_request(m_sp, static_cast<u8>((m_pc >> 8) & 0xFF));
	m_sp--;
}

void CpuMicro::call_write_lo()
{
	write_request(m_sp, static_cast<u8>(m_pc & 0xFF));
	m_pc = static_cast<u16>((static_cast<u16>(m_addr_hi) << 8) | m_addr_lo);
}

void CpuMicro::ret_read_lo()
{
	read_request(m_sp);
	m_sp++;
}

void CpuMicro::ret_read_hi()
{
	m_pop_low = m_io_data;
	read_request(m_sp);
	m_sp++;
}

void CpuMicro::ret_set_pc()
{
	m_pc = static_cast<u16>((static_cast<u16>(m_io_data) << 8) | m_pop_low);
}

void CpuMicro::reti_set_pc()
{
	m_pc = static_cast<u16>((static_cast<u16>(m_io_data) << 8) | m_pop_low);
	m_ime = ImeState::Enabled;
}

void CpuMicro::ret_cc_begin()
{
}

void CpuMicro::ret_cc_check()
{
	if(check_condition())
	{
		read_request(m_sp);
		m_sp++;
	}
	else
	{
		fetch();
	}
}

void CpuMicro::rst_sp()
{
	m_sp--;
}

void CpuMicro::rst_write_hi()
{
	write_request(m_sp, static_cast<u8>((m_pc >> 8) & 0xFF));
	m_sp--;
}

void CpuMicro::rst_write_lo()
{
	write_request(m_sp, static_cast<u8>(m_pc & 0xFF));
	m_pc = static_cast<u16>(m_opcode & 0x38);
}

void CpuMicro::ld_sp_d16_lo()
{
	read_request(m_pc);
	m_pc++;
}

void CpuMicro::ld_sp_d16_hi()
{
	m_addr_lo = m_io_data;
	read_request(m_pc);
	m_pc++;
}

void CpuMicro::ld_sp_d16_fetch()
{
	m_sp = static_cast<u16>((static_cast<u16>(m_io_data) << 8) | m_addr_lo);
	fetch();
}

void CpuMicro::ld_sp_hl()
{
	m_sp = hl();
}

void CpuMicro::set_z(bool value)
{
	m_f = static_cast<u8>((m_f & ~0x80) | (value ? 0x80 : 0));
}

void CpuMicro::set_n(bool value)
{
	m_f = static_cast<u8>((m_f & ~0x40) | (value ? 0x40 : 0));
}

void CpuMicro::set_h(bool value)
{
	m_f = static_cast<u8>((m_f & ~0x20) | (value ? 0x20 : 0));
}

void CpuMicro::set_c(bool value)
{
	m_f = static_cast<u8>((m_f & ~0x10) | (value ? 0x10 : 0));
}

void CpuMicro::fetch_cb()
{
	m_fetching_cb = true;
	read_request(m_pc);
	m_pc++;
}

void CpuMicro::cb_prefix()
{
	fetch_cb();
}

u8 CpuMicro::cb_rotate(u8 value, bool& carry)
{
	const u8 operation = static_cast<u8>((m_cb_opcode >> 3) & 0x07);
	const bool old_carry = (m_f & 0x10) != 0;

	switch(operation)
	{
		case 0:	// RLC
			carry = (value & 0x80) != 0;
			value = static_cast<u8>((value << 1) | (carry ? 1 : 0));
			break;
		case 1:	// RRC
			carry = (value & 0x01) != 0;
			value = static_cast<u8>((value >> 1) | (carry ? 0x80 : 0));
			break;
		case 2:	// RL
			carry = (value & 0x80) != 0;
			value = static_cast<u8>((value << 1) | (old_carry ? 1 : 0));
			break;
		case 3:	// RR
			carry = (value & 0x01) != 0;
			value = static_cast<u8>((value >> 1) | (old_carry ? 0x80 : 0));
			break;
		case 4:	// SLA
			carry = (value & 0x80) != 0;
			value = static_cast<u8>(value << 1);
			break;
		case 5:	// SRA
			carry = (value & 0x01) != 0;
			value = static_cast<u8>((value >> 1) | (value & 0x80));
			break;
		case 6:	// SWAP
			carry = false;
			value = static_cast<u8>((value >> 4) | (value << 4));
			break;
		case 7:	// SRL
			carry = (value & 0x01) != 0;
			value = static_cast<u8>(value >> 1);
			break;
		default:
			carry = false;
			break;
	}

	set_z(value == 0);
	set_n(false);
	set_h(false);
	set_c(carry);

	return value;
}

void CpuMicro::cb_rotate_r()
{
	bool carry = false;
	const u8 reg = static_cast<u8>(m_cb_opcode & 0x07);
	const u8 value = cb_rotate(read_reg(reg), carry);
	write_reg(reg, value);
	fetch();
}

void CpuMicro::cb_arr_read()
{
	read_request(hl());
}

void CpuMicro::cb_rotate_arr()
{
	bool carry = false;
	const u8 value = cb_rotate(m_io_data, carry);
	write_request(hl(), value);
}

void CpuMicro::cb_bit_r()
{
	const u8 reg = static_cast<u8>(m_cb_opcode & 0x07);
	const u8 bit = static_cast<u8>((m_cb_opcode >> 3) & 0x07);
	const bool set = ((read_reg(reg) >> bit) & 0x01) != 0;

	set_z(!set);
	set_n(false);
	set_h(true);
	fetch();
}

void CpuMicro::cb_bit_arr()
{
	const u8 bit = static_cast<u8>((m_cb_opcode >> 3) & 0x07);
	const bool set = ((m_io_data >> bit) & 0x01) != 0;

	set_z(!set);
	set_n(false);
	set_h(true);
	fetch();
}

void CpuMicro::cb_res_r()
{
	const u8 reg = static_cast<u8>(m_cb_opcode & 0x07);
	const u8 bit = static_cast<u8>((m_cb_opcode >> 3) & 0x07);
	const u8 value = static_cast<u8>(read_reg(reg) & ~(1u << bit));

	write_reg(reg, value);
	fetch();
}

void CpuMicro::cb_res_arr()
{
	const u8 bit = static_cast<u8>((m_cb_opcode >> 3) & 0x07);
	write_request(hl(), static_cast<u8>(m_io_data & ~(1u << bit)));
}

void CpuMicro::cb_set_r()
{
	const u8 reg = static_cast<u8>(m_cb_opcode & 0x07);
	const u8 bit = static_cast<u8>((m_cb_opcode >> 3) & 0x07);
	const u8 value = static_cast<u8>(read_reg(reg) | (1u << bit));

	write_reg(reg, value);
	fetch();
}

void CpuMicro::cb_set_arr()
{
	const u8 bit = static_cast<u8>((m_cb_opcode >> 3) & 0x07);
	write_request(hl(), static_cast<u8>(m_io_data | (1u << bit)));
}

u8 CpuMicro::alu_op(u8 operation, u8 value)
{
	const u8 a = m_a;
	const u8 carry = static_cast<u8>((m_f & 0x10) != 0 ? 1 : 0);
	u8 result = a;

	switch(operation)
	{
		case 0:	// ADD
		{
			const u16 sum = static_cast<u16>(a) + value;
			result = static_cast<u8>(sum & 0xFF);
			set_z(result == 0);
			set_n(false);
			set_h(((a & 0x0F) + (value & 0x0F)) > 0x0F);
			set_c(sum > 0xFF);
			m_a = result;
			break;
		}
		case 1:	// ADC
		{
			const u16 sum = static_cast<u16>(a) + value + carry;
			result = static_cast<u8>(sum & 0xFF);
			set_z(result == 0);
			set_n(false);
			set_h(((a & 0x0F) + (value & 0x0F) + carry) > 0x0F);
			set_c(sum > 0xFF);
			m_a = result;
			break;
		}
		case 2:	// SUB
		{
			const int diff = static_cast<int>(a) - value;
			result = static_cast<u8>(diff & 0xFF);
			set_z(result == 0);
			set_n(true);
			set_h((a & 0x0F) < (value & 0x0F));
			set_c(a < value);
			m_a = result;
			break;
		}
		case 3:	// SBC
		{
			const int diff = static_cast<int>(a) - value - carry;
			result = static_cast<u8>(diff & 0xFF);
			set_z(result == 0);
			set_n(true);
			set_h((static_cast<int>(a & 0x0F) - (value & 0x0F) - carry) < 0);
			set_c(diff < 0);
			m_a = result;
			break;
		}
		case 4:	// AND
			result = static_cast<u8>(a & value);
			set_z(result == 0);
			set_n(false);
			set_h(true);
			set_c(false);
			m_a = result;
			break;
		case 5:	// XOR
			result = static_cast<u8>(a ^ value);
			set_z(result == 0);
			set_n(false);
			set_h(false);
			set_c(false);
			m_a = result;
			break;
		case 6:	// OR
			result = static_cast<u8>(a | value);
			set_z(result == 0);
			set_n(false);
			set_h(false);
			set_c(false);
			m_a = result;
			break;
		case 7:	// CP
			result = static_cast<u8>(a - value);
			set_z(result == 0);
			set_n(true);
			set_h((a & 0x0F) < (value & 0x0F));
			set_c(a < value);
			break;
		default:
			break;
	}

	return result;
}

void CpuMicro::alu_r_fetch()
{
	const u8 operation = static_cast<u8>((m_opcode >> 3) & 0x07);
	const u8 reg = static_cast<u8>(m_opcode & 0x07);

	alu_op(operation, read_reg(reg));
	fetch();
}

void CpuMicro::alu_value_fetch()
{
	const u8 operation = static_cast<u8>((m_opcode >> 3) & 0x07);
	const u8 value = m_io_data;
	alu_op(operation, value);
	fetch();
}

void CpuMicro::hl_read()
{
	read_request(hl());
}

void CpuMicro::inc_r_fetch()
{
	const u8 reg = static_cast<u8>((m_opcode >> 3) & 0x07);
	const u8 value = read_reg(reg);
	const u8 result = static_cast<u8>(value + 1);

	write_reg(reg, result);
	set_z(result == 0);
	set_n(false);
	set_h((value & 0x0F) == 0x0F);
	fetch();
}

void CpuMicro::dec_r_fetch()
{
	const u8 reg = static_cast<u8>((m_opcode >> 3) & 0x07);
	const u8 value = read_reg(reg);
	const u8 result = static_cast<u8>(value - 1);

	write_reg(reg, result);
	set_z(result == 0);
	set_n(true);
	set_h((value & 0x0F) == 0x00);
	fetch();
}

void CpuMicro::inc_arr_write()
{
	const u8 value = m_io_data;
	const u8 result = static_cast<u8>(value + 1);

	write_request(hl(), result);
	set_z(result == 0);
	set_n(false);
	set_h((value & 0x0F) == 0x0F);
}

void CpuMicro::dec_arr_write()
{
	const u8 value = m_io_data;
	const u8 result = static_cast<u8>(value - 1);

	write_request(hl(), result);
	set_z(result == 0);
	set_n(true);
	set_h((value & 0x0F) == 0x00);
}

void CpuMicro::inc_rr()
{
	const u8 rr = static_cast<u8>((m_opcode >> 4) & 0x03);
	write_rr16_alu(rr, static_cast<u16>(read_rr16_alu(rr) + 1));
}

void CpuMicro::dec_rr()
{
	const u8 rr = static_cast<u8>((m_opcode >> 4) & 0x03);
	write_rr16_alu(rr, static_cast<u16>(read_rr16_alu(rr) - 1));
}

void CpuMicro::add_hl_rr()
{
	const u8 rr = static_cast<u8>((m_opcode >> 4) & 0x03);
	const u16 hl_value = read_reg16(2);
	const u16 rr_value = read_rr16_alu(rr);
	const u32 sum = static_cast<u32>(hl_value) + rr_value;

	write_reg16(2, static_cast<u16>(sum & 0xFFFF));
	set_n(false);
	set_h(((hl_value & 0x0FFF) + (rr_value & 0x0FFF)) > 0x0FFF);
	set_c(sum > 0xFFFF);
}

void CpuMicro::ld_rr_d16_fetch()
{
	const u8 rr = static_cast<u8>((m_opcode >> 4) & 0x03);
	write_rr16_alu(rr, static_cast<u16>((static_cast<u16>(m_io_data) << 8) | m_addr_lo));
	fetch();
}

void CpuMicro::ld_ann_sp_write_lo()
{
	const u16 addr = static_cast<u16>((static_cast<u16>(m_io_data) << 8) | m_addr_lo);
	write_request(addr, static_cast<u8>(m_sp & 0xFF));
}

void CpuMicro::ld_ann_sp_write_hi()
{
	const u16 addr = static_cast<u16>((static_cast<u16>(m_io_data) << 8) | m_addr_lo);
	write_request(static_cast<u16>(addr + 1), static_cast<u8>((m_sp >> 8) & 0xFF));
}

void CpuMicro::add_sp_e_apply()
{
	const u16 sp_value = m_sp;
	const u8 offset = m_io_data;

	m_sp = static_cast<u16>(sp_value + static_cast<int8_t>(offset));
	set_z(false);
	set_n(false);
	set_h(((sp_value & 0x0F) + (offset & 0x0F)) > 0x0F);
	set_c(((sp_value & 0xFF) + (offset & 0xFF)) > 0xFF);
}

void CpuMicro::add_sp_e_noop()
{
}

void CpuMicro::ld_hl_sp_e_apply()
{
	const u16 sp_value = m_sp;
	const u8 offset = m_io_data;

	write_reg16(2, static_cast<u16>(sp_value + static_cast<int8_t>(offset)));
	set_z(false);
	set_n(false);
	set_h(((sp_value & 0x0F) + (offset & 0x0F)) > 0x0F);
	set_c(((sp_value & 0xFF) + (offset & 0xFF)) > 0xFF);
}

void CpuMicro::ld_hl_inc_a_write()
{
	const u16 address = hl();
	write_request(address, m_a);
	write_reg16(2, static_cast<u16>(address + 1));
}

void CpuMicro::ld_hl_dec_a_write()
{
	const u16 address = hl();
	write_request(address, m_a);
	write_reg16(2, static_cast<u16>(address - 1));
}

void CpuMicro::ld_a_hl_inc_read()
{
	read_request(hl());
}

void CpuMicro::ld_a_hl_dec_read()
{
	read_request(hl());
}

void CpuMicro::ld_a_hl_inc_fetch()
{
	const u16 address = hl();
	m_a = m_io_data;
	write_reg16(2, static_cast<u16>(address + 1));
	fetch();
}

void CpuMicro::ld_a_hl_dec_fetch()
{
	const u16 address = hl();
	m_a = m_io_data;
	write_reg16(2, static_cast<u16>(address - 1));
	fetch();
}

void CpuMicro::ldh_c_a_write()
{
	write_request(static_cast<u16>(0xFF00 | m_c), m_a);
}

void CpuMicro::ldh_a_c_read()
{
	read_request(static_cast<u16>(0xFF00 | m_c));
}

void CpuMicro::ldh_a_c_fetch()
{
	m_a = m_io_data;
	fetch();
}

void CpuMicro::rlca_fetch()
{
	const bool carry = (m_a & 0x80) != 0;
	m_a = static_cast<u8>((m_a << 1) | (carry ? 1 : 0));
	set_z(false);
	set_n(false);
	set_h(false);
	set_c(carry);
	fetch();
}

void CpuMicro::rrca_fetch()
{
	const bool carry = (m_a & 0x01) != 0;
	m_a = static_cast<u8>((m_a >> 1) | (carry ? 0x80 : 0));
	set_z(false);
	set_n(false);
	set_h(false);
	set_c(carry);
	fetch();
}

void CpuMicro::rla_fetch()
{
	const bool old_carry = (m_f & 0x10) != 0;
	const bool carry = (m_a & 0x80) != 0;
	m_a = static_cast<u8>((m_a << 1) | (old_carry ? 1 : 0));
	set_z(false);
	set_n(false);
	set_h(false);
	set_c(carry);
	fetch();
}

void CpuMicro::rra_fetch()
{
	const bool old_carry = (m_f & 0x10) != 0;
	const bool carry = (m_a & 0x01) != 0;
	m_a = static_cast<u8>((m_a >> 1) | (old_carry ? 0x80 : 0));
	set_z(false);
	set_n(false);
	set_h(false);
	set_c(carry);
	fetch();
}

void CpuMicro::daa_fetch()
{
	u8 value = m_a;
	const bool n = (m_f & 0x40) != 0;
	const bool h = (m_f & 0x20) != 0;
	bool c = (m_f & 0x10) != 0;

	if(!n)
	{
		if(c || value > 0x99)
		{
			value = static_cast<u8>(value + 0x60);
			c = true;
		}

		if(h || (value & 0x0F) > 0x09)
		{
			value = static_cast<u8>(value + 0x06);
		}
	}
	else
	{
		if(c)
		{
			value = static_cast<u8>(value - 0x60);
		}

		if(h)
		{
			value = static_cast<u8>(value - 0x06);
		}
	}

	m_a = value;
	set_z(value == 0);
	set_h(false);
	set_c(c);
	fetch();
}

void CpuMicro::cpl_fetch()
{
	m_a = static_cast<u8>(~m_a);
	set_n(true);
	set_h(true);
	fetch();
}

void CpuMicro::scf_fetch()
{
	set_n(false);
	set_h(false);
	set_c(true);
	fetch();
}

void CpuMicro::ccf_fetch()
{
	const bool old_carry = (m_f & 0x10) != 0;
	set_n(false);
	set_h(false);
	set_c(!old_carry);
	fetch();
}

void CpuMicro::halt_m0()
{
	const u8 pending = static_cast<u8>(m_ie & m_if & 0x1F);
	m_is_halted = (pending == 0);

	m_fetching = true;
	m_instruction = nullptr;
	m_microop_index = 0;
	read_request(m_pc);

	if(m_is_halted)
	{
		m_pc++;
	}
}

void CpuMicro::check_interrupt(u8 phase)
{
	if(m_interrupt_state == InterruptState::None && (m_is_halted || m_ime == ImeState::Enabled))
	{
		const u8 pending = static_cast<u8>(m_ie & m_if & 0x1F);

		if(pending != 0)
		{
			m_interrupt_state = InterruptState::Pending;
			m_interrupt_remaining = interrupt_delay(pending, phase);
		}
	}
}

u8 CpuMicro::interrupt_delay(u8 pending, u8 phase) const
{
	// DMG halted mode. Normal (not halted) is always 1 M-cycle, except when
	// the request is detected at phase 3 (2 M-cycles).
	static constexpr u8 HALTED[5][4] = {
		{1, 2, 2, 2},	// VBlank
		{1, 2, 2, 2},	// STAT
		{1, 2, 2, 3},	// Timer
		{2, 2, 2, 3},	// Serial
		{1, 2, 2, 3}	// Joypad
	};

	if(m_is_halted)
	{
		u8 delay = 4;

		for(u8 bit = 0; bit < 5; bit++)
		{
			if((pending & (1u << bit)) != 0)
			{
				const u8 candidate = HALTED[bit][phase & 0x03];
				if(candidate < delay)
				{
					delay = candidate;
				}
			}
		}

		return delay;
	}

	return (phase & 0x03) == 3 ? 2 : 1;
}

void CpuMicro::serve_interrupt()
{
	m_interrupt_state = InterruptState::Serving;
	m_fetching = false;
	m_fetching_cb = false;
	m_instruction = isr_table();
	m_microop_index = 0;
	m_end_opcode = false;
}

void CpuMicro::ei_fetch()
{
	if(m_ime == ImeState::Disabled)
	{
		m_ime = ImeState::Requested;
	}

	fetch();
}

void CpuMicro::di_fetch()
{
	m_ime = ImeState::Disabled;
	fetch();
}

void CpuMicro::isr_m0()
{
	m_pc--;
}

void CpuMicro::isr_m1()
{
	m_sp--;
}

void CpuMicro::isr_m2()
{
	write_request(m_sp, static_cast<u8>((m_pc >> 8) & 0xFF));
	m_sp--;
}

void CpuMicro::isr_m3()
{
	write_request(m_sp, static_cast<u8>(m_pc & 0xFF));

	const u8 pending = static_cast<u8>(m_ie & m_if & 0x1F);

	if(pending == 0)
	{
		m_pc = 0x0000;
		return;
	}

	const u8 bit = static_cast<u8>(pending & static_cast<u8>(-static_cast<int>(pending)));
	m_if &= static_cast<u8>(~bit);

	switch(bit)
	{
		case 0x01: m_pc = 0x0040; break;	// VBlank
		case 0x02: m_pc = 0x0048; break;	// STAT
		case 0x04: m_pc = 0x0050; break;	// Timer
		case 0x08: m_pc = 0x0058; break;	// Serial
		case 0x10: m_pc = 0x0060; break;	// Joypad
		default: m_pc = 0x0000; break;
	}
}

void CpuMicro::isr_m4()
{
	m_ime = ImeState::Disabled;
	m_interrupt_state = InterruptState::None;
	fetch();
}

const CpuMicro::Instruction* CpuMicro::isr_table()
{
	static const Instruction instruction = []()
	{
		Instruction i {};
		i.ops[0].fn = &CpuMicro::isr_m0;
		i.ops[1].fn = &CpuMicro::isr_m1;
		i.ops[2].fn = &CpuMicro::isr_m2;
		i.ops[3].fn = &CpuMicro::isr_m3;
		i.ops[4].fn = &CpuMicro::isr_m4;
		i.count = 5;
		return i;
	}();

	return &instruction;
}

const CpuMicro::Instruction* CpuMicro::table()
{
	static const std::array<Instruction, 256> instructions = []()
	{
		std::array<Instruction, 256> t {};

		auto make = [](auto... fns)
		{
			Instruction i {};
			u8 n = 0;
			((i.ops[n++].fn = fns), ...);
			i.count = n;
			return i;
		};

		t[0x00] = make(&CpuMicro::nop_fetch);

		// LD r,r' (register-only). The (HL) variants are in the memory family.
		for(int op = 0x40; op <= 0x7F; op++)
		{
			const int dst = (op >> 3) & 0x07;
			const int src = op & 0x07;

			if(op == 0x76 || dst == 6 || src == 6)
			{
				continue;
			}

			t[op] = make(&CpuMicro::ld_r_r_fetch);
		}

		// LD r,d8.
		for(int op : {0x06, 0x0E, 0x16, 0x1E, 0x26, 0x2E, 0x3E})
		{
			t[op] = make(&CpuMicro::ld_r_d8_read, &CpuMicro::ld_r_d8_fetch);
		}

		// LD (BC/DE),A and LD A,(BC/DE).
		t[0x02] = make(&CpuMicro::ld_addr_r_write, &CpuMicro::fetch);
		t[0x12] = make(&CpuMicro::ld_addr_r_write, &CpuMicro::fetch);
		t[0x0A] = make(&CpuMicro::ld_r_addr_read, &CpuMicro::ld_r_addr_fetch);
		t[0x1A] = make(&CpuMicro::ld_r_addr_read, &CpuMicro::ld_r_addr_fetch);

		// LD (HL),d8.
		t[0x36] = make(&CpuMicro::ld_addr_d8_read, &CpuMicro::ld_addr_d8_write, &CpuMicro::fetch);

		// LD r,(HL).
		for(int op : {0x46, 0x4E, 0x56, 0x5E, 0x66, 0x6E, 0x7E})
		{
			t[op] = make(&CpuMicro::ld_r_addr_read, &CpuMicro::ld_r_addr_fetch);
		}

		// LD (HL),r.
		for(int op = 0x70; op <= 0x77; op++)
		{
			if(op == 0x76)
			{
				continue;
			}

			t[op] = make(&CpuMicro::ld_addr_r_write, &CpuMicro::fetch);
		}

		// LDH (a8),A and LDH A,(a8).
		t[0xE0] = make(&CpuMicro::ldh_an_read, &CpuMicro::ldh_an_write, &CpuMicro::fetch);
		t[0xF0] = make(&CpuMicro::ldh_an_read, &CpuMicro::ldh_r_an_read, &CpuMicro::ldh_r_an_fetch);

		// LD (a16),A and LD A,(a16).
		t[0xEA] = make(&CpuMicro::ld_ann_lo_read, &CpuMicro::ld_ann_hi_read, &CpuMicro::ld_ann_write, &CpuMicro::fetch);
		t[0xFA] = make(&CpuMicro::ld_ann_lo_read, &CpuMicro::ld_ann_hi_read, &CpuMicro::ld_ann_read, &CpuMicro::ld_ann_fetch);

		// PUSH/POP BC, DE, HL, AF.
		t[0xC1] = make(&CpuMicro::pop_begin, &CpuMicro::pop_read, &CpuMicro::pop_fetch);
		t[0xD1] = t[0xC1];
		t[0xE1] = t[0xC1];
		t[0xF1] = t[0xC1];

		t[0xC5] = make(&CpuMicro::push_begin, &CpuMicro::push_write_hi, &CpuMicro::push_write_lo, &CpuMicro::fetch);
		t[0xD5] = t[0xC5];
		t[0xE5] = t[0xC5];
		t[0xF5] = t[0xC5];

		// JP a16 / JP cc,a16 / JP (HL).
		t[0xC3] = make(&CpuMicro::jp_read_lo, &CpuMicro::jp_read_hi, &CpuMicro::jp_set_pc, &CpuMicro::fetch);
		t[0xC2] = make(&CpuMicro::jp_read_lo, &CpuMicro::jp_read_hi, &CpuMicro::jp_cc, &CpuMicro::fetch);
		t[0xCA] = t[0xC2];
		t[0xD2] = t[0xC2];
		t[0xDA] = t[0xC2];
		t[0xE9] = make(&CpuMicro::jp_hl_fetch);

		// JR e / JR cc,e.
		t[0x18] = make(&CpuMicro::jr_read, &CpuMicro::jr_add, &CpuMicro::fetch);
		t[0x20] = make(&CpuMicro::jr_read, &CpuMicro::jr_cc, &CpuMicro::fetch);
		t[0x28] = t[0x20];
		t[0x30] = t[0x20];
		t[0x38] = t[0x20];

		// CALL a16 / CALL cc,a16.
		t[0xCD] = make(&CpuMicro::call_read_lo, &CpuMicro::call_read_hi, &CpuMicro::call_sp,
			&CpuMicro::call_write_hi, &CpuMicro::call_write_lo, &CpuMicro::fetch);
		t[0xC4] = make(&CpuMicro::call_read_lo, &CpuMicro::call_read_hi, &CpuMicro::call_cc,
			&CpuMicro::call_write_hi, &CpuMicro::call_write_lo, &CpuMicro::fetch);
		t[0xCC] = t[0xC4];
		t[0xD4] = t[0xC4];
		t[0xDC] = t[0xC4];

		// RET / RET cc / RETI.
		t[0xC9] = make(&CpuMicro::ret_read_lo, &CpuMicro::ret_read_hi, &CpuMicro::ret_set_pc, &CpuMicro::fetch);
		t[0xC0] = make(&CpuMicro::ret_cc_begin, &CpuMicro::ret_cc_check, &CpuMicro::ret_read_hi,
			&CpuMicro::ret_set_pc, &CpuMicro::fetch);
		t[0xC8] = t[0xC0];
		t[0xD0] = t[0xC0];
		t[0xD8] = t[0xC0];
		t[0xD9] = make(&CpuMicro::ret_read_lo, &CpuMicro::ret_read_hi, &CpuMicro::reti_set_pc, &CpuMicro::fetch);

		// RST n.
		t[0xC7] = make(&CpuMicro::rst_sp, &CpuMicro::rst_write_hi, &CpuMicro::rst_write_lo, &CpuMicro::fetch);
		t[0xCF] = t[0xC7];
		t[0xD7] = t[0xC7];
		t[0xDF] = t[0xC7];
		t[0xE7] = t[0xC7];
		t[0xEF] = t[0xC7];
		t[0xF7] = t[0xC7];
		t[0xFF] = t[0xC7];

		// Stack setup helpers.
		t[0x31] = make(&CpuMicro::ld_sp_d16_lo, &CpuMicro::ld_sp_d16_hi, &CpuMicro::ld_sp_d16_fetch);
		t[0xF9] = make(&CpuMicro::ld_sp_hl, &CpuMicro::fetch);

		// Interrupt enable/disable.
		t[0xFB] = make(&CpuMicro::ei_fetch);
		t[0xF3] = make(&CpuMicro::di_fetch);

		// ALU A,r / ALU A,d8.
		for(int op = 0x80; op <= 0xBF; op++)
		{
			if((op & 0x07) == 6)
			{
				t[op] = make(&CpuMicro::hl_read, &CpuMicro::alu_value_fetch);
			}
			else
			{
				t[op] = make(&CpuMicro::alu_r_fetch);
			}
		}

		for(int op : {0xC6, 0xCE, 0xD6, 0xDE, 0xE6, 0xEE, 0xF6, 0xFE})
		{
			t[op] = make(&CpuMicro::ld_r_d8_read, &CpuMicro::alu_value_fetch);
		}

		// INC/DEC r and (HL).
		for(int op : {0x04, 0x0C, 0x14, 0x1C, 0x24, 0x2C, 0x3C})
		{
			t[op] = make(&CpuMicro::inc_r_fetch);
		}

		for(int op : {0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x3D})
		{
			t[op] = make(&CpuMicro::dec_r_fetch);
		}

		t[0x34] = make(&CpuMicro::hl_read, &CpuMicro::inc_arr_write, &CpuMicro::fetch);
		t[0x35] = make(&CpuMicro::hl_read, &CpuMicro::dec_arr_write, &CpuMicro::fetch);

		// 16-bit loads and arithmetic.
		t[0x01] = make(&CpuMicro::jp_read_lo, &CpuMicro::jp_read_hi, &CpuMicro::ld_rr_d16_fetch);
		t[0x11] = t[0x01];
		t[0x21] = t[0x01];

		for(int op : {0x03, 0x13, 0x23, 0x33})
		{
			t[op] = make(&CpuMicro::inc_rr, &CpuMicro::fetch);
		}

		for(int op : {0x0B, 0x1B, 0x2B, 0x3B})
		{
			t[op] = make(&CpuMicro::dec_rr, &CpuMicro::fetch);
		}

		for(int op : {0x09, 0x19, 0x29, 0x39})
		{
			t[op] = make(&CpuMicro::add_hl_rr, &CpuMicro::fetch);
		}

		t[0x08] = make(&CpuMicro::jp_read_lo, &CpuMicro::jp_read_hi,
			&CpuMicro::ld_ann_sp_write_lo, &CpuMicro::ld_ann_sp_write_hi, &CpuMicro::fetch);
		t[0xE8] = make(&CpuMicro::ld_r_d8_read, &CpuMicro::add_sp_e_apply,
			&CpuMicro::add_sp_e_noop, &CpuMicro::fetch);
		t[0xF8] = make(&CpuMicro::ld_r_d8_read, &CpuMicro::ld_hl_sp_e_apply, &CpuMicro::fetch);

		// LD (HL+/-),A and LD A,(HL+/-).
		t[0x22] = make(&CpuMicro::ld_hl_inc_a_write, &CpuMicro::fetch);
		t[0x32] = make(&CpuMicro::ld_hl_dec_a_write, &CpuMicro::fetch);
		t[0x2A] = make(&CpuMicro::ld_a_hl_inc_read, &CpuMicro::ld_a_hl_inc_fetch);
		t[0x3A] = make(&CpuMicro::ld_a_hl_dec_read, &CpuMicro::ld_a_hl_dec_fetch);

		// LD (C),A and LD A,(C).
		t[0xE2] = make(&CpuMicro::ldh_c_a_write, &CpuMicro::fetch);
		t[0xF2] = make(&CpuMicro::ldh_a_c_read, &CpuMicro::ldh_a_c_fetch);

		// A-register rotates, DAA, CPL, SCF, CCF.
		t[0x07] = make(&CpuMicro::rlca_fetch);
		t[0x0F] = make(&CpuMicro::rrca_fetch);
		t[0x17] = make(&CpuMicro::rla_fetch);
		t[0x1F] = make(&CpuMicro::rra_fetch);
		t[0x27] = make(&CpuMicro::daa_fetch);
		t[0x2F] = make(&CpuMicro::cpl_fetch);
		t[0x37] = make(&CpuMicro::scf_fetch);
		t[0x3F] = make(&CpuMicro::ccf_fetch);

		t[0x76] = make(&CpuMicro::halt_m0);

		// CB prefix: the second opcode byte is read in this M-cycle and the
		// CB table is decoded in the next one.
		t[0xCB] = make(&CpuMicro::cb_prefix);

		return t;
	}();

	return instructions.data();
}

const CpuMicro::Instruction* CpuMicro::cb_table()
{
	static const std::array<Instruction, 256> instructions = []()
	{
		std::array<Instruction, 256> t {};

		auto make = [](auto... fns)
		{
			Instruction i {};
			u8 n = 0;
			((i.ops[n++].fn = fns), ...);
			i.count = n;
			return i;
		};

		for(int sub = 0; sub < 256; sub++)
		{
			const int reg = sub & 0x07;

			if(sub < 0x40)
			{
				// RLC/RRC/RL/RR/SLA/SRA/SWAP/SRL.
				if(reg == 6)
				{
					t[sub] = make(&CpuMicro::cb_arr_read, &CpuMicro::cb_rotate_arr, &CpuMicro::fetch);
				}
				else
				{
					t[sub] = make(&CpuMicro::cb_rotate_r);
				}
			}
			else if(sub < 0x80)
			{
				// BIT n,r / BIT n,(HL).
				if(reg == 6)
				{
					t[sub] = make(&CpuMicro::cb_arr_read, &CpuMicro::cb_bit_arr);
				}
				else
				{
					t[sub] = make(&CpuMicro::cb_bit_r);
				}
			}
			else if(sub < 0xC0)
			{
				// RES n,r / RES n,(HL).
				if(reg == 6)
				{
					t[sub] = make(&CpuMicro::cb_arr_read, &CpuMicro::cb_res_arr, &CpuMicro::fetch);
				}
				else
				{
					t[sub] = make(&CpuMicro::cb_res_r);
				}
			}
			else
			{
				// SET n,r / SET n,(HL).
				if(reg == 6)
				{
					t[sub] = make(&CpuMicro::cb_arr_read, &CpuMicro::cb_set_arr, &CpuMicro::fetch);
				}
				else
				{
					t[sub] = make(&CpuMicro::cb_set_r);
				}
			}
		}

		return t;
	}();

	return instructions.data();
}
