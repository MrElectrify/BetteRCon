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
bool g_loadedPlugins = false;

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
		Server::Worker_t worker;
		Server server(worker);

		// try to connect
		Server::Endpoint_t endpoint(asio::ip::make_address_v4(ip), port);

		server.AsyncConnect(endpoint, [&server, &password]
			(const Server::ErrorCode_t& ec)
			{
				// add a test callback on player.onJoin
				server.RegisterCallback("player.onJoin",
					[](const std::vector<std::string>& args)
					{
						BetteRCon::Internal::g_stdOutLog << "OnJoin: " << args.at(1) << '\n';
					});

				server.AsyncLogin(password, [&server](const Server::LoginResult loginRes)
					{
						// notify the main thread that we logged in
						std::lock_guard lock(g_mutex);

						if (loginRes != Server::LoginResult_OK)
						{
							BetteRCon::Internal::g_stdOutLog << "Failed to login to server: " << loginRes << '\n';
							server.Disconnect();
						}
						else
							g_loggedIn = true;

						g_loginComplete = true;
						g_conVar.notify_one();
					},
					[&server]()
					{
						// enable the plugin
						if (server.EnablePlugin("Sample Plugin") == false)
						{
							BetteRCon::Internal::g_stdErrLog << "Failed to enable plugin\n";
							return 1;
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
						[](const Server::PlayerMap_t& players, const Server::TeamMap_t& teams)
					{
						BetteRCon::Internal::g_stdOutLog << "PlayerInfo updated with " << players.size() << " players and " << teams.size() << " teams\n";

						// print the teams and squads
						static const std::string squads[] = { "None", "Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "Golf", "Hotel", "India", "Juliet", "Kilo", "Lima", "Mike", "November", "Oscar", "Papa", "Quebec", "Romeo", "Sierra", "Tango", "Uniform", "Victor", "Whiskey", "X-Ray", "Yankee", "Zulu", "Haggard", "Sweetwater", "Preston", "Redford", "Faith", "Celeste", };

						for (const Server::TeamMap_t::value_type& team : teams)
						{
							std::cout << "Team " << static_cast<uint32_t>(team.first) << " (" << team.second.playerCount << " players, " << team.second.squads.size() << " squads, " << team.second.commanderCount << " commanders):\n";
							for (const Server::SquadMap_t::value_type& squad : team.second.squads)
							{
								std::cout << "\tSquad " << squads[squad.first] << " (" << squad.second.size() << " players):\n";
								for (const auto& player : squad.second)
								{
									const auto& pPlayer = player.second;
									std::cout << "\t\t" << pPlayer->name << " (rank " << static_cast<uint32_t>(pPlayer->rank) << "): " << pPlayer->score << " score, " << pPlayer->kills << " kills, " << pPlayer->deaths << " deaths, " << static_cast<float>(pPlayer->kills) / pPlayer->deaths << " K/D";
									if (pPlayer->ipAddress.size() > 0)
										std::cout << ", IP / port: " << pPlayer->ipAddress << ':' << pPlayer->port;
									std::cout << '\n';
								}
							}
						}
					});

				// schedule the shut down
				server.ScheduleAction([&server] { server.Disconnect(); }, 60000);
			}, {});


	}

	BetteRCon::Internal::g_stdOutLog << "Testing complete\n";

	return 0;
}