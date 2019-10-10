#include <BetteRCon/Internal/Packet.h>

using BetteRCon::Internal::Packet;

std::atomic<int32_t> Packet::s_lastSequence = 0;

Packet::Packet(const std::string& command) : m_sequence(s_lastSequence++)
{
	size_t offset = 0;
	// Sequence, Size, and NumWords
	m_size = sizeof(int32_t) * 3;
	while (offset < command.size() - 1)
	{
		// size of word
		m_size += sizeof(int32_t);

		// parse the commandLine
		auto nextSpace = command.find(' ', offset);
		if (nextSpace == std::string::npos)
		{
			// we reached the end of the string. send it all if there is not a quote
			auto nextQuote = command.find('\"', offset);
			if (nextQuote == std::string::npos ||
				nextQuote != offset)
			{
				// make sure that we add the null terminator for all words
				m_words.push_back(command.substr(offset) + '\0');
				m_size += command.size() - (offset + 1) + 1;
			}
			else
			{
				nextQuote = command.find('\"', offset + 1);
				if (nextQuote != std::string::npos)
				{
					// there is a quoted word, check if it is the last character
					if (nextQuote == command.size() - 1)
					{
						// add the word between the quotes
						m_words.push_back(command.substr(offset + 1, nextQuote - (offset + 1)) + '\0');
						m_size += nextQuote - (offset + 1) + 1;
					}
					else
					{
						// just add the whole word
						m_words.push_back(command.substr(offset) + '\0');
						m_size += command.size() - offset + 1;
					}
				}
				else
				{
					// add the rest of the word after the quote
					m_words.push_back(command.substr(offset + 1) + '\0');
					m_size += command.size() - (offset + 1) + 1;
				}
			}
			break;
		}
		// check for quotes
		auto nextQuote = command.find('\"', offset);
		if (nextQuote < nextSpace)
		{
			// the next quote is before the next space, check if it is in the first position
			if (nextQuote != offset)
			{
				// just add the word
				m_words.push_back(command.substr(offset, nextSpace - offset) + '\0');
				m_size += nextSpace - offset + 1;
				offset = nextSpace + 1;
				continue;
			}
			// find the next quote after
			nextQuote = command.find('\"', offset + 1);
			if (nextQuote == std::string::npos)
			{
				// we reached the end of the string without a matching quote. just add the rest of the string
				m_words.push_back(command.substr(offset) + '\0');
				m_size += command.size() - offset + 1;
				break;
			}
			// we found the second quote. add the word
			m_words.push_back(command.substr(offset + 1, nextQuote - (offset + 1)) + '\0');
			m_size += nextQuote - (offset + 1) + 1;
			offset = nextQuote + 2;
			continue;
		}
		// we didn't find any quotes before the next space, add the word
		m_words.push_back(command.substr(offset, nextSpace - offset) + '\0');
		m_size += nextSpace - offset + 1;
		offset = nextSpace + 1;
	}

	// this is a request from the client
	m_fromClient = true;
	m_response = false;
}

Packet::Packet(const std::vector<Word>& command)
{
	// Sequence, Size, and NumWords
	m_size = sizeof(int32_t) * 3;
}

// assume the packet is the size that it says that it is, and that it is properly formed
/// TODO: Add some bounds checking and error handling
Packet::Packet(const std::vector<char>& buf)
{
	size_t offset = 0;

	// parse the packet
	const auto sequence = *reinterpret_cast<const int32_t*>(&buf[offset]);
	offset += sizeof(int32_t);

	m_fromClient = (sequence >> 31) & 1;
	m_response = (sequence >> 30) & 1;

	m_sequence = sequence & 0x3FFFFFFF;

	m_size = *reinterpret_cast<const int32_t*>(&buf[offset]);
	offset += sizeof(int32_t);

	const auto numWords = *reinterpret_cast<const int32_t*>(&buf[sizeof(int32_t) * 2]);
	offset += sizeof(int32_t);

	// parse each word
	for (int32_t i = 0; i < numWords; ++i)
	{
		const auto wordSize = *reinterpret_cast<const int32_t*>(&buf[offset]);
		
		offset += sizeof(int32_t);
		// capture the string
		m_words.emplace_back(Word(&buf[offset], wordSize));
		offset += wordSize;
	}
}

bool Packet::IsFromClient() const
{
	return m_fromClient == true;
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
	*reinterpret_cast<int32_t*>(&bufOut[offset]) = (m_fromClient << 31) | (m_response << 30) | m_sequence;
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

		// write the word
		memcpy(&bufOut[offset], word.data(), word.size());
		offset += word.size();
	}
}