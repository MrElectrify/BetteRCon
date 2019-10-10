#include <BetteRCon/Server.h>
#include <BetteRCon/Internal/Packet.h>

#include <iostream>
#include <string>
#include <vector>

using BetteRCon::Internal::Connection;
using BetteRCon::Internal::Packet;

int main(int argc, char* argv[])
{
	/*if (argc != 2 && argc != 3)
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
	}*/

	// make a packet
	Packet p("testCommand arg1 \"arg 2\" \"\"");

	assert(p.GetWords().size() == 4);
	assert(p.GetSequence() == 0);
	assert(p.GetSize() == 52);

	// serialize the packet to a buffer
	std::vector<char> outBuf;
	p.Serialize(outBuf);

	assert(outBuf.size() == p.GetSize());

	// deserialize the packet from the buffer
	Packet r(outBuf);

	assert(r.GetWords().size() == 4);
	assert(r.GetSequence() == 0);
	assert(r.GetSize() == 52);

	return 0;
}