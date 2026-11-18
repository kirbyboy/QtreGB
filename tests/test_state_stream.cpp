#include "doctest.h"
#include "state_stream.h"

using namespace qtreGB;

namespace
{

enum class TestEnum : u8
{
	Zero = 0,
	One = 1,
	Max = 0xFF,
};

std::vector<u8> make_basic_blob()
{
	StateWriter writer(0x8000, 0x1122334455667788ull);
	writer.begin_chunk(StateChunk::Cpu);
	writer.write_u8(1, 0xAB);
	writer.write_u16(2, 0x1234);
	writer.write_u32(3, 0x89ABCDEF);
	writer.write_u64(4, 0x0123456789ABCDEFull);
	writer.write_i8(5, -5);
	writer.write_i16(6, -1234);
	writer.write_i32(7, -1234567);
	writer.write_i64(8, -1234567890123ll);
	writer.write_bool(9, true);
	writer.write_enum(10, TestEnum::Max);
	const std::vector<u8> bytes {1, 2, 3, 4, 5};
	writer.write_bytes(11, bytes);
	writer.write_f32(12, 1.5f);
	writer.write_f64(13, -2.25);
	writer.end_chunk();
	writer.finish();
	REQUIRE(writer.ok());
	return writer.data();
}

} // namespace

TEST_CASE("State stream: header and typed round-trip")
{
	const std::vector<u8> blob = make_basic_blob();
	REQUIRE(blob.size() >= STATE_HEADER_SIZE);

	StateReader reader(blob);
	REQUIRE(reader.ok());
	CHECK(reader.header().format_version == STATE_FORMAT_VERSION);
	CHECK(reader.header().schema_version == STATE_SCHEMA_VERSION);
	CHECK(reader.header().rom_size == 0x8000);
	CHECK(reader.header().rom_hash == 0x1122334455667788ull);
	CHECK(reader.rom_matches(0x8000, 0x1122334455667788ull));
	CHECK(!reader.rom_matches(0x8000, 0x1122334455667789ull));

	REQUIRE(reader.begin_chunk(StateChunk::Cpu));

	u8 u8_value = 0;
	u16 u16_value = 0;
	u32 u32_value = 0;
	u64 u64_value = 0;
	int8_t i8_value = 0;
	int16_t i16_value = 0;
	int32_t i32_value = 0;
	int64_t i64_value = 0;
	bool bool_value = false;
	TestEnum enum_value = TestEnum::Zero;
	std::vector<u8> bytes;
	float f32_value = 0.0f;
	double f64_value = 0.0;

	CHECK(reader.read_u8(1, u8_value));
	CHECK(reader.read_u16(2, u16_value));
	CHECK(reader.read_u32(3, u32_value));
	CHECK(reader.read_u64(4, u64_value));
	CHECK(reader.read_i8(5, i8_value));
	CHECK(reader.read_i16(6, i16_value));
	CHECK(reader.read_i32(7, i32_value));
	CHECK(reader.read_i64(8, i64_value));
	CHECK(reader.read_bool(9, bool_value));
	CHECK(reader.read_enum(10, enum_value));
	CHECK(reader.read_bytes(11, bytes));
	CHECK(reader.read_f32(12, f32_value));
	CHECK(reader.read_f64(13, f64_value));

	CHECK(u8_value == 0xAB);
	CHECK(u16_value == 0x1234);
	CHECK(u32_value == 0x89ABCDEF);
	CHECK(u64_value == 0x0123456789ABCDEFull);
	CHECK(i8_value == -5);
	CHECK(i16_value == -1234);
	CHECK(i32_value == -1234567);
	CHECK(i64_value == -1234567890123ll);
	CHECK(bool_value);
	CHECK(enum_value == TestEnum::Max);
	CHECK(bytes == std::vector<u8>{1, 2, 3, 4, 5});
	CHECK(f32_value == 1.5f);
	CHECK(f64_value == -2.25);

	CHECK(reader.end_chunk());
	CHECK(reader.finish());
	CHECK(reader.ok());
}

