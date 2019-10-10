#include <BetteRCon/Internal/Packet.h>

using BetteRCon::Internal::Packet;

std::atomic<int32_t> Packet::s_lastSequence = 0;

Packet::Packet(const std::string& command) : m_sequence(s_lastSequence++)
{
	
}

Packet::Packet(const std::vector<char>& buf)
{

}