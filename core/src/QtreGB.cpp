
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>

#include <cstdio>
#include <utility>

#include "QtreGB.h"

using namespace qtreGB;
using namespace std;

namespace
{

constexpr u16 CLOCK_PHASE = 0x0001;
constexpr u16 MEMORY_WRAM = 0x0001;
constexpr u16 MEMORY_HRAM = 0x0002;

} // namespace

//static int contador = 0;

QtreGB::QtreGB() :
	m_interrupt_controller(InterruptController()),
	m_cpu(),
	m_cart(Cart()),
	m_wram(Memory(0x2000)),
	m_hram(Memory(0x80)),
	m_io(IO()),
	m_serial(&m_io),
	m_timer(Timer()),
	m_apu(),
	m_lcd(LCD()),
	m_ppu(PPU(&m_lcd)),
	m_dma(DMA()),
	m_gamepad(Gamepad()),
	m_bus(Bus(&m_cart, &m_timer, &m_dma, &m_gamepad, &m_wram, &m_hram, &m_io, &m_lcd, &m_ppu, &m_apu, static_cast<ICPURegisters*>(&m_cpu))),
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
	m_serial.set_interrupt_callback([this](InterruptType bit){ m_interrupt_controller.request_interrupt(bit);});
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
#ifdef QTR_ENABLE_IRQDBG
	// Development-only CPUEVT trace. Enable by rebuilding with:
	//   make clean && make QTR_IRQDBG=1
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
				static_cast<unsigned>(m_ppu.debug_ly()),
				static_cast<unsigned>(m_cpu.read_IF()), static_cast<unsigned>(m_cpu.read_IE()));
		}
	}
#endif
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
	}
	// The CPU must see the current wave-RAM access window when it runs.
	m_apu.prepare_tick();

	// Timer: one tick per M-cycle, after the CPU T3.
	m_cpu.tick();
	if(m_clock.get() == Phase::T3)
	{
		m_timer.timer_tick();
	}
	m_ppu.ppu_tick();
	m_apu.tick(m_timer.read_div16());
	m_serial.tick();

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

void QtreGB::clear_boot_rom()
{
	m_boot_rom.clear();
	m_has_boot_rom = false;
}

void QtreGB::reset()
{
	reset_machine();
}

void QtreGB::reset_machine()
{
	m_errors.clear();

	if(m_has_boot_rom)
	{
		m_bus.set_boot_rom(m_boot_rom);
	}
	m_bus.set_boot_rom_enabled(m_has_boot_rom);
	m_cpu.reset(m_has_boot_rom);
	m_serial.reset();
	m_timer.timer_init();
	m_apu.reset();
	m_ppu.ppu_init(m_has_boot_rom);
	m_dma.reset();
	m_clock.reset();
	m_frame_ready = false;
}

bool QtreGB::save_state(std::vector<u8>& out) const
{
	const std::vector<u8>& rom = m_cart.rom();
	StateWriter writer(static_cast<u32>(rom.size()), state_hash_rom(rom));
	if(!writer.ok())
	{
		return false;
	}

	writer.begin_chunk(StateChunk::Clock);
	writer.write_u8(CLOCK_PHASE, static_cast<u8>(m_clock.get()));
	writer.end_chunk();

	writer.begin_chunk(StateChunk::Cpu);
	m_cpu.save_state(writer);
	writer.end_chunk();

	writer.begin_chunk(StateChunk::Timer);
	m_timer.save_state(writer);
	writer.end_chunk();

	writer.begin_chunk(StateChunk::Apu);
	m_apu.save_state(writer);
	writer.end_chunk();

	writer.begin_chunk(StateChunk::Memory);
	writer.write_bytes(MEMORY_WRAM, m_wram.data());
	writer.write_bytes(MEMORY_HRAM, m_hram.data());
	writer.end_chunk();

	writer.begin_chunk(StateChunk::Bus);
	m_bus.save_state(writer);
	writer.end_chunk();

	writer.begin_chunk(StateChunk::Io);
	m_io.save_state(writer);
	writer.end_chunk();

	writer.begin_chunk(StateChunk::Serial);
	m_serial.save_state(writer);
	writer.end_chunk();

	writer.begin_chunk(StateChunk::Dma);
	m_dma.save_state(writer);
	writer.end_chunk();

	writer.begin_chunk(StateChunk::Gamepad);
	m_gamepad.save_state(writer);
	writer.end_chunk();

	writer.begin_chunk(StateChunk::Cart);
	m_cart.save_state(writer);
	writer.end_chunk();

	writer.begin_chunk(StateChunk::Ppu);
	m_ppu.save_state(writer);
	writer.end_chunk();

	writer.begin_chunk(StateChunk::Lcd);
	m_lcd.save_state(writer);
	writer.end_chunk();

	writer.finish();
	if(!writer.ok())
	{
		return false;
	}

	out = writer.take();
	return true;
}

