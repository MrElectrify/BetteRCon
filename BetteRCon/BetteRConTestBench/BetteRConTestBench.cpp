#include <BetteRCon/Server.h>

#include <functional>
#include <iostream>
#include <string>
#include <vector>

using BetteRCon::Server;
using BetteRCon::Internal::Packet;

int main(int argc, char* argv[])
{
	if (argc != 2 && argc != 3)
	{
		std::cout << "Usage: " << argv[0] << " [ip:string] [port:ushort:OPT]\n";
		return 1;
	}

	// parse IP and port
	char* ip = argv[1];
	uint16_t port = (argc == 3) ? atoi(argv[2]) : 47200;

	{
		Server server;

		// try to connect
		Server::Endpoint_t endpoint(asio::ip::make_address_v4(ip), port);
		Server::ErrorCode_t ec;

		server.Connect(endpoint, ec);

		if (ec)
		{
			std::cout << "Failed to connect: " << ec.message() << '\n';
			return 1;
		}

		// create the serverInfo request
		Packet serverInfoRequest({ "serverInfo" }, 0);

		// send the serverInfo request
		/*conn.SendPacket(serverInfoRequest, [&conn] (const Connection::ErrorCode_t& ec, const std::vector<std::string>& words)
		{
			if (ec)
			{
				std::cout << "Error receiving response: " << ec.message() << '\n';
				return;
			}

			// we got the packet. print the response
			std::cout << "Response: ";
			for (const auto& word : words)
			{
				std::cout << word << ' ';
			}
			std::cout << '\n';

			// close the connection
			conn.Disconnect();
		});*/

		//worker.run();

		/*if (auto e = conn.GetLastErrorCode())
		{
			std::cout << "Error on exit: " << e.message() << '\n';
		}*/
	}

	std::cout << "Testing complete\n";

	return 0;
}