TEST_CASE("State stream: nested chunks and unknown fields are skipped")
{
	StateWriter writer(0, 0);
	writer.begin_chunk(StateChunk::Cpu);
	writer.write_u8(1, 0x11);
	writer.begin_chunk(0x7777);
	writer.write_u8(1, 0x22);
	writer.end_chunk();
	writer.write_u8(2, 0x33);
	writer.write_bytes(0x8888, std::vector<u8>{9, 8, 7});
	writer.end_chunk();
	writer.finish();
	REQUIRE(writer.ok());

	StateReader reader(writer.data());
	REQUIRE(reader.ok());
	REQUIRE(reader.begin_chunk(StateChunk::Cpu));

	u8 first = 0;
	u8 second = 0;
	CHECK(reader.read_u8(1, first));
	CHECK(reader.read_u8(2, second));
	CHECK(first == 0x11);
	CHECK(second == 0x33);

	// The unknown nested chunk is skipped while searching for the field.
	std::vector<u8> unknown_bytes;
	CHECK(reader.read_bytes(0x8888, unknown_bytes));
	CHECK(unknown_bytes == std::vector<u8>{9, 8, 7});
	CHECK(reader.end_chunk());
	CHECK(reader.finish());
	CHECK(reader.ok());
}

TEST_CASE("State stream: unknown top-level chunks are skipped")
{
	StateWriter writer(0, 0);
	writer.begin_chunk(0x7000);
	writer.write_u32(1, 0xDEADBEEF);
	writer.end_chunk();
	writer.begin_chunk(StateChunk::Timer);
	writer.write_u16(1, 0x1234);
	writer.end_chunk();
	writer.begin_chunk(0x7001);
	writer.write_u8(1, 1);
	writer.end_chunk();
	writer.finish();
	REQUIRE(writer.ok());

	StateReader reader(writer.data());
	REQUIRE(reader.ok());
	REQUIRE(reader.begin_chunk(StateChunk::Timer));
	u16 value = 0;
	CHECK(reader.read_u16(1, value));
	CHECK(value == 0x1234);
	CHECK(reader.end_chunk());
	CHECK(reader.finish());
	CHECK(reader.ok());
}

TEST_CASE("State stream: top-level chunks are found regardless of order")
{
	StateWriter writer(0, 0);
	writer.begin_chunk(StateChunk::Timer);
	writer.write_u16(1, 0x1234);
	writer.end_chunk();
	writer.begin_chunk(StateChunk::Cpu);
	writer.write_u8(1, 0x42);
	writer.end_chunk();
	writer.finish();
	REQUIRE(writer.ok());

	StateReader reader(writer.data());
	REQUIRE(reader.ok());
	REQUIRE(reader.find_chunk(StateChunk::Cpu));
	u8 value = 0;
	CHECK(reader.read_u8(1, value));
	CHECK(value == 0x42);
	CHECK(reader.end_chunk());

	REQUIRE(reader.find_chunk(StateChunk::Timer));
	u16 timer_value = 0;
	CHECK(reader.read_u16(1, timer_value));
	CHECK(timer_value == 0x1234);
	CHECK(reader.end_chunk());
	CHECK(reader.finish());
}

TEST_CASE("State stream: duplicate top-level chunks are rejected")
{
	StateWriter writer(0, 0);
	writer.begin_chunk(StateChunk::Cpu);
	writer.write_u8(1, 1);
	writer.end_chunk();
	writer.begin_chunk(StateChunk::Cpu);
	writer.write_u8(1, 2);
	writer.end_chunk();
	writer.finish();
	REQUIRE(writer.ok());

	StateReader reader(writer.data());
	REQUIRE(reader.ok());
	CHECK(reader.find_chunk(StateChunk::Cpu));
	CHECK(reader.end_chunk());
	CHECK(!reader.find_chunk(StateChunk::Cpu));
	CHECK(reader.error() == StateError::BadValue);
}

TEST_CASE("State stream: old schema versions are rejected")
{
	StateWriter writer(0, 0, 1);	// previous schema
	writer.begin_chunk(StateChunk::Cpu);
	writer.write_u8(1, 1);
	writer.end_chunk();
	writer.finish();
	REQUIRE(writer.ok());

	StateReader reader(writer.data());
	CHECK(reader.error() == StateError::UnsupportedSchema);
}

