#include "tagged_serializer.h"

using namespace qtreGB;

TaggedSerializer::TaggedSerializer(Mode mode) :
mode_(mode),
pos_(0)
{}

TaggedSerializer::TaggedSerializer(Mode mode, const std::vector<uint8_t> &data) :
mode_(mode),
buffer_(data),
pos_(0)
{}

const std::vector<uint8_t> &TaggedSerializer::data() const
{
	return buffer_;
}

std::vector<uint8_t> &TaggedSerializer::data()
{
	return buffer_;
}

bool TaggedSerializer::isReading() const
{
	return mode_ == Mode::Read;
}

bool TaggedSerializer::isWriting() const
{
	return mode_ == Mode::Write;
}

void TaggedSerializer::reset()
{
	pos_ = 0;
}

void TaggedSerializer::append(const TaggedSerializer &other)
{
	buffer_.insert(buffer_.end(), other.buffer_.begin(), other.buffer_.end());
}

void TaggedSerializer::writeRaw(const void *data, size_t size)
{
	const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data);
	buffer_.insert(buffer_.end(), ptr, ptr + size);
}

void TaggedSerializer::writeTag(const std::string &tag)
{
	uint16_t len = static_cast<uint16_t>(tag.size());
	writeRaw(&len, sizeof(len));
	writeRaw(tag.data(), len);
}

bool TaggedSerializer::readRaw(void *dst, size_t size)
{
	if(pos_ + size > buffer_.size())
	{
		return false;
	}
	memcpy(dst, &buffer_[pos_], size);
	pos_ += size;
	return true;
}

bool TaggedSerializer::readNextTag(std::string &outTag)
{
	uint16_t len = 0;
	if(!readRaw(&len, sizeof(len)) || len == 0)
	{
		return false;
	}
	outTag.resize(len);
	return readRaw(&outTag[0], len);
}

void TaggedSerializer::skipUnknownTag(const std::string &)
{
	// TODO: implement the actual skip - requires storing the value size
	// in the format (see TODO in tagged_serializer.h)
}
