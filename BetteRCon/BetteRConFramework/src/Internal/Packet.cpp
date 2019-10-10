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
				m_words.push_back(command.substr(offset));
				m_size += command.size() - offset;
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
						m_words.push_back(command.substr(offset + 1, nextQuote - (offset + 1)));
						m_size += nextQuote - (offset + 1);
					}
					else
					{
						// just add the whole word
						m_words.push_back(command.substr(offset));
						m_size += command.size() - offset;
					}
				}
				else
				{
					// add the rest of the word after the quote
					m_words.push_back(command.substr(offset + 1));
					m_size += command.size() - (offset + 1);
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
				m_words.push_back(command.substr(offset, nextSpace - offset));
				m_size += nextSpace - offset;
				offset = nextSpace + 1;
				continue;
			}
			// find the next quote after
			nextQuote = command.find('\"', offset + 1);
			if (nextQuote == std::string::npos)
			{
				// we reached the end of the string without a matching quote. just add the rest of the string
				m_words.push_back(command.substr(offset));
				m_size += command.size() - offset;
				break;
			}
			// we found the second quote. add the word
			m_words.push_back(command.substr(offset + 1, nextQuote - (offset + 1)));
			m_size += nextQuote - (offset + 1);
			offset = nextQuote + 2;
			continue;
		}
		// we didn't find any quotes before the next space, add the word
		m_words.push_back(command.substr(offset, nextSpace - offset));
		m_size += nextSpace - offset;
		offset = nextSpace + 1;
	}

	// this is a request from the client
	m_fromClient = true;
	m_response = false;
}

Packet::Packet(const std::vector<char>& buf)
{

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