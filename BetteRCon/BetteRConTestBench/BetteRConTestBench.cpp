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


	// create the serverInfo request
	Packet serverInfoRequest({ "serverInfo" }, 0);

	// send the serverInfo request
	conn.SendPacket(serverInfoRequest);

	if (ec)
	{
		if (ec == asio::error::not_connected)
		{
			// we are not connected. fetch the error
			ec = conn.GetLastErrorCode();
		}
		std::cout << "Failed to send packet: " << ec.message() << '\n';
		return 1;
	}

	std::function<void()> job = [&conn, &job, &worker]() -> void
	{
		Connection::ErrorCode_t ec;

		// wait for the packet to arrive
		auto pPacket = conn.RecvResponse(0, ec);

		if (ec)
		{
			if (ec == asio::error::not_connected)
			{
				// we are not connected. fetch the error
				ec = conn.GetLastErrorCode();
			}
			std::cout << "Failed to send packet: " << ec.message() << '\n';
			return;
		}

		if (pPacket == nullptr)
		{
			// add the job again
			worker.post(job);
			return;
		}

		// we got the packet. print the response
		std::cout << "Response: ";
		for (const auto& word : pPacket->GetWords())
		{
			std::cout << word << ':';
		}
		std::cout << '\n';

		// close the connection
		conn.Disconnect();
	};

	worker.post(job);

	worker.run();

	std::cout << "Testing complete\n";

	return 0;
}