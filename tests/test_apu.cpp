#include "doctest.h"
#include "apu.h"
#include "state_stream.h"

using namespace qtreGB;

namespace
{

std::vector<u8> save_apu(const APU& apu)
{
	StateWriter writer(0, 0);
	writer.begin_chunk(StateChunk::Apu);
	apu.save_state(writer);
	writer.end_chunk();
	writer.finish();
	REQUIRE(writer.ok());
	return writer.take();
}

bool load_apu(APU& apu, const std::vector<u8>& blob)
{
	StateReader reader(blob);
	if(!reader.ok() || !reader.find_chunk(StateChunk::Apu) ||
	   !apu.load_state(reader) || !reader.end_chunk())
	{
		return false;
	}
	return reader.finish();
}

// Test harness: mimics the Timer's 16-bit divider (increments by 4 at T3).
struct TestApu
{
	APU apu;
	u16 div = 0;
	u8 phase = 0;

	void reset()
	{
		apu.reset();
		div = 0;
		phase = 0;
	}

	void tick()
	{
		if(phase == 3)
		{
			div = static_cast<u16>(div + 4);
		}
		phase = static_cast<u8>((phase + 1) & 0x03);
		apu.tick(div);
	}

	void write(u16 address, u8 value) { apu.write(address, value); }
	u8 read(u16 address) { return apu.read(address); }
	bool is_powered() const { return apu.is_powered(); }
	u8 channel_output(APU::Channel channel) const { return apu.channel_output(channel); }
	bool is_channel_enabled(APU::Channel channel) const { return apu.is_channel_enabled(channel); }
	u8 channel_volume(APU::Channel channel) const { return apu.channel_volume(channel); }
	u16 channel_frequency(APU::Channel channel) const { return apu.channel_frequency(channel); }
	size_t available_frames() const { return apu.available_frames(); }
	size_t read_samples(int16_t* dst, size_t frames) { return apu.read_samples(dst, frames); }
};
}

TEST_CASE("APU: reset leaves the APU powered off")
{
	TestApu apu;
	apu.reset();

	CHECK(apu.is_powered() == false);
	CHECK(apu.read(0xFF26) == 0x70);
	CHECK(apu.read(0xFF10) == 0x80);
	CHECK(apu.read(0xFF30) == 0x00);
}

TEST_CASE("APU: channel writes are ignored while powered off")
{
	TestApu apu;
	apu.reset();

	apu.write(0xFF11, 0xAB);
	apu.write(0xFF24, 0x77);
	apu.write(0xFF30, 0x5A);

	CHECK(apu.read(0xFF11) == 0x3F);
	CHECK(apu.read(0xFF24) == 0x00);
	// Wave RAM is accessible even with the APU powered off.
	CHECK(apu.read(0xFF30) == 0x5A);
}

TEST_CASE("APU: NR52 bit 7 powers the register file on")
{
	TestApu apu;
	apu.reset();

	apu.write(0xFF26, 0x80);
	CHECK(apu.is_powered() == true);
	CHECK(apu.read(0xFF26) == 0xF0);

	apu.write(0xFF11, 0xAB);
	apu.write(0xFF12, 0xF3);
	apu.write(0xFF25, 0xFF);

	CHECK(apu.read(0xFF11) == 0xBF);
	CHECK(apu.read(0xFF12) == 0xF3);
	CHECK(apu.read(0xFF25) == 0xFF);
}

TEST_CASE("APU: power off clears the channel registers but keeps wave RAM")
{
	TestApu apu;
	apu.reset();

	apu.write(0xFF26, 0x80);
	apu.write(0xFF11, 0xAB);
	apu.write(0xFF24, 0x77);
	apu.write(0xFF30, 0x5A);

	apu.write(0xFF26, 0x00);

	CHECK(apu.is_powered() == false);
	CHECK(apu.read(0xFF26) == 0x70);
	CHECK(apu.read(0xFF11) == 0x3F);
	CHECK(apu.read(0xFF24) == 0x00);
	CHECK(apu.read(0xFF30) == 0x5A);
}

TEST_CASE("APU: NR52 lower bits are read-only")
{
	TestApu apu;
	apu.reset();

	apu.write(0xFF26, 0x8F);
	CHECK(apu.read(0xFF26) == 0xF0);
}

TEST_CASE("APU: unused register bits read as one")
{
	TestApu apu;
	apu.reset();
	apu.write(0xFF26, 0x80);

	apu.write(0xFF10, 0xFF);
	CHECK(apu.read(0xFF10) == 0xFF);	// bit 7 unused

	apu.write(0xFF14, 0xFF);
	CHECK(apu.read(0xFF14) == 0xFF);

	apu.write(0xFF1A, 0xFF);
	CHECK(apu.read(0xFF1A) == 0xFF);	// bits 6-0 unused

	apu.write(0xFF1C, 0xFF);
	CHECK(apu.read(0xFF1C) == 0xFF);	// bits 7 and 4-0 unused

	apu.write(0xFF20, 0xFF);
	CHECK(apu.read(0xFF20) == 0xFF);	// bits 7-6 unused

	apu.write(0xFF22, 0xFF);
	CHECK(apu.read(0xFF22) == 0xFF);	// shift/width/divider

	apu.write(0xFF23, 0xFF);
	CHECK(apu.read(0xFF23) == 0xFF);
}

