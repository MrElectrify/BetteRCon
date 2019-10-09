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

			// Creates a packet from predefined arguments
			Packet(const std::vector<Word>& command);
			// Creates a packet from a received buffer
			Packet(const std::vector<char>& buf);

			// Serializes the packet to a buffer
			void Serialize(std::vector<char>& bufOut);
		private:
			void Deserialize(const std::vector<char>& buf);

			static std::atomic<int32_t> s_lastSequence;

			int32_t m_sequence;
			int32_t m_size;
			std::vector<Word> m_words;
		};
	}
}

#endif