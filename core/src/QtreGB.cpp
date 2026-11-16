
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>

#include <cstdlib>
#include <cstdio>
#include <utility>

#include "QtreGB.h"

using namespace qtreGB;
using namespace std;

#if 1
	static string serial;
#endif

//static int contador = 0;

QtreGB::QtreGB() :
	m_interrupt_controller(InterruptController()),
	m_cpu(),
	m_cart(Cart()),
	m_wram(Memory(0x2000)),
	m_hram(Memory(0x80)),
	m_io(IO()),
	m_timer(Timer()),
	m_lcd(LCD()),
	m_ppu(PPU(&m_lcd)),
	m_dma(DMA()),
	m_gamepad(Gamepad()),
	m_bus(Bus(&m_cart, &m_timer, &m_dma, &m_gamepad, &m_wram, &m_hram, &m_io, &m_lcd, &m_ppu, static_cast<ICPURegisters*>(&m_cpu))),
	m_frame_ready(false),
	m_bus_cpu(BusCpuView(&m_bus, &m_ppu, &m_dma))
{
	m_cpu.connect_bus(&m_bus_cpu);
	m_cpu.connect_error_reporter(&m_errors);

	// CpuMicro does not own the interrupt controller: point its IF/IE
	// callbacks at the active CPU.
	m_interrupt_controller.set_IE_read_callback([this]() { return m_cpu.read_IE(); });
	m_interrupt_controller.set_IE_write_callback([this](u8 value) { m_cpu.write_IE(value); });
	m_interrupt_controller.set_IF_read_callback([this]() { return m_cpu.read_IF(); });
	m_interrupt_controller.set_IF_write_callback([this](u8 value) { m_cpu.write_IF(value); });

	m_cart.connect_error_reporter(&m_errors);
	m_dma.connect(&m_bus, &m_ppu);
	m_ppu.connect_bus(&m_bus);
	m_ppu.set_interrupt_callback([this](InterruptType bit){ m_interrupt_controller.request_interrupt(bit);});
	m_ppu.set_frame_callback([this](bool value){ set_frame_ready(value);});

	m_timer.set_interrupt_callback([this](InterruptType bit){ m_interrupt_controller.request_interrupt(bit);});
	reset_machine();
}

QtreGB::~QtreGB()
{

}

void QtreGB::tick()
{
    switch(m_clock.get())
    {
        case Phase::T0: tick_t0(); break;
        case Phase::T1: tick_t1(); break;
        case Phase::T2: tick_t2(); break;
        case Phase::T3: tick_t3(); break;
        default: break;
    }

    m_clock.advance();
}

void QtreGB::cycle()
{
    // Un M-cycle = cuatro T-cycles. tick() sigue siendo un T-cycle para no
    // romper la GUI, el runner ni los tests.
    tick();
    tick();
    tick();
    tick();
}

void QtreGB::tick_t0() { tick_once(); }
void QtreGB::tick_t1() { tick_once(); }
void QtreGB::tick_t2() { tick_once(); }
void QtreGB::tick_t3() { tick_once(); }