TEST_CASE("APU: tick keeps the register file stable")
{
	TestApu apu;
	apu.reset();
	apu.write(0xFF26, 0x80);
	apu.write(0xFF11, 0x42);
	apu.write(0xFF13, 0x34);

	for(int i = 0; i < 20000; ++i)
	{
		apu.tick();
	}

	CHECK(apu.read(0xFF11) == 0x7F);
	CHECK(apu.read(0xFF13) == 0xFF);
	CHECK(apu.read(0xFF26) == 0xF0);
}

namespace
{
void power_on(TestApu& apu)
{
	apu.reset();
	apu.write(0xFF26, 0x80);
	apu.write(0xFF24, 0x77);	// master volume
	apu.write(0xFF25, 0xFF);	// all channels to both sides
}

void trigger_ch1(TestApu& apu, u8 duty, u8 volume, u16 frequency)
{
	apu.write(0xFF11, static_cast<u8>(duty << 6));
	apu.write(0xFF12, static_cast<u8>(volume << 4));
	apu.write(0xFF13, static_cast<u8>(frequency & 0xFF));
	apu.write(0xFF14, static_cast<u8>(0x80 | ((frequency >> 8) & 0x07)));
}
}

TEST_CASE("APU: CH1 trigger requires an enabled DAC")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF12, 0x00);	// DAC off
	apu.write(0xFF14, 0x87);	// trigger
	CHECK(apu.channel_output(APU::CH1) == 0);
	CHECK((apu.read(0xFF26) & 0x01) == 0x00);

	apu.write(0xFF12, 0xF0);	// DAC on, volume 15
	apu.write(0xFF14, 0x87);	// trigger again
	CHECK((apu.read(0xFF26) & 0x01) == 0x01);
}

TEST_CASE("APU: CH1 duty step advances at the channel period")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF11, 0xC0);	// duty 3 (75 %)
	apu.write(0xFF12, 0xF0);	// volume 15
	apu.write(0xFF13, 0xFF);
	apu.write(0xFF14, 0x87);	// frequency = 0x7FF -> 4 T-cycles per step

	CHECK(apu.channel_output(APU::CH1) == 0);	// step 0 is low

	// The trigger delay holds the first duty step for three extra M-cycles
	// before the period timer starts.
	for(int i = 0; i < 12; ++i)
	{
		apu.tick();
	}
	CHECK(apu.channel_output(APU::CH1) == 0);	// still step 0

	const u8 expected[8] = {15, 15, 15, 15, 15, 15, 0, 0};
	for(int step = 1; step <= 8; ++step)
	{
		for(int i = 0; i < 4; ++i)
		{
			apu.tick();
		}
		CHECK(apu.channel_output(APU::CH1) == expected[step - 1]);
	}
}

TEST_CASE("APU: CH1 period comes from the frequency registers")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF11, 0x80);	// duty 2 (50 %): step 0 is high
	apu.write(0xFF12, 0x90);	// volume 9
	apu.write(0xFF13, 0x00);
	apu.write(0xFF14, 0x80);	// frequency = 0 -> 8192 T-cycles per step

	CHECK(apu.channel_output(APU::CH1) == 9);

	// Trigger delay: the first duty step is held for three M-cycles.
	for(int i = 0; i < 12; ++i)
	{
		apu.tick();
	}
	CHECK(apu.channel_output(APU::CH1) == 9);

	for(int i = 0; i < 8191; ++i)
	{
		apu.tick();
	}
	CHECK(apu.channel_output(APU::CH1) == 9);	// still on step 0

	apu.tick();	// 8192nd tick advances to step 1
	CHECK(apu.channel_output(APU::CH1) == 0);	// step 1 is low
}

TEST_CASE("APU: CH1 volume is reloaded on trigger")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF11, 0x80);	// duty 2: step 0 is high
	apu.write(0xFF12, 0x50);	// volume 5
	apu.write(0xFF13, 0xFF);
	apu.write(0xFF14, 0x87);	// frequency = 0x7FF, trigger

	CHECK(apu.channel_output(APU::CH1) == 5);

	apu.write(0xFF12, 0x90);	// volume 9 without trigger
	CHECK(apu.channel_output(APU::CH1) == 5);

	apu.write(0xFF14, 0x87);	// trigger reloads the volume
	CHECK(apu.channel_volume(APU::CH1) == 9);
}

TEST_CASE("APU: NRX2 zombie writes follow the CGB-E glitch table")
{
	// First row of SameSuite channel_1_nrx2_glitch: previous NRX2, new NRX2
	// and the resulting channel volume.
	struct Case
	{
		u8 previous;
		u8 next;
		u8 volume;
	};

	const Case cases[] = {
		{0x20, 0x20, 2},
		{0x08, 0x08, 1},
		{0x22, 0x22, 2},
		{0x32, 0x32, 3},
		{0x20, 0x08, 13},
		{0x08, 0x47, 15},
		{0x22, 0x08, 12},
		{0x08, 0x32, 15},
	};

	for(const Case& test : cases)
	{
		TestApu apu;
		power_on(apu);

		apu.write(0xFF13, 0x8F);
		apu.write(0xFF11, 0xC0);
		apu.write(0xFF12, test.previous);
		apu.write(0xFF14, 0x87);	// trigger
		apu.write(0xFF12, test.next);	// zombie-mode glitch

		CHECK(apu.channel_volume(APU::CH1) == test.volume);
	}
}

