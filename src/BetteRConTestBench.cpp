#include <BetteRCon/Server.h>

#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

using BetteRCon::Server;
using BetteRCon::Internal::Packet;

std::condition_variable g_conVar;
std::mutex g_mutex;
bool g_loggedIn = false;
bool g_loginComplete = false;
bool g_responseReceived = false;

int main(int argc, char* argv[])
{
	if (argc != 4)
	{
		std::cout << "Usage: " << argv[0] << " [ip:string] [port:ushort] [password:string]\n";
		return 1;
	}

	// parse IP and port
	char* ip = argv[1];
	uint16_t port = atoi(argv[2]);
	std::string password = argv[3];

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

		server.Login(password, [&server](const Server::LoginResult loginRes)
		{
			// notify the main thread that we logged in
			std::lock_guard lock(g_mutex);

			if (loginRes != Server::LoginResult_OK)
			{
				std::cout << "Failed to login to server: " << loginRes << '\n';

				// we don't care about the result, just disconnect
				Server::ErrorCode_t ec;
				server.Disconnect(ec);
			}
			else
				g_loggedIn = true;

			g_loginComplete = true;
			g_conVar.notify_one();
		}, 
			[](const Server::ErrorCode_t& ec) {},
			[](const std::vector<std::string>& eventWords)
		{
			std::cout << "Event " << eventWords.at(0) << ": ";

			// print out the event for debugging
			for (size_t i = 1; i < eventWords.size(); ++i)
			{
				std::cout << eventWords.at(i) << ' ';
			}
			std::cout << '\n';
		}, 
			[](const Server::ServerInfo& serverInfo)
		{
			std::cout << "Got serverInfo for " << serverInfo.m_serverName << ": " << serverInfo.m_playerCount << "/" << serverInfo.m_maxPlayerCount << " (" << serverInfo.m_blazePlayerCount << ")\n";
		}, 
			[] (const Server::PlayerInfo& playerInfo) {});

		{
			// wait for the login response
			std::unique_lock lock(g_mutex);
			g_conVar.wait(lock, [] { return g_loginComplete == true; });
		}

		if (g_loggedIn == false)
		{
			std::cout << "Failed to login\n";
			return 1;
		}

		// wait for 30 seconds to capture any events
		std::this_thread::sleep_for(std::chrono::seconds(30));

		// close the connection
		server.Disconnect();

		if (auto e = server.GetLastErrorCode())
		{
			std::cout << "Error on exit: " << e.message() << '\n';
		}
	}

	std::cout << "Testing complete\n";

	return 0;
}