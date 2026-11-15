#ifndef QTREGB_CPU_MICRO_H
#define QTREGB_CPU_MICRO_H

#include "types.h"
#include "ibus.h"
#include "icpu_registers.h"
#include "tagged_serializer.h"

namespace qtreGB
{

class ErrorReporter;

// Parallel micro-op CPU skeleton (DocBoy-style).
//
// This class is intentionally not wired into QtreGB yet. It is developed and
// tested in isolation so the current Z80GB remains the default until the port
// passes the full suite (doctest, blargg, mts, PPU acceptance and the m3
// harness).
//
// Model:
//   - one M-cycle = four T-cycles: t0, t1, t2, t3
//   - t0: decode/prefetch + execute the current micro-op
//   - t1: commit a pending write
//   - t2: internal phase
//   - t3: commit a pending read
//   - the last micro-op of every instruction is a fetch of the next opcode
class CpuMicro : public ICPURegisters
{
	private:
		enum class ImeState : u8
		{
			Disabled = 0,
			Requested = 1,
			Pending = 2,
			Enabled = 3
		};

		enum class InterruptState : u8
		{
			None,
			Pending,
			Serving
		};

	public:
		explicit CpuMicro(IBus* bus = nullptr);

		void connect_bus(IBus* bus) { m_bus = bus; }
		void reset();

		// One T-cycle. Four calls are one M-cycle: t0, t1, t2, t3.
		void tick();
		void tick_m_cycle();

		// Observable state (tests and the future adapter).
		u8 get_a() const { return m_a; }
		u8 get_f() const { return m_f; }
		u8 get_b() const { return m_b; }
		u8 get_c() const { return m_c; }
		u8 get_d() const { return m_d; }
		u8 get_e() const { return m_e; }
		u8 get_h() const { return m_h; }
		u8 get_l() const { return m_l; }
		u16 get_pc() const { return m_pc; }
		u16 get_sp() const { return m_sp; }
		u8 get_phase() const { return m_phase; }
		u8 get_opcode() const { return m_opcode; }
		bool is_end_opcode() const { return m_end_opcode; }
		bool is_unimplemented() const { return m_unimplemented; }
		bool get_ime() const { return m_ime == ImeState::Enabled; }
		bool is_halted() const { return m_is_halted; }
		bool is_opcode_implemented(u8 opcode) const { return table()[opcode].count != 0; }
		u8 peek_next_opcode() const;

		u8 read_if() const { return m_if; }
		void write_if(u8 value) { m_if = static_cast<u8>(value & 0x1F); }
		u8 read_ie() const { return m_ie; }
		void write_ie(u8 value) { m_ie = static_cast<u8>(value & 0x1F); }
		void request_interrupt(InterruptType type) { m_if |= static_cast<u8>(type); }

		// --- Compatibility with the Z80GB / QtreGB integration ---
		u8 read_IF() const override { return static_cast<u8>(m_if | 0xE0); }
		void write_IF(u8 value) override { m_if = static_cast<u8>(value & 0x1F); }
		u8 read_IE() const override { return m_ie; }
		void write_IE(u8 value) override { m_ie = static_cast<u8>(value & 0x1F); }

		void reset(bool boot_rom) { reset(); if(boot_rom) { m_pc = 0x0000; } }
		void connect_error_reporter(ErrorReporter*) {}
		bool is_end_m_cycle() const { return m_phase == 0; }
		bool is_interruption_time() const { return m_interrupt_state == InterruptState::Serving; }

		u8 read_register(RegType type) const;
		u16 read_register16(RegType type) const;

		struct State
		{
			u8 a, f, b, c, d, e, h, l;
			u16 pc, sp;

			void serialize(TaggedSerializer& s);
		};

		void serialize(TaggedSerializer& s);

	private:
		struct MicroOp
		{
			void (CpuMicro::*fn)();
		};

		struct Instruction
		{
			MicroOp ops[6];
			u8 count;
		};

		static const Instruction* table();
		static const Instruction* cb_table();

		IBus* m_bus = nullptr;

		u8 m_a = 0x01;
		u8 m_f = 0xB0;
		u8 m_b = 0x00;
		u8 m_c = 0x13;
		u8 m_d = 0x00;
		u8 m_e = 0xD8;
		u8 m_h = 0x01;
		u8 m_l = 0x4D;
		u16 m_pc = 0x0100;
		u16 m_sp = 0xFFFE;

		u8 m_phase = 0;
		bool m_fetching = false;
		bool m_fetching_cb = false;
		u8 m_opcode = 0;
		u8 m_cb_opcode = 0;
		u8 m_io_data = 0;
		bool m_end_opcode = true;
		bool m_unimplemented = false;

		bool m_read_pending = false;
		u16 m_read_addr = 0;

		bool m_write_pending = false;
		u16 m_write_addr = 0;
		u8 m_write_value = 0;

