#include <BetteRCon/Server.h>
#include <BetteRCon/Internal/Log.h>

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

bool g_disconnected = false;

int main(int argc, char* argv[])
{
	if (argc < 4)
	{
		std::cout << "Usage: " << argv[0] << " [ip:string] [port:ushort] [password:string] [plugins:string...]\n";
		return 1;
	}

	// parse IP and port
	const char* ip = argv[1];
	const uint16_t port = atoi(argv[2]);
	const std::string password = argv[3];

	Server server;

	// try to connect
	Server::Endpoint_t endpoint(asio::ip::make_address_v4(ip), port);
	Server::ErrorCode_t ec;

	server.Connect(endpoint, ec);

	if (ec)
	{
		BetteRCon::Internal::g_stdErrLog << "Failed to connect: " << ec.message() << '\n';
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
		[](const Server::ErrorCode_t& ec)
	{
		BetteRCon::Internal::g_stdOutLog << "Disconnected for reason " << ec.message() << '\n';

		// notify the main thread that we disconnected
		std::lock_guard lock(g_mutex);

		g_disconnected = true;
		g_conVar.notify_one();
	},
		[](const std::string& pluginName, const bool load, const bool success, const std::string& failReason)
	{
		if (load == true)
		{
			if (success == true)
				BetteRCon::Internal::g_stdOutLog << "Loaded plugin " << pluginName << '\n';
			else
				BetteRCon::Internal::g_stdErrLog << "Failed to load plugin " << pluginName << ": " << failReason << '\n';
		}
		else
			BetteRCon::Internal::g_stdOutLog << "Unloaded plugin " << pluginName << '\n';
	},
		[](const std::vector<std::string>& eventWords)
	{
		BetteRCon::Internal::g_stdOutLog << "Event " << eventWords.front() << ": ";

		// special case for punkBuster.onMessage
		if (eventWords.front() == "punkBuster.onMessage")
		{
			auto& message = eventWords.at(1);
			std::cout << message.substr(0, message.size() - 1) << '\n';
			return;
		}

		// print out the event for debugging
		for (size_t i = 1; i < eventWords.size(); ++i)
		{
			std::cout << eventWords.at(i) << ' ';
		}
		std::cout << '\n';
	},
		[](const Server::ServerInfo& serverInfo)
	{
		BetteRCon::Internal::g_stdOutLog << "Got serverInfo for " << serverInfo.m_serverName << ": " << serverInfo.m_playerCount << "/" << serverInfo.m_maxPlayerCount << " (" << serverInfo.m_blazePlayerCount << ")\n";
	},
		[](const Server::PlayerMap_t& players, const Server::TeamSquadMap_t& teams)
	{
		std::cout << "PlayerInfo updated with " << players.size() << " players and " << teams.size() << " teams\n";
	});

	{
		// wait for the login response
		std::unique_lock lock(g_mutex);
		g_conVar.wait(lock, [] { return g_loginComplete == true; });
	}

	if (g_loggedIn == false)
	{
		BetteRCon::Internal::g_stdOutLog << "Failed to login\n";
		return 1;
	}

	// enable all of the plugins that are in the list
	for (int i = 4; i < argc; ++i)
	{
		if (server.EnablePlugin(argv[i]) == true)
			BetteRCon::Internal::g_stdOutLog << "Enabled plugin " << argv[i] << '\n';
		else
			BetteRCon::Internal::g_stdErrLog << "Failed to enable plugin " << argv[i] << '\n';
	}

	// wait until we disconnect for some reason
	{
		// wait for the login response
		std::unique_lock lock(g_mutex);
		g_conVar.wait(lock, [] { return g_disconnected == true; });
	}

	return 0;
}