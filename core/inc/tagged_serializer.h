#ifndef QTREGB_TAGGED_SERIALIZER_H
#define QTREGB_TAGGED_SERIALIZER_H

#include <vector>
#include <string>
#include <type_traits>
#include <stdint.h>
#include <string.h>

namespace qtreGB
{

// Vector helper is_vector_v
template<typename T>
struct is_vector : std::false_type {};

template<typename U, typename Alloc>
struct is_vector<std::vector<U, Alloc>> : std::true_type {};

template<typename T>
inline constexpr bool is_vector_v = is_vector<T>::value;

// char array helper is_char_array_v
template<typename T>
struct is_char_array : std::false_type {};

template<std::size_t N>
struct is_char_array<char[N]> : std::true_type {};

template<std::size_t N>
struct is_char_array<const char[N]> : std::true_type {};

template<typename T>
inline constexpr bool is_char_array_v = is_char_array<T>::value;

// TODO (savestates): pending format robustness. Everything is an internal
// change, process(tag, value) and existing callers do not change:
//  - The format does not store the value size, only the tag size, so
//    skipUnknownTag cannot skip an unknown tag (the cursor ends up
//    misaligned). Solution: prefix each entry with a [u32 len] of the value.
//  - readNextTag does not distinguish end of stream from a corrupt tag, and
//    process ignores the return bool (a truncated stream would be swallowed
//    silently).
//  - enum class types are not serialized: is_arithmetic_v is false for them
//    and they would fall into the fallback serialize(*this). Solution: an
//    is_enum_v branch with a cast to the underlying type.
//  - There is no type/size verification when reading: a corrupt stream
//    produces silent corruption instead of a detected error.

class TaggedSerializer
{
	public:
		enum class Mode { Read, Write };

		TaggedSerializer(Mode mode);
		TaggedSerializer(Mode mode, const std::vector<uint8_t>& data);

		const std::vector<uint8_t>& data() const;
		std::vector<uint8_t>& data();

		bool isReading() const;
		bool isWriting() const;

		void reset();

		// --- Core tagged serialization ---
		template<typename T>
		void process(const std::string& tag, T& value);

		// --- Concatenation helper ---
		void append(const TaggedSerializer& other);

	private:
		Mode mode_;
		std::vector<uint8_t> buffer_;
		size_t pos_;

		// ------------------------------
		// Raw I/O
		// ------------------------------
		void writeRaw(const void* data, size_t size);
		bool readRaw(void* dst, size_t size);

		// ------------------------------
		// Tags
		// ------------------------------
		void writeTag(const std::string& tag);
		bool readNextTag(std::string& outTag);
		void skipUnknownTag(const std::string&);

		// ------------------------------
		// Value write (C++17 style)
		// ------------------------------
		template<typename T>
		void writeValue(const T& value);

		// ------------------------------
		// Value read (C++17 style)
		// ------------------------------
		template<typename T>
		void readValue(T& value);
};


template <typename T>
inline void TaggedSerializer::process(const std::string &tag, T &value)
{
	if (isWriting())
	{
		writeTag(tag);
		writeValue(value);
	}
	else
	{
		std::string readTag;
		if(!readNextTag(readTag))
		{
			return; // end of data
		}
		if(readTag == tag)
		{
			readValue(value);
		}
		else
		{
			skipUnknownTag(readTag);
		}
	}
}

template <typename T>
inline void TaggedSerializer::readValue(T &value)
{
	if constexpr(std::is_arithmetic_v<T>)
	{
		readRaw(&value, sizeof(T));
	}
	else if constexpr(std::is_same_v<T, std::string>)
	{
		uint32_t len = 0;
		readRaw(&len, sizeof(len));
		value.resize(len);
		readRaw(value.data(), len);
	}
	else if constexpr(is_vector_v<T>)
	{
		uint32_t len = 0;
		readRaw(&len, sizeof(len));
		value.resize(len);
		for(auto& elem : value)
		{
			readValue(elem);
		}
	}
	else if constexpr(is_char_array_v<T>)
	{
		readRaw(value, sizeof(T));
	}
	else
	{
		// Custom type
		value.serialize(*this);
	}
}

template<typename T>
inline void TaggedSerializer::writeValue(const T& value)
{
	if constexpr(std::is_arithmetic_v<T>)
	{
		writeRaw(&value, sizeof(T));
	}
	else if constexpr(std::is_same_v<T, std::string>)
	{
		uint32_t len = static_cast<uint32_t>(value.size());
		writeRaw(&len, sizeof(len));
		writeRaw(value.data(), len);
	}
	else if constexpr(is_vector_v<T>)
	{
		uint32_t len = static_cast<uint32_t>(value.size());
		writeRaw(&len, sizeof(len));
		for (const auto& elem : value)
		{
			writeValue(elem);
		}
	}
	else if constexpr(is_char_array_v<T>)
	{
		writeRaw(value, sizeof(T));
	}
	else
	{
		// Custom type
		value.serialize(*this);
	}
}


} // namespace qtreGB
#endif
