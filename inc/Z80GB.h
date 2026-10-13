#ifndef QTREGB_Z80GB_H
#define QTREGB_Z80GB_H

#include "types.h"
#include "ibus.h"
#include "icpu_registers.h"
#include "interrupt_controller.h"
#include "tagged_serializer.h"

namespace qtreGB
{

class ErrorReporter;

class Z80GB : public ICPURegisters
{
	public:

		Z80GB(InterruptController* interrupt_controller);

		void tick();
		void reset();

		u8 read_register(RegType reg_type);
		void write_register(RegType reg_type, u8 value);
		u16 read_register16(RegType reg_type);
		void write_register16(RegType reg_type, u16 value);

		void connect_bus(IBus* bus);
		void connect_error_reporter(ErrorReporter* reporter);

		// Interrupt management
		u8 read_IF() const override;
		void write_IF(u8 val) override;
		u8 read_IE() const override;
		void write_IE(u8 val) override;
		void request_interrupt(InterruptType IType);

		// --- State observable from outside the CPU ---
		bool is_end_m_cycle() const;	// m-cycle edge: m_t_cycle % 4 == 0
		bool is_end_opcode() const;
		bool is_interruption_time() const;

		// --- Savestates / access backdoor ---
		// Mirror of the observable registers. It is consumed in the same order
		// as Z80GB::serialize, so it works as a prefix of a snapshot:
		// writing to State and serializing it allows injecting values into the CPU.
		struct State
		{
			u8 a, f, b, c, d, e, h, l;
			u16 pc, sp;

			void serialize(TaggedSerializer& s);
		};

		void serialize(TaggedSerializer& s);

	private:

		// Registers
		u8 m_a;
		u8 m_f;
		u8 m_b;
		u8 m_c;
		u8 m_d;
		u8 m_e;
		u8 m_h;
		u8 m_l;
		u16 m_pc;	// Program counter
		u16 m_sp;	// Stack pointer

		bool m_is_halted;	// Indicates if CPU is halted
		bool m_halt_bug;	// Indicates halt bug state

		bool m_ime; // Interrupt master enabled
		u8 m_if;	// Interrupt Flag
		u8 m_ie;	// Interrupt Enable
		InterruptController* m_interrupt_controller;	// Interrupt controller
		bool m_enabling_ime;	// Indicates if IME must be enabled next cycle
		bool m_is_interruption_time;	// Indicates if we are treating an interruption right now
		bool m_next_m_cycle;	// Temp var to change IME value in next cycle

		u8 m_opcode;	// actual opcode
		u8 m_t_cycle;	// actual t-cycle of the actual opcode
		u8 m_m_cycle;	// actual m-cycle of the actual opcode => 1 m-cycle = 4 t-cycles
		bool m_end_opcode;	// indicates if actual opcode has ended.

		IBus *m_bus = nullptr;	// address bus
		ErrorReporter *m_errors = nullptr;	// central error reporter, not owned

		u16 m_fetched_address;	// just a var to put temporary addresses between ticks
		u8 m_fetched_data_lo;	// just a var to put temporary values between ticks
		u8 m_fetched_data_hi;	// just a var to put temporary values between ticks

		void execute();

		void set_flag(FlagType flag, bool value);
		bool get_flag(FlagType flag);

		void in_ld_r_r(RegType destiny, RegType origin);	// LD instruction, register to register
		void in_ld_r16_d16(RegType destiny);	// LD instruction, inmediate 2 bytes to 16 bits register
		void in_ld_r_addr(RegType destiny, u16 address);	// LD instruction, address content into register
		void in_ld_addr_r(u16 address, RegType origin);	// LD instruction, address content into register
		void in_ld_r_d8(RegType destiny);	// LD instruction, inmediate byte into register
		void in_ld_d16_r16(RegType origin); // LD instruction, register value into address indicated by 2 inmediate bytes.

		void in_inc_r(RegType reg);	// INC instruction, increment register
		void in_inc_r16(RegType reg);	// INC instruction, increment 16-bit register
		void in_dec_r(RegType reg);	// DEC instruction, decrement register
		void in_dec_r16(RegType reg);	// DEC instruction, decrement 16-bit register

		void in_add_r_r(RegType destiny, RegType origin);	// ADD instruccion, from 8 bit register to 8 bit register
		void in_add_r16_r16(RegType destiny, RegType origin);	// ADD instruccion, from 16 bit register to 16 bit register

		void in_adc_r_r(RegType destiny, RegType origin);	// ADC instruccion, add with carry, from 8 bit register to 8 bit register

		void in_sub_r(RegType origin);	// SUB instruction, substract register value from register A, the result is writen into A

		void in_sbc_r_r(RegType destiny, RegType origin);	// SBC instruccion, substract with carry, from 8 bit register to 8 bit register

		void in_jr(JpCond condition = JpCond::JC_NONE);	// JR instruction, jump relative with or without condition to PC +/- inmediate byte
		void in_jp(JpCond condition = JpCond::JC_NONE);	// JP instruction, jump with or without condition to address given by next 2 bytes

		void in_call(JpCond condition = JpCond::JC_NONE); // CALL instruction, jumps to the address given by next 2 inmediate bytes
		void in_ret(JpCond condition = JpCond::JC_NONE);	// RET instruction,
		void in_rst(u16 address);	// RST instruction, jumps to the address given by the opcode unconditionally

		void in_push(RegType reg);	// PUSH instruction

		void in_pop(RegType reg);	// POP instruction

		void in_and_r(RegType reg);	// AND instruction, bitwise with A register

		void in_or_r(RegType reg);	// OR instruction, bitwise with A register
		void in_xor_r(RegType reg);	// XOR instruction, bitwise with A register

		void in_cp(RegType reg);	// CP instruction, flags from subtract a register from register A

		void in_cb(RegType reg, u8 data);	//CB instructions, inside, all the CB instructions logic is managed
};



} // namespace qtreGB
#endif