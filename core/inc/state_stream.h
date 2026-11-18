#ifndef QTREGB_STATE_STREAM_H
#define QTREGB_STATE_STREAM_H

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "types.h"

namespace qtreGB
{

// Core state stream. This is deliberately independent from the GUI: it
// contains only emulator state, with a small versioned header and TLV chunks.
// Metadata, thumbnails, compression, rewind and slots belong to the GUI layer.

constexpr u16 STATE_FORMAT_VERSION = 1;
constexpr u16 STATE_SCHEMA_VERSION = 5;
constexpr size_t STATE_HEADER_SIZE = 36;

enum class StateType : u8
{
	U8 = 1,
	U16,
	U32,
	U64,
	I8,
	I16,
	I32,
	I64,
	F32,
	F64,
	Bool,
	Bytes,
	Chunk,
};

enum class StateError : u8
{
	Ok = 0,
	BadMagic,
	UnsupportedFormat,
	UnsupportedSchema,
	HeaderChecksum,
	PayloadChecksum,
	Truncated,
	SizeMismatch,
	BadType,
	BadLength,
	BadValue,
	MissingField,
	MissingChunk,
	UnbalancedChunk,
	WrongRom,
	WriterMisuse,
	TooLarge,
};

const char* state_error_name(StateError error);

// Stable top-level component identifiers. Field tags are component-local
// u16 values; they are defined next to each component serializer.
enum class StateChunk : u16
{
	Cpu = 0x0100,
	Timer = 0x0101,
	Apu = 0x0102,
	Memory = 0x0103,
	Bus = 0x0104,
	Cart = 0x0105,
	Ppu = 0x0106,
	Lcd = 0x0107,
	Dma = 0x0108,
	Gamepad = 0x0109,
	Interrupts = 0x010A,
	Clock = 0x010B,
	Io = 0x010C,
	BusLocks = 0x010D,
	Serial = 0x010E,
};

struct StateHeader
{
	u16 format_version = STATE_FORMAT_VERSION;
	u16 schema_version = STATE_SCHEMA_VERSION;
	u32 rom_size = 0;
	u64 rom_hash = 0;
	u32 payload_size = 0;
	u32 payload_crc32 = 0;
};

u32 state_crc32(const void* data, size_t size);
u64 state_hash_bytes(const void* data, size_t size);
u64 state_hash_rom(const std::vector<u8>& rom);

class StateWriter
{
	public:
		explicit StateWriter(u32 rom_size, u64 rom_hash,
		                     u16 schema_version = STATE_SCHEMA_VERSION);

		void begin_chunk(u16 tag);
		void begin_chunk(StateChunk chunk) { begin_chunk(static_cast<u16>(chunk)); }
		void end_chunk();

		void write_u8(u16 tag, u8 value);
		void write_u16(u16 tag, u16 value);
		void write_u32(u16 tag, u32 value);
		void write_u64(u16 tag, u64 value);
		void write_i8(u16 tag, int8_t value);
		void write_i16(u16 tag, int16_t value);
		void write_i32(u16 tag, int32_t value);
		void write_i64(u16 tag, int64_t value);
		void write_f32(u16 tag, float value);
		void write_f64(u16 tag, double value);
		void write_bool(u16 tag, bool value);
		void write_bytes(u16 tag, const void* data, size_t size);
		void write_bytes(u16 tag, const std::vector<u8>& data)
		{
			write_bytes(tag, data.data(), data.size());
		}

		template<typename E>
		void write_enum(u16 tag, E value)
		{
			static_assert(std::is_enum_v<E>, "write_enum requires an enum type");
			using U = std::underlying_type_t<E>;
			const U raw = static_cast<U>(value);
			if constexpr(std::is_same_v<U, u8>)
			{
				write_u8(tag, raw);
			}
			else if constexpr(std::is_same_v<U, u16>)
			{
				write_u16(tag, raw);
			}
			else if constexpr(std::is_same_v<U, u32>)
			{
				write_u32(tag, raw);
			}
			else if constexpr(std::is_same_v<U, u64>)
			{
				write_u64(tag, raw);
			}
			else if constexpr(std::is_same_v<U, int8_t>)
			{
				write_i8(tag, raw);
			}
			else if constexpr(std::is_same_v<U, int16_t>)
			{
				write_i16(tag, raw);
			}
			else if constexpr(std::is_same_v<U, int32_t>)
			{
				write_i32(tag, raw);
			}
			else if constexpr(std::is_same_v<U, int64_t>)
			{
				write_i64(tag, raw);
			}
			else
			{
				static_assert(!std::is_same_v<U, U>, "unsupported enum underlying type");
			}
		}

		void finish();
		const std::vector<u8>& data();
		std::vector<u8> take();
		StateError error() const { return m_error; }
		bool ok() const { return m_error == StateError::Ok; }
		// Lets a component serializer report an internal encoding failure
		// (for example an unregistered PPU selector).
		void mark_error(StateError error) { set_error(error); }

