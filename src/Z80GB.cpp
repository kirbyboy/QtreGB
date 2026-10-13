#include "Z80GB.h"
#include "operations.h"
#include "error_reporter.h"

#include <cstdlib>
#include <cstdio>

using namespace qtreGB;

static RegType decode_reg(u8 reg)
{
	reg = reg & 0x07;
	switch(reg)
	{
		case 0: return RegType::RT_B;
		case 1: return RegType::RT_C;
		case 2: return RegType::RT_D;
		case 3: return RegType::RT_E;
		case 4: return RegType::RT_H;
		case 5: return RegType::RT_L;
		case 6: return RegType::RT_HL;
		case 7: return RegType::RT_A;
	}

	return RegType::RT_NONE;
}

Z80GB::Z80GB(InterruptController* interrupt_controller) : m_interrupt_controller(interrupt_controller)
{
	if(m_interrupt_controller != nullptr)
	{
		m_interrupt_controller->set_IE_read_callback([this]() { return this->read_IE(); });
		m_interrupt_controller->set_IE_write_callback([this](u8 value) { this->write_IE(value); });
		m_interrupt_controller->set_IF_read_callback([this]() { return this->read_IF(); });
		m_interrupt_controller->set_IF_write_callback([this](u8 value) { this->write_IF(value); });
	}
}

void Z80GB::tick()
{
	// EI defers the IME enable to the boundary after the instruction that
	// follows it: ei() in mooneye-gb prefetches the next opcode with IME still
	// 0 and only then sets ime = true. Applying the deferred enable *before*
	// execute() makes that following instruction's end-of-opcode interrupt
	// check observe IME=1, so the interrupt is dispatched exactly like the
	// reference. (RETI instead sets IME=1 directly in its handler, so it
	// dispatches at its own boundary.)
	if(m_next_m_cycle && m_end_opcode)
	{
		m_ime = true;
		m_next_m_cycle = false;
	}

	execute();

	if(m_enabling_ime && m_end_opcode)
	{
		m_next_m_cycle = true;
		m_enabling_ime = false;
	}
}

void Z80GB::reset()
{
	m_pc = 0x0100;
	m_sp = 0xFFFE;
	m_a = 0x01;
	m_f = 0xB0;
	m_b = 0x00;
	m_c = 0x13;
	m_d = 0x00;
	m_e = 0xD8;
	m_h = 0x01;
	m_l = 0x4D;

	m_ime = false;
	m_enabling_ime = false;
	m_next_m_cycle = false;

	m_opcode = 0;
	m_t_cycle = 1;
	m_m_cycle = 1;
	m_end_opcode = true;

	m_is_interruption_time = false;

	m_if = 0;
	m_ie = 0;

	m_is_halted = false;
	m_halt_bug = false;

	m_fetched_address = 0;
	m_fetched_data_lo = 0;
	m_fetched_data_hi = 0;
}


bool Z80GB::is_end_m_cycle() const
{
	return m_t_cycle % 4 == 0;
}

bool Z80GB::is_end_opcode() const
{
	return m_end_opcode;
}

bool Z80GB::is_interruption_time() const
{
	return m_is_interruption_time;
}

void Z80GB::State::serialize(TaggedSerializer& s)
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

// Snapshot / backdoor: the first ten tags are, in order, the State ones
// (the tests read through that prefix). The rest is internal state; in
// Read mode any of them can be injected.
void Z80GB::serialize(TaggedSerializer& s)
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

	s.process("ime", m_ime);
	s.process("if", m_if);
	s.process("ie", m_ie);
	s.process("is_halted", m_is_halted);
	s.process("halt_bug", m_halt_bug);
	s.process("enabling_ime", m_enabling_ime);
	s.process("next_m_cycle", m_next_m_cycle);
	s.process("is_interruption_time", m_is_interruption_time);
	s.process("opcode", m_opcode);
	s.process("t_cycle", m_t_cycle);
	s.process("m_cycle", m_m_cycle);
	s.process("end_opcode", m_end_opcode);
	s.process("fetched_address", m_fetched_address);
	s.process("fetched_data_lo", m_fetched_data_lo);
	s.process("fetched_data_hi", m_fetched_data_hi);
}