StateError QtreGB::load_state(const std::vector<u8>& in)
{
	StateReader reader(in);
	if(!reader.ok())
	{
		return reader.error();
	}

	const std::vector<u8>& rom = m_cart.rom();
	if(!reader.rom_matches(static_cast<u32>(rom.size()), state_hash_rom(rom)))
	{
		return StateError::WrongRom;
	}

	// Transactional load: keep the current blob and restore it if any
	// component rejects the incoming state.
	std::vector<u8> backup;
	if(!save_state(backup))
	{
		return StateError::WriterMisuse;
	}

	const auto apply = [](StateReader& state, QtreGB& machine)
	{
		if(!state.find_chunk(StateChunk::Clock))
		{
			return false;
		}
		u8 clock_phase = 0;
		if(!state.read_u8(CLOCK_PHASE, clock_phase) || clock_phase > 3)
		{
			if(clock_phase > 3)
			{
				state.mark_error(StateError::BadValue);
			}
			return false;
		}
		if(!state.end_chunk())
		{
			return false;
		}
		machine.m_clock.set_value(clock_phase);

		return state.find_chunk(StateChunk::Cpu) &&
		       machine.m_cpu.load_state(state) &&
		       state.end_chunk() &&
		       state.find_chunk(StateChunk::Timer) &&
		       machine.m_timer.load_state(state) &&
		       state.end_chunk() &&
		       state.find_chunk(StateChunk::Apu) &&
		       machine.m_apu.load_state(state) &&
		       state.end_chunk() &&
		       state.find_chunk(StateChunk::Memory) &&
		       [&state, &machine]()
		       {
			       std::vector<u8> wram;
			       std::vector<u8> hram;
			       if(!state.read_bytes(MEMORY_WRAM, wram) ||
			          !state.read_bytes(MEMORY_HRAM, hram))
			       {
				       return false;
			       }
			       if(wram.size() != machine.m_wram.size() ||
			          hram.size() != machine.m_hram.size())
			       {
				       state.mark_error(StateError::BadLength);
				       return false;
			       }
			       if(!state.end_chunk())
			       {
				       return false;
			       }
			       machine.m_wram.data() = std::move(wram);
			       machine.m_hram.data() = std::move(hram);
			       return true;
		       }() &&
		       state.find_chunk(StateChunk::Bus) &&
		       machine.m_bus.load_state(state) &&
		       state.end_chunk() &&
		       state.find_chunk(StateChunk::Io) &&
		       machine.m_io.load_state(state) &&
		       state.end_chunk() &&
		       state.find_chunk(StateChunk::Serial) &&
		       machine.m_serial.load_state(state) &&
		       state.end_chunk() &&
		       state.find_chunk(StateChunk::Dma) &&
		       machine.m_dma.load_state(state) &&
		       state.end_chunk() &&
		       state.find_chunk(StateChunk::Gamepad) &&
		       machine.m_gamepad.load_state(state) &&
		       state.end_chunk() &&
		       state.find_chunk(StateChunk::Cart) &&
		       machine.m_cart.load_state(state) &&
		       state.end_chunk() &&
		       state.find_chunk(StateChunk::Ppu) &&
		       machine.m_ppu.load_state(state) &&
		       state.end_chunk() &&
		       state.find_chunk(StateChunk::Lcd) &&
		       machine.m_lcd.load_state(state) &&
		       state.end_chunk() &&
		       state.finish();
	};

	if(!apply(reader, *this))
	{
		const StateError error = reader.error() == StateError::Ok ?
			StateError::BadValue : reader.error();

		StateReader restore(backup);
		if(!apply(restore, *this))
		{
			// The backup was produced by this build, so this can only fail if
			// the machine was already in an inconsistent state. Report the
			// original load error.
		}
		return error;
	}

	return StateError::Ok;
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
