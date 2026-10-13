#ifndef QTREGB_TYPES_H
#define QTREGB_TYPES_H

#include <cstdint>
#include <functional>

namespace qtreGB
{

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

// Cartridge type
enum class CartType : u8
{
	CT_NONE,
	CT_NO_MBC,
	CT_MBC1
};

// Register types
enum class RegType : u8
{
	RT_NONE,
	RT_A,
	RT_F,
	RT_B,
	RT_C,
	RT_D,
	RT_E,
	RT_H,
	RT_L,
	RT_AF,
	RT_BC,
	RT_DE,
	RT_HL,
	RT_SP,
	RT_PC
};

// CPU flags
enum class FlagType : u8
{
	FL_Z,
	FL_N,
	FL_H,
	FL_C
};

// Jump conditions, call, etc...
enum class JpCond : u8
{
	JC_NONE,
	JC_C,
	JC_Z,
	JC_NC,
	JC_NZ
};

// Interrupt type
enum class InterruptType : u8
{
	IT_VBLANK = 1,
	IT_LCD_STAT = 2,
	IT_TIMER = 4,
	IT_SERIAL = 8,
	IT_JOYPAD = 16
};

using InterruptCallback = std::function<void(InterruptType)>;

} // namespace qtreGB

#endif