#ifndef QTREGB_Z80GB_H
#define QTREGB_Z80GB_H

#include "common.h"
#include "ibus.h"
#include "icpu_registers.h"
#include "interrupt_controller.h"

class Z80GB : public ICPURegisters
{
	public:

		// Everything is public, nothing to hide
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

		Z80GB(InterruptController* interrupt_controller);

		void tick();
		void reset();

		u8 read_register(RegType reg_type);
		void write_register(RegType reg_type, u8 value);
		u16 read_register16(RegType reg_type);
		void write_register16(RegType reg_type, u16 value);

		void connect_bus(IBus* bus);

		// Interrupt management
		u8 read_IF() const override;
		void write_IF(u8 val) override;
		u8 read_IE() const override;
		void write_IE(u8 val) override;
		void request_interrupt(InterruptType IType);

	private:

		IBus *m_bus = nullptr;	// address bus

		u16 m_fetched_address;	// just a var to put temporary addresses between ticks
		u8 m_fetched_data_lo;	// just a var to put temporary values between ticks
		u8 m_fetched_data_hi;	// just a var to put temporary values between ticks

		void execute();

		void set_flag(FlagType flag, bool value);
		bool get_flag(FlagType flag);

		void in_ld_r_r(RegType destiny, RegType origin);	// LD instruction, register to register
		void in_ld_r16_d16(RegType destiny);	// LD instruction, inmediate 2 bytes to 16 bits register
		void in_ld_r_addr(RegType destiny, u16 address);	// LD instruction, address content into register
		void in_ld_addr_r(u16 address, RegType origin);	// LD instruction, register value into address pointer
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


#endif