	private:
		struct ChunkFrame
		{
			size_t length_pos = 0;
			size_t data_start = 0;
		};

		void write_entry_header(u16 tag, StateType type, u32 length);
		void write_raw(const void* data, size_t size);
		void set_error(StateError error);

		std::vector<u8> m_buffer;
		std::vector<ChunkFrame> m_chunks;
		StateError m_error = StateError::Ok;
		u32 m_rom_size = 0;
		u64 m_rom_hash = 0;
		u16 m_schema_version = STATE_SCHEMA_VERSION;
		bool m_finished = false;
};

class StateReader
{
	public:
		explicit StateReader(const std::vector<u8>& data);
		StateReader(const void* data, size_t size);

		const StateHeader& header() const { return m_header; }
		StateError error() const { return m_error; }
		bool ok() const { return m_error == StateError::Ok; }
		// Lets a component serializer report a semantic validation failure
		// (for example an out-of-range enum) without exposing set_error().
		void mark_error(StateError error) { set_error(error); }
		bool rom_matches(u32 rom_size, u64 rom_hash) const
		{
			return m_header.rom_size == rom_size && m_header.rom_hash == rom_hash;
		}

		bool begin_chunk(u16 tag);
		bool begin_chunk(StateChunk chunk) { return begin_chunk(static_cast<u16>(chunk)); }
		// Top-level lookup by tag. It always scans from the start of the
		// payload, so components do not depend on chunk order. Duplicate
		// top-level chunks are rejected.
		bool find_chunk(u16 tag);
		bool find_chunk(StateChunk chunk) { return find_chunk(static_cast<u16>(chunk)); }
		bool next_chunk(u16& tag, u32& length);
		void enter_chunk(u32 length);
		void skip_chunk(u32 length);
		bool end_chunk();
		bool finish();

		bool read_u8(u16 tag, u8& value);
		bool read_u16(u16 tag, u16& value);
		bool read_u32(u16 tag, u32& value);
		bool read_u64(u16 tag, u64& value);
		bool read_i8(u16 tag, int8_t& value);
		bool read_i16(u16 tag, int16_t& value);
		bool read_i32(u16 tag, int32_t& value);
		bool read_i64(u16 tag, int64_t& value);
		bool read_f32(u16 tag, float& value);
		bool read_f64(u16 tag, double& value);
		bool read_bool(u16 tag, bool& value);
		bool read_bytes(u16 tag, void* data, size_t size);
		bool read_bytes(u16 tag, std::vector<u8>& data);

		template<typename E>
		bool read_enum(u16 tag, E& value)
		{
			static_assert(std::is_enum_v<E>, "read_enum requires an enum type");
			using U = std::underlying_type_t<E>;
			U raw {};
			bool ok = false;
			if constexpr(std::is_same_v<U, u8>)
			{
				ok = read_u8(tag, raw);
			}
			else if constexpr(std::is_same_v<U, u16>)
			{
				ok = read_u16(tag, raw);
			}
			else if constexpr(std::is_same_v<U, u32>)
			{
				ok = read_u32(tag, raw);
			}
			else if constexpr(std::is_same_v<U, u64>)
			{
				ok = read_u64(tag, raw);
			}
			else if constexpr(std::is_same_v<U, int8_t>)
			{
				ok = read_i8(tag, raw);
			}
			else if constexpr(std::is_same_v<U, int16_t>)
			{
				ok = read_i16(tag, raw);
			}
			else if constexpr(std::is_same_v<U, int32_t>)
			{
				ok = read_i32(tag, raw);
			}
			else if constexpr(std::is_same_v<U, int64_t>)
			{
				ok = read_i64(tag, raw);
			}
			else
			{
				static_assert(!std::is_same_v<U, U>, "unsupported enum underlying type");
			}
			if(ok)
			{
				value = static_cast<E>(raw);
			}
			return ok;
		}

	private:
		size_t current_end() const
		{
			return m_chunk_ends.empty() ? m_payload_end : m_chunk_ends.back();
		}

		bool read_entry_header(size_t at, u16& tag, StateType& type,
		                       u32& length, size_t& value_pos) const;
		bool find_field(u16 tag, StateType type, bool check_length,
		                u32 expected_length);
		bool read_raw(void* data, size_t size);
		void set_error(StateError error);

		const u8* m_data = nullptr;
		size_t m_size = 0;
		size_t m_pos = 0;
		size_t m_payload_start = 0;
		size_t m_payload_end = 0;
		std::vector<size_t> m_chunk_ends;
		std::vector<u16> m_seen_chunks;
		StateHeader m_header {};
		StateError m_error = StateError::Ok;
		u32 m_last_field_length = 0;
};

} // namespace qtreGB

#endif // QTREGB_STATE_STREAM_H
