#include <BetteRCon/Server.h>
#include <BetteRCon/Internal/Packet.h>

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

	Connection conn(worker);

	// try to connect
	Connection::Endpoint_t endpoint(asio::ip::make_address_v4(ip), port);
	Connection::ErrorCode_t ec;
	
	conn.Connect(endpoint, ec);

	if (ec)
	{
		std::cout << "Failed to connect: " << ec.message() << '\n';
		return 1;
	}

	

	return 0;
}