TEST_CASE("APU: writing NR11 changes the duty immediately")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF11, 0x00);	// duty 0: step 0 is low
	apu.write(0xFF12, 0xF0);	// volume 15
	apu.write(0xFF13, 0xFF);
	apu.write(0xFF14, 0x87);
	CHECK(apu.channel_output(APU::CH1) == 0);

	apu.write(0xFF11, 0x80);	// duty 2: step 0 is high
	CHECK(apu.channel_output(APU::CH1) == 15);
}

TEST_CASE("APU: turning the CH1 DAC off disables the channel")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF11, 0x80);
	apu.write(0xFF12, 0xF0);
	apu.write(0xFF13, 0xFF);
	apu.write(0xFF14, 0x87);
	CHECK((apu.read(0xFF26) & 0x01) == 0x01);

	apu.write(0xFF12, 0x00);
	CHECK(apu.channel_output(APU::CH1) == 0);
	CHECK((apu.read(0xFF26) & 0x01) == 0x00);
}

TEST_CASE("APU: power off disables CH1")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF11, 0x80);
	apu.write(0xFF12, 0xF0);
	apu.write(0xFF13, 0xFF);
	apu.write(0xFF14, 0x87);
	CHECK((apu.read(0xFF26) & 0x01) == 0x01);

	apu.write(0xFF26, 0x00);
	CHECK(apu.channel_output(APU::CH1) == 0);
	CHECK(apu.read(0xFF26) == 0x70);
}

TEST_CASE("APU: the mixer produces PCM samples for CH1")
{
	TestApu apu;
	power_on(apu);
	trigger_ch1(apu, 2, 15, 0x7FF);

	CHECK(apu.available_frames() == 0);
	for(int i = 0; i < 1000; ++i)
	{
		apu.tick();
	}
	REQUIRE(apu.available_frames() > 0);

	int16_t buffer[2048];
	const size_t frames = apu.read_samples(buffer, 1024);
	CHECK(frames > 0);

	bool non_zero = false;
	for(size_t i = 0; i < frames * 2; ++i)
	{
		if(buffer[i] != 0)
		{
			non_zero = true;
		}
	}
	CHECK(non_zero);
	CHECK(apu.available_frames() == 0);
}

TEST_CASE("APU: read_samples is bounded by the available frames")
{
	TestApu apu;
	power_on(apu);
	trigger_ch1(apu, 2, 15, 0x7FF);

	for(int i = 0; i < 10000; ++i)
	{
		apu.tick();
	}

	const size_t available = apu.available_frames();
	REQUIRE(available > 32);

	int16_t buffer[64];	// 32 stereo frames
	CHECK(apu.read_samples(buffer, 32) == 32);
	CHECK(apu.available_frames() == available - 32);
}

TEST_CASE("APU: mixer is silent while no channel is active")
{
	TestApu apu;
	power_on(apu);

	for(int i = 0; i < 1000; ++i)
	{
		apu.tick();
	}
	REQUIRE(apu.available_frames() > 0);

	int16_t buffer[2048];
	const size_t frames = apu.read_samples(buffer, 1024);
	for(size_t i = 0; i < frames * 2; ++i)
	{
		CHECK(buffer[i] == 0);
	}
}

TEST_CASE("APU: resampling targets the configured sample rate")
{
	TestApu apu;
	power_on(apu);

	for(int i = 0; i < 48000; ++i)
	{
		apu.tick();
	}

	CHECK(apu.available_frames() == 549);	// 48000 * 48000 / 4194304
}

void trigger_ch2(TestApu& apu, u8 duty, u8 volume, u16 frequency)
{
	apu.write(0xFF16, static_cast<u8>(duty << 6));
	apu.write(0xFF17, static_cast<u8>(volume << 4));
	apu.write(0xFF18, static_cast<u8>(frequency & 0xFF));
	apu.write(0xFF19, static_cast<u8>(0x80 | ((frequency >> 8) & 0x07)));
}

TEST_CASE("APU: CH2 trigger requires an enabled DAC")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF17, 0x00);	// DAC off
	apu.write(0xFF19, 0x87);	// trigger
	CHECK(apu.channel_output(APU::CH2) == 0);
	CHECK((apu.read(0xFF26) & 0x02) == 0x00);

	apu.write(0xFF17, 0xF0);	// DAC on, volume 15
	apu.write(0xFF19, 0x87);	// trigger again
	CHECK((apu.read(0xFF26) & 0x02) == 0x02);
}