void Z80GB::execute()
{
	if(m_errors != nullptr && m_errors->has_error())
	{
		return;
	}

	if(m_bus == nullptr)
	{
		if(m_errors != nullptr)
		{
			m_errors->report(ErrorCode::CPU_BUS_NOT_CONNECTED);
		}
		return;
	}

	// Free halt-wake check (mooneye Step::Halt semantics): while halted, poll the
	// interrupt line every tick; when set, wake immediately and start the dispatch
	// in the SAME tick (the halt M-cycle that would have been spent is skipped).
	if(m_is_halted && (m_ie & m_if) != 0)
	{
		m_is_halted = false;
		m_end_opcode = true;
		m_t_cycle = 1;
		if(m_ime)
		{
			m_is_interruption_time = true;
			m_ime = false;
			m_m_cycle = 1;
		}
	}

	if(m_is_interruption_time)
	{
		// Interrupt dispatch: 4 M-cycles; the handler opcode fetch is the 5th
		// M-cycle (done by the m_end_opcode path once m_is_interruption_time is
		// cleared). The pending IF&IE is sampled before the low-byte push lands,
		// matching mooneye-gb's write_cycle_intr: a write to IE during the
		// low-byte push is too late to cancel the dispatch.
		if(m_t_cycle == 4)
		{
			switch(m_m_cycle)
			{
				case 1:
					break;
				case 2:
					break;
				case 3:
					m_sp--;
					m_bus->write(m_sp, (m_pc >> 8) & 0xFF);
					break;
				case 4:
				{
					u8 pending = m_if & m_ie;

					m_sp--;
					m_bus->write(m_sp, m_pc & 0xFF);

					if(pending == 0)
					{
						// The push wrote IE and cleared the pending interrupt:
						// dispatch is cancelled, PC goes to 0x0000 and IF is
						// left untouched.
						m_pc = 0x0000;
					}
					else if(pending & static_cast<u8>(InterruptType::IT_VBLANK))
					{
						m_if &= ~static_cast<u8>(InterruptType::IT_VBLANK);
						m_pc = 0x0040;
					}
					else if(pending & static_cast<u8>(InterruptType::IT_LCD_STAT))
					{
						m_if &= ~static_cast<u8>(InterruptType::IT_LCD_STAT);
						m_pc = 0x0048;
					}
					else if(pending & static_cast<u8>(InterruptType::IT_TIMER))
					{
						m_if &= ~static_cast<u8>(InterruptType::IT_TIMER);
						m_pc = 0x0050;
					}
					else if(pending & static_cast<u8>(InterruptType::IT_SERIAL))
					{
						m_if &= ~static_cast<u8>(InterruptType::IT_SERIAL);
						m_pc = 0x0058;
					}
					else if(pending & static_cast<u8>(InterruptType::IT_JOYPAD))
					{
						m_if &= ~static_cast<u8>(InterruptType::IT_JOYPAD);
						m_pc = 0x0060;
					}

					break;
				}
				case 5:
					// Fifth dispatch M-cycle. mooneye-gb's InterruptDispatch fetches
					// the handler opcode on its own cycle, separate from the handler
					// instruction's execution; without this cycle the dispatch would
					// run one M-cycle short (visible in intr_timing).
					m_is_interruption_time = false;
					break;
			}
			m_m_cycle++;
			m_t_cycle = 0;
		}
		m_t_cycle++;
	}
	else
	{
		if(m_is_halted)
		{
			if(m_end_opcode)
			{
				m_opcode = m_bus->read(m_pc);
				m_end_opcode = false;
				m_t_cycle = 1;
				m_m_cycle = 1;
			}

			m_t_cycle++;
		}
		else
		{
			if(m_end_opcode)
			{
				m_opcode = m_bus->read(m_pc);
				m_end_opcode = false;
				m_t_cycle = 1;
				m_m_cycle = 1;
			}

			switch(m_opcode)
			{
				case 0x00:	// NOP
				{
					if(m_t_cycle == 4)
					{
						m_pc++;
						m_end_opcode = true;
					}
					m_t_cycle++;
				}
				break;

				case 0x01:	// LD BC,d16
					in_ld_r16_d16(RegType::RT_BC);
				break;

				case 0x02:	// LD (BC),A
					in_ld_addr_r(read_register16(RegType::RT_BC), RegType::RT_A);
				break;

				case 0x03:	// INC BC
					in_inc_r16(RegType::RT_BC);
				break;

				case 0x04:	// INC B
					in_inc_r(RegType::RT_B);
				break;

				case 0x05:	// DEC B
					in_dec_r(RegType::RT_B);
				break;

				case 0x06:	// LD B,d8
					in_ld_r_d8(RegType::RT_B);
				break;

				case 0x07:	// RLCA
				{
					if(m_t_cycle == 4)
					{
						m_pc++;
						u8 val_C = 0;
						if(Operations::get_bit(m_a, 7))
						{
							val_C = 1;
						}
						m_a = (m_a << 1) | val_C;

						set_flag(FlagType::FL_Z, false);
						set_flag(FlagType::FL_N, false);
						set_flag(FlagType::FL_H, false);
						set_flag(FlagType::FL_C, val_C != 0);

						m_end_opcode = true;
					}
					m_t_cycle++;
				}
				break;


				case 0x08:	// LD (a16),SP
					in_ld_d16_r16(RegType::RT_SP);
				break;

				case 0x09:	// ADD HL,BC
				{
					in_add_r16_r16(RegType::RT_HL, RegType::RT_BC);
				}
				break;

				case 0x0A:	// LD A,(BC)
					in_ld_r_addr(RegType::RT_A, read_register16(RegType::RT_BC));
				break;

				case 0x0B:	// DEC BC
					in_dec_r16(RegType::RT_BC);
				break;

				case 0x0C:	// INC C
					in_inc_r(RegType::RT_C);
				break;

				case 0x0D:	// DEC C
					in_dec_r(RegType::RT_C);
				break;

				case 0x0E:	// LD C,d8
					in_ld_r_d8(RegType::RT_C);
				break;

				case 0x0F:	// RRCA
				{
					if(m_t_cycle == 4)
					{
						m_pc++;
						u8 val_C = 0;
						if(Operations::get_bit(m_a, 0))
						{
							val_C = 1;
						}
						m_a = (m_a >> 1) | (val_C << 7);

						set_flag(FlagType::FL_Z, false);
						set_flag(FlagType::FL_N, false);
						set_flag(FlagType::FL_H, false);
						set_flag(FlagType::FL_C, val_C != 0);

						m_end_opcode = true;
					}
					m_t_cycle++;
				}
				break;

				case 0x11:	// LD DE,d16
					in_ld_r16_d16(RegType::RT_DE);
				break;

				case 0x12:	// LD (DE),A
					in_ld_addr_r(read_register16(RegType::RT_DE), RegType::RT_A);
				break;

				case 0x13:	// INC DE
					in_inc_r16(RegType::RT_DE);
				break;

				case 0x14:	// INC D
					in_inc_r(RegType::RT_D);
				break;

				case 0x15:	// DEC D
					in_dec_r(RegType::RT_D);
				break;

				case 0x16:	// LD D,d8
					in_ld_r_d8(RegType::RT_D);
				break;

				case 0x17:	// RLA
				{
					if(m_t_cycle == 4)
					{
						m_pc++;
						u8 val_C = 0;
						if(Operations::get_bit(m_a, 7))
						{
							val_C = 1;
						}

						u8 val_c_flag = 0;
						if(get_flag(FlagType::FL_C))
						{
							val_c_flag = 1;
						}

						m_a = (m_a << 1) | val_c_flag;

						set_flag(FlagType::FL_Z, false);
						set_flag(FlagType::FL_N, false);
						set_flag(FlagType::FL_H, false);
						set_flag(FlagType::FL_C, val_C != 0);

						m_end_opcode = true;
					}
					m_t_cycle++;
				}
				break;

				case 0x18:	//JR r8
					in_jr(JpCond::JC_NONE);
				break;

				case 0x19:	// ADD HL,DE
				{
					in_add_r16_r16(RegType::RT_HL, RegType::RT_DE);
				}
				break;

				case 0x1A: // LD A,(DE)
					in_ld_r_addr(RegType::RT_A, read_register16(RegType::RT_DE));
				break;

				case 0x1B:	// DEC DE
					in_dec_r16(RegType::RT_DE);
				break;

				case 0x1C:	// INC E
					in_inc_r(RegType::RT_E);
				break;

				case 0x1D:	// DEC E
					in_dec_r(RegType::RT_E);
				break;

				case 0x1E:	// LD E,d8
					in_ld_r_d8(RegType::RT_E);
				break;

				case 0x1F:	// RRA
				{
					if(m_t_cycle == 4)
					{
						m_pc++;
						bool new_c = (m_a & 1) == 1;

						m_a = m_a >> 1;
						m_a = Operations::set_bit(m_a, 7, get_flag(FlagType::FL_C));

						set_flag(FlagType::FL_Z, false);
						set_flag(FlagType::FL_N, false);
						set_flag(FlagType::FL_H, false);
						set_flag(FlagType::FL_C, new_c);

						m_end_opcode = true;
					}
					m_t_cycle++;
				}
				break;

				case 0x20:	//JR NZ,r8
					in_jr(JpCond::JC_NZ);
				break;

				case 0x21: // LD HL,d16
					in_ld_r16_d16(RegType::RT_HL);
				break;

				case 0x22:	// LD (HL+),A
					in_ld_addr_r(read_register16(RegType::RT_HL), RegType::RT_A);
					if(m_end_opcode)
					{
						write_register16(RegType::RT_HL, read_register16(RegType::RT_HL) + 1);
					}
				break;

				case 0x23:	// INC HL
					in_inc_r16(RegType::RT_HL);
				break;

				case 0x24:	// INC H
					in_inc_r(RegType::RT_H);
				break;

				case 0x25:	// DEC H
					in_dec_r(RegType::RT_H);
				break;

				case 0x26:	// LD H,d8
					in_ld_r_d8(RegType::RT_H);
				break;

				case 0x27:	// DAA
				{
					if(m_t_cycle == 4)
					{
						m_pc++;

						u8 value = 0;
						bool new_c = false;

						if(get_flag(FlagType::FL_H) || (!get_flag(FlagType::FL_N) && ((m_a & 0xF) > 9)))
						{
							value = 6;
						}

						if(get_flag(FlagType::FL_C) || (!get_flag(FlagType::FL_N) && (m_a > 0x99)))
						{
							value |= 0x60;
							new_c = true;
						}

						m_a += get_flag(FlagType::FL_N) ? -value : value;

						set_flag(FlagType::FL_Z, m_a == 0);
						set_flag(FlagType::FL_H, false);
						set_flag(FlagType::FL_C, new_c);

						m_end_opcode = true;
					}
					m_t_cycle++;
				}
				break;

				case 0x28:	//JR Z,r8
					in_jr(JpCond::JC_Z);
				break;

				case 0x29:	// ADD HL,HL
				{
					in_add_r16_r16(RegType::RT_HL, RegType::RT_HL);
				}
				break;

				case 0x2A: // LD A,(HL+)
					in_ld_r_addr(RegType::RT_A, read_register16(RegType::RT_HL));
					if(m_end_opcode)
					{
						write_register16(RegType::RT_HL, read_register16(RegType::RT_HL) + 1);
					}
				break;

				case 0x2B:	// DEC HL
					in_dec_r16(RegType::RT_HL);
				break;

				case 0x2C:	// INC L
					in_inc_r(RegType::RT_L);
				break;

				case 0x2D:	// DEC L
					in_dec_r(RegType::RT_L);
				break;

				case 0x2E:	// LD L,d8
					in_ld_r_d8(RegType::RT_L);
				break;

				case 0x2F:	// CPL
				{
					if(m_t_cycle == 4)
					{
						m_pc++;
						m_a = ~m_a;

						set_flag(FlagType::FL_N, true);
						set_flag(FlagType::FL_H, true);

						m_end_opcode = true;
					}
					m_t_cycle++;
				}
				break;

				case 0x30:	//JR NC,r8
					in_jr(JpCond::JC_NC);
				break;

				case 0x31: // LD SP,d16
					in_ld_r16_d16(RegType::RT_SP);
				break;

				case 0x32:	// LD (HL-),A
					in_ld_addr_r(read_register16(RegType::RT_HL), RegType::RT_A);
					if(m_end_opcode)
					{
						write_register16(RegType::RT_HL, read_register16(RegType::RT_HL) - 1);
					}
				break;

				case 0x33:	// INC SP
					in_inc_r16(RegType::RT_SP);
				break;

				case 0x34:	// INC (HL)
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
								m_fetched_data_lo = m_bus->read(read_register16(RegType::RT_HL));
								set_flag(FlagType::FL_N, false);
								set_flag(FlagType::FL_Z, ((m_fetched_data_lo + 1) & 0xFF) == 0);
								set_flag(FlagType::FL_H, (((m_fetched_data_lo & 0xF) + 1) & 0x10) == 0x10);
								break;
							case 3:
								m_bus->write(read_register16(RegType::RT_HL), (m_fetched_data_lo + 1) & 0xFF);
								m_end_opcode = true;
								break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0x35:	// DEC (HL)
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
								m_fetched_data_lo = m_bus->read(read_register16(RegType::RT_HL));
								set_flag(FlagType::FL_N, true);
								set_flag(FlagType::FL_Z, ((m_fetched_data_lo - 1) & 0xFF) == 0);
								set_flag(FlagType::FL_H, (((m_fetched_data_lo & 0xF) - 1) & 0x10) == 0x10);
								break;
							case 3:
								m_bus->write(read_register16(RegType::RT_HL), (m_fetched_data_lo - 1) & 0xFF);
								m_end_opcode = true;
								break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0x36:	// LD (HL),d8
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
								m_fetched_data_lo = m_bus->read(m_pc);
								m_pc++;
								break;
							case 3:
								m_bus->write(read_register16(RegType::RT_HL), m_fetched_data_lo);
								m_end_opcode = true;
								break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0x37:	// SCF
				{
					if(m_t_cycle == 4)
					{
						m_pc++;

						set_flag(FlagType::FL_N, false);
						set_flag(FlagType::FL_H, false);
						set_flag(FlagType::FL_C, true);

						m_end_opcode = true;
					}
					m_t_cycle++;
				}
				break;

				case 0x38:	//JR C,r8
					in_jr(JpCond::JC_C);
				break;

				case 0x39:	// ADD HL,SP
				{
					in_add_r16_r16(RegType::RT_HL, RegType::RT_SP);
				}
				break;

				case 0x3A: // LD A,(HL-)
					in_ld_r_addr(RegType::RT_A, read_register16(RegType::RT_HL));
					if(m_end_opcode)
					{
						write_register16(RegType::RT_HL, read_register16(RegType::RT_HL) - 1);
					}
				break;

				case 0x3B:	// DEC SP
					in_dec_r16(RegType::RT_SP);
				break;

				case 0x3C:	// INC A
					in_inc_r(RegType::RT_A);
				break;

				case 0x3D:	// DEC A
					in_dec_r(RegType::RT_A);
				break;

				case 0x3E:	// LD A,d8
					in_ld_r_d8(RegType::RT_A);
				break;

				case 0x3F:	// CCF
				{
					if(m_t_cycle == 4)
					{
						m_pc++;

						set_flag(FlagType::FL_N, false);
						set_flag(FlagType::FL_H, false);
						set_flag(FlagType::FL_C, !get_flag(FlagType::FL_C));

						m_end_opcode = true;
					}
					m_t_cycle++;
				}
				break;

				case 0x40:	//LD B,B
				{
					in_ld_r_r(RegType::RT_B, RegType::RT_B);
				}
				break;

				case 0x41:	//LD B,C
				{
					in_ld_r_r(RegType::RT_B, RegType::RT_C);
				}
				break;

				case 0x42:	//LD B,D
				{
					in_ld_r_r(RegType::RT_B, RegType::RT_D);
				}
				break;

				case 0x43:	//LD B,E
				{
					in_ld_r_r(RegType::RT_B, RegType::RT_E);
				}
				break;

				case 0x44:	//LD B,H
				{
					in_ld_r_r(RegType::RT_B, RegType::RT_H);
				}
				break;

				case 0x45:	//LD B,L
				{
					in_ld_r_r(RegType::RT_B, RegType::RT_L);
				}
				break;

				case 0x46:	// LD B,(HL)
				{
					in_ld_r_addr(RegType::RT_B, read_register16(RegType::RT_HL));
				}
				break;

				case 0x47:	//LD B,A
				{
					in_ld_r_r(RegType::RT_B, RegType::RT_A);
				}
				break;

				case 0x48:	//LD C,B
				{
					in_ld_r_r(RegType::RT_C, RegType::RT_B);
				}
				break;

				case 0x49:	//LD C,C
				{
					in_ld_r_r(RegType::RT_C, RegType::RT_C);
				}
				break;

				case 0x4A:	//LD C,D
				{
					in_ld_r_r(RegType::RT_C, RegType::RT_D);
				}
				break;

				case 0x4B:	//LD C,E
				{
					in_ld_r_r(RegType::RT_C, RegType::RT_E);
				}
				break;

				case 0x4C:	//LD C,H
				{
					in_ld_r_r(RegType::RT_C, RegType::RT_H);
				}
				break;

				case 0x4D:	//LD C,L
				{
					in_ld_r_r(RegType::RT_C, RegType::RT_L);
				}
				break;

				case 0x4E:	// LD C,(HL)
				{
					in_ld_r_addr(RegType::RT_C, read_register16(RegType::RT_HL));
				}
				break;

				case 0x4F:	//LD C,A
				{
					in_ld_r_r(RegType::RT_C, RegType::RT_A);
				}
				break;

				case 0x50:	//LD D,B
				{
					in_ld_r_r(RegType::RT_D, RegType::RT_B);
				}
				break;

				case 0x51:	//LD D,C
				{
					in_ld_r_r(RegType::RT_D, RegType::RT_C);
				}
				break;

				case 0x52:	//LD D,D
				{
					in_ld_r_r(RegType::RT_D, RegType::RT_D);
				}
				break;

				case 0x53:	//LD D,E
				{
					in_ld_r_r(RegType::RT_D, RegType::RT_E);
				}
				break;

				case 0x54:	//LD D,H
				{
					in_ld_r_r(RegType::RT_D, RegType::RT_H);
				}
				break;

				case 0x55:	//LD D,L
				{
					in_ld_r_r(RegType::RT_D, RegType::RT_L);
				}
				break;

				case 0x56:	// LD D,(HL)
				{
					in_ld_r_addr(RegType::RT_D, read_register16(RegType::RT_HL));
				}
				break;

				case 0x57:	//LD D,A
				{
					in_ld_r_r(RegType::RT_D, RegType::RT_A);
				}
				break;

				case 0x58:	//LD E,B
				{
					in_ld_r_r(RegType::RT_E, RegType::RT_B);
				}
				break;

				case 0x59:	//LD E,C
				{
					in_ld_r_r(RegType::RT_E, RegType::RT_C);
				}
				break;

				case 0x5A:	//LD E,D
				{
					in_ld_r_r(RegType::RT_E, RegType::RT_D);
				}
				break;

				case 0x5B:	//LD E,E
				{
					in_ld_r_r(RegType::RT_E, RegType::RT_E);
				}
				break;

				case 0x5C:	//LD E,H
				{
					in_ld_r_r(RegType::RT_E, RegType::RT_H);
				}
				break;

				case 0x5D:	//LD E,L
				{
					in_ld_r_r(RegType::RT_E, RegType::RT_L);
				}
				break;

				case 0x5E:	// LD E,(HL)
				{
					in_ld_r_addr(RegType::RT_E, read_register16(RegType::RT_HL));
				}
				break;

				case 0x5F:	//LD E,A
				{
					in_ld_r_r(RegType::RT_E, RegType::RT_A);
				}
				break;

				case 0x60:	//LD H,B
				{
					in_ld_r_r(RegType::RT_H, RegType::RT_B);
				}
				break;

				case 0x61:	//LD H,C
				{
					in_ld_r_r(RegType::RT_H, RegType::RT_C);
				}
				break;

				case 0x62:	//LD H,D
				{
					in_ld_r_r(RegType::RT_H, RegType::RT_D);
				}
				break;

				case 0x63:	//LD H,E
				{
					in_ld_r_r(RegType::RT_H, RegType::RT_E);
				}
				break;

				case 0x64:	//LD H,H
				{
					in_ld_r_r(RegType::RT_H, RegType::RT_H);
				}
				break;

				case 0x65:	//LD H,L
				{
					in_ld_r_r(RegType::RT_H, RegType::RT_L);
				}
				break;

				case 0x66:	// LD H,(HL)
				{
					in_ld_r_addr(RegType::RT_H, read_register16(RegType::RT_HL));
				}
				break;

				case 0x67:	//LD H,A
				{
					in_ld_r_r(RegType::RT_H, RegType::RT_A);
				}
				break;

				case 0x68:	//LD L,B
				{
					in_ld_r_r(RegType::RT_L, RegType::RT_B);
				}
				break;

				case 0x69:	//LD L,C
				{
					in_ld_r_r(RegType::RT_L, RegType::RT_C);
				}
				break;

				case 0x6A:	//LD L,D
				{
					in_ld_r_r(RegType::RT_L, RegType::RT_D);
				}
				break;

				case 0x6B:	//LD L,E
				{
					in_ld_r_r(RegType::RT_L, RegType::RT_E);
				}
				break;

				case 0x6C:	//LD L,H
				{
					in_ld_r_r(RegType::RT_L, RegType::RT_H);
				}
				break;

				case 0x6D:	//LD L,L
				{
					in_ld_r_r(RegType::RT_L, RegType::RT_L);
				}
				break;

				case 0x6E:	// LD L,(HL)
				{
					in_ld_r_addr(RegType::RT_L, read_register16(RegType::RT_HL));
				}
				break;

				case 0x6F:	//LD L,A
				{
					in_ld_r_r(RegType::RT_L, RegType::RT_A);
				}
				break;

				case 0x70:	// LD (HL),B
				{
					in_ld_addr_r(read_register16(RegType::RT_HL), RegType::RT_B);
				}
				break;

				case 0x71:	// LD (HL),C
				{
					in_ld_addr_r(read_register16(RegType::RT_HL), RegType::RT_C);
				}
				break;

				case 0x72:	// LD (HL),D
				{
					in_ld_addr_r(read_register16(RegType::RT_HL), RegType::RT_D);
				}
				break;

				case 0x73:	// LD (HL),E
				{
					in_ld_addr_r(read_register16(RegType::RT_HL), RegType::RT_E);
				}
				break;

				case 0x74:	// LD (HL),H
				{
					in_ld_addr_r(read_register16(RegType::RT_HL), RegType::RT_H);
				}
				break;

				case 0x75:	// LD (HL),L
				{
					in_ld_addr_r(read_register16(RegType::RT_HL), RegType::RT_L);
				}
				break;

				case 0x76:	// HALT
				{
					if(m_t_cycle == 4)
					{
						if((m_ie & m_if) != 0)
						{
							m_halt_bug = true;
						}
						else
						{
							m_pc++;
							m_is_halted = true;
							m_halt_bug = false;
						}
						m_end_opcode = true;
					}
					m_t_cycle++;
				}
				break;

				case 0x77:	// LD (HL),A
				{
					in_ld_addr_r(read_register16(RegType::RT_HL), RegType::RT_A);
				}
				break;

				case 0x78:	//LD A,B
				{
					in_ld_r_r(RegType::RT_A, RegType::RT_B);
				}
				break;

				case 0x79:	//LD A,C
				{
					in_ld_r_r(RegType::RT_A, RegType::RT_C);
				}
				break;

				case 0x7A:	//LD A,D
				{
					in_ld_r_r(RegType::RT_A, RegType::RT_D);
				}
				break;

				case 0x7B:	//LD A,E
				{
					in_ld_r_r(RegType::RT_A, RegType::RT_E);
				}
				break;

				case 0x7C:	//LD A,H
				{
					in_ld_r_r(RegType::RT_A, RegType::RT_H);
				}
				break;

				case 0x7D:	//LD A,L
				{
					in_ld_r_r(RegType::RT_A, RegType::RT_L);
				}
				break;

				case 0x7E:	// LD A,(HL)
				{
					in_ld_r_addr(RegType::RT_A, read_register16(RegType::RT_HL));
				}
				break;

				case 0x7F:	// LD A,A
				{
					in_ld_r_r(RegType::RT_A, RegType::RT_A);
				}
				break;

				case 0x80:	// ADD A,B
				{
					in_add_r_r(RegType::RT_A, RegType::RT_B);
				}
				break;

				case 0x81:	// ADD A,C
				{
					in_add_r_r(RegType::RT_A, RegType::RT_C);
				}
				break;

				case 0x82:	// ADD A,D
				{
					in_add_r_r(RegType::RT_A, RegType::RT_D);
				}
				break;

				case 0x83:	// ADD A,E
				{
					in_add_r_r(RegType::RT_A, RegType::RT_E);
				}
				break;

				case 0x84:	// ADD A,H
				{
					in_add_r_r(RegType::RT_A, RegType::RT_H);
				}
				break;

				case 0x85:	// ADD A,L
				{
					in_add_r_r(RegType::RT_A, RegType::RT_L);
				}
				break;

				case 0x86:	// ADD A,(HL)
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
							{
								m_fetched_data_lo = m_bus->read(read_register16(RegType::RT_HL));
								u16 result = m_a + m_fetched_data_lo;

								set_flag(FlagType::FL_Z, (result & 0xFF) == 0);
								set_flag(FlagType::FL_N, false);
								set_flag(FlagType::FL_H, ((m_a & 0x0F) + (m_fetched_data_lo & 0x0F)) > 0x0F);
								set_flag(FlagType::FL_C, result > 0xFF);

								m_a = result & 0xFF;
								m_end_opcode = true;
							}
							break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0x87:	// ADD A,A
				{
					in_add_r_r(RegType::RT_A, RegType::RT_A);
				}
				break;

				case 0x88:	//ADC A,B
				{
					in_adc_r_r(RegType::RT_A, RegType::RT_B);
				}
				break;

				case 0x89:	//ADC A,C
				{
					in_adc_r_r(RegType::RT_A, RegType::RT_C);
				}
				break;

				case 0x8A:	//ADC A,D
				{
					in_adc_r_r(RegType::RT_A, RegType::RT_D);
				}
				break;

				case 0x8B:	//ADC A,E
				{
					in_adc_r_r(RegType::RT_A, RegType::RT_E);
				}
				break;

				case 0x8C:	//ADC A,H
				{
					in_adc_r_r(RegType::RT_A, RegType::RT_H);
				}
				break;

				case 0x8D:	//ADC A,L
				{
					in_adc_r_r(RegType::RT_A, RegType::RT_L);
				}
				break;

				case 0x8E:	// ADC A,(HL)
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
							{
								m_fetched_data_lo = m_bus->read(read_register16(RegType::RT_HL));
								u8 val_C = 0;
								if(get_flag(FlagType::FL_C))
								{
									val_C = 1;
								}
								u16 result = m_a + m_fetched_data_lo + val_C;

								set_flag(FlagType::FL_Z, (result & 0xFF) == 0);
								set_flag(FlagType::FL_N, false);
								set_flag(FlagType::FL_H, ((m_a & 0x0F) + (m_fetched_data_lo & 0x0F) + val_C) > 0x0F);
								set_flag(FlagType::FL_C, result > 0xFF);

								m_a = result & 0xFF;

								m_end_opcode = true;
							}
							break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0x8F:	//ADC A,A
				{
					in_adc_r_r(RegType::RT_A, RegType::RT_A);
				}
				break;

				case 0x90:	//SUB B
				{
					in_sub_r(RegType::RT_B);
				}
				break;

				case 0x91:	//SUB C
				{
					in_sub_r(RegType::RT_C);
				}
				break;

				case 0x92:	//SUB D
				{
					in_sub_r(RegType::RT_D);
				}
				break;

				case 0x93:	//SUB E
				{
					in_sub_r(RegType::RT_E);
				}
				break;

				case 0x94:	//SUB H
				{
					in_sub_r(RegType::RT_H);
				}
				break;

				case 0x95:	//SUB L
				{
					in_sub_r(RegType::RT_L);
				}
				break;

				case 0x96:	// SUB A,(HL)
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
							{
								m_fetched_data_lo = m_bus->read(read_register16(RegType::RT_HL));
								u16 result = m_a - m_fetched_data_lo;

								set_flag(FlagType::FL_Z, (result & 0xFF) == 0);
								set_flag(FlagType::FL_N, true);
								set_flag(FlagType::FL_H, ((int)m_a & 0x0F) - ((int)m_fetched_data_lo & 0x0F) < 0);
								set_flag(FlagType::FL_C, (int)m_a - int(m_fetched_data_lo) < 0);

								m_a = result & 0xFF;
								m_end_opcode = true;
							}
							break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0x97:	//SUB A
				{
					in_sub_r(RegType::RT_A);
				}
				break;

				case 0x98:	// SBC A,B
				{
					in_sbc_r_r(RegType::RT_A, RegType::RT_B);
				}
				break;

				case 0x99:	// SBC A,C
				{
					in_sbc_r_r(RegType::RT_A, RegType::RT_C);
				}
				break;

				case 0x9A:	// SBC A,D
				{
					in_sbc_r_r(RegType::RT_A, RegType::RT_D);
				}
				break;

				case 0x9B:	// SBC A,E
				{
					in_sbc_r_r(RegType::RT_A, RegType::RT_E);
				}
				break;

				case 0x9C:	// SBC A,H
				{
					in_sbc_r_r(RegType::RT_A, RegType::RT_H);
				}
				break;

				case 0x9D:	// SBC A,L
				{
					in_sbc_r_r(RegType::RT_A, RegType::RT_L);
				}
				break;

				case 0x9E:	// SBC A,(HL)
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
							{
								m_fetched_data_lo = m_bus->read(read_register16(RegType::RT_HL));
								u8 val_C = 0;
								if(get_flag(FlagType::FL_C))
								{
									val_C = 1;
								}
								u16 result = m_a - m_fetched_data_lo - val_C;

								set_flag(FlagType::FL_Z, (result & 0xFF) == 0);
								set_flag(FlagType::FL_N, true);
								set_flag(FlagType::FL_H, (((int)m_a & 0x0F) - ((int)m_fetched_data_lo & 0x0F) - (int)val_C) < 0);
								set_flag(FlagType::FL_C, ((int)m_a - (int)m_fetched_data_lo - (int)val_C) < 0);

								m_a = result & 0xFF;

								m_end_opcode = true;
							}
							break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0x9F:	// SBC A,A
				{
					in_sbc_r_r(RegType::RT_A, RegType::RT_A);
				}
				break;

				case 0xA0:	// AND B
				{
					in_and_r(RegType::RT_B);
				}
				break;

				case 0xA1:	// AND C
				{
					in_and_r(RegType::RT_C);
				}
				break;

				case 0xA2:	// AND D
				{
					in_and_r(RegType::RT_D);
				}
				break;

				case 0xA3:	// AND E
				{
					in_and_r(RegType::RT_E);
				}
				break;

				case 0xA4:	// AND H
				{
					in_and_r(RegType::RT_H);
				}
				break;

				case 0xA5:	// AND L
				{
					in_and_r(RegType::RT_L);
				}
				break;

				case 0xA6:	// AND (HL)
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
							{
								m_fetched_data_lo = m_bus->read(read_register16(RegType::RT_HL));
								m_a &= m_fetched_data_lo;

								set_flag(FlagType::FL_Z, m_a == 0);
								set_flag(FlagType::FL_N, false);
								set_flag(FlagType::FL_H, true);
								set_flag(FlagType::FL_C, false);

								m_end_opcode = true;
							}
							break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0xA7:	// AND A
				{
					in_and_r(RegType::RT_A);
				}
				break;

				case 0xA8:	// XOR B
				{
					in_xor_r(RegType::RT_B);
				}
				break;

				case 0xA9:	// XOR C
				{
					in_xor_r(RegType::RT_C);
				}
				break;

				case 0xAA:	// XOR D
				{
					in_xor_r(RegType::RT_D);
				}
				break;

				case 0xAB:	// XOR E
				{
					in_xor_r(RegType::RT_E);
				}
				break;

				case 0xAC:	// XOR H
				{
					in_xor_r(RegType::RT_H);
				}
				break;

				case 0xAD:	// XOR L
				{
					in_xor_r(RegType::RT_L);
				}
				break;

				case 0xAE:	// XOR (HL)
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;

							case 2:
								m_fetched_data_lo = m_bus->read(read_register16(RegType::RT_HL));
								m_a ^= m_fetched_data_lo;

								set_flag(FlagType::FL_Z, m_a == 0);
								set_flag(FlagType::FL_N, false);
								set_flag(FlagType::FL_H, false);
								set_flag(FlagType::FL_C, false);

								m_end_opcode = true;
								break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0xAF:	// XOR A
				{
					in_xor_r(RegType::RT_A);
				}
				break;

				case 0xB0:	// OR B
				{
					in_or_r(RegType::RT_B);
				}
				break;

				case 0xB1:	// OR C
				{
					in_or_r(RegType::RT_C);
				}
				break;

				case 0xB2:	// OR D
				{
					in_or_r(RegType::RT_D);
				}
				break;

				case 0xB3:	// OR E
				{
					in_or_r(RegType::RT_E);
				}
				break;

				case 0xB4:	// OR H
				{
					in_or_r(RegType::RT_H);
				}
				break;

				case 0xB5:	// OR L
				{
					in_or_r(RegType::RT_L);
				}
				break;

				case 0xB6:	// OR (HL)
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
							{
								m_fetched_data_lo = m_bus->read(read_register16(RegType::RT_HL));
								m_a |= m_fetched_data_lo;


								set_flag(FlagType::FL_Z, m_a == 0);
								set_flag(FlagType::FL_N, false);
								set_flag(FlagType::FL_H, false);
								set_flag(FlagType::FL_C, false);

								m_end_opcode = true;
							}
							break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0xB7:	// OR A
				{
					in_or_r(RegType::RT_A);
				}
				break;

				case 0xB8:	// CP B
				{
					in_cp(RegType::RT_B);
				}
				break;

				case 0xB9:	// CP C
				{
					in_cp(RegType::RT_C);
				}
				break;

				case 0xBA:	// CP D
				{
					in_cp(RegType::RT_D);
				}
				break;

				case 0xBB:	// CP E
				{
					in_cp(RegType::RT_E);
				}
				break;

				case 0xBC:	// CP H
				{
					in_cp(RegType::RT_H);
				}
				break;

				case 0xBD:	// CP L
				{
					in_cp(RegType::RT_L);
				}
				break;

				case 0xBE:	// CP (HL)
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
								m_fetched_data_lo = m_bus->read(read_register16(RegType::RT_HL));
								int n = static_cast<int>(m_a) - static_cast<int>(m_fetched_data_lo);

								set_flag(FlagType::FL_Z, n == 0);
								set_flag(FlagType::FL_N, true);
								set_flag(FlagType::FL_H, ((static_cast<int>(m_a) & 0xF) - (static_cast<int>(m_fetched_data_lo & 0xF))) < 0);
								set_flag(FlagType::FL_C, n < 0);

								m_end_opcode = true;
								break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0xBF:	// CP A
				{
					in_cp(RegType::RT_A);
				}
				break;

				case 0xC0:	// RET NZ
				{
					in_ret(JpCond::JC_NZ);
				}
				break;

				case 0xC1:	// POP BC
				{
					in_pop(RegType::RT_BC);
				}
				break;

				case 0xC2:	// JP NZ,a16
				{
					in_jp(JpCond::JC_NZ);
				}
				break;

				case 0xC3:	// JP a16
				{
					in_jp();
				}
				break;

				case 0xC4:	// CALL NZ,a16
				{
					in_call(JpCond::JC_NZ);
				}
				break;

				case 0xC5:	// PUSH BC
				{
					in_push(RegType::RT_BC);
				}
				break;

				case 0xC6:	// ADD A,d8
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
								m_fetched_data_lo = m_bus->read(m_pc);
								m_pc++;
								set_flag(FlagType::FL_Z, ((m_a + m_fetched_data_lo) & 0xFF) == 0);
								set_flag(FlagType::FL_N, false);
								set_flag(FlagType::FL_H, ((m_a & 0xF) + (m_fetched_data_lo & 0xF)) > 0x0F);
								set_flag(FlagType::FL_C, (static_cast<u16>(m_a) + static_cast<u16>(m_fetched_data_lo)) > 0xFF);

								m_a += m_fetched_data_lo;
								m_end_opcode = true;
								break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0xC7:	// RST 00H
				{
					in_rst(0x0000);
				}
				break;

				case 0xC8:	// RET Z
				{
					in_ret(JpCond::JC_Z);
				}
				break;

				case 0xC9:	// RET
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
								m_fetched_data_lo = m_bus->read(m_sp);
								m_sp++;
								break;
							case 3:
								m_fetched_data_hi = m_bus->read(m_sp);
								m_sp++;
								break;
							case 4:
								m_pc = (m_fetched_data_hi << 8) | m_fetched_data_lo;
								m_end_opcode = true;
								break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0xCA:	// JP Z,a16
				{
					in_jp(JpCond::JC_Z);
				}
				break;

				case 0xCB:	// CB
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								m_fetched_data_lo = m_bus->read(m_pc);
								break;

					case 2:
					{
						m_pc++;

						RegType reg = decode_reg(m_fetched_data_lo & 0x07);
						if(reg != RegType::RT_HL)
						{
							in_cb(reg, m_fetched_data_lo);
							m_end_opcode = true;
						}
					}
					break;

					case 3:
					{
						u8 bit_op = (m_fetched_data_lo >> 6) & 0x07;

						// Read (HL) lands here (observed at M3, Gekkio).
						m_fetched_data_hi = m_bus->read(read_register16(RegType::RT_HL));
						if(bit_op == 1)	// BIT: no write; flags computed on the read
						{
							in_cb(RegType::RT_HL, m_fetched_data_lo);
							m_end_opcode = true;
						}
					}
					break;

					case 4:
					{
						// RMW: write (HL) lands here (observed at M4, Gekkio)
						in_cb(RegType::RT_HL, m_fetched_data_lo);
						m_end_opcode = true;	// RMW ends after 4 M-cycles (16 T)
					}
					break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0xCC:	// CALL Z,a16
					in_call(JpCond::JC_Z);
				break;

				case 0xCD:	// CALL a16
					in_call();
				break;

				case 0xCE:	// ADC A,d8
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
							{
								m_fetched_data_lo = m_bus->read(m_pc);
								m_pc++;

								u16 result;
								u8 flag_c = 0;
								if(get_flag(FlagType::FL_C))
								{
									flag_c = 1;
								}

								result = m_a + m_fetched_data_lo + flag_c;

								set_flag(FlagType::FL_Z, (result & 0xFF) == 0);
								set_flag(FlagType::FL_N, false);
								set_flag(FlagType::FL_H, ((m_a & 0xF) + (m_fetched_data_lo & 0xF) + flag_c) > 0xF);
								set_flag(FlagType::FL_C, (result) > 0xFF);

								m_a = result & 0xFF;

								m_end_opcode = true;
							}
							break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0xCF:	// RST 08H
				{
					in_rst(0x0008);
				}
				break;

				case 0xD0:	// RET NC
				{
					in_ret(JpCond::JC_NC);
				}
				break;

				case 0xD1:	// POP DE
					in_pop(RegType::RT_DE);
				break;

				case 0xD2:	// JP NC,a16
				{
					in_jp(JpCond::JC_NC);
				}
				break;

				case 0xD4:	// CALL NC,a16
					in_call(JpCond::JC_NC);
				break;

				case 0xD5:	// PUSH DE
					in_push(RegType::RT_DE);
				break;

				case 0xD6:	// SUB A,d8
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
								m_fetched_data_lo = m_bus->read(m_pc);
								m_pc++;
								set_flag(FlagType::FL_Z, ((m_a - m_fetched_data_lo) & 0xFF) == 0);
								set_flag(FlagType::FL_N, true);
								set_flag(FlagType::FL_H, ((static_cast<u16>(m_a) & 0xF) - (static_cast<u16>(m_fetched_data_lo) & 0xF)) < 0);
								set_flag(FlagType::FL_C, (static_cast<u16>(m_a) - static_cast<u16>(m_fetched_data_lo)) < 0);

								m_a -= m_fetched_data_lo;
								m_end_opcode = true;
								break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0xD7:	// RST 10H
				{
					in_rst(0x0010);
				}
				break;

				case 0xD8:	// RET C
				{
					in_ret(JpCond::JC_C);
				}
				break;

				case 0xD9:	// RETI
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
								m_fetched_data_lo = m_bus->read(m_sp);
								m_sp++;
								break;
							case 3:
								m_fetched_data_hi = m_bus->read(m_sp);
								m_sp++;
								break;
							case 4:
								m_pc = (m_fetched_data_hi << 8) | m_fetched_data_lo;
								// RETI enables IME immediately: mooneye-gb reti()
								// sets ime = true before prefetching, so a pending
								// interrupt is dispatched at RETI's own boundary
								// (unlike EI, deferred by one instruction).
								m_ime = true;
								m_enabling_ime = false;
								m_next_m_cycle = false;
								m_end_opcode = true;
								break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				break;

				case 0xDA:	// JP C,a16
				{
					in_jp(JpCond::JC_C);
				}
				break;

				case 0xDC:	// CALL C,a16
					in_call(JpCond::JC_C);
				break;

				case 0xDE:	// SBC A,d8
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
							{
								m_fetched_data_lo = m_bus->read(m_pc);
								m_pc++;

								u8 value_C = 0;
								if(get_flag(FlagType::FL_C))
								{
									value_C = 1;
								}
								u8 value = m_fetched_data_lo + value_C;


								set_flag(FlagType::FL_Z, (m_a - value) == 0);
								set_flag(FlagType::FL_N, true);
								set_flag(FlagType::FL_H, (((int)m_a & 0x0F) - ((int)m_fetched_data_lo & 0x0F) - ((int)value_C)) < 0);
								set_flag(FlagType::FL_C, (((int)m_a) - ((int)m_fetched_data_lo) - ((int)value_C)) < 0);

								m_a -= value;

								m_end_opcode = true;
							}
							break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;

				}
				break;

				case 0xDF:	// RST 18H
				{
					in_rst(0x0018);
				}
				break;

				case 0xE0:	// LDH (a8),A
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
								m_fetched_data_lo = m_bus->read(m_pc);
								m_pc++;
								m_fetched_address = 0xFF00 + m_fetched_data_lo;
								break;
							case 3:
								m_bus->write(m_fetched_address, m_a);
								m_end_opcode = true;
								break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0xE1:	// POP HL
				{
					in_pop(RegType::RT_HL);
				}
				break;

				case 0xE2:	// LD (C),A
				{
					in_ld_addr_r(0xFF00 + m_c, RegType::RT_A);
				}
				break;

				case 0xE5:	// PUSH HL
				{
					in_push(RegType::RT_HL);
				}
				break;

				case 0xE6:	// AND d8
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
								m_fetched_data_lo = m_bus->read(m_pc);
								m_pc++;
								m_a &= m_fetched_data_lo;

								set_flag(FlagType::FL_Z, m_a == 0);
								set_flag(FlagType::FL_N, false);
								set_flag(FlagType::FL_H, true);
								set_flag(FlagType::FL_C, false);

								m_end_opcode = true;
								break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0xE7:	// RST 20H
				{
					in_rst(0x0020);
				}
				break;

				case 0xE8:	// ADD SP,r8
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
								m_fetched_data_lo = m_bus->read(m_pc);
								m_pc++;
								break;
							case 3:
								set_flag(FlagType::FL_Z, false);
								set_flag(FlagType::FL_N, false);
								set_flag(FlagType::FL_H, ((m_sp & 0x0F) + (m_fetched_data_lo & 0x0F)) >= 0x10);
								set_flag(FlagType::FL_C, ((m_sp & 0xFF) + (m_fetched_data_lo & 0xFF)) >= 0x100);
								break;
							case 4:
							{
								m_sp = m_sp + static_cast<char>(m_fetched_data_lo);
								m_end_opcode = true;
							}
							break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0xE9:	// JP (HL)
					if(m_t_cycle == 4)
					{
						m_pc = read_register16(RegType::RT_HL);
						m_end_opcode = true;
					}
					m_t_cycle++;
				break;

				case 0xEA:	// LD (a16),A
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
								m_fetched_data_lo = m_bus->read(m_pc);
								m_pc++;
								break;
							case 3:
								m_fetched_data_hi = m_bus->read(m_pc);
								m_pc++;
								m_fetched_address = (m_fetched_data_hi << 8) | m_fetched_data_lo;
								break;
							case 4:
								m_bus->write(m_fetched_address, m_a);
								m_end_opcode = true;
								break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0xEE:	// XOR d8
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
								m_fetched_data_lo = m_bus->read(m_pc);
								m_pc++;
								m_a ^= m_fetched_data_lo;

								set_flag(FlagType::FL_Z, m_a == 0);
								set_flag(FlagType::FL_N, false);
								set_flag(FlagType::FL_H, false);
								set_flag(FlagType::FL_C, false);

								m_end_opcode = true;
								break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0xEF:	// RST 28H
				{
					in_rst(0x0028);
				}
				break;

				case 0xF0:	// LDH A,(a8)
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
								m_fetched_data_lo = m_bus->read(m_pc);
								m_pc++;
								m_fetched_address = 0xFF00 + m_fetched_data_lo;
								break;
							case 3:
								m_a = m_bus->read(m_fetched_address);
								m_end_opcode = true;
								break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0xF1:	// POP AF
				{
					in_pop(RegType::RT_AF);
				}
				break;

				case 0xF2:	// LD A,(C)
				{
					in_ld_r_addr(RegType::RT_A, 0xFF00 + m_c);
				}
				break;

				case 0xF3:	// DI
				{
					if(m_t_cycle == 4)
					{
						m_pc++;

						// Disabling IME for next cycle
						m_ime = false;
						m_enabling_ime = false;
						m_next_m_cycle = false;

						m_t_cycle = 0;
						m_end_opcode = true;
					}
					m_t_cycle++;
				}
				break;

				case 0xF5:	// PUSH AF
					in_push(RegType::RT_AF);
				break;

				case 0xF6:	// OR d8
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
								m_fetched_data_lo = m_bus->read(m_pc);
								m_pc++;
								m_a |= m_fetched_data_lo;

								set_flag(FlagType::FL_Z, m_a == 0);
								set_flag(FlagType::FL_N, false);
								set_flag(FlagType::FL_H, false);
								set_flag(FlagType::FL_C, false);

								m_end_opcode = true;
								break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0xF7:	// RST 30H
				{
					in_rst(0x0030);
				}
				break;

				case 0xF8:	// LD HL,SP+r8
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
								m_fetched_data_lo = m_bus->read(m_pc);
								m_pc++;
								break;
							case 3:
							{
								set_flag(FlagType::FL_Z, false);
								set_flag(FlagType::FL_N, false);
								set_flag(FlagType::FL_H, ((m_sp & 0x0F) + (m_fetched_data_lo & 0x0F)) >= 0x10);
								set_flag(FlagType::FL_C, ((m_sp & 0xFF) + (m_fetched_data_lo & 0xFF)) >= 0x100);

								write_register16(RegType::RT_HL, m_sp + static_cast<char>(m_fetched_data_lo));
								m_end_opcode = true;
							}
							break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0xF9:	// LD SP,HL
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
								m_sp = (m_h << 8) | m_l;
								m_end_opcode = true;
								break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0xFA:	// LD A,(a16)
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;
							case 2:
								m_fetched_data_lo = m_bus->read(m_pc);
								m_pc++;
								break;
							case 3:
								m_fetched_data_hi = m_bus->read(m_pc);
								m_pc++;
								m_fetched_address = (m_fetched_data_hi << 8) | m_fetched_data_lo;
								break;
							case 4:
								m_a = m_bus->read(m_fetched_address);
								m_end_opcode = true;
								break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0xFB:	// EI
				{
					if(m_t_cycle == 4)
					{
						m_pc++;
						m_enabling_ime = true;
						m_end_opcode = true;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0xFE:	// CP d8
				{
					if(m_t_cycle == 4)
					{
						switch(m_m_cycle)
						{
							case 1:
								m_pc++;
								break;

							case 2:
								m_fetched_data_lo = m_bus->read(m_pc);
								m_pc++;
								int result = (int)m_a - (int)m_fetched_data_lo;

								set_flag(FlagType::FL_Z, result == 0);
								set_flag(FlagType::FL_N, true);
								set_flag(FlagType::FL_H, ((int)m_a & 0x0F) - ((int)m_fetched_data_lo & 0x0F) < 0);
								set_flag(FlagType::FL_C, result < 0);

								m_end_opcode = true;
								break;
						}
						m_m_cycle++;
						m_t_cycle = 0;
					}
					m_t_cycle++;
				}
				break;

				case 0xFF:	// RST 38H
				{
					in_rst(0x0038);
				}
				break;

				default:
					if(m_errors != nullptr)
					{
						m_errors->report(cpu_unimplemented_opcode(m_opcode), m_opcode);
					}
					return;

			}
		}

		// m_t_cycle must stay in [0,4]: single-m-cycle opcodes leave it at 5
		// on their last t-cycle. Normalise it to the start of the next m-cycle
		// (1, as in the multi-m-cycle idiom "m_t_cycle = 0; m_t_cycle++").
		if(m_t_cycle > 4)
		{
			m_t_cycle = 1;
		}

		// If interruptions are enabled
		if(m_end_opcode && m_ime)
		{
			if(((m_if & static_cast<u8>(InterruptType::IT_VBLANK)) && m_ie & static_cast<u8>(InterruptType::IT_VBLANK)) ||
			   ((m_if & static_cast<u8>(InterruptType::IT_LCD_STAT)) && m_ie & static_cast<u8>(InterruptType::IT_LCD_STAT)) ||
			   ((m_if & static_cast<u8>(InterruptType::IT_TIMER)) && m_ie & static_cast<u8>(InterruptType::IT_TIMER)) ||
			   ((m_if & static_cast<u8>(InterruptType::IT_SERIAL)) && m_ie & static_cast<u8>(InterruptType::IT_SERIAL)) ||
			   ((m_if & static_cast<u8>(InterruptType::IT_JOYPAD)) && m_ie & static_cast<u8>(InterruptType::IT_JOYPAD))
			)
			{
				m_is_halted = false;
				//m_halt_bug = false;
				m_is_interruption_time = true;
				m_ime = false;
				m_m_cycle = 1;
			}
		}
	}
}

void Z80GB::set_flag(FlagType flag, bool value)
{
	switch(flag)
	{
		case FlagType::FL_Z: m_f = Operations::set_bit(m_f, 7, value); break;
		case FlagType::FL_N: m_f = Operations::set_bit(m_f, 6, value); break;
		case FlagType::FL_H: m_f = Operations::set_bit(m_f, 5, value); break;
		case FlagType::FL_C: m_f = Operations::set_bit(m_f, 4, value); break;
	}
}

bool Z80GB::get_flag(FlagType flag)
{
	switch(flag)
	{
		case FlagType::FL_Z: return Operations::get_bit(m_f, 7); break;
		case FlagType::FL_N: return Operations::get_bit(m_f, 6); break;
		case FlagType::FL_H: return Operations::get_bit(m_f, 5); break;
		case FlagType::FL_C: return Operations::get_bit(m_f, 4); break;
	}
	return false;
}

u8 Z80GB::read_register(RegType reg_type)
{
	switch(reg_type)
	{
		case RegType::RT_A: return m_a; break;
		case RegType::RT_F: return m_f; break;
		case RegType::RT_B: return m_b; break;
		case RegType::RT_C: return m_c; break;
		case RegType::RT_D: return m_d; break;
		case RegType::RT_E: return m_e; break;
		case RegType::RT_H: return m_h; break;
		case RegType::RT_L: return m_l; break;
		default: return 0; break;
	}
	return 0;
}

void Z80GB::write_register(RegType reg_type, u8 value)
{
	switch(reg_type)
	{
		case RegType::RT_A: m_a = value; break;
		case RegType::RT_F: m_f = value; break;
		case RegType::RT_B: m_b = value; break;
		case RegType::RT_C: m_c = value; break;
		case RegType::RT_D: m_d = value; break;
		case RegType::RT_E: m_e = value; break;
		case RegType::RT_H: m_h = value; break;
		case RegType::RT_L: m_l = value; break;
		default: break;
	}
}

u16 Z80GB::read_register16(RegType reg_type)
{
	switch(reg_type)
	{
		case RegType::RT_AF: return (static_cast<u16>(m_a) << 8) | m_f; break;
		case RegType::RT_BC: return (static_cast<u16>(m_b) << 8) | m_c; break;
		case RegType::RT_DE: return (static_cast<u16>(m_d) << 8) | m_e; break;
		case RegType::RT_HL: return (static_cast<u16>(m_h) << 8) | m_l; break;
		case RegType::RT_SP: return m_sp; break;
		case RegType::RT_PC: return m_pc; break;
		default: return 0; break;
	}
	return 0;
}

void Z80GB::write_register16(RegType reg_type, u16 value)
{
	switch(reg_type)
	{
		case RegType::RT_AF:
			m_a = (value >> 8) & 0xFF;
			m_f = value & 0xF0;
			break;
		case RegType::RT_BC:
			m_b = (value >> 8) & 0xFF;
			m_c = value & 0xFF;
			break;
		case RegType::RT_DE:
			m_d = (value >> 8) & 0xFF;
			m_e = value & 0xFF;
			break;
		case RegType::RT_HL:
			m_h = (value >> 8) & 0xFF;
			m_l = value & 0xFF;
			break;

		case RegType::RT_SP: m_sp = value; break;
		case RegType::RT_PC: m_pc = value; break;
		default: break;
	}
}

void Z80GB::connect_bus(IBus *bus)
{
	m_bus = bus;
}

void Z80GB::connect_error_reporter(ErrorReporter* reporter)
{
	m_errors = reporter;

	if(m_errors != nullptr && m_interrupt_controller == nullptr)
	{
		m_errors->report(ErrorCode::CPU_INTERRUPT_CONTROLLER_NULL);
	}
}

u8 Z80GB::read_IF() const
{
	// The upper 3 bits of IF are unmapped and read as 1.
	return m_if | 0xE0;
}

void Z80GB::write_IF(u8 val)
{
	m_if = val & 0x1F;
}

u8 Z80GB::read_IE() const
{
	return m_ie;
}

void Z80GB::write_IE(u8 val)
{
	m_ie = val;
}

void Z80GB::request_interrupt(InterruptType IType)
{
	m_if |= static_cast<u8>(IType);
}

void Z80GB::in_ld_r_r(RegType destiny, RegType origin)
{
	if(m_t_cycle == 4)
	{
		write_register(destiny, read_register(origin));
		m_pc++;
		m_end_opcode = true;
	}
	m_t_cycle++;
}

void Z80GB::in_ld_r16_d16(RegType destiny)
{
	if(m_t_cycle == 4)
	{
		switch(m_m_cycle)
		{
			case 1:
				m_pc++;
				break;
			case 2:
				m_fetched_data_lo = m_bus->read(m_pc);
				m_pc++;
				break;
			case 3:
				m_fetched_data_hi = m_bus->read(m_pc);
				m_pc++;
				write_register16(destiny, (m_fetched_data_hi <<8) | m_fetched_data_lo );
				m_end_opcode = true;
				break;
		}
		m_m_cycle++;
		m_t_cycle = 0;
	}
	m_t_cycle++;
}

void Z80GB::in_ld_r_addr(RegType destiny, u16 address)
{
	if(m_t_cycle == 4)
	{
		switch(m_m_cycle)
		{
			case 1:
				m_pc++;
				break;
			case 2:
				m_fetched_data_lo = m_bus->read(address);
				write_register(destiny, m_fetched_data_lo);
				m_end_opcode = true;
				break;
		}
		m_m_cycle++;
		m_t_cycle = 0;
	}
	m_t_cycle++;
}

void Z80GB::in_ld_addr_r(u16 address, RegType origin)
{
	if(m_t_cycle == 4)
	{
		switch(m_m_cycle)
		{
			case 1:
				m_pc++;
				m_fetched_data_lo = read_register(origin);
				break;
			case 2:
				m_bus->write(address, m_fetched_data_lo);
				m_end_opcode = true;
				break;
		}
		m_m_cycle++;
		m_t_cycle = 0;
	}
	m_t_cycle++;
}

void Z80GB::in_ld_r_d8(RegType destiny)
{
	if(m_t_cycle == 4)
	{
		switch(m_m_cycle)
		{
			case 1:
				m_pc++;
				break;
			case 2:
				m_fetched_data_lo = m_bus->read(m_pc);
				m_pc++;
				write_register(destiny, m_fetched_data_lo);
				m_end_opcode = true;
				break;
		}
		m_m_cycle++;
		m_t_cycle = 0;
	}
	m_t_cycle++;
}

void Z80GB::in_ld_d16_r16(RegType origin)
{
	if(m_t_cycle == 4)
	{
		switch(m_m_cycle)
		{
			case 1:
				m_pc++;
				break;
			case 2:
				m_fetched_data_lo = m_bus->read(m_pc);
				m_pc++;
				break;

			case 3:
				m_fetched_data_hi = m_bus->read(m_pc);
				m_fetched_address = (m_fetched_data_hi << 8) | m_fetched_data_lo;
				m_pc++;
				break;

			case 4:
				m_bus->write(m_fetched_address, read_register16(origin) & 0xFF);
				break;

			case 5:
				m_bus->write(m_fetched_address + 1, read_register16(origin) >> 8);
				m_end_opcode = true;
				break;
		}
		m_m_cycle++;
		m_t_cycle = 0;
	}
	m_t_cycle++;
}

void Z80GB::in_inc_r(RegType reg)
{
	if(m_t_cycle == 4)
	{
		m_pc++;
		u8 value = read_register(reg);

		set_flag(FlagType::FL_N, false);
		set_flag(FlagType::FL_Z, ((value + 1) & 0xFF) == 0);
		set_flag(FlagType::FL_H, (((value & 0xF) + 1) & 0x10) == 0x10);

		write_register(reg, (value + 1) & 0xFF);
		m_end_opcode = true;
	}
	m_t_cycle++;
}

void Z80GB::in_inc_r16(RegType reg)
{
	if(m_t_cycle == 4)
	{
		switch(m_m_cycle)
		{
			case 1:
				m_pc++;
				break;

			case 2:
				write_register16(reg, (read_register16(reg) + 1));
				m_end_opcode = true;
				break;
		}
		m_m_cycle++;
		m_t_cycle = 0;
	}
	m_t_cycle++;
}

void Z80GB::in_dec_r(RegType reg)
{
	if(m_t_cycle == 4)
	{
		m_pc++;
		u8 value = read_register(reg);

		set_flag(FlagType::FL_N, true);
		set_flag(FlagType::FL_Z, ((value - 1) & 0xFF) == 0);
		set_flag(FlagType::FL_H, (((value & 0xF) - 1) & 0x10) == 0x10);

		write_register(reg, (value - 1) & 0xFF);
		m_end_opcode = true;
	}
	m_t_cycle++;
}

void Z80GB::in_dec_r16(RegType reg)
{
	if(m_t_cycle == 4)
	{
		switch(m_m_cycle)
		{
			case 1:
				m_pc++;
				break;

			case 2:
				write_register16(reg, (read_register16(reg) - 1));
				m_end_opcode = true;
				break;
		}
		m_m_cycle++;
		m_t_cycle = 0;
	}
	m_t_cycle++;
}

void Z80GB::in_add_r_r(RegType destiny, RegType origin)
{
	if(m_t_cycle == 4)
	{
		m_pc++;

		u16 result = read_register(origin) + read_register(destiny);

		set_flag(FlagType::FL_Z, (result & 0xFF) == 0);
		set_flag(FlagType::FL_N, false);
		set_flag(FlagType::FL_H, ((read_register(destiny) & 0x0F) + (read_register(origin) & 0x0F)) > 0x0F);
		set_flag(FlagType::FL_C, result > 0xFF);

		write_register(destiny, result & 0xFF);
		m_end_opcode = true;
	}
	m_t_cycle++;
}

void Z80GB::in_add_r16_r16(RegType destiny, RegType origin)
{
	if(m_t_cycle == 4)
	{
		switch(m_m_cycle)
		{
			case 1:
				m_pc++;
				break;
			case 2:
			{
				u32 result = read_register16(destiny) + read_register16(origin);

				set_flag(FlagType::FL_N, false);
				set_flag(FlagType::FL_H, ((read_register16(destiny) & 0xFFF) + (read_register16(origin) & 0xFFF)) > 0xFFF);
				set_flag(FlagType::FL_C, result > 0xFFFF);

				write_register16(destiny, result & 0xFFFF);

				m_end_opcode = true;
			}
			break;
		}
		m_m_cycle++;
		m_t_cycle = 0;
	}
	m_t_cycle++;
}

void Z80GB::in_adc_r_r(RegType destiny, RegType origin)
{
	if(m_t_cycle == 4)
	{
		m_pc++;

		u8 val_C = 0;
		if(get_flag(FlagType::FL_C))
		{
			val_C = 1;
		}
		u16 result = read_register(origin) + read_register(destiny) + val_C;

		set_flag(FlagType::FL_Z, (result & 0xFF) == 0);
		set_flag(FlagType::FL_N, false);
		set_flag(FlagType::FL_H, ((read_register(destiny) & 0x0F) + (read_register(origin) & 0x0F) + val_C) > 0x0F);
		set_flag(FlagType::FL_C, result > 0xFF);

		write_register(destiny, result & 0xFF);
		m_end_opcode = true;
	}
	m_t_cycle++;
}

void Z80GB::in_sub_r(RegType origin)
{
	if(m_t_cycle == 4)
	{
		m_pc++;

		u16 result = m_a - read_register(origin);

		set_flag(FlagType::FL_Z, (result & 0xFF) == 0);
		set_flag(FlagType::FL_N, true);
		set_flag(FlagType::FL_H, ((int)m_a & 0x0F) - ((int)read_register(origin) & 0x0F) < 0);
		set_flag(FlagType::FL_C, (int)m_a - int(read_register(origin)) < 0);

		m_a = result & 0xFF;
		m_end_opcode = true;
	}
	m_t_cycle++;
}

void Z80GB::in_sbc_r_r(RegType destiny, RegType origin)
{
	if(m_t_cycle == 4)
	{
		m_pc++;

		u8 val_C = 0;
		if(get_flag(FlagType::FL_C))
		{
			val_C = 1;
		}
		u16 result = read_register(destiny) - read_register(origin) - val_C;

		set_flag(FlagType::FL_Z, (result & 0xFF) == 0);
		set_flag(FlagType::FL_N, true);
		set_flag(FlagType::FL_H, (((int)read_register(destiny) & 0x0F) - ((int)read_register(origin) & 0x0F) - (int)val_C) < 0);
		set_flag(FlagType::FL_C, ((int)read_register(destiny) - (int)read_register(origin) - (int)val_C) < 0);

		write_register(destiny, result & 0xFF);
		m_end_opcode = true;
	}
	m_t_cycle++;
}

void Z80GB::in_jr(JpCond condition)
{
	if(m_t_cycle == 4)
	{
		switch(m_m_cycle)
		{
			case 1:
				m_pc++;
				break;
			case 2:
				m_fetched_data_lo = m_bus->read(m_pc);
				m_pc++;
				if(	(condition == JpCond::JC_C && !get_flag(FlagType::FL_C)) ||
					(condition == JpCond::JC_NC && get_flag(FlagType::FL_C)) ||
					(condition == JpCond::JC_Z && !get_flag(FlagType::FL_Z)) ||
					(condition == JpCond::JC_NZ && get_flag(FlagType::FL_Z))
				)
				{
					m_end_opcode = true;
				}
				break;
			case 3:
				m_pc = m_pc + static_cast<char>(m_fetched_data_lo);
				m_end_opcode = true;
				break;
		}
		m_m_cycle++;
		m_t_cycle = 0;
	}
	m_t_cycle++;
}

void Z80GB::in_jp(JpCond condition)
{
	if(m_t_cycle == 4)
	{
		switch(m_m_cycle)
		{
			case 1:
				m_pc++;
				break;
			case 2:
				m_fetched_data_lo = m_bus->read(m_pc);
				m_pc++;
				break;
			case 3:
				m_fetched_data_hi = m_bus->read(m_pc);
				m_pc++;
				if(	(condition == JpCond::JC_C && !get_flag(FlagType::FL_C)) ||
					(condition == JpCond::JC_NC && get_flag(FlagType::FL_C)) ||
					(condition == JpCond::JC_Z && !get_flag(FlagType::FL_Z)) ||
					(condition == JpCond::JC_NZ && get_flag(FlagType::FL_Z))
				)
				{
					m_end_opcode = true;
				}
				break;
			case 4:
				m_pc = (m_fetched_data_hi << 8) | m_fetched_data_lo;
				m_end_opcode = true;
				break;
		}
		m_m_cycle++;
		m_t_cycle = 0;
	}
	m_t_cycle++;

}

void Z80GB::in_call(JpCond condition)
{
	if(m_t_cycle == 4)
	{
		switch(m_m_cycle)
		{
			case 1:
				m_pc++;
				break;
			case 2:
				m_fetched_data_lo = m_bus->read(m_pc);
				m_pc++;
				break;
			case 3:
				m_fetched_data_hi = m_bus->read(m_pc);
				m_pc++;
				if(	(condition == JpCond::JC_C && !get_flag(FlagType::FL_C)) ||
					(condition == JpCond::JC_NC && get_flag(FlagType::FL_C)) ||
					(condition == JpCond::JC_Z && !get_flag(FlagType::FL_Z)) ||
					(condition == JpCond::JC_NZ && get_flag(FlagType::FL_Z))
				)
				{
					m_end_opcode = true;
				}
				break;
			case 4:
				m_sp--;
				break;
			case 5:
				m_bus->write(m_sp, (m_pc >> 8) & 0xFF);
				m_sp--;
				break;
			case 6:
				m_bus->write(m_sp, m_pc & 0xFF);
				m_pc = (m_fetched_data_hi << 8) | m_fetched_data_lo;
				m_end_opcode = true;
				break;
		}
		m_m_cycle++;
		m_t_cycle = 0;
	}
	m_t_cycle++;
}

void Z80GB::in_ret(JpCond condition)
{
	if(m_t_cycle == 4)
	{
		switch(m_m_cycle)
		{
			case 1:
				m_pc++;
			break;
			case 2:
				if(	(condition == JpCond::JC_C && !get_flag(FlagType::FL_C)) ||
					(condition == JpCond::JC_NC && get_flag(FlagType::FL_C)) ||
					(condition == JpCond::JC_Z && !get_flag(FlagType::FL_Z)) ||
					(condition == JpCond::JC_NZ && get_flag(FlagType::FL_Z))
				)
				{
					m_end_opcode = true;
				}
				break;
			case 3:
				m_fetched_data_lo = m_bus->read(m_sp);
				m_sp++;
				break;
			case 4:
				m_fetched_data_hi = m_bus->read(m_sp);
				m_sp++;
				break;
			case 5:
				m_pc = (m_fetched_data_hi << 8) | m_fetched_data_lo;
				m_end_opcode = true;
				break;
		}
		m_m_cycle++;
		m_t_cycle = 0;
	}
	m_t_cycle++;
}

void Z80GB::in_rst(u16 address)
{
	if(m_t_cycle == 4)
	{
		switch(m_m_cycle)
		{
			case 1:
				m_pc++;
				break;
			case 2:
				m_sp--;
				break;
			case 3:
				m_bus->write(m_sp, (m_pc >> 8) & 0xFF);
				m_sp--;
				break;
			case 4:
				m_bus->write(m_sp, m_pc & 0xFF);
				m_pc = address;
				m_end_opcode = true;
				break;
		}
		m_m_cycle++;
		m_t_cycle = 0;
	}
	m_t_cycle++;
}

void Z80GB::in_push(RegType reg)
{
	if(m_t_cycle == 4)
	{
		switch(m_m_cycle)
		{
			case 1:
				m_pc++;
				break;
			case 2:
				m_sp--;
				break;
			case 3:
				m_bus->write(read_register16(RegType::RT_SP), (read_register16(reg) >> 8) & 0xFF);
				m_sp--;
				break;
			case 4:
				m_bus->write(read_register16(RegType::RT_SP), read_register16(reg) & 0xFF);
				m_end_opcode = true;
				break;
		}
		m_m_cycle++;
		m_t_cycle = 0;
	}
	m_t_cycle++;
}

void Z80GB::in_pop(RegType reg)
{
	if(m_t_cycle == 4)
	{
		switch(m_m_cycle)
		{
			case 1:
				m_pc++;
				break;
			case 2:
				m_fetched_data_lo = m_bus->read(m_sp);
				m_sp++;
				break;
			case 3:
				m_fetched_data_hi = m_bus->read(m_sp);
				m_sp++;
				write_register16(reg, (m_fetched_data_hi << 8) | m_fetched_data_lo);
				m_end_opcode = true;
				break;
		}
		m_m_cycle++;
		m_t_cycle = 0;
	}
	m_t_cycle++;
}

void Z80GB::in_and_r(RegType reg)
{
	if(m_t_cycle == 4)
	{
		m_pc++;
		m_a &= read_register(reg);

		set_flag(FlagType::FL_Z, m_a == 0);
		set_flag(FlagType::FL_N, false);
		set_flag(FlagType::FL_H, true);
		set_flag(FlagType::FL_C, false);

		m_end_opcode = true;
	}
	m_t_cycle++;
}

void Z80GB::in_or_r(RegType reg)
{
	if(m_t_cycle == 4)
	{
		m_pc++;
		m_a |= read_register(reg);

		set_flag(FlagType::FL_Z, m_a == 0);
		set_flag(FlagType::FL_N, false);
		set_flag(FlagType::FL_H, false);
		set_flag(FlagType::FL_C, false);

		m_end_opcode = true;
	}
	m_t_cycle++;
}

void Z80GB::in_xor_r(RegType reg)
{
	if(m_t_cycle == 4)
	{
		m_pc++;
		m_a ^= read_register(reg);

		set_flag(FlagType::FL_Z, m_a == 0);
		set_flag(FlagType::FL_N, false);
		set_flag(FlagType::FL_H, false);
		set_flag(FlagType::FL_C, false);

		m_end_opcode = true;
	}
	m_t_cycle++;
}

void Z80GB::in_cp(RegType reg)
{
	if(m_t_cycle == 4)
	{
		m_pc++;
		int n = static_cast<int>(m_a) - static_cast<int>(read_register(reg));

		set_flag(FlagType::FL_Z, n == 0);
		set_flag(FlagType::FL_N, true);
		set_flag(FlagType::FL_H, ((static_cast<int>(m_a) & 0xF) - (static_cast<int>(read_register(reg) & 0xF))) < 0);
		set_flag(FlagType::FL_C, n < 0);

		m_end_opcode = true;
	}
	m_t_cycle++;
}

void Z80GB::in_cb(RegType reg, u8 data)
{
	u8 value;
	if(reg == RegType::RT_HL)
	{
		// (HL) was already read by the CB dispatch (case 2 / M3); the RMW
		// write phase (case 3 / M4) must not touch the bus again.
		value = m_fetched_data_hi;
	}
	else
	{
		value = read_register(reg);
	}

	u8 bit = (data >> 3) & 0x07;
	u8 bit_op = (data >> 6) & 0x07;

	switch(bit_op)
	{
		case 0:
		{
			switch(bit)
			{
				case 0:
				{
					// RLC
					bool new_c = false;
					u8 result = (value << 1) & 0xFF;

					if((value & (1 << 7)) != 0)
					{
						result |= 1;
						new_c = true;
					}

					set_flag(FlagType::FL_Z, result == 0);
					set_flag(FlagType::FL_N, false);
					set_flag(FlagType::FL_H, false);
					set_flag(FlagType::FL_C, new_c);

					if(reg == RegType::RT_HL)
					{
						m_bus->write(read_register16(reg), result);
					}
					else
					{
						write_register(reg, result);
					}
				}
				break;

				case 1:
				{
					// RRC
					u8 old = value;
					value >>=1;
					value |= (old << 7);

					if(reg == RegType::RT_HL)
					{
						m_bus->write(read_register16(reg), value);
					}
					else
					{
						write_register(reg, value);
					}

					set_flag(FlagType::FL_Z, value == 0);
					set_flag(FlagType::FL_N, false);
					set_flag(FlagType::FL_H, false);
					set_flag(FlagType::FL_C, (old & 1) == 1);
				}
				break;

				case 2:
				{
					// RL
					bool new_c = false;
					u8 old = value;
					value <<= 1;
					if(get_flag(FlagType::FL_C))
					{
						value |= 1;
					}

					if((old & 0x80) != 0)
					{
						new_c = true;
					}

					if(reg == RegType::RT_HL)
					{
						m_bus->write(read_register16(reg), value);
					}
					else
					{
						write_register(reg, value);
					}

					set_flag(FlagType::FL_Z, value == 0);
					set_flag(FlagType::FL_N, false);
					set_flag(FlagType::FL_H, false);
					set_flag(FlagType::FL_C, new_c);
				}
				break;

				case 3:
				{
					// RR
					u8 old = value;
					value >>= 1;

					if(get_flag(FlagType::FL_C))
					{
						value |= (1 << 7);
					}

					if(reg == RegType::RT_HL)
					{
						m_bus->write(read_register16(reg), value);
					}
					else
					{
						write_register(reg, value);
					}

					set_flag(FlagType::FL_Z, value == 0);
					set_flag(FlagType::FL_N, false);
					set_flag(FlagType::FL_H, false);
					set_flag(FlagType::FL_C, (old & 1) != 0);
				}
				break;

				case 4:
				{
					// SLA
					bool new_c = false;
					u8 old = value;
					value <<= 1;

					if((old & 0x80) != 0)
					{
						new_c = true;
					}

					if(reg == RegType::RT_HL)
					{
						m_bus->write(read_register16(reg), value);
					}
					else
					{
						write_register(reg, value);
					}

					set_flag(FlagType::FL_Z, value == 0);
					set_flag(FlagType::FL_N, false);
					set_flag(FlagType::FL_H, false);
					set_flag(FlagType::FL_C, new_c);
				}
				break;

				case 5:
				{
					// SRA
					u8 u = (int8_t) value >> 1;
					if(reg == RegType::RT_HL)
					{
						m_bus->write(read_register16(reg), u);
					}
					else
					{
						write_register(reg, u);
					}

					set_flag(FlagType::FL_Z, u == 0);
					set_flag(FlagType::FL_N, false);
					set_flag(FlagType::FL_H, false);
					set_flag(FlagType::FL_C, (value & 1) != 0);
				}
				break;

				case 6:
				{
					// SWAP
					value = ((value & 0xF0) >> 4) | ((value & 0x0F) << 4);

					if(reg == RegType::RT_HL)
					{
						m_bus->write(read_register16(reg), value);
					}
					else
					{
						write_register(reg, value);
					}

					set_flag(FlagType::FL_Z, value == 0);
					set_flag(FlagType::FL_N, false);
					set_flag(FlagType::FL_H, false);
					set_flag(FlagType::FL_C, false);
				}
				break;

				case 7:
				{
					// SRL
					u8 result = value >> 1;
					if(reg == RegType::RT_HL)
					{
						m_bus->write(read_register16(reg), result);
					}
					else
					{
						write_register(reg, result);
					}

					set_flag(FlagType::FL_Z, result == 0);
					set_flag(FlagType::FL_N, false);
					set_flag(FlagType::FL_H, false);
					set_flag(FlagType::FL_C, (value & 1) != 0);
				}
				break;
			}
		}
		break;

		case 1:
			// BIT
			set_flag(FlagType::FL_Z, !(value & (1 << bit)));
			set_flag(FlagType::FL_N, false);
			set_flag(FlagType::FL_H, true);
		break;

		case 2:
			// RST
			value &= ~(1 << bit);
			if(reg == RegType::RT_HL)
			{
				m_bus->write(read_register16(reg), value);
			}
			else
			{
				write_register(reg, value);
			}
		break;

		case 3:
			// SET
			value |= (1 << bit);
			if(reg == RegType::RT_HL)
			{
				m_bus->write(read_register16(reg), value);
			}
			else
			{
				write_register(reg, value);
			}
		break;
	}
}
