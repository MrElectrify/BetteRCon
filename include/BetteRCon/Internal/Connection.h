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
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

namespace BetteRCon
{
	namespace Internal
	{
		/*
		 *	Connection is the raw connection to the server. It is callback based, and
		 *	notifies the parent of any incoming data, or outgoing data.
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

			using ConnectCallback_t = std::function<void(const ErrorCode_t&)>;
			using DisconnectCallback_t = std::function<void(const ErrorCode_t&)>;
			using RecvCallback_t = std::function<void(const ErrorCode_t&, const std::optional<Packet>&)>;
			using RecvCallbackMap_t = std::unordered_map<int32_t, RecvCallback_t>;

			using SendQueue_t = std::queue<std::vector<char>>;

			// Creates a disconnected server connection
			Connection(Worker_t& worker);

			// not moveable or copyable
			Connection(const Connection& other) = delete;
			Connection(Connection&& other) = delete;
			Connection& operator=(const Connection& other) = delete;
			Connection& operator=(Connection&& other) = delete;

			// Attempts to asynchronously connect to a remote server, and starts listening for data.
			// Must not be called from any thread other than the worker thread if the worker thread
			// is actively running the worker.
			// @connectCallback is called when either a connection is successfully made, or an error
			// occurs while making the connection.
			// @disconnectCallback is called *only* after a successful connection is ended.
			// A failed connection attempt will not invoke this callback. This callback will be
			// called with the special reason invalid_argument if an internal error occurs
			// and the endpoint is no longer suitable
			void AsyncConnect(const Endpoint_t& endpoint, ConnectCallback_t&& connectCallback, 
				DisconnectCallback_t&& disconnectCallback, RecvCallback_t&& eventCallback) noexcept;

			// Disconnects from the remote endpoint if an active connection exists.
			// Cancels any ongoing requests or connection attempts.
			// Can be called from any thread.
			// Otherwise, the function has no effect.
			void Disconnect() noexcept;

			// Returns whether or not the connection appears to be active
			bool IsConnected() const noexcept;

			// Asynchronously sends a packet to the server.
			// @recvCallback is called when the response is received. It will be called immediately with
			// not_connected if there is not an active connection.
			// Can be called from any thread.
			// It is not called if an error occurs during the request, in which case disconnectCallback is called.
			void SendPacket(const Packet& packet, RecvCallback_t&& callback);
			
			// Cancels any ongoing asynchronous operations.
			// Disconnects an active connection, calling the handler.
			// If an active connection is in progress, this must
			// be called from the worker thread.
			~Connection();
		private:
			void CloseConnection(const ErrorCode_t& ec);

			void HandleReadHeader(const ErrorCode_t& ec, const size_t bytes_transferred);
			void HandleReadBody(const ErrorCode_t& ec, const size_t bytes_transferred);
			void HandleTimeout(const ErrorCode_t& ec);
			void HandleWrite(const ErrorCode_t& ec, const size_t bytes_transferred);

			void SendUnsentBuffers();

			Worker_t& m_worker;

			std::atomic_bool m_connected;

			std::vector<char> m_incomingBuf;

			DisconnectCallback_t m_disconnectCallback;
			RecvCallback_t m_eventCallback;
			RecvCallbackMap_t m_recvCallbacks;
			std::mutex m_recvCallbacksMutex;

			SendQueue_t m_sendQueue;
			std::mutex m_sendQueueMutex;

			Socket_t m_socket;
			asio::steady_timer m_timeoutTimer;
		};
	}
}

#endif