TEST_CASE("APU: CH2 duty step advances at the channel period")
{
	TestApu apu;
	power_on(apu);
	trigger_ch2(apu, 3, 15, 0x7FF);	// duty 3, frequency = 0x7FF

	CHECK(apu.channel_output(APU::CH2) == 0);	// step 0 is low

	// The trigger delay holds the first duty step for three extra M-cycles
	// before the period timer starts.
	for(int i = 0; i < 12; ++i)
	{
		apu.tick();
	}
	CHECK(apu.channel_output(APU::CH2) == 0);	// still step 0

	const u8 expected[8] = {15, 15, 15, 15, 15, 15, 0, 0};
	for(int step = 1; step <= 8; ++step)
	{
		for(int i = 0; i < 4; ++i)
		{
			apu.tick();
		}
		CHECK(apu.channel_output(APU::CH2) == expected[step - 1]);
	}
}

TEST_CASE("APU: NR52 reports both pulse channels")
{
	TestApu apu;
	power_on(apu);

	trigger_ch1(apu, 2, 15, 0x7FF);
	CHECK((apu.read(0xFF26) & 0x03) == 0x01);

	trigger_ch2(apu, 2, 15, 0x7FF);
	CHECK((apu.read(0xFF26) & 0x03) == 0x03);

	apu.write(0xFF26, 0x00);
	CHECK((apu.read(0xFF26) & 0x03) == 0x00);
}

TEST_CASE("APU: mixer combines both pulse channels")
{
	TestApu apu;
	power_on(apu);

	trigger_ch1(apu, 2, 15, 0);	// duty 2, step 0 is high
	trigger_ch2(apu, 2, 15, 0);

	for(int i = 0; i < 88; ++i)
	{
		apu.tick();
	}

	int16_t buffer[2];
	REQUIRE(apu.read_samples(buffer, 1) == 1);
	CHECK(buffer[0] == -16000);	// (-8) + (-8), 1000 per unit
	CHECK(buffer[1] == -16000);
}

TEST_CASE("APU: NR51 pans the two pulse channels")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF25, 0x12);	// CH1 left, CH2 right
	trigger_ch1(apu, 2, 15, 0);	// centered -8
	trigger_ch2(apu, 2, 5, 0);	// centered +2

	for(int i = 0; i < 88; ++i)
	{
		apu.tick();
	}

	int16_t buffer[2];
	REQUIRE(apu.read_samples(buffer, 1) == 1);
	CHECK(buffer[0] == -8000);
	CHECK(buffer[1] == 2000);
}

TEST_CASE("APU: NR50 scales the mixed output")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF24, 0x00);	// both sides at 1/8 volume
	trigger_ch1(apu, 2, 15, 0);
	trigger_ch2(apu, 2, 15, 0);

	for(int i = 0; i < 88; ++i)
	{
		apu.tick();
	}

	int16_t buffer[2];
	REQUIRE(apu.read_samples(buffer, 1) == 1);
	CHECK(buffer[0] == -2000);
	CHECK(buffer[1] == -2000);
}

namespace
{
void trigger_ch3(TestApu& apu, u8 level, u16 frequency)
{
	apu.write(0xFF1A, 0x80);	// DAC on
	apu.write(0xFF1C, static_cast<u8>((level & 0x03) << 5));
	apu.write(0xFF1D, static_cast<u8>(frequency & 0xFF));
	apu.write(0xFF1E, static_cast<u8>(0x80 | ((frequency >> 8) & 0x07)));
}
}

TEST_CASE("APU: CH3 trigger requires an enabled DAC")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF1A, 0x00);	// DAC off
	apu.write(0xFF1E, 0x87);	// trigger
	CHECK(apu.channel_output(APU::CH3) == 0);
	CHECK((apu.read(0xFF26) & 0x04) == 0x00);

	apu.write(0xFF1A, 0x80);	// DAC on
	apu.write(0xFF1E, 0x87);	// trigger again
	CHECK((apu.read(0xFF26) & 0x04) == 0x04);
}

TEST_CASE("APU: CH3 plays wave RAM nibbles")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF30, 0x12);
	apu.write(0xFF31, 0x34);
	trigger_ch3(apu, 1, 0x7FF);	// level 1, 2 T-cycles per sample

	CHECK(apu.channel_output(APU::CH3) == 0);	// sample buffer starts at 0

	for(int i = 0; i < 6; ++i)	// 3 odd ticks of trigger delay
	{
		apu.tick();
	}
	apu.tick();
	apu.tick();
	CHECK(apu.channel_output(APU::CH3) == 2);	// index 1: lower nibble of FF30

	apu.tick();
	apu.tick();
	CHECK(apu.channel_output(APU::CH3) == 3);	// index 2: upper nibble of FF31

	apu.tick();
	apu.tick();
	CHECK(apu.channel_output(APU::CH3) == 4);	// index 3: lower nibble of FF31
}

TEST_CASE("APU: CH3 output level shifts the sample")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF30, 0x0F);	// lower nibble = 15
	trigger_ch3(apu, 1, 0x7FF);
	for(int i = 0; i < 6; ++i)	// 3 odd ticks of trigger delay
	{
		apu.tick();
	}
	apu.tick();
	apu.tick();
	CHECK(apu.channel_output(APU::CH3) == 15);

	apu.write(0xFF1C, 0x40);	// 50 %
	CHECK(apu.channel_output(APU::CH3) == 7);

	apu.write(0xFF1C, 0x60);	// 25 %
	CHECK(apu.channel_output(APU::CH3) == 3);

	apu.write(0xFF1C, 0x00);	// mute
	CHECK(apu.channel_output(APU::CH3) == 0);

	apu.write(0xFF1C, 0x20);	// 100 %
	CHECK(apu.channel_output(APU::CH3) == 15);
}