		u8 m_addr_lo = 0;
		u8 m_addr_hi = 0;
		u16 m_push_value = 0;
		u8 m_pop_low = 0;

		ImeState m_ime = ImeState::Disabled;
		u8 m_ie = 0;
		u8 m_if = 0;
		bool m_is_halted = false;
		InterruptState m_interrupt_state = InterruptState::None;
		u8 m_interrupt_remaining = 0;

		const Instruction* m_instruction = nullptr;
		u8 m_microop_index = 0;

		void tick_t0();
		void tick_t1();
		void tick_t2();
		void tick_t3();
		void tick_microop();

		void fetch();
		void read_request(u16 addr);
		void flush_read();
		void write_request(u16 addr, u8 value);
		void flush_write();

		u8 read_reg(u8 reg) const;
		void write_reg(u8 reg, u8 value);
		u16 read_reg16(u8 rr) const;
		void write_reg16(u8 rr, u16 value);
		u16 read_rr16_alu(u8 rr) const;
		void write_rr16_alu(u8 rr, u16 value);
		u16 hl() const { return static_cast<u16>((static_cast<u16>(m_h) << 8) | m_l); }

		// First family: NOP, LD r,r' and LD r,d8 (register-only).
		void nop_fetch();
		void ld_r_r_fetch();
		void ld_r_d8_read();
		void ld_r_d8_fetch();

		// Second family: memory loads/stores.
		void ld_addr_r_write();
		void ld_r_addr_read();
		void ld_r_addr_fetch();

		void ld_addr_d8_read();
		void ld_addr_d8_write();

		void ldh_an_read();
		void ldh_an_write();
		void ldh_r_an_read();
		void ldh_r_an_fetch();

		void ld_ann_lo_read();
		void ld_ann_hi_read();
		void ld_ann_write();
		void ld_ann_read();
		void ld_ann_fetch();

		void push_begin();
		void push_write_hi();
		void push_write_lo();

		void pop_begin();
		void pop_read();
		void pop_fetch();

		// Third family: control flow.
		bool check_condition() const;

		void jp_hl_fetch();
		void jp_read_lo();
		void jp_read_hi();
		void jp_set_pc();
		void jp_cc();

		void jr_read();
		void jr_add();
		void jr_cc();

		void call_read_lo();
		void call_read_hi();
		void call_sp();
		void call_cc();
		void call_write_hi();
		void call_write_lo();

		void ret_read_lo();
		void ret_read_hi();
		void ret_set_pc();
		void reti_set_pc();
		void ret_cc_begin();
		void ret_cc_check();

		void rst_sp();
		void rst_write_hi();
		void rst_write_lo();

		void ld_sp_d16_lo();
		void ld_sp_d16_hi();
		void ld_sp_d16_fetch();
		void ld_sp_hl();

		// Fourth family: CB.
		void set_z(bool value);
		void set_n(bool value);
		void set_h(bool value);
		void set_c(bool value);

		void fetch_cb();
		void cb_prefix();
		u8 cb_rotate(u8 value, bool& carry);
		void cb_rotate_r();
		void cb_rotate_arr();
		void cb_arr_read();
		void cb_bit_r();
		void cb_bit_arr();
		void cb_res_r();
		void cb_res_arr();
		void cb_set_r();
		void cb_set_arr();

		// Remaining instruction families.
		u8 alu_op(u8 operation, u8 value);
		void alu_r_fetch();
		void alu_value_fetch();

		void hl_read();
		void inc_r_fetch();
		void dec_r_fetch();
		void inc_arr_write();
		void dec_arr_write();

		void inc_rr();
		void dec_rr();
		void add_hl_rr();
		void ld_rr_d16_fetch();
		void ld_ann_sp_write_lo();
		void ld_ann_sp_write_hi();

		void add_sp_e_apply();
		void add_sp_e_noop();
		void ld_hl_sp_e_apply();

		void ld_hl_inc_a_write();
		void ld_hl_dec_a_write();
		void ld_a_hl_inc_read();
		void ld_a_hl_dec_read();
		void ld_a_hl_inc_fetch();
		void ld_a_hl_dec_fetch();

		void ldh_c_a_write();
		void ldh_a_c_read();
		void ldh_a_c_fetch();

		void rlca_fetch();
		void rrca_fetch();
		void rla_fetch();
		void rra_fetch();
		void daa_fetch();
		void cpl_fetch();
		void scf_fetch();
		void ccf_fetch();

		void halt_m0();

		// Fifth family: interrupts / ISR.
		void finish_instruction();
		void check_interrupt(u8 phase);
		u8 interrupt_delay(u8 pending, u8 phase) const;
		void serve_interrupt();
		static const Instruction* isr_table();

		void ei_fetch();
		void di_fetch();
		void isr_m0();
		void isr_m1();
		void isr_m2();
		void isr_m3();
		void isr_m4();
};

} // namespace qtreGB

#endif
