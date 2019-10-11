#include <BetteRCon/Internal/Packet.h>

using BetteRCon::Internal::Packet;

Packet::Packet(const std::vector<Word>& command, const int32_t sequence, bool response) : m_response(response), m_sequence(sequence)
{
	// Sequence, Size, and NumWords
	m_size = sizeof(int32_t) * 3;

	for (auto commandPart : command)
	{
		// wordSize size
		m_size += sizeof(int32_t);
		// null terminator
		m_size += commandPart.size() + sizeof(char);

		// move the word into our local storage
		m_words.emplace_back(std::move(commandPart));
	}

	m_fromServer = true;
}

// assume the packet is the size that it says that it is, and that it is properly formed
/// TODO: Add some bounds checking and error handling
Packet::Packet(const std::vector<char>& buf)
{
	// our networking design guarantees we at least have the first 8 bytes. make sure we have more
	size_t offset = 0;

	// parse the packet
	const auto sequence = *reinterpret_cast<const int32_t*>(&buf[offset]);
	offset += sizeof(int32_t);

	m_fromServer = (sequence >> 31) & 1;
	m_response = (sequence >> 30) & 1;

	m_sequence = sequence & 0x3FFFFFFF;

	m_size = *reinterpret_cast<const int32_t*>(&buf[offset]);
	offset += sizeof(int32_t);

	// make sure we have the space

	const auto numWords = *reinterpret_cast<const int32_t*>(&buf[sizeof(int32_t) * 2]);
	offset += sizeof(int32_t);

	// parse each word
	for (int32_t i = 0; i < numWords; ++i)
	{
		const auto wordSize = *reinterpret_cast<const int32_t*>(&buf[offset]);
		
		offset += sizeof(int32_t);
		// capture the string
		m_words.emplace_back(Word(&buf[offset], wordSize));

		// null terminator space
		offset += wordSize + 1;
	}
}

bool Packet::IsFromServer() const
{
	return m_fromServer == true;
}

bool Packet::IsResponse() const
{
	return m_response == true;
}

int32_t Packet::GetSequence() const
{
	return m_sequence;
}

int32_t Packet::GetSize() const
{
	return m_size;
}

std::vector<Packet::Word> Packet::GetWords() const
{
	return m_words;
}

void Packet::Serialize(std::vector<char>& bufOut) const
{
	// make space for our buffer
	bufOut.resize(m_size);
	
	size_t offset = 0;

	// write the sequence
	*reinterpret_cast<int32_t*>(&bufOut[offset]) = (m_fromServer << 31) | (m_response << 30) | m_sequence;
	offset += sizeof(int32_t);

	// write the size
	*reinterpret_cast<int32_t*>(&bufOut[offset]) = m_size;
	offset += sizeof(int32_t);

	// write the number of words
	*reinterpret_cast<int32_t*>(&bufOut[offset]) = m_words.size();
	offset += sizeof(int32_t);

	// write the words
	for (const auto& word : m_words)
	{
		// write word size
		*reinterpret_cast<int32_t*>(&bufOut[offset]) = word.size();
		offset += sizeof(int32_t);

		// write the word and the null terminator
		memcpy(&bufOut[offset], word.data(), word.size());
		offset += word.size();
		bufOut[offset] = '\0';
		offset += sizeof(char);
	}
}