TEST_CASE("APU: turning the CH3 DAC off disables the channel")
{
	TestApu apu;
	power_on(apu);
	trigger_ch3(apu, 1, 0x7FF);
	CHECK((apu.read(0xFF26) & 0x04) == 0x04);

	apu.write(0xFF1A, 0x00);
	CHECK(apu.channel_output(APU::CH3) == 0);
	CHECK((apu.read(0xFF26) & 0x04) == 0x00);
}

TEST_CASE("APU: power off disables CH3")
{
	TestApu apu;
	power_on(apu);
	trigger_ch3(apu, 1, 0x7FF);
	CHECK((apu.read(0xFF26) & 0x04) == 0x04);

	apu.write(0xFF26, 0x00);
	CHECK(apu.channel_output(APU::CH3) == 0);
	CHECK((apu.read(0xFF26) & 0x04) == 0x00);
}

TEST_CASE("APU: mixer includes CH3")
{
	TestApu apu;
	power_on(apu);

	for(int i = 0; i < 16; ++i)
	{
		apu.write(static_cast<u16>(0xFF30 + i), 0xFF);
	}
	trigger_ch3(apu, 1, 0x7FF);

	for(int i = 0; i < 88; ++i)
	{
		apu.tick();
	}

	int16_t buffer[2];
	REQUIRE(apu.read_samples(buffer, 1) == 1);
	CHECK(buffer[0] == -8000);	// sample 15 -> centered -8
	CHECK(buffer[1] == -8000);
}

namespace
{
void trigger_ch4(TestApu& apu, u8 volume, u8 nr43)
{
	apu.write(0xFF21, static_cast<u8>(volume << 4));
	apu.write(0xFF22, nr43);
	apu.write(0xFF23, 0x80);	// trigger
}
}

TEST_CASE("APU: CH4 trigger requires an enabled DAC")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF21, 0x00);	// DAC off
	apu.write(0xFF22, 0x00);
	apu.write(0xFF23, 0x80);	// trigger
	CHECK(apu.channel_output(APU::CH4) == 0);
	CHECK((apu.read(0xFF26) & 0x08) == 0x00);

	apu.write(0xFF21, 0xF0);	// DAC on, volume 15
	apu.write(0xFF23, 0x80);	// trigger again
	CHECK(apu.channel_output(APU::CH4) == 15);
	CHECK((apu.read(0xFF26) & 0x08) == 0x08);
}

TEST_CASE("APU: CH4 LFSR produces both output levels")
{
	TestApu apu;
	power_on(apu);
	trigger_ch4(apu, 15, 0x00);	// 15-bit LFSR, 8 T-cycles per step

	bool low = false;
	bool high = false;
	for(int step = 0; step < 200; ++step)
	{
		if(apu.channel_output(APU::CH4) == 0)
		{
			low = true;
		}
		else
		{
			high = true;
		}

		for(int i = 0; i < 8; ++i)
		{
			apu.tick();
		}
	}

	CHECK(low);
	CHECK(high);
}

TEST_CASE("APU: CH4 clock shift 14 stops the LFSR")
{
	TestApu apu;
	power_on(apu);
	trigger_ch4(apu, 15, 0xE0);	// shift 14

	CHECK(apu.channel_output(APU::CH4) == 15);
	for(int i = 0; i < 1000; ++i)
	{
		apu.tick();
	}
	CHECK(apu.channel_output(APU::CH4) == 15);
}

TEST_CASE("APU: turning the CH4 DAC off disables the channel")
{
	TestApu apu;
	power_on(apu);
	trigger_ch4(apu, 15, 0x00);
	CHECK((apu.read(0xFF26) & 0x08) == 0x08);

	apu.write(0xFF21, 0x00);
	CHECK(apu.channel_output(APU::CH4) == 0);
	CHECK((apu.read(0xFF26) & 0x08) == 0x00);
}

TEST_CASE("APU: NR52 reports CH4 and power off disables it")
{
	TestApu apu;
	power_on(apu);
	trigger_ch4(apu, 15, 0x00);
	CHECK((apu.read(0xFF26) & 0x08) == 0x08);

	apu.write(0xFF26, 0x00);
	CHECK((apu.read(0xFF26) & 0x08) == 0x00);
	CHECK(apu.channel_output(APU::CH4) == 0);
}

TEST_CASE("APU: NR43 keeps all eight bits")
{
	TestApu apu;
	power_on(apu);
	apu.write(0xFF22, 0xA5);
	CHECK(apu.read(0xFF22) == 0xA5);
}

TEST_CASE("APU: mixer includes CH4")
{
	TestApu apu;
	power_on(apu);
	trigger_ch4(apu, 15, 0x00);

	for(int i = 0; i < 88; ++i)
	{
		apu.tick();
	}

	int16_t buffer[2];
	REQUIRE(apu.read_samples(buffer, 1) == 1);
	CHECK(buffer[0] == -8000);	// output 15 -> centered -8
	CHECK(buffer[1] == -8000);
}