TEST_CASE("State stream: missing fields and chunks are errors")
{
	StateWriter writer(0, 0);
	writer.begin_chunk(StateChunk::Cpu);
	writer.write_u8(1, 1);
	writer.end_chunk();
	writer.finish();
	REQUIRE(writer.ok());

	StateReader missing_field(writer.data());
	REQUIRE(missing_field.begin_chunk(StateChunk::Cpu));
	u8 value = 0;
	CHECK(!missing_field.read_u8(2, value));
	CHECK(missing_field.error() == StateError::MissingField);

	StateReader missing_chunk(writer.data());
	CHECK(!missing_chunk.begin_chunk(StateChunk::Timer));
	CHECK(missing_chunk.error() == StateError::MissingChunk);
}

TEST_CASE("State stream: type and length mismatches are errors")
{
	StateWriter writer(0, 0);
	writer.begin_chunk(StateChunk::Cpu);
	writer.write_u16(1, 0x1234);
	writer.write_bytes(2, std::vector<u8>{1, 2});
	writer.end_chunk();
	writer.finish();
	REQUIRE(writer.ok());

	StateReader wrong_type(writer.data());
	REQUIRE(wrong_type.begin_chunk(StateChunk::Cpu));
	u8 value = 0;
	CHECK(!wrong_type.read_u8(1, value));
	CHECK(wrong_type.error() == StateError::BadType);

	StateReader wrong_length(writer.data());
	REQUIRE(wrong_length.begin_chunk(StateChunk::Cpu));
	u16 value16 = 0;
	CHECK(wrong_length.read_u16(1, value16));
	u8 bytes[3] {};
	CHECK(!wrong_length.read_bytes(2, bytes, sizeof(bytes)));
	CHECK(wrong_length.error() == StateError::BadLength);
}

TEST_CASE("State stream: header, payload and truncation validation")
{
	const std::vector<u8> blob = make_basic_blob();

	std::vector<u8> bad_magic = blob;
	bad_magic[0] ^= 0x01;
	StateReader magic_reader(bad_magic);
	CHECK(magic_reader.error() == StateError::BadMagic);

	std::vector<u8> bad_header = blob;
	bad_header[12] ^= 0x01; // rom_size field inside the header
	StateReader header_reader(bad_header);
	CHECK(header_reader.error() == StateError::HeaderChecksum);

	std::vector<u8> bad_payload = blob;
	bad_payload.back() ^= 0x01;
	StateReader payload_reader(bad_payload);
	CHECK(payload_reader.error() == StateError::PayloadChecksum);

	std::vector<u8> truncated_payload(blob.begin(), blob.end() - 1);
	StateReader truncated_reader(truncated_payload);
	CHECK(truncated_reader.error() == StateError::SizeMismatch);

	std::vector<u8> truncated_header(blob.begin(), blob.begin() + 8);
	StateReader truncated_header_reader(truncated_header);
	CHECK(truncated_header_reader.error() == StateError::Truncated);
}

TEST_CASE("State stream: writer misuse is reported")
{
	StateWriter unbalanced(0, 0);
	unbalanced.end_chunk();
	CHECK(unbalanced.error() == StateError::WriterMisuse);

	StateWriter open_chunk(0, 0);
	open_chunk.begin_chunk(StateChunk::Cpu);
	open_chunk.finish();
	CHECK(open_chunk.error() == StateError::WriterMisuse);
}

TEST_CASE("State stream: ROM hash helper is deterministic")
{
	const std::vector<u8> rom {0x00, 0x01, 0x02, 0x03};
	CHECK(state_hash_bytes(rom.data(), rom.size()) == state_hash_rom(rom));
	CHECK(state_hash_rom(rom) == state_hash_rom(rom));
	CHECK(state_hash_rom(rom) != state_hash_rom(std::vector<u8>{0x00, 0x01, 0x02, 0x04}));
}

TEST_CASE("State stream: CRC-32 known vectors")
{
	const char* check = "123456789";
	CHECK(state_crc32(check, 9) == 0xCBF43926u);
	CHECK(state_crc32(nullptr, 0) == 0x00000000u);
}

TEST_CASE("State stream: FNV-1a 64 known vectors")
{
	const std::vector<u8> empty;
	const std::vector<u8> a {'a'};
	const std::vector<u8> foobar {'f', 'o', 'o', 'b', 'a', 'r'};

	CHECK(state_hash_rom(empty) == 0xCBF29CE484222325ull);
	CHECK(state_hash_rom(a) == 0xAF63DC4C8601EC8Cull);
	CHECK(state_hash_rom(foobar) == 0x85944171F73967E8ull);
}
