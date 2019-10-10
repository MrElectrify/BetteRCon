#ifndef BETTERCON_INTERNAL_PACKET_H_
#define BETTERCON_INTERNAL_PACKET_H_

/*
 *	Packet implmentation
 *	10/9/19 16:07
 */

// STL
#include <atomic>
#include <cstdint>
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

			// Creates a packet from predefined arguments, separated by spaces. Arguments with spaces must go in quotes
			Packet(const std::string& command);
			// Creates a packet from a received buffer
			Packet(const std::vector<char>& buf);

			// Gets whether or not the packet was from the client
			bool IsFromClient() const;
			// Gets whether or not the packet was a response
			bool IsResponse() const;
			// Get the sequence of the packet
			int32_t GetSequence() const;
			// Gets the total packet size in bytes (max 16384)
			int32_t GetSize() const;

			// Gets the arguments from the packet
			std::vector<Word> GetWords() const;

			// Serializes the packet to a buffer
			void Serialize(std::vector<char>& bufOut);
		private:
			void Deserialize(const std::vector<char>& buf);

			static std::atomic<int32_t> s_lastSequence;

			bool m_fromClient;
			bool m_response;
			int32_t m_sequence;
			int32_t m_size;
			std::vector<Word> m_words;
		};
	}
}

#endif