TEST_CASE("APU: CH1 length timer disables the channel at 256 Hz")
{
	TestApu apu;
	power_on(apu);
	// Consume the power-on event; with div_apu odd, enabling the length
	// timer does not clock it.
	apu.tick();

	apu.write(0xFF11, 0x3F);	// length = 1
	apu.write(0xFF12, 0xF0);	// DAC on, envelope off
	apu.write(0xFF14, 0xC0);	// trigger + length enable

	CHECK(apu.is_channel_enabled(APU::CH1));

	for(int i = 0; i < 8190; ++i)
	{
		apu.tick();
	}
	CHECK(apu.is_channel_enabled(APU::CH1));	// first length clock at +8191

	apu.tick();
	CHECK(apu.is_channel_enabled(APU::CH1) == false);
	CHECK((apu.read(0xFF26) & 0x01) == 0x00);
}

TEST_CASE("APU: length is clocked every two frame-sequencer steps")
{
	TestApu apu;
	power_on(apu);
	// Consume the power-on event; with div_apu odd, enabling the length
	// timer does not clock it.
	apu.tick();

	apu.write(0xFF11, 0x3E);	// length = 2
	apu.write(0xFF12, 0xF0);
	apu.write(0xFF14, 0xC0);

	for(int i = 0; i < 8191; ++i)
	{
		apu.tick();
	}
	CHECK(apu.is_channel_enabled(APU::CH1));	// 2 -> 1 at the first length step

	for(int i = 0; i < 16383; ++i)
	{
		apu.tick();
	}
	CHECK(apu.is_channel_enabled(APU::CH1));	// the next step does not clock

	apu.tick();
	CHECK(apu.is_channel_enabled(APU::CH1) == false);	// 1 -> 0
}

TEST_CASE("APU: length timer is ignored when disabled")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF11, 0x3F);	// length = 1
	apu.write(0xFF12, 0xF0);
	apu.write(0xFF14, 0x80);	// trigger only

	for(int i = 0; i < 20000; ++i)
	{
		apu.tick();
	}
	CHECK(apu.is_channel_enabled(APU::CH1));
}

TEST_CASE("APU: trigger reloads an expired length timer")
{
	TestApu apu;
	power_on(apu);
	apu.tick();	// consume the power-on event

	apu.write(0xFF11, 0x3F);	// length = 1
	apu.write(0xFF12, 0xF0);
	apu.write(0xFF14, 0xC0);	// trigger + length enable

	for(int i = 0; i < 8192; ++i)
	{
		apu.tick();
	}
	CHECK(apu.is_channel_enabled(APU::CH1) == false);

	apu.write(0xFF14, 0xC0);	// length is 0 -> reload to 64
	CHECK(apu.is_channel_enabled(APU::CH1));

	for(int i = 0; i < 8192; ++i)
	{
		apu.tick();
	}
	CHECK(apu.is_channel_enabled(APU::CH1));	// 64 -> 63
}

TEST_CASE("APU: CH3 length timer disables the channel")
{
	TestApu apu;
	power_on(apu);
	// Consume the power-on event; with div_apu odd, enabling the length
	// timer does not clock it.
	apu.tick();

	apu.write(0xFF1A, 0x80);
	apu.write(0xFF1B, 0xFF);	// length = 1
	apu.write(0xFF1E, 0xC0);	// trigger + length enable

	CHECK(apu.is_channel_enabled(APU::CH3));
	for(int i = 0; i < 8190; ++i)
	{
		apu.tick();
	}
	CHECK(apu.is_channel_enabled(APU::CH3));

	apu.tick();
	CHECK(apu.is_channel_enabled(APU::CH3) == false);
}

TEST_CASE("APU: CH4 length timer disables the channel")
{
	TestApu apu;
	power_on(apu);
	// Consume the power-on event; with div_apu odd, enabling the length
	// timer does not clock it.
	apu.tick();

	apu.write(0xFF20, 0x3F);	// length = 1
	apu.write(0xFF21, 0xF0);
	apu.write(0xFF22, 0x00);
	apu.write(0xFF23, 0xC0);	// trigger + length enable

	CHECK(apu.is_channel_enabled(APU::CH4));
	for(int i = 0; i < 8190; ++i)
	{
		apu.tick();
	}
	CHECK(apu.is_channel_enabled(APU::CH4));

	apu.tick();
	CHECK(apu.is_channel_enabled(APU::CH4) == false);
}

TEST_CASE("APU: CH1 envelope decreases at 64 Hz")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF12, 0xF1);	// volume 15, decrease, period 1
	apu.write(0xFF14, 0x80);	// trigger, length disabled
	CHECK(apu.channel_volume(APU::CH1) == 15);

	for(int i = 0; i < 65535; ++i)
	{
		apu.tick();
	}
	CHECK(apu.channel_volume(APU::CH1) == 15);	// first envelope tick at 65536

	apu.tick();
	CHECK(apu.channel_volume(APU::CH1) == 14);

	for(int i = 0; i < 65536; ++i)
	{
		apu.tick();
	}
	CHECK(apu.channel_volume(APU::CH1) == 13);
}

