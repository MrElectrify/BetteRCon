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
		enum LoginResult
		{
			LoginResult_OK,					// Success
			LoginResult_PasswordNotSet,		// Password was not set by the server
			LoginResult_InvalidPasswordHash,// Password was incorrect
			LoginResult_Unknown				// Socket-related error, or unknown response. More information can be retreived by GetLastErrorCode()
		};

		using Connection_t = Internal::Connection;
		using Endpoint_t = Connection_t::Endpoint_t;
		using ErrorCode_t = Connection_t::ErrorCode_t;
		using LoginCallback_t = std::function<void(const LoginResult result)>;
		using Packet_t = Internal::Packet;
		using RecvCallback_t = std::function<void(const ErrorCode_t& ec, const std::vector<std::string>& response)>;
		using Worker_t = Connection_t::Worker_t;
		// Default constructor. Creates thread
		Server();

		// Attempts to connect to a server. Throws ErrorCode_t on error
		void Connect(const Endpoint_t& endpoint);
		// Attempts to connect to a server. Returns ErrorCode_t in on error
		void Connect(const Endpoint_t& endpoint, ErrorCode_t& ec) noexcept;

		// Attempts to login to the server using a hashed password. Calls loginCallback on completion with the result
		void Login(const std::string& password, LoginCallback_t&& loginCallback);

		// Attempts to disconnect from an active server. Throws ErrorCode_t on error
		void Disconnect();
		// Attempts to disconnect from an active server. Returns ErrorCode_t in ec error
		void Disconnect(ErrorCode_t& ec) noexcept;

		// Returns whether or not we are connected
		bool IsConnected() const noexcept;

		// Gets the last error code, which will tell why the server disconnected if it did
		ErrorCode_t GetLastErrorCode() const noexcept;

		// Attempts to send a command to the server, and calls recvCallback when the response is received.
		// RecvCallback_t must not block, as it is called from the worker thread
		void SendCommand(const std::vector<std::string>& command, RecvCallback_t&& recvCallback);

		~Server();
	private:
		void HandleEvent(const ErrorCode_t& ec, std::shared_ptr<Packet_t> event);
		void HandleLoginRecvHash(const ErrorCode_t& ec, const std::vector<std::string>& response, const std::string& password, const LoginCallback_t& loginCallback);
		void HandleLoginRecvResponse(const ErrorCode_t& ec, const std::vector<std::string>& response, const LoginCallback_t& loginCallback);

		void MainLoop();

		static int32_t s_lastSequence;

		Worker_t m_worker;
		Connection_t m_connection;
		std::thread m_thread;
	};
}

#endif