#ifndef BETTERCON_INTERNAL_CONNECTION_H_
#define BETTERCON_INTERNAL_CONNECTION_H_

/*
 *	Server Connection
 *	10/9/19 13:22
 */

// BetteRCon
#include <BetteRCon/Internal/Packet.h>

// ASIO
#include <asio.hpp>

// STL
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace BetteRCon
{
	namespace Internal
	{
		/*
		 *	Connection is the raw connection to the server. It is callback based, and
		 *	notifies the parent of any incoming data, or outgoing data. Sending and receiving
		 *	data is not thread safe.
		 */
		class Connection
		{
		public:
			using Buffer_t = std::vector<char>;
			using ErrorCode_t = asio::error_code;
			using Worker_t = asio::io_context;
			using Proto_t = asio::ip::tcp;			
			using Endpoint_t = Proto_t::endpoint;
			using Socket_t = Proto_t::socket;

			// Creates a server connection
			Connection(Worker_t& worker);

			// Attempts to connect to a remote server. Throws ErrorCode_t on error
			void Connect(const Endpoint_t& endpoint);
			// Attempts to connect to a remote server. Returns ErrorCode_t in ec on error
			void Connect(const Endpoint_t& endpoint, ErrorCode_t& ec) noexcept;

			// Returns whether or not the connection is active
			bool IsConnected() const noexcept;

			// Sends a packet to the server. Throws ErrorCode_t on error
			void SendPacket(const Packet& packet);
			// Sends a packet to the server. Returns ErrorCode_t in ec on error
			void SendPacket(const Packet& packet, ErrorCode_t& ec) noexcept;

			// Receives a packet from the server. Throws ErrorCode_t on error
			std::shared_ptr<Packet> RecvPacket(int32_t sequence);
			// Receives a packet from the server. Returns ErrorCode_t in ec on error
			std::shared_ptr<Packet> RecvPacket(int32_t sequence, ErrorCode_t& ec) noexcept;
		private:
			void CloseConnection();

			void HandleWrite(const ErrorCode_t& ec, const size_t bytes_transferred);

			std::vector<char> m_outgoingBuf;
			std::vector<char> m_incomingBuf;

			std::map<int32_t, Packet> m_incomingPackets;
			std::mutex m_incomingPacketMutex;

			Socket_t m_socket;
		};
	}
}

#endif