TEST_CASE("APU: envelope period 0 keeps the volume")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF12, 0xF0);	// volume 15, period 0
	apu.write(0xFF14, 0x80);

	for(int i = 0; i < 200000; ++i)
	{
		apu.tick();
	}
	CHECK(apu.channel_volume(APU::CH1) == 15);
}

TEST_CASE("APU: CH1 envelope increases at 64 Hz")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF12, 0x09);	// volume 0, increase, period 1
	apu.write(0xFF14, 0x80);
	CHECK(apu.channel_volume(APU::CH1) == 0);

	for(int i = 0; i < 65536; ++i)
	{
		apu.tick();
	}
	CHECK(apu.channel_volume(APU::CH1) == 1);
}

TEST_CASE("APU: CH4 envelope decreases at 64 Hz")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF21, 0xF1);	// volume 15, decrease, period 1
	apu.write(0xFF22, 0x00);
	apu.write(0xFF23, 0x80);

	CHECK(apu.channel_volume(APU::CH4) == 15);
	for(int i = 0; i < 65536; ++i)
	{
		apu.tick();
	}
	CHECK(apu.channel_volume(APU::CH4) == 14);
}

TEST_CASE("APU: CH1 sweep increases the frequency at 128 Hz")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF10, 0x11);	// pace 1, add, shift 1
	apu.write(0xFF12, 0xF0);
	apu.write(0xFF13, 0x00);
	apu.write(0xFF14, 0x81);	// frequency 0x100, trigger
	CHECK(apu.channel_frequency(APU::CH1) == 0x100);

	for(int i = 0; i < 24576; ++i)	// frame sequencer step 2
	{
		apu.tick();
	}
	CHECK(apu.channel_frequency(APU::CH1) == 0x180);

	for(int i = 0; i < 32768; ++i)	// frame sequencer step 6
	{
		apu.tick();
	}
	CHECK(apu.channel_frequency(APU::CH1) == 0x240);
}

TEST_CASE("APU: CH1 sweep subtracts frequency")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF10, 0x19);	// pace 1, subtract, shift 1
	apu.write(0xFF12, 0xF0);
	apu.write(0xFF13, 0x00);
	apu.write(0xFF14, 0x84);	// frequency 0x400, trigger
	CHECK(apu.channel_frequency(APU::CH1) == 0x400);

	for(int i = 0; i < 24576; ++i)
	{
		apu.tick();
	}
	CHECK(apu.channel_frequency(APU::CH1) == 0x200);
}

TEST_CASE("APU: CH1 sweep disables on trigger overflow")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF10, 0x11);	// pace 1, add, shift 1
	apu.write(0xFF12, 0xF0);
	apu.write(0xFF13, 0xFF);
	apu.write(0xFF14, 0x87);	// frequency 0x7FF, trigger

	CHECK(apu.is_channel_enabled(APU::CH1) == false);
}

TEST_CASE("APU: CH1 sweep disables on the second overflow check")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF10, 0x11);	// pace 1, add, shift 1
	apu.write(0xFF12, 0xF0);
	apu.write(0xFF13, 0x00);
	apu.write(0xFF14, 0x84);	// frequency 0x400, trigger
	CHECK(apu.is_channel_enabled(APU::CH1));

	for(int i = 0; i < 24576; ++i)
	{
		apu.tick();
	}
	// 0x400 -> 0x600 is valid, but the immediate second check gives 0x900.
	CHECK(apu.is_channel_enabled(APU::CH1) == false);
}

TEST_CASE("APU: CH1 sweep with shift 0 keeps the frequency")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF10, 0x10);	// pace 1, add, shift 0
	apu.write(0xFF12, 0xF0);
	apu.write(0xFF13, 0x00);
	apu.write(0xFF14, 0x81);	// frequency 0x100

	for(int i = 0; i < 24576; ++i)
	{
		apu.tick();
	}
	CHECK(apu.channel_frequency(APU::CH1) == 0x100);
	CHECK(apu.is_channel_enabled(APU::CH1));
}

TEST_CASE("APU: high-pass filter removes a constant DC offset")
{
	TestApu apu;
	power_on(apu);

	for(int i = 0; i < 16; ++i)
	{
		apu.write(static_cast<u16>(0xFF30 + i), 0xFF);
	}
	apu.write(0xFF1A, 0x80);
	apu.write(0xFF1C, 0x20);
	apu.write(0xFF1D, 0xFF);
	apu.write(0xFF1E, 0x87);	// frequency 0x7FF, trigger

	for(int i = 0; i < 88; ++i)
	{
		apu.tick();
	}
	int16_t first[2];
	REQUIRE(apu.read_samples(first, 1) == 1);
	CHECK(first[0] == -8000);	// first sample sees an empty capacitor
	CHECK(first[1] == -8000);

	for(int i = 0; i < 400000; ++i)
	{
		apu.tick();
	}
	int16_t buffer[8192];
	const size_t frames = apu.read_samples(buffer, 4096);
	REQUIRE(frames == 4096);
	CHECK(buffer[(frames - 1) * 2 + 0] == 0);
	CHECK(buffer[(frames - 1) * 2 + 1] == 0);
}

