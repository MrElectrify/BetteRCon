#include <BetteRCon/Server.h>
#include <BetteRCon/Internal/Log.h>

#include <functional>
#include <iostream>
#include <string>
#include <vector>

using BetteRCon::Server;
using BetteRCon::Internal::Packet;

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

	Server::Worker_t worker;
	Server server(worker);

	// try to connect
	Server::Endpoint_t endpoint(asio::ip::make_address_v4(ip), port);
	Server::ErrorCode_t ec;

	server.AsyncConnect(endpoint,
		[&server, &password, argc, argv](const Server::ErrorCode_t& ec)
		{
			if (ec)
			{
				BetteRCon::Internal::g_stdErrLog << "Failed to connect: " << ec.message() << '\n';
				return;
			}

			server.AsyncLogin(password, [&server](const Server::LoginResult loginRes)
				{
					if (loginRes != Server::LoginResult_OK)
					{
						BetteRCon::Internal::g_stdErrLog << "Failed to login to server: " << Server::s_LoginResultStr[loginRes] << '\n';
						// we don't care about the result, just disconnect
						server.Disconnect();
					}
				},
				[&server, argc, argv]()
				{
					// enable all of the plugins that are in the list
					for (int i = 4; i < argc; ++i)
					{
						if (server.EnablePlugin(argv[i]) == true)
							BetteRCon::Internal::g_stdOutLog << "Enabled plugin " << argv[i] << '\n';
						else
							BetteRCon::Internal::g_stdErrLog << "Failed to enable plugin " << argv[i] << '\n';
					}
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
					[](const std::vector<std::string>&) {},
					[](const Server::ServerInfo& serverInfo)
				{
					BetteRCon::Internal::g_stdOutLog << "Got serverInfo for " << serverInfo.m_serverName << ": " << serverInfo.m_playerCount << "/" << serverInfo.m_maxPlayerCount << " (" << serverInfo.m_blazePlayerCount << ")\n";
				},
					[](const Server::PlayerMap_t& players, const Server::TeamMap_t& teams)
				{
					BetteRCon::Internal::g_stdOutLog << "PlayerInfo updated with " << players.size() << " players and " << teams.size() << " teams\n";
				});
		}, [](const Server::ErrorCode_t& ec)
		{
			BetteRCon::Internal::g_stdOutLog << "Disconnected for reason " << ec.message() << '\n';
		});

	worker.run();

	BetteRCon::Internal::g_stdOutLog << "Main thread complete\n";

	return 0;
}