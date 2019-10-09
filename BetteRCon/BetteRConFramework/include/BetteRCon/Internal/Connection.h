#ifndef BETTERCON_INTERNAL_CONNECTION_H_
#define BETTERCON_INTERNAL_CONNECTION_H_

/*
 *	Server Connection
 *	10/9/19 13:22
 */

// ASIO
#include <asio.hpp>

// STL
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

			// Creates a server connection
			Connection(Worker_t& worker);

			// Attempts to connect to a remote server. Throws ErrorCode_t on error
			void Connect(const Endpoint_t& endpoint);
			// Attempts to connect to a remote server. Returns ErrorCode_t in ec on error
			void Connect(const Endpoint_t endpoint, ErrorCode_t& ec);
		};
	}
}

#endif