void QtreGB::tick_once()
{
	if(std::getenv("QTR_IRQDBG"))
	{
		static int n61 = 0;
		static int n26 = 0;
		static int n2b = 0;
		static int n100 = 0;
		const u16 pc = m_cpu.get_pc();
		const bool show =
			(pc == 0x061D && n61++ < 5) ||
			(pc == 0x0626 && n26++ < 5) ||
			(pc == 0x062B && n2b++ < 5) ||
			(pc == 0x0100 && n100++ < 1);
		if(show)
		{
			std::fprintf(stderr, "CPUEVT pc=%04X dot=%u ly=%u if=%02X ie=%02X\n",
				static_cast<unsigned>(pc), static_cast<unsigned>(m_ppu.debug_dots()),
				static_cast<unsigned>(m_lcd.m_ly),
				static_cast<unsigned>(m_cpu.read_IF()), static_cast<unsigned>(m_cpu.read_IE()));
		}
	}
	if(m_errors.has_error())
	{
		return;
	}

	// Safety net for boot ROMs that jump to the cartridge without writing
	// FF50: reaching 0x0100 unmaps the internal ROM as the real boot ROM does.
	if(m_bus.is_boot_rom_enabled() && m_cpu.get_pc() >= 0x0100)
	{
		m_bus.disable_boot_rom();
	}

	if(m_cpu.is_end_opcode() && !m_cpu.is_interruption_time())
	{
		#if 0
			stringstream buffer;

			//A:00 F:11 B:22 C:33 D:44 E:55 H:66 L:77 SP:8888 PC:9999 PCMEM:AA,BB,CC,DD
			buffer << "A:" << hex << uppercase << setfill('0') << setw(2) << right << unsigned(m_cpu.m_a) << " "
			<< "F:" << hex << uppercase << setfill('0') << setw(2) << right << unsigned(m_cpu.m_f) << " "
			<< "B:" << hex << uppercase << setfill('0') << setw(2) << right << unsigned(m_cpu.m_b) << " "
			<< "C:" << hex << uppercase << setfill('0') << setw(2) << right << unsigned(m_cpu.m_c) << " "
			<< "D:" << hex << uppercase << setfill('0') << setw(2) << right << unsigned(m_cpu.m_d) << " "
			<< "E:" << hex << uppercase << setfill('0') << setw(2) << right << unsigned(m_cpu.m_e) << " "
			<< "H:" << hex << uppercase << setfill('0') << setw(2) << right << unsigned(m_cpu.m_h) << " "
			<< "L:" << hex << uppercase << setfill('0') << setw(2) << right << unsigned(m_cpu.m_l) << " "
			<< "SP:" << hex << uppercase << setfill('0') << setw(4) << right << unsigned(m_cpu.m_sp) << " "
			<< "PC:" << hex << uppercase << setfill('0') << setw(4) << right << unsigned(m_cpu.m_pc) << " "
			<< "PCMEM:" << hex << uppercase << setfill('0') << setw(2) << right << unsigned(m_bus.read(m_cpu.m_pc)) << ","
			<< hex << uppercase << setfill('0') << setw(2) << right << unsigned(m_bus.read(m_cpu.m_pc+1)) << ","
			<< hex << uppercase << setfill('0') << setw(2) << right << unsigned(m_bus.read(m_cpu.m_pc+2)) << ","
			<< hex << uppercase << setfill('0') << setw(2) << right << unsigned(m_bus.read(m_cpu.m_pc+3)) << endl;

			cout << buffer.str();
		#endif
		#if 1
			if(m_io.io_read(0xFF02) == 0x81)
			{
				char c = static_cast<char>(m_io.io_read(0xFF01));
				serial.push_back(c);
				m_io.io_write(0xFF02, 0);
				if(getenv("SERIAL_TRACE"))
				{
					cout << "Result: " << serial << endl;
				}
			}
		#endif
	}
	// Timer: one tick per M-cycle, after the CPU T3.
	m_cpu.tick();
	if(m_clock.get() == Phase::T3)
	{
		m_timer.timer_tick();
	}
	m_ppu.ppu_tick();

	if(m_cpu.is_end_m_cycle())
	{
		m_dma.dma_tick();
	}

	#if 0
		if(m_cpu.m_end_opcode)
		{
			contador++;
			if(contador > 1070852)
			{
				for(int i = 0; i < 0x2000; i = i + 16)
				{
					if(i%256 == 0)
					{
						printf("---- 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n");
					}
					printf("%04X ", i + 0xC000);
					for(int j = 0; j < 16; ++j)
					{
						printf("%02X ", m_wram.read(j+i));
					}
					printf("\n");
				}
				printf("\n");
			}
		}
	#endif


}

bool QtreGB::load_cart(vector<u8> cart)
{
	m_errors.clear();
	serial.clear();
	if(m_cart.load_cart(cart) != 0)
	{
		return false;
	}
	reset_machine();
	return true;
}

bool QtreGB::load_boot_rom(vector<u8> boot_rom)
{
	if(boot_rom.size() != 0x100)
	{
		return false;
	}

	m_boot_rom = std::move(boot_rom);
	m_has_boot_rom = true;
	return true;
}

void QtreGB::reset()
{
	reset_machine();
}

void QtreGB::reset_machine()
{
	m_errors.clear();
	serial.clear();

	if(m_has_boot_rom)
	{
		m_bus.set_boot_rom(m_boot_rom);
	}
	m_bus.set_boot_rom_enabled(m_has_boot_rom);
	m_cpu.reset(m_has_boot_rom);
	m_timer.timer_init();
	m_ppu.ppu_init(m_has_boot_rom);
	m_dma.reset();
	m_clock.reset();
	m_frame_ready = false;
}

bool QtreGB::get_frame_ready()
{
	if(m_frame_ready)
	{
		m_frame_ready = false;
		return true;
	}
	return false;
}

void QtreGB::set_frame_ready(bool value)
{
	m_frame_ready = value;
}
