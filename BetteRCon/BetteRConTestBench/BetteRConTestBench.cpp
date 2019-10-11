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
				std::cout << "Failed to login to server: " << loginRes << '\n';
			else
				g_loggedIn = true;

			g_loginComplete = true;
			g_conVar.notify_one();
		});

		{
			// wait for the login response
			std::unique_lock lock(g_mutex);
			g_conVar.wait(lock, [] { return g_loginComplete == true; });
		}

		if (g_loggedIn == false)
		{
			std::cout << "Failed to login\n";

			Server::ErrorCode_t ec;
			server.Disconnect(ec);

			return 1;
		}

		// send the serverInfo request
		server.SendCommand({ "serverInfo" }, [&server] (const Server::ErrorCode_t& ec, const std::vector<std::string>& words)
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

			// make sure we got an whole response
			assert(words.size() == 26);

			// make sure the response is OK
			assert(words.at(0) == "OK");

			// extract some data as a test
			const auto serverName = words.at(1);
			const auto playerCount = std::stoi(words.at(2));
			const auto maxPlayerCount = std::stoi(words.at(3));
			const auto currentMap = words.at(5);
			const auto currentGamemode = words.at(4);

			// output our test data
			std::cout << "Server Name: " << serverName << '\n';
			std::cout << "Players: " << playerCount << " / " << maxPlayerCount << '\n';
			std::cout << "Map: " << currentMap << '\n';
			std::cout << "Gamemode: " << currentGamemode << '\n';

			// close the connection
			server.Disconnect();

			// notify the main thread that we got our result
			{
				std::lock_guard lock(g_mutex);
				g_responseReceived = true;
				g_conVar.notify_one();
			}
		});

		{
			// wait for the response
			std::unique_lock lock(g_mutex);
			g_conVar.wait(lock, [] { return g_responseReceived == true; });
		}

		if (auto e = server.GetLastErrorCode())
		{
			std::cout << "Error on exit: " << e.message() << '\n';
		}
	}

	std::cout << "Testing complete\n";

	return 0;
}