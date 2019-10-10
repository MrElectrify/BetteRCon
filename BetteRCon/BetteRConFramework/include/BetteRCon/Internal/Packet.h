#ifndef BETTERCON_INTERNAL_PACKET_H_
#define BETTERCON_INTERNAL_PACKET_H_

/*
 *	Packet implmentation
 *	10/9/19 16:07
 */

// STL
#include <cstdint>
#include <system_error>
#include <string>
#include <vector>

namespace BetteRCon
{
	namespace Internal
	{
		/*
		 *	Packet is the format used by Frostbite ServerAdministration,
		 *	to properly split up arguments and transport them over a
		 *	network socket.
		 */
		class Packet
		{
		public:
			// Each word of a packet has a special format.
			using Word = std::string;

			// Creates a packet from a vector of arguments
			Packet(const std::vector<Word>& command, const int32_t sequence);
			// Creates a packet from a received buffer
			Packet(const std::vector<char>& buf);

			// Gets whether or not the packet was from the client
			bool IsFromServer() const;
			// Gets whether or not the packet was a response
			bool IsResponse() const;
			// Get the sequence of the packet
			int32_t GetSequence() const;
			// Gets the total packet size in bytes (max 16384)
			int32_t GetSize() const;

			// Gets the arguments from the packet
			std::vector<Word> GetWords() const;

			// Serializes the packet to a buffer
			void Serialize(std::vector<char>& bufOut) const;
		private:
			bool m_fromServer;
			bool m_response;
			int32_t m_sequence;
			int32_t m_size;
			std::vector<Word> m_words;
		};
	}
}

#endif