#ifndef QTREGB_ICPU_REGISTERS_H
#define QTREGB_ICPU_REGISTERS_H

#include "common.h"

class ICPURegisters
{
	public:
		virtual ~ICPURegisters() = default;

		virtual u8 read_IF() const = 0;
		virtual void write_IF(u8 value) = 0;

		virtual u8 read_IE() const = 0;
		virtual void write_IE(u8 value) = 0;
};

#endif
