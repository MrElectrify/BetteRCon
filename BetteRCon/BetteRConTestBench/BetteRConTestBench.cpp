#include <BetteRCon/Server.h>
#include <BetteRCon/Internal/Packet.h>

#include <functional>
#include <iostream>
#include <string>
#include <vector>

using BetteRCon::Internal::Connection;
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

	// we need a worker
	Connection::Worker_t worker;

	Connection conn(worker, [](const Connection::ErrorCode_t&, std::shared_ptr<Packet>) {});

	// try to connect
	Connection::Endpoint_t endpoint(asio::ip::make_address_v4(ip), port);
	Connection::ErrorCode_t ec;
	
	conn.Connect(endpoint, ec);

	if (ec)
	{
		std::cout << "Failed to connect: " << ec.message() << '\n';
		return 1;
	}

	// create the serverInfo request
	Packet serverInfoRequest({ "serverInfo" }, 0);

	// send the serverInfo request
	conn.SendPacket(serverInfoRequest, [&conn] (const Connection::ErrorCode_t& ec, std::shared_ptr<Packet> pPacket)
	{
		if (ec)
		{
			std::cout << "Error receiving response: " << ec.message() << '\n';
			return;
		}

		// we got the packet. print the response
		std::cout << "Response: ";
		for (const auto& word : pPacket->GetWords())
		{
			std::cout << word << ' ';
		}
		std::cout << '\n';

		// close the connection
		conn.Disconnect();
	});

	worker.run();

	std::cout << "Testing complete\n";

	return 0;
}