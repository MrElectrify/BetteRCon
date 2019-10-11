#ifndef BETTERCON_BETTERCONSERVER_H_
#define BETTERCON_BETTERCONSERVER_H_

/*
 *	BetteRCon Server Main
 *	10/9/19 01:14
 */

 // BetteRCon
#include <BetteRCon/Internal/Connection.h>

// STL
#include <mutex>
#include <thread>
#include <vector>

namespace BetteRCon
{
	/*
	 *	BetteRConServer is a class signifying a connection to a Battlefield server.
	 *	It encompasses the connection itself, the thread, layer, player management,
	 *	and plugins.
	 */
	class Server
	{
	public:
		using Connection_t = Internal::Connection;
		using Endpoint_t = Connection_t::Endpoint_t;
		using ErrorCode_t = Connection_t::ErrorCode_t;
		using Packet_t = Internal::Packet;
		using RecvCallback_t = std::function<void(const ErrorCode_t& ec, const std::vector<std::string>& response)>;
		using Worker_t = Connection_t::Worker_t;
		// Default constructor. Creates thread
		Server();

		// Attempts to connect to a server. Throws ErrorCode_t on error
		void Connect(const Endpoint_t& endpoint);
		// Attempts to connect to a server. Returns ErrorCode_t in on error
		void Connect(const Endpoint_t& endpoint, ErrorCode_t& ec) noexcept;

		// Returns whether or not we are connected
		bool IsConnected() const noexcept;

		// Attempts to send a command to the server, and calls recvCallback when the response is received
		void SendCommand(const std::vector<std::string>& command, RecvCallback_t&& recvCallback);

		~Server();
	private:
		void HandleEvent(const ErrorCode_t& ec, std::shared_ptr<Packet_t> event);
		
		void MainLoop();

		Worker_t m_worker;
		Connection_t m_connection;
		std::thread m_thread;
	};
}

#endif