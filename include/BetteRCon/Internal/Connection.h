#ifndef BETTERCON_INTERNAL_CONNECTION_H_
#define BETTERCON_INTERNAL_CONNECTION_H_

/*
 *	Server Connection
 *	10/9/19 13:22
 */

// BetteRCon
#include <BetteRCon/Internal/Packet.h>

// ASIO
#define ASIO_STANDALONE 1
#include <asio.hpp>

// STL
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace BetteRCon
{
	namespace Internal
	{
		/*
		 *	Connection is the raw connection to the server. It is callback based, and
		 *	notifies the parent of any incoming data, or outgoing data. Sending and receiving
		 *	data is not guaranteed to be thread safe.
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
			using RecvCallback_t = std::function<void(const ErrorCode_t&, std::shared_ptr<Packet>)>;

			// Creates a server connection
			Connection(Worker_t& worker, RecvCallback_t&& eventCallback);

			// Attempts to connect to a remote server, and starts the read loop. Throws ErrorCode_t on error
			void Connect(const Endpoint_t& endpoint);
			// Attempts to connect to a remote server, and starts the read loop. Returns ErrorCode_t in ec on error
			void Connect(const Endpoint_t& endpoint, ErrorCode_t& ec) noexcept;

			// Attempts to disconnect from the remote endpoint. Throws ErrorCode_t on error
			void Disconnect();
			// Attempts to disconnect from the remote endpoint. Returns ErrorCode_t in ec on error
			void Disconnect(ErrorCode_t& ec) noexcept;

			// Gets the last error code, which will tell why the server disconnected if it did
			ErrorCode_t GetLastErrorCode() const noexcept;

			// Returns whether or not the connection is active
			bool IsConnected() const noexcept;

			// Asynchronously sends a packet to the server, and calls the callback when the response is received, or immediately if an error occurs
			// RecvCallback signature: void(const ErrorCode_t&, const Packet&)
			void SendPacket(const Packet& packet, RecvCallback_t&& callback);
		private:
			void CloseConnection(const ErrorCode_t& ec);

			void HandleReadHeader(const ErrorCode_t& ec, const size_t bytes_transferred);
			void HandleReadBody(const ErrorCode_t& ec, const size_t bytes_transferred);
			void HandleTimeout(const ErrorCode_t& ec);
			void HandleWrite(const ErrorCode_t& ec, const size_t bytes_transferred);

			ErrorCode_t m_lastErrorCode;

			bool m_connected;

			std::vector<char> m_outgoingBuf;
			std::vector<char> m_incomingBuf;

			RecvCallback_t m_eventCallback;
			std::map<int32_t, RecvCallback_t> m_recvCallbacks;

			Socket_t m_socket;

			asio::steady_timer m_timeoutTimer;
		};
	}
}

#endif