TEST_CASE("APU: savestate round-trip restores registers and wave RAM")
{
	TestApu apu;
	power_on(apu);

	apu.write(0xFF10, 0x37);
	apu.write(0xFF11, 0xC7);
	apu.write(0xFF12, 0xF3);
	apu.write(0xFF13, 0x45);
	apu.write(0xFF14, 0x43);
	apu.write(0xFF1A, 0x80);
	apu.write(0xFF1C, 0x40);
	apu.write(0xFF24, 0x77);
	apu.write(0xFF25, 0xAB);
	for(int i = 0; i < 16; ++i)
	{
		apu.write(static_cast<u16>(0xFF30 + i), static_cast<u8>(i * 0x11));
	}

	const std::vector<u8> blob = save_apu(apu.apu);

	apu.reset();
	CHECK(apu.is_powered() == false);

	REQUIRE(load_apu(apu.apu, blob));

	CHECK(apu.is_powered() == true);
	CHECK(apu.read(0xFF10) == 0xB7);
	CHECK(apu.read(0xFF11) == 0xFF);
	CHECK(apu.read(0xFF12) == 0xF3);
	CHECK(apu.read(0xFF13) == 0xFF);
	CHECK(apu.read(0xFF14) == 0xFF);
	CHECK(apu.read(0xFF1A) == 0xFF);
	CHECK(apu.read(0xFF1C) == 0xDF);
	CHECK(apu.read(0xFF24) == 0x77);
	CHECK(apu.read(0xFF25) == 0xAB);
	CHECK(apu.read(0xFF26) == 0xF0);

	for(int i = 0; i < 16; ++i)
	{
		CHECK(apu.read(static_cast<u16>(0xFF30 + i)) == static_cast<u8>(i * 0x11));
	}
}

TEST_CASE("APU: savestate round-trip restores channel state")
{
	TestApu apu;
	power_on(apu);

	trigger_ch1(apu, 2, 13, 0x6D6);
	trigger_ch2(apu, 3, 9, 0x500);
	trigger_ch3(apu, 2, 0x739);
	trigger_ch4(apu, 11, 0x35);

	for(int i = 0; i < 20; ++i)
	{
		apu.tick();
	}

	u8 outputs[4];
	for(int i = 0; i < 4; ++i)
	{
		outputs[i] = apu.channel_output(static_cast<APU::Channel>(i));
	}

	const std::vector<u8> blob = save_apu(apu.apu);

	apu.reset();
	REQUIRE(load_apu(apu.apu, blob));

	for(int i = 0; i < 4; ++i)
	{
		CHECK(apu.is_channel_enabled(static_cast<APU::Channel>(i)));
		CHECK(apu.channel_output(static_cast<APU::Channel>(i)) == outputs[i]);
	}

	CHECK(apu.channel_volume(APU::CH1) == 13);
	CHECK(apu.channel_volume(APU::CH2) == 9);
	CHECK(apu.channel_volume(APU::CH4) == 11);
	CHECK(apu.channel_frequency(APU::CH1) == 0x6D6);
	CHECK(apu.channel_frequency(APU::CH2) == 0x500);
	CHECK(apu.channel_frequency(APU::CH3) == 0x739);
	CHECK((apu.read(0xFF26) & 0x0F) == 0x0F);
}

TEST_CASE("APU: savestate does not restore the PCM ring buffer")
{
	TestApu apu;
	power_on(apu);
	trigger_ch1(apu, 2, 15, 0x7FF);

	for(int i = 0; i < 1000; ++i)
	{
		apu.tick();
	}
	REQUIRE(apu.available_frames() > 0);

	const std::vector<u8> blob = save_apu(apu.apu);
	REQUIRE(load_apu(apu.apu, blob));

	CHECK(apu.available_frames() == 0);
}

TEST_CASE("APU: savestate keeps sweep, envelope and length timing")
{
	TestApu a;
	power_on(a);
	a.write(0xFF10, 0x11);	// sweep: pace 1, add, shift 1
	a.write(0xFF11, 0x80);	// duty 2, length = 64
	a.write(0xFF12, 0xF1);	// volume 15, decrease, period 1
	a.write(0xFF13, 0x00);
	a.write(0xFF14, 0xC0);	// frequency 0x100, trigger + length enable

	for(int i = 0; i < 70000; ++i)
	{
		a.tick();
	}

	const std::vector<u8> blob = save_apu(a.apu);

	TestApu b;
	power_on(b);
	REQUIRE(load_apu(b.apu, blob));
	b.div = a.div;
	b.phase = a.phase;

	for(int i = 0; i < 100000; ++i)
	{
		a.tick();
		b.tick();
	}

	CHECK(a.channel_frequency(APU::CH1) == b.channel_frequency(APU::CH1));
	CHECK(a.channel_volume(APU::CH1) == b.channel_volume(APU::CH1));
	CHECK(a.channel_output(APU::CH1) == b.channel_output(APU::CH1));
	CHECK(a.is_channel_enabled(APU::CH1) == b.is_channel_enabled(APU::CH1));
}
