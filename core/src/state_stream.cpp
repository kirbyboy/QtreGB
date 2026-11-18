#include "state_stream.h"

#include <cstring>

namespace qtreGB
{

namespace
{

constexpr u8 STATE_MAGIC[8] = {'Q', 'T', 'G', 'B', 'S', 'T', '0', '1'};

constexpr size_t OFF_FORMAT = 8;
constexpr size_t OFF_SCHEMA = 10;
constexpr size_t OFF_ROM_SIZE = 12;
constexpr size_t OFF_ROM_HASH = 16;
constexpr size_t OFF_PAYLOAD_SIZE = 24;
constexpr size_t OFF_PAYLOAD_CRC = 28;
constexpr size_t OFF_HEADER_CRC = 32;
constexpr size_t ENTRIES_BEGIN = STATE_HEADER_SIZE;
constexpr size_t ENTRY_HEADER_SIZE = 7; // tag u16 + type u8 + length u32

u16 load_u16(const u8* p)
{
	return static_cast<u16>(p[0] | (static_cast<u16>(p[1]) << 8));
}

u32 load_u32(const u8* p)
{
	return static_cast<u32>(p[0]) |
	       (static_cast<u32>(p[1]) << 8) |
	       (static_cast<u32>(p[2]) << 16) |
	       (static_cast<u32>(p[3]) << 24);
}

u64 load_u64(const u8* p)
{
	u64 value = 0;
	for(int i = 0; i < 8; ++i)
	{
		value |= static_cast<u64>(p[i]) << (8 * i);
	}
	return value;
}

void store_u16(u8* p, u16 value)
{
	p[0] = static_cast<u8>(value & 0xFF);
	p[1] = static_cast<u8>((value >> 8) & 0xFF);
}

void store_u32(u8* p, u32 value)
{
	p[0] = static_cast<u8>(value & 0xFF);
	p[1] = static_cast<u8>((value >> 8) & 0xFF);
	p[2] = static_cast<u8>((value >> 16) & 0xFF);
	p[3] = static_cast<u8>((value >> 24) & 0xFF);
}

void store_u64(u8* p, u64 value)
{
	for(int i = 0; i < 8; ++i)
	{
		p[i] = static_cast<u8>((value >> (8 * i)) & 0xFF);
	}
}

} // namespace

u32 state_crc32(const void* data, size_t size)
{
	const u8* bytes = static_cast<const u8*>(data);
	u32 crc = 0xFFFFFFFFu;
	for(size_t i = 0; i < size; ++i)
	{
		crc ^= bytes[i];
		for(int bit = 0; bit < 8; ++bit)
		{
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
		}
	}
	return ~crc;
}

const char* state_error_name(StateError error)
{
	switch(error)
	{
		case StateError::Ok: return "ok";
		case StateError::BadMagic: return "bad magic";
		case StateError::UnsupportedFormat: return "unsupported format";
		case StateError::UnsupportedSchema: return "unsupported schema";
		case StateError::HeaderChecksum: return "header checksum";
		case StateError::PayloadChecksum: return "payload checksum";
		case StateError::Truncated: return "truncated";
		case StateError::SizeMismatch: return "size mismatch";
		case StateError::BadType: return "bad type";
		case StateError::BadLength: return "bad length";
		case StateError::BadValue: return "bad value";
		case StateError::MissingField: return "missing field";
		case StateError::MissingChunk: return "missing chunk";
		case StateError::UnbalancedChunk: return "unbalanced chunk";
		case StateError::WrongRom: return "wrong rom";
		case StateError::WriterMisuse: return "writer misuse";
		case StateError::TooLarge: return "too large";
		default: return "unknown";
	}
}

u64 state_hash_bytes(const void* data, size_t size)
{
	const u8* bytes = static_cast<const u8*>(data);
	u64 hash = 14695981039346656037ull; // FNV-1a 64 offset basis (0xCBF29CE484222325)
	for(size_t i = 0; i < size; ++i)
	{
		hash ^= bytes[i];
		hash *= 1099511628211ull;
	}
	return hash;
}

u64 state_hash_rom(const std::vector<u8>& rom)
{
	return state_hash_bytes(rom.data(), rom.size());
}

StateWriter::StateWriter(u32 rom_size, u64 rom_hash, u16 schema_version) :
m_rom_size(rom_size),
m_rom_hash(rom_hash),
m_schema_version(schema_version)
{
	if(schema_version == 0)
	{
		m_error = StateError::UnsupportedSchema;
		return;
	}
	m_buffer.assign(STATE_HEADER_SIZE, 0);
}

void StateWriter::set_error(StateError error)
{
	if(m_error == StateError::Ok)
	{
		m_error = error;
	}
}

void StateWriter::write_raw(const void* data, size_t size)
{
	if(m_error != StateError::Ok)
	{
		return;
	}
	if(size == 0)
	{
		return;
	}
	const u8* bytes = static_cast<const u8*>(data);
	m_buffer.insert(m_buffer.end(), bytes, bytes + size);
}

void StateWriter::write_entry_header(u16 tag, StateType type, u32 length)
{
	if(m_error != StateError::Ok)
	{
		return;
	}
	u8 header[ENTRY_HEADER_SIZE];
	store_u16(header, tag);
	header[2] = static_cast<u8>(type);
	store_u32(header + 3, length);
	write_raw(header, sizeof(header));
}

void StateWriter::begin_chunk(u16 tag)
{
	if(m_error != StateError::Ok)
	{
		return;
	}
	if(m_chunks.size() >= 32)
	{
		set_error(StateError::TooLarge);
		return;
	}
	write_entry_header(tag, StateType::Chunk, 0);
	if(m_error != StateError::Ok)
	{
		return;
	}
	ChunkFrame frame;
	frame.length_pos = m_buffer.size() - 4;
	frame.data_start = m_buffer.size();
	m_chunks.push_back(frame);
}

void StateWriter::end_chunk()
{
	if(m_error != StateError::Ok)
	{
		return;
	}
	if(m_chunks.empty())
	{
		set_error(StateError::WriterMisuse);
		return;
	}
	const ChunkFrame frame = m_chunks.back();
	m_chunks.pop_back();
	const size_t length = m_buffer.size() - frame.data_start;
	if(length > 0xFFFFFFFFull)
	{
		set_error(StateError::TooLarge);
		return;
	}
	store_u32(m_buffer.data() + frame.length_pos, static_cast<u32>(length));
}

void StateWriter::write_u8(u16 tag, u8 value)
{
	write_entry_header(tag, StateType::U8, 1);
	write_raw(&value, 1);
}

void StateWriter::write_u16(u16 tag, u16 value)
{
	u8 bytes[2];
	store_u16(bytes, value);
	write_entry_header(tag, StateType::U16, 2);
	write_raw(bytes, sizeof(bytes));
}

void StateWriter::write_u32(u16 tag, u32 value)
{
	u8 bytes[4];
	store_u32(bytes, value);
	write_entry_header(tag, StateType::U32, 4);
	write_raw(bytes, sizeof(bytes));
}

void StateWriter::write_u64(u16 tag, u64 value)
{
	u8 bytes[8];
	store_u64(bytes, value);
	write_entry_header(tag, StateType::U64, 8);
	write_raw(bytes, sizeof(bytes));
}

void StateWriter::write_i8(u16 tag, int8_t value)
{
	const u8 raw = static_cast<u8>(value);
	write_entry_header(tag, StateType::I8, 1);
	write_raw(&raw, 1);
}

void StateWriter::write_i16(u16 tag, int16_t value)
{
	u8 bytes[2];
	store_u16(bytes, static_cast<u16>(value));
	write_entry_header(tag, StateType::I16, 2);
	write_raw(bytes, sizeof(bytes));
}

void StateWriter::write_i32(u16 tag, int32_t value)
{
	u8 bytes[4];
	store_u32(bytes, static_cast<u32>(value));
	write_entry_header(tag, StateType::I32, 4);
	write_raw(bytes, sizeof(bytes));
}

void StateWriter::write_i64(u16 tag, int64_t value)
{
	u8 bytes[8];
	store_u64(bytes, static_cast<u64>(value));
	write_entry_header(tag, StateType::I64, 8);
	write_raw(bytes, sizeof(bytes));
}

void StateWriter::write_f32(u16 tag, float value)
{
	u32 raw = 0;
	static_assert(sizeof(raw) == sizeof(value), "unexpected float width");
	std::memcpy(&raw, &value, sizeof(raw));
	write_u32(tag, raw);
}

void StateWriter::write_f64(u16 tag, double value)
{
	u64 raw = 0;
	static_assert(sizeof(raw) == sizeof(value), "unexpected double width");
	std::memcpy(&raw, &value, sizeof(raw));
	write_u64(tag, raw);
}

void StateWriter::write_bool(u16 tag, bool value)
{
	const u8 raw = value ? 1 : 0;
	write_entry_header(tag, StateType::Bool, 1);
	write_raw(&raw, 1);
}

void StateWriter::write_bytes(u16 tag, const void* data, size_t size)
{
	if(m_error != StateError::Ok)
	{
		return;
	}
	if(size > 0xFFFFFFFFull)
	{
		set_error(StateError::TooLarge);
		return;
	}
	if(size != 0 && data == nullptr)
	{
		set_error(StateError::WriterMisuse);
		return;
	}
	write_entry_header(tag, StateType::Bytes, static_cast<u32>(size));
	write_raw(data, size);
}

void StateWriter::finish()
{
	if(m_finished || m_error != StateError::Ok)
	{
		return;
	}
	if(!m_chunks.empty())
	{
		set_error(StateError::WriterMisuse);
		return;
	}

	const size_t payload_size = m_buffer.size() - STATE_HEADER_SIZE;
	if(payload_size > 0xFFFFFFFFull)
	{
		set_error(StateError::TooLarge);
		return;
	}

	std::memcpy(m_buffer.data(), STATE_MAGIC, sizeof(STATE_MAGIC));
	store_u16(m_buffer.data() + OFF_FORMAT, STATE_FORMAT_VERSION);
	store_u16(m_buffer.data() + OFF_SCHEMA, m_schema_version);
	store_u32(m_buffer.data() + OFF_ROM_SIZE, m_rom_size);
	store_u64(m_buffer.data() + OFF_ROM_HASH, m_rom_hash);
	store_u32(m_buffer.data() + OFF_PAYLOAD_SIZE, static_cast<u32>(payload_size));
	store_u32(m_buffer.data() + OFF_PAYLOAD_CRC,
	          state_crc32(m_buffer.data() + STATE_HEADER_SIZE, payload_size));
	store_u32(m_buffer.data() + OFF_HEADER_CRC, state_crc32(m_buffer.data(), OFF_HEADER_CRC));
	m_finished = true;
}

const std::vector<u8>& StateWriter::data()
{
	finish();
	return m_buffer;
}

std::vector<u8> StateWriter::take()
{
	finish();
	return std::move(m_buffer);
}

StateReader::StateReader(const std::vector<u8>& data) :
StateReader(data.data(), data.size())
{
}

StateReader::StateReader(const void* data, size_t size) :
m_data(static_cast<const u8*>(data)),
m_size(size)
{
	if(m_data == nullptr || m_size < STATE_HEADER_SIZE)
	{
		set_error(StateError::Truncated);
		return;
	}
	if(std::memcmp(m_data, STATE_MAGIC, sizeof(STATE_MAGIC)) != 0)
	{
		set_error(StateError::BadMagic);
		return;
	}

	m_header.format_version = load_u16(m_data + OFF_FORMAT);
	m_header.schema_version = load_u16(m_data + OFF_SCHEMA);
	m_header.rom_size = load_u32(m_data + OFF_ROM_SIZE);
	m_header.rom_hash = load_u64(m_data + OFF_ROM_HASH);
	m_header.payload_size = load_u32(m_data + OFF_PAYLOAD_SIZE);
	m_header.payload_crc32 = load_u32(m_data + OFF_PAYLOAD_CRC);
	const u32 header_crc = load_u32(m_data + OFF_HEADER_CRC);

	if(m_header.format_version != STATE_FORMAT_VERSION)
	{
		set_error(StateError::UnsupportedFormat);
		return;
	}
	if(m_header.schema_version != STATE_SCHEMA_VERSION)
	{
		set_error(StateError::UnsupportedSchema);
		return;
	}
	if(header_crc != state_crc32(m_data, OFF_HEADER_CRC))
	{
		set_error(StateError::HeaderChecksum);
		return;
	}
	if(m_header.payload_size != m_size - STATE_HEADER_SIZE)
	{
		set_error(StateError::SizeMismatch);
		return;
	}
	if(state_crc32(m_data + STATE_HEADER_SIZE, m_header.payload_size) != m_header.payload_crc32)
	{
		set_error(StateError::PayloadChecksum);
		return;
	}

	m_payload_start = STATE_HEADER_SIZE;
	m_payload_end = m_size;
	m_pos = m_payload_start;
}

void StateReader::set_error(StateError error)
{
	if(m_error == StateError::Ok)
	{
		m_error = error;
	}
}

bool StateReader::read_entry_header(size_t at, u16& tag, StateType& type,
                                    u32& length, size_t& value_pos) const
{
	const size_t end = m_chunk_ends.empty() ? m_payload_end : m_chunk_ends.back();
	if(at + ENTRY_HEADER_SIZE > end)
	{
		return false;
	}
	tag = load_u16(m_data + at);
	type = static_cast<StateType>(m_data[at + 2]);
	length = load_u32(m_data + at + 3);
	value_pos = at + ENTRY_HEADER_SIZE;
	if(value_pos + length > end)
	{
		return false;
	}
	return true;
}

bool StateReader::find_field(u16 tag, StateType type, bool check_length,
                             u32 expected_length)
{
	if(m_error != StateError::Ok)
	{
		return false;
	}

	const size_t end = current_end();
	while(m_pos < end)
	{
		u16 entry_tag = 0;
		StateType entry_type {};
		u32 entry_length = 0;
		size_t value_pos = 0;
		if(!read_entry_header(m_pos, entry_tag, entry_type, entry_length, value_pos))
		{
			set_error(StateError::BadLength);
			return false;
		}
		if(entry_tag == tag)
		{
			if(entry_type != type)
			{
				set_error(StateError::BadType);
				return false;
			}
			if(check_length && entry_length != expected_length)
			{
				set_error(StateError::BadLength);
				return false;
			}
			m_pos = value_pos;
			m_last_field_length = entry_length;
			return true;
		}
		m_pos = value_pos + entry_length;
	}

	set_error(StateError::MissingField);
	return false;
}

bool StateReader::read_raw(void* data, size_t size)
{
	if(m_error != StateError::Ok)
	{
		return false;
	}
	if(size != 0 && data == nullptr)
	{
		set_error(StateError::BadValue);
		return false;
	}
	const size_t end = current_end();
	if(m_pos + size > end)
	{
		set_error(StateError::Truncated);
		return false;
	}
	if(size != 0)
	{
		std::memcpy(data, m_data + m_pos, size);
	}
	m_pos += size;
	return true;
}

bool StateReader::begin_chunk(u16 tag)
{
	if(m_error != StateError::Ok)
	{
		return false;
	}
	for(;;)
	{
		u16 found_tag = 0;
		u32 length = 0;
		if(!next_chunk(found_tag, length))
		{
			if(m_error == StateError::Ok)
			{
				set_error(StateError::MissingChunk);
			}
			return false;
		}
		if(found_tag == tag)
		{
			enter_chunk(length);
			return m_error == StateError::Ok;
		}
		skip_chunk(length);
		if(m_error != StateError::Ok)
		{
			return false;
		}
	}
}

bool StateReader::find_chunk(u16 tag)
{
	if(m_error != StateError::Ok)
	{
		return false;
	}
	if(!m_chunk_ends.empty())
	{
		set_error(StateError::UnbalancedChunk);
		return false;
	}
	for(const u16 seen : m_seen_chunks)
	{
		if(seen == tag)
		{
			set_error(StateError::BadValue);
			return false;
		}
	}

	m_chunk_ends.clear();
	const size_t saved_pos = m_pos;
	size_t pos = m_payload_start;
	while(pos < m_payload_end)
	{
		u16 found_tag = 0;
		StateType type {};
		u32 length = 0;
		size_t value_pos = 0;
		if(!read_entry_header(pos, found_tag, type, length, value_pos))
		{
			set_error(StateError::BadLength);
			return false;
		}
		if(type != StateType::Chunk)
		{
			set_error(StateError::BadType);
			return false;
		}
		if(found_tag == tag)
		{
			m_seen_chunks.push_back(tag);
			m_pos = value_pos;
			enter_chunk(length);
			return m_error == StateError::Ok;
		}
		pos = value_pos + length;
	}

	m_pos = saved_pos;
	set_error(StateError::MissingChunk);
	return false;
}

bool StateReader::next_chunk(u16& tag, u32& length)
{
	if(m_error != StateError::Ok)
	{
		return false;
	}
	const size_t end = current_end();
	if(m_pos >= end)
	{
		return false;
	}
	StateType type {};
	size_t value_pos = 0;
	if(!read_entry_header(m_pos, tag, type, length, value_pos))
	{
		set_error(StateError::BadLength);
		return false;
	}
	if(type != StateType::Chunk)
	{
		set_error(StateError::BadType);
		return false;
	}
	m_pos = value_pos;
	return true;
}

void StateReader::enter_chunk(u32 length)
{
	if(m_error != StateError::Ok)
	{
		return;
	}
	if(m_chunk_ends.size() >= 32)
	{
		set_error(StateError::TooLarge);
		return;
	}
	const size_t end = current_end();
	if(m_pos + length > end)
	{
		set_error(StateError::BadLength);
		return;
	}
	m_chunk_ends.push_back(m_pos + length);
}

void StateReader::skip_chunk(u32 length)
{
	if(m_error != StateError::Ok)
	{
		return;
	}
	const size_t end = current_end();
	if(m_pos + length > end)
	{
		set_error(StateError::BadLength);
		return;
	}
	m_pos += length;
}

bool StateReader::end_chunk()
{
	if(m_error != StateError::Ok)
	{
		return false;
	}
	if(m_chunk_ends.empty())
	{
		set_error(StateError::UnbalancedChunk);
		return false;
	}
	const size_t end = m_chunk_ends.back();
	if(m_pos > end)
	{
		set_error(StateError::BadLength);
		return false;
	}
	// Unknown trailing fields are skipped for forward compatibility.
	m_pos = end;
	m_chunk_ends.pop_back();
	return true;
}

bool StateReader::finish()
{
	if(m_error != StateError::Ok)
	{
		return false;
	}
	if(!m_chunk_ends.empty())
	{
		set_error(StateError::UnbalancedChunk);
		return false;
	}
	while(m_pos < m_payload_end)
	{
		u16 tag = 0;
		u32 length = 0;
		if(!next_chunk(tag, length))
		{
			if(m_error == StateError::Ok)
			{
				set_error(StateError::Truncated);
			}
			return false;
		}
		skip_chunk(length);
	}
	return m_error == StateError::Ok;
}

bool StateReader::read_u8(u16 tag, u8& value)
{
	if(!find_field(tag, StateType::U8, true, 1))
	{
		return false;
	}
	return read_raw(&value, 1);
}

bool StateReader::read_u16(u16 tag, u16& value)
{
	if(!find_field(tag, StateType::U16, true, 2))
	{
		return false;
	}
	u8 bytes[2] {};
	if(!read_raw(bytes, sizeof(bytes)))
	{
		return false;
	}
	value = load_u16(bytes);
	return true;
}

bool StateReader::read_u32(u16 tag, u32& value)
{
	if(!find_field(tag, StateType::U32, true, 4))
	{
		return false;
	}
	u8 bytes[4] {};
	if(!read_raw(bytes, sizeof(bytes)))
	{
		return false;
	}
	value = load_u32(bytes);
	return true;
}

bool StateReader::read_u64(u16 tag, u64& value)
{
	if(!find_field(tag, StateType::U64, true, 8))
	{
		return false;
	}
	u8 bytes[8] {};
	if(!read_raw(bytes, sizeof(bytes)))
	{
		return false;
	}
	value = load_u64(bytes);
	return true;
}

bool StateReader::read_i8(u16 tag, int8_t& value)
{
	if(!find_field(tag, StateType::I8, true, 1))
	{
		return false;
	}
	u8 raw = 0;
	if(!read_raw(&raw, 1))
	{
		return false;
	}
	value = static_cast<int8_t>(raw);
	return true;
}

bool StateReader::read_i16(u16 tag, int16_t& value)
{
	if(!find_field(tag, StateType::I16, true, 2))
	{
		return false;
	}
	u8 bytes[2] {};
	if(!read_raw(bytes, sizeof(bytes)))
	{
		return false;
	}
	value = static_cast<int16_t>(load_u16(bytes));
	return true;
}

bool StateReader::read_i32(u16 tag, int32_t& value)
{
	if(!find_field(tag, StateType::I32, true, 4))
	{
		return false;
	}
	u8 bytes[4] {};
	if(!read_raw(bytes, sizeof(bytes)))
	{
		return false;
	}
	value = static_cast<int32_t>(load_u32(bytes));
	return true;
}

bool StateReader::read_i64(u16 tag, int64_t& value)
{
	if(!find_field(tag, StateType::I64, true, 8))
	{
		return false;
	}
	u8 bytes[8] {};
	if(!read_raw(bytes, sizeof(bytes)))
	{
		return false;
	}
	value = static_cast<int64_t>(load_u64(bytes));
	return true;
}

bool StateReader::read_f32(u16 tag, float& value)
{
	u32 raw = 0;
	if(!read_u32(tag, raw))
	{
		return false;
	}
	std::memcpy(&value, &raw, sizeof(value));
	return true;
}

bool StateReader::read_f64(u16 tag, double& value)
{
	u64 raw = 0;
	if(!read_u64(tag, raw))
	{
		return false;
	}
	std::memcpy(&value, &raw, sizeof(value));
	return true;
}

bool StateReader::read_bool(u16 tag, bool& value)
{
	if(!find_field(tag, StateType::Bool, true, 1))
	{
		return false;
	}
	u8 raw = 0;
	if(!read_raw(&raw, 1))
	{
		return false;
	}
	if(raw > 1)
	{
		set_error(StateError::BadValue);
		return false;
	}
	value = raw != 0;
	return true;
}

bool StateReader::read_bytes(u16 tag, void* data, size_t size)
{
	if(size > 0xFFFFFFFFull)
	{
		set_error(StateError::TooLarge);
		return false;
	}
	if(!find_field(tag, StateType::Bytes, true, static_cast<u32>(size)))
	{
		return false;
	}
	return read_raw(data, size);
}

bool StateReader::read_bytes(u16 tag, std::vector<u8>& data)
{
	if(!find_field(tag, StateType::Bytes, false, 0))
	{
		return false;
	}
	data.resize(m_last_field_length);
	return read_raw(data.data(), data.size());
}

} // namespace qtreGB
