
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>

#include <cstdlib>
#include <cstdio>

#include "QtreGB.h"

using namespace qtreGB;
using namespace std;

#if 1
	static string serial;
#endif

//static int contador = 0;

QtreGB::QtreGB() :
	m_interrupt_controller(InterruptController()),
	m_cpu(Z80GB(&m_interrupt_controller)),
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
	m_cart.connect_error_reporter(&m_errors);
	m_dma.connect(&m_bus, &m_ppu);
	m_ppu.connect_bus(&m_bus);
	m_ppu.set_interrupt_callback([this](InterruptType bit){ m_interrupt_controller.request_interrupt(bit);});
	m_ppu.set_frame_callback([this](bool value){ set_frame_ready(value);});

	m_cpu.reset();
	m_timer.timer_init();
	m_ppu.ppu_init();
	m_timer.set_interrupt_callback([this](InterruptType bit){ m_interrupt_controller.request_interrupt(bit);});

}

QtreGB::~QtreGB()
{

}

void QtreGB::tick()
{
	if(m_errors.has_error())
	{
		return;
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
	m_timer.begin_cpu_phase();
	m_cpu.tick();

	m_timer.timer_tick();
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
	return m_cart.load_cart(cart) == 0;
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
