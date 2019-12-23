#include <BetteRCon/Plugin.h>
#include <BetteRCon/Server.h>
#include <MD5.h>

#include <filesystem>

#ifdef _WIN32
#include <Windows.h>
#elif __linux__
#include <dlfcn.h>
#endif

#ifdef _WIN32
#define BLoadLibrary(fileName) LoadLibraryA(fileName)
#define BFindFunction(hLibrary, functionName) GetProcAddress(hLibrary, functionName)
#define BFreeLibrary(hLibrary) FreeLibrary(hLibrary)
#elif __linux__
#define BLoadLibrary(fileName) dlopen(fileName, RTLD_LAZY)
#define BFindFunction(hLibrary, functionName) dlsym(hLibrary, functionName)
#define BFreeLibrary(hLibrary) dlclose(hLibrary)
#endif

using BetteRCon::Server;

const std::string Server::s_LoginResultStr[] = { "OK", "Password was not set by the server", "Password was incorrect", "Unknown" };
int32_t Server::s_lastSequence = 0;

Server::Server() 
	: m_gotServerInfo(false), m_gotServerPlayers(false),
	m_initializedServer(false),
	m_connection(m_worker, 
		std::bind(&Server::HandleEvent, this, 
			std::placeholders::_1, std::placeholders::_2)),
	m_serverInfoTimer(m_worker), m_playerInfoTimer(m_worker), 
	m_punkbusterPlayerListTimer(m_worker)
{
	// initialize all serverInfo stuff to 0
	m_serverInfo.m_playerCount = 0;
	m_serverInfo.m_maxPlayerCount = 0;
	m_serverInfo.m_roundsPlayed = 0;
	m_serverInfo.m_roundsTotal = 0;
	m_serverInfo.m_scores.m_goalScore = 0;
	m_serverInfo.m_ranked = false;
	m_serverInfo.m_punkBuster = false;
	m_serverInfo.m_hasPassword = false;
	m_serverInfo.m_serverUpTime = 0;
	m_serverInfo.m_roundTime = 0;
	m_serverInfo.m_joinQueueEnabled = false;
	m_serverInfo.m_matchmakingEnabled = false;
	m_serverInfo.m_blazePlayerCount = 0;
}

void Server::Connect(const Endpoint_t& endpoint)
{
	// try to connect to the server
	m_connection.Connect(endpoint);

	// we are successfully connected. start the worker thread
	m_thread = std::thread(std::bind(static_cast<size_t(Worker_t::*)()>(&Worker_t::run), &m_worker));
}

void Server::Connect(const Endpoint_t& endpoint, ErrorCode_t& ec) noexcept
{
	try
	{
		Connect(endpoint);
	}
	catch (const ErrorCode_t& e)
	{
		ec = e;
	}
}

void Server::Login(const std::string& password, LoginCallback_t&& loginCallback, DisconnectCallback_t&& disconnectCallback, FinishedLoadingPluginsCallback_t&& finishedLoadingPluginsCallback, PluginCallback_t&& pluginCallback, EventCallback_t&& eventCallback, ServerInfoCallback_t&& serverInfoCallback, PlayerInfoCallback_t&& playerInfoCallback)
{
	// send the login request
	SendCommand({ "login.hashed" }, 
		std::bind(&Server::HandleLoginRecvHash, this,
			std::placeholders::_1, std::placeholders::_2, password, loginCallback));

	// store the callbacks
	m_disconnectCallback = std::move(disconnectCallback);
	m_eventCallback = std::move(eventCallback);
	m_finishedLoadingPluginsCallback = std::move(finishedLoadingPluginsCallback);
	m_pluginCallback = std::move(pluginCallback);
	m_serverInfoCallback = std::move(serverInfoCallback);
	m_playerInfoCallback = std::move(playerInfoCallback);
}

void Server::Disconnect()
{
	m_connection.Disconnect();

	ErrorCode_t ec;

	ClearContainers();

	// kill timers
	m_serverInfoTimer.cancel(ec);

	if (ec)
		throw ec;
}

void Server::Disconnect(ErrorCode_t& ec) noexcept
{
	m_connection.Disconnect(ec);
}

bool Server::IsConnected() const noexcept
{
	return m_connection.IsConnected() == true;
}

const Server::ServerInfo& Server::GetServerInfo() const noexcept
{
	return m_serverInfo;
}

const Server::PlayerMap_t& Server::GetPlayers() const noexcept
{
	return m_players;
}

const Server::TeamMap_t& Server::GetTeams() const noexcept
{
	return m_teams;
}

const Server::Team& Server::GetTeam(const uint8_t teamId) const noexcept
{
	static const Team emptyTeam;

	const TeamMap_t::const_iterator teamIt = m_teams.find(teamId);
	if (teamIt == m_teams.end())
		return emptyTeam;

	return teamIt->second;
}

const Server::PlayerMap_t& Server::GetSquad(const uint8_t teamId, const uint8_t squadId) const noexcept
{
	static const PlayerMap_t emptySquad;

	const SquadMap_t& squadMap = GetTeam(teamId).squads;

	const SquadMap_t::const_iterator squadIt = squadMap.find(squadId);
	if (squadIt == squadMap.end())
		return emptySquad;

	return squadIt->second;
}

Server::ErrorCode_t Server::GetLastErrorCode() const noexcept
{
	return m_connection.GetLastErrorCode();
}

void Server::SendCommand(const std::vector<std::string>& command, RecvCallback_t&& recvCallback)
{
	// create our packet
	Packet_t packet(command, s_lastSequence++);

	// send the packet
	std::lock_guard connectionLock(m_connectionMutex);
	m_connection.SendPacket(packet, [ recvCallback{ std::move(recvCallback) }](const Connection_t::ErrorCode_t& ec, std::shared_ptr<Packet_t> packet)
	{
		// make sure we don't have an error
		if (ec)
			return recvCallback(ec, std::vector<std::string>{});
		
		// return the words to the outside callback
		recvCallback(ec, packet->GetWords());
	});
}

void Server::RegisterCallback(const std::string& eventName, EventCallback_t&& eventCallback)
{
	RegisterPrePluginCallback(eventName, std::move(eventCallback));
}

void Server::RegisterPrePluginCallback(const std::string& eventName, EventCallback_t&& eventCallback)
{
	// add the event callback to the list of callbacks for the event name
	m_prePluginEventCallbacks.emplace(eventName, std::move(eventCallback));
}

void Server::RegisterPostPluginCallback(const std::string& eventName, EventCallback_t&& eventCallback)
{
	// add the event callback to the list of callbacks for the event name
	m_postPluginEventCallbacks.emplace(eventName, std::move(eventCallback));
}

bool Server::EnablePlugin(const std::string& pluginName)
{
	const PluginMap_t::const_iterator pluginIt = m_plugins.find(pluginName);

	if (pluginIt == m_plugins.end())
		return false;

	pluginIt->second.pPlugin->Enable();

	return true;
}

bool Server::DisablePlugin(const std::string& pluginName)
{
	const PluginMap_t::const_iterator pluginIt = m_plugins.find(pluginName);

	if (pluginIt == m_plugins.end())
		return false;

	pluginIt->second.pPlugin->Disable();

	return true;
}

void Server::ScheduleAction(TimedAction_t&& timedAction, const std::chrono::system_clock::duration& timeFromNow)
{
	// create the timer
	std::shared_ptr<asio::steady_timer> pTimer = std::make_shared<asio::steady_timer>(m_worker);
	pTimer->expires_from_now(timeFromNow);

	pTimer->async_wait(
		[timedAction = std::move(timedAction), pTimer](const ErrorCode_t& ec)
	{
		if (!ec)
			timedAction();
	});
}

void Server::ScheduleAction(TimedAction_t&& timedAction, const size_t millisecondsFromNow)
{
	ScheduleAction(std::move(timedAction), std::chrono::milliseconds(millisecondsFromNow));
}

void Server::MovePlayer(const uint8_t teamId, const uint8_t squadId, const std::shared_ptr<PlayerInfo>& pPlayer)
{
	const uint8_t oldTeamId = pPlayer->teamId;
	const uint8_t oldSquadId = pPlayer->squadId;

	// assume it worked, remove them from their old team/squad and add them to the new one
	RemovePlayerFromSquad(pPlayer, oldTeamId, oldSquadId);
	AddPlayerToSquad(pPlayer, teamId, squadId);

	// send the command
	SendCommand({ "admin.movePlayer", pPlayer->name, std::to_string(teamId), std::to_string(squadId), "true" },
		std::bind(&Server::HandleMovePlayer, this, oldTeamId, oldSquadId, pPlayer, std::placeholders::_1, std::placeholders::_2));
}

Server::~Server()
{
	ErrorCode_t ec;

	// disconnect
	if (IsConnected() == true)
		Disconnect(ec);

	// wait for the thread
	if (m_thread.joinable() == true)
		m_thread.join();
}

void Server::ClearContainers()
{
	// reset serverInfo status
	m_gotServerInfo = false;
	m_gotServerPlayers = false;

	// disable plugins
	for (const PluginMap_t::value_type& plugin : m_plugins)
	{
		// make a copy, because the string_view's pointer is no longer valid once we free the library
		const std::string pluginName(plugin.second.pPlugin->GetPluginName());
		
		if (plugin.second.pPlugin->IsEnabled() == true)
			plugin.second.pPlugin->Disable();
		plugin.second.pDestructor(plugin.second.pPlugin);

		// free the module
		BFreeLibrary(plugin.second.hPluginModule);

		// call the unload callback
		m_pluginCallback(pluginName.data(), false, true, "");
	}

	// clear the players and handlers
	m_prePluginEventCallbacks.clear();
	m_postPluginEventCallbacks.clear();
	m_players.clear();
	m_teams.clear();
}

void Server::SendResponse(const std::vector<std::string>& response, const int32_t sequence)
{
	// create our packet
	const Packet_t packet(response, sequence, true);

	// send the packet
	std::lock_guard connectionLock(m_connectionMutex);
	m_connection.SendPacket(packet, [](const Connection_t::ErrorCode_t&, std::shared_ptr<Packet_t>) {});
}

void Server::HandleEvent(const ErrorCode_t& ec, std::shared_ptr<Packet_t> event)
{
	if (ec)
	{
		BetteRCon::Internal::g_stdErrLog << "ErrorCode on HandleEvent: " << ec.message() << '\n';
		ClearContainers();
		return m_disconnectCallback(ec);
	}

	if (event == nullptr)
	{
		ClearContainers();
		return m_disconnectCallback(ec);
	}

	auto callHandlers = [&event = std::as_const(event)](const EventCallbackMap_t& eventHandlerMap)
	{
		// call each event handler
		const std::pair<const EventCallbackMap_t::const_iterator, const EventCallbackMap_t::const_iterator> eventRange = eventHandlerMap.equal_range(event->GetWords().front());

		for (EventCallbackMap_t::const_iterator it = eventRange.first; it != eventRange.second; ++it)
			it->second(event->GetWords());
	};

	// call prePlugin handlers before plugin handlers are called
	callHandlers(m_prePluginEventCallbacks);

	// call each plugin's event handler
	for (const PluginMap_t::value_type& plugin : m_plugins)
	{
		// make sure the plugin is enabled
		if (plugin.second.pPlugin->IsEnabled() == false)
			continue;

		const Plugin::EventHandlerMap_t& handlers = plugin.second.pPlugin->GetEventHandlers();

		// call their handler
		const Plugin::EventHandlerMap_t::const_iterator handlerIt = handlers.find(event->GetWords().front());

		if (handlerIt != handlers.end())
			handlerIt->second(event->GetWords());
	}

	// call postPlugin handlers after plugin handlers are called
	callHandlers(m_postPluginEventCallbacks);

	// call the main event handler
	m_eventCallback(event->GetWords());

	// send back the OK response
	SendResponse({ "OK" }, event->GetSequence());
}

void Server::HandleLoginRecvHash(const ErrorCode_t& ec, const std::vector<std::string>& response, const std::string& password, const LoginCallback_t& loginCallback)
{
	// we disconnected or something. 
	if (ec)
		return loginCallback(LoginResult_Unknown);

	if (response.size() != 2)
	{
		// something strange happened
		return loginCallback(LoginResult_Unknown);
	}

	// did we get an OK?
	if (response[0] == "OK")
	{
		// decode the salt into bytes
		const std::string& saltHex = response[1];
		std::string salt;

		for (size_t i = 0; i < saltHex.size(); i += 2)
		{
			char byte = static_cast<char>(stoi(saltHex.substr(i, 2), nullptr, 16));
			salt.push_back(byte);
		}

		// md5 the password appended to salt
		std::string hashResult = MD5(salt + password).hexdigest();

		// send the hashed password
		SendCommand({ "login.hashed", hashResult },
			std::bind(&Server::HandleLoginRecvResponse, this, 
				std::placeholders::_1, std::placeholders::_2, loginCallback));
	}
	else if (response[0] == "PasswordNotSet")
	{
		// server did not set a password
		return loginCallback(LoginResult_PasswordNotSet);
	}
	else
	{
		// something strange happened
		return loginCallback(LoginResult_Unknown);
	}
}

void Server::HandleLoginRecvResponse(const ErrorCode_t& ec, const std::vector<std::string>& response, const LoginCallback_t& loginCallback)
{
	// we disconnected or something. 
	if (ec)
		return loginCallback(LoginResult_Unknown);

	if (response.size() != 1)
	{
		// something strange happened
		return loginCallback(LoginResult_Unknown);
	}

	// did we get an OK?
	if (response[0] == "OK")
	{
		// tell the server to send us events
		SendCommand({ "admin.eventsEnabled", "true" }, 
			[](const ErrorCode_t&, const std::vector<std::string>&) {});

		// login is successful. start the serverInfoLoop
		m_serverInfoTimer.async_wait(std::bind(
			&Server::HandleServerInfoTimerExpire, 
			this, std::placeholders::_1));

		// and the playerInfoLoop
		m_playerInfoTimer.async_wait(std::bind(
			&Server::HandlePlayerListTimerExpire,
			this, std::placeholders::_1));

		// call the login callback
		return loginCallback(LoginResult_OK);
	}
	else if (response[0] == "InvalidPasswordHash")
	{
		// invalid password
		return loginCallback(LoginResult_InvalidPasswordHash);
	}
	else
	{
		// something strange happened
		return loginCallback(LoginResult_Unknown);
	}
}

void Server::FireEvent(const std::vector<std::string>& eventArgs)
{
	HandleEvent(ErrorCode_t{}, std::make_shared<Packet_t>(eventArgs, 0));
}

void Server::HandlePlayerInfo(const std::vector<std::string>& playerInfo)
{
	if (playerInfo.size() < 13)
	{
		// the server is not ok, disconnect
		BetteRCon::Internal::g_stdErrLog << "PlayerInfo was too small size: " << playerInfo.size() << '\n';
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}

	if (playerInfo[1] != "10")
	{
		// the server is not ok, disconnect
		BetteRCon::Internal::g_stdErrLog << "PlayerInfo did not have 10 members: " << playerInfo[1] << '\n';
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}

	/// TODO: Make this whole section work independently of variable count per player
	// process each player
	constexpr size_t offset = 13;
	constexpr size_t numVar = 10;
	size_t playerCount = std::stoi(playerInfo[12]);

	// set that none of the players were yet seen this check
	for (PlayerMap_t::value_type& playerPair : m_players)
		playerPair.second->seenThisCheck = false;
	
	try
	{
		for (size_t i = 0; i < playerCount; ++i)
		{
			std::string playerName = playerInfo.at(offset + numVar * i);
			std::shared_ptr<PlayerInfo> pPlayer;

			bool firstTime = false;
			const PlayerMap_t::const_iterator playerIt = m_players.find(playerName);
			if (playerIt == m_players.end())
			{
				pPlayer = m_players.emplace(playerName, std::make_shared<PlayerInfo>()).first->second;
				pPlayer->firstSeen = std::chrono::system_clock::now();
				firstTime = true;
			}
			else
				pPlayer = playerIt->second;

			pPlayer->name = playerName;
			pPlayer->GUID = playerInfo.at(offset + numVar * i + 1);
			pPlayer->teamId = static_cast<uint8_t>(std::stoi(playerInfo.at(offset + numVar * i + 2)));
			pPlayer->squadId = static_cast<uint8_t>(std::stoi(playerInfo.at(offset + numVar * i + 3)));
			pPlayer->kills = static_cast<uint32_t>(std::stoi(playerInfo.at(offset + numVar * i + 4)));
			pPlayer->deaths = static_cast<uint32_t>(std::stoi(playerInfo.at(offset + numVar * i + 5)));
			pPlayer->score = static_cast<uint32_t>(std::stoi(playerInfo.at(offset + numVar * i + 6)));
			pPlayer->rank = static_cast<uint32_t>(std::stoi(playerInfo.at(offset + numVar * i + 7)));
			pPlayer->ping = static_cast<uint16_t>(std::stoi(playerInfo.at(offset + numVar * i + 8)));
			pPlayer->type = static_cast<PlayerInfo::TYPE>(std::stoi(playerInfo.at(offset + numVar * i + 9)));
			pPlayer->seenThisCheck = true;

			if (firstTime == true)
			// add the player to their squad if they are new
				AddPlayerToSquad(pPlayer, pPlayer->teamId, pPlayer->squadId);
		}
	}
	catch (const std::exception& e)
	{
		BetteRCon::Internal::g_stdErrLog << "ERROR: Malformed playerInfo\n";
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}

	for (PlayerMap_t::iterator playerIt = m_players.begin(); playerIt != m_players.end(); ++playerIt)
	{
		// if we didn't see them in the list, remove them
		if (playerIt->second->seenThisCheck == false)
		{
			BetteRCon::Internal::g_stdErrLog << "ERROR: Player " << playerIt->second->name << " has disappeared\n";

			// delete the player
			RemovePlayerFromSquad(playerIt->second, playerIt->second->teamId, playerIt->second->squadId);
			m_players.erase(playerIt);
		}
	}

	// fire a playerInfo event
	FireEvent({ "bettercon.playerInfo" });

	// call the playerInfo callback
	m_playerInfoCallback(m_players, m_teams);
}

void Server::HandlePlayerJoinTimeout(const ErrorCode_t& ec, const std::shared_ptr<PlayerInfo>& pPlayer)
{
	// it was cancelled
	if (ec)
		return;

	// find their timer
	const PlayerTimerMap_t::iterator playerTimerIt = m_playerTimers.find(pPlayer->name);
	// shouldn't happen
	if (playerTimerIt == m_playerTimers.end())
		return;

	// cancel their timer and delete them
	ErrorCode_t e;
	playerTimerIt->second.second.cancel(e);

	m_playerTimers.erase(playerTimerIt);
}

void Server::HandleOnAuthenticated(const std::vector<std::string>& eventArgs)
{
	// onAuthenticated means they successfully completed the client/server handshake, fb::online::OnlineClient::onConnected has been called, and they are connected. create a player for them
	if (eventArgs.size() != 2)
	{
		// the server is not ok, disconnect
		BetteRCon::Internal::g_stdErrLog << "OnAuthenticated did not have 2 members: " << eventArgs.size() << '\n';
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}
	
	const std::string& playerName = eventArgs[1];
	
	// see if they have a timer
	const PlayerTimerMap_t::iterator playerTimerIt = m_playerTimers.find(playerName);
	if (playerTimerIt != m_playerTimers.end())
	{
		// they joined, add them to the maps and cancel their timer
		ErrorCode_t ec;
		playerTimerIt->second.second.cancel(ec);
		m_players.emplace(playerName, playerTimerIt->second.first);
		AddPlayerToSquad(playerTimerIt->second.first, 0, 0);

		m_playerTimers.erase(playerTimerIt);
		return;
	}

	// make a new player
	const std::shared_ptr<PlayerInfo>& pPlayer = m_players.emplace(playerName, std::make_shared<PlayerInfo>()).first->second;

	pPlayer->name = playerName;
	pPlayer->firstSeen = std::chrono::system_clock::now();

	AddPlayerToSquad(pPlayer, 0, 0);
}

void Server::HandleOnChat(const std::vector<std::string>& eventArgs)
{
	if (eventArgs.size() < 4)
	{
		// the server is not ok, disconnect
		BetteRCon::Internal::g_stdErrLog << "OnChat did not have at least 4 members: " << eventArgs.size() << '\n';
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}

	const std::string& playerName = eventArgs[1];
	const std::string& chatMessage = eventArgs[2];

	// make sure it isn't an empty chat message
	if (chatMessage.empty() == true)
		return;

	// see if the player exists
	const PlayerMap_t::const_iterator playerIt = m_players.find(playerName);
	if (playerIt == m_players.end())
		return;

	size_t offset = 0;
	// remove the slash if there is one
	if (chatMessage[0] == '/')
		offset = 1;

	char prefix = '\0';

	// all commands begin with !
	if (chatMessage[offset] == '!' ||
		chatMessage[offset] == '@' ||
		chatMessage[offset] == '#')
	{
		prefix = chatMessage[offset];
		++offset;
	}
	else if (chatMessage[0] != '/')
		return;

	// split up their message by spaces
	std::vector<std::string> commandArgs;
	size_t space = chatMessage.find(' ', offset);
	if (space == std::string::npos)
	{
		commandArgs.emplace_back(chatMessage.substr(offset));
	}
	else
	{
		do
		{
			commandArgs.emplace_back(chatMessage.substr(offset, space - offset));
			offset = space + 1;
		} while ((space = chatMessage.find(' ', offset)) != std::string::npos);
		commandArgs.emplace_back(chatMessage.substr(offset));
	}

	// store the lowercase command
	std::string lowerCommand;
	std::transform(commandArgs[0].begin(), commandArgs[0].end(), std::back_inserter(lowerCommand), [](const char c) { return std::tolower(c); });

	// call each plugin's command handler
	for (const PluginMap_t::value_type& plugin : m_plugins)
	{
		// make sure the plugin is enabled
		if (plugin.second.pPlugin->IsEnabled() == false)
			continue;

		const Plugin::CommandHandlerMap_t& commandHandlers = plugin.second.pPlugin->GetCommandHandlers();

		// call their handler
		const std::pair<const Plugin::CommandHandlerMap_t::const_iterator, const Plugin::CommandHandlerMap_t::const_iterator> commandRange = commandHandlers.equal_range(lowerCommand);

		for (Plugin::CommandHandlerMap_t::const_iterator it = commandRange.first; it != commandRange.second; ++it)
			it->second(playerIt->second, commandArgs, prefix);
	}
}

void Server::HandleOnJoin(const std::vector<std::string>& eventArgs)
{
	// find the player and their GUID
	if (eventArgs.size() != 3)
	{
		// the server is not ok, disconnect
		BetteRCon::Internal::g_stdErrLog << "OnJoin did not have 3 members: " << eventArgs.size() << '\n';
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}

	const std::string& name = eventArgs[1];
	const std::string& guid = eventArgs[2];

	// see if they are already joining. cancel their timer if they are
	PlayerTimerMap_t::iterator playerTimerIt = m_playerTimers.find(name);
	if (playerTimerIt != m_playerTimers.end())
	{
		ErrorCode_t ec;
		playerTimerIt->second.second.cancel(ec);
	}
	else
		playerTimerIt = m_playerTimers.emplace(name, std::make_pair(std::make_shared<PlayerInfo>(), asio::steady_timer(m_worker))).first;

	std::shared_ptr<PlayerInfo>& pPlayer = playerTimerIt->second.first;
	pPlayer->name = name;
	pPlayer->GUID = guid;
	pPlayer->firstSeen = std::chrono::system_clock::now();

	// start their timer
	playerTimerIt->second.second.expires_from_now(std::chrono::minutes(2));
	playerTimerIt->second.second.async_wait(std::bind(&Server::HandlePlayerJoinTimeout, this, std::placeholders::_1, pPlayer));
}

void Server::HandleOnKill(const std::vector<std::string>& eventArgs)
{
	// find both the killer and victim
	if (eventArgs.size() != 5)
	{
		// the server is not ok, disconnect
		BetteRCon::Internal::g_stdErrLog << "OnKill did not have 5 members: " << eventArgs.size() << '\n';
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}

	const std::string& killerName = eventArgs[1];
	const std::string& victimName = eventArgs[2];

	const PlayerMap_t::const_iterator victimIt = m_players.find(victimName);
	if (victimIt == m_players.end())
	{
		BetteRCon::Internal::g_stdErrLog << "ERROR: Victim " << killerName << " not found in player map\n";
		return;
	}

	// increment victim's deaths
	++victimIt->second->deaths;

	// update that they are dead
	victimIt->second->alive = false;

	// they suicided
	if (killerName.size() == 0 ||
		killerName == victimName)
		return;

	const PlayerMap_t::const_iterator killerIt = m_players.find(killerName);
	if (killerIt == m_players.end())
	{
		BetteRCon::Internal::g_stdErrLog << "ERROR: Killer " << killerName << " not found in player map\n";
		return;
	}

	// increment killer's kills
	++killerIt->second->kills;
}

void Server::HandleOnLeave(const std::vector<std::string>& eventArgs)
{
	// onLeave means they left the game. Remove them from the list of players
	if (eventArgs.size() < 2)
	{
		// the server is not ok, disconnect
		BetteRCon::Internal::g_stdErrLog << "OnLeave did not have at least members: " << eventArgs.size() << '\n';
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}

	const std::string& playerName = eventArgs[1];

	const PlayerMap_t::const_iterator playerIt = m_players.find(playerName);
	if (playerIt == m_players.end())
	{
		BetteRCon::Internal::g_stdErrLog << "ERROR: Player " << playerName << " left but was not found in the internal player map\n";
		return;
	}

	const uint8_t teamId = playerIt->second->teamId;
	const uint8_t squadId = playerIt->second->squadId;

	// erase them from the team map
	RemovePlayerFromSquad(playerIt->second, teamId, squadId);

	// remove them from the player map
	m_players.erase(playerIt);
}

void Server::HandleOnSpawn(const std::vector<std::string>& eventArgs)
{
	if (eventArgs.size() < 2)
	{
		// the server is not ok, disconnect
		BetteRCon::Internal::g_stdErrLog << "OnSpawn did not have at least 2 members: " << eventArgs.size() << '\n';
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}

	const std::string& playerName = eventArgs[1];

	// find the player
	const PlayerMap_t::iterator playerIt = m_players.find(playerName);
	if (playerIt == m_players.end())
	{
		BetteRCon::Internal::g_stdErrLog << "ERROR: Player " << playerName << " spawned but is not stored!\n";
		return;
	}

	playerIt->second->alive = true;
}

void Server::HandleOnSquadChange(const std::vector<std::string>& eventArgs)
{
	// they both do the same thing but can be called in certain circumstances
	HandleOnTeamChange(eventArgs);
}

void Server::HandleOnTeamChange(const std::vector<std::string>& eventArgs)
{
	// onLeave means they left the game. Remove them from the list of players
	if (eventArgs.size() != 4)
	{
		// the server is not ok, disconnect
		BetteRCon::Internal::g_stdErrLog << "OnTeamChange did not have 4 members: " << eventArgs.size() << '\n';
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}

	const std::string& playerName = eventArgs[1];
	const uint8_t newTeamId = static_cast<uint8_t>(std::stoi(eventArgs[2]));
	const uint8_t newSquadId = static_cast<uint8_t>(std::stoi(eventArgs[3]));

	// the game likes to tell us after they leave that they switch teams, and this might also be the first we see of them
	const PlayerMap_t::const_iterator playerIt = m_players.find(playerName);
	if (playerIt == m_players.end())
		return;

	const std::shared_ptr<PlayerInfo>& pPlayer = playerIt->second;
	const uint8_t teamId = pPlayer->teamId;
	const uint8_t squadId = pPlayer->squadId;

	// they didn't actually change, or we are handling both messages
	if (newTeamId == teamId &&
		newSquadId == squadId)
		return;

	// remove them from their squad
	RemovePlayerFromSquad(pPlayer, teamId, squadId);

	// add them to the new squad
	AddPlayerToSquad(pPlayer, newTeamId, newSquadId);
}

void Server::HandleOnRoundEnd(const std::vector<std::string>& eventArgs)
{
	HandlePlayerInfo(eventArgs);
}

void Server::HandlePunkbusterMessage(const std::vector<std::string>& eventArgs)
{
	if (eventArgs.size() != 2)
	{
		// the server is not ok, disconnect
		BetteRCon::Internal::g_stdErrLog << "OnLeave did not have 2 members: " << eventArgs.size() << '\n';
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}

	const std::string& pbMessage = eventArgs[1];

	static bool s_expectPlayerList = false;

	// see if we should expect a playerList
	if (pbMessage.find("Player List:") != std::string::npos)
	{
		// reset the timer and wait again
		ErrorCode_t ec;
		m_punkbusterPlayerListTimer.cancel_one(ec);

		m_punkbusterPlayerListTimer.expires_from_now(std::chrono::seconds(30));
		m_punkbusterPlayerListTimer.async_wait(std::bind(
			&Server::HandlePunkbusterPlayerListTimerExpire,
			this, std::placeholders::_1));

		s_expectPlayerList = true;
	}
	else if (pbMessage.find("End of Player List") != std::string::npos)
	{
		FireEvent({ "bettercon.endOfPBPlayerList" });
		s_expectPlayerList = false;
	}
	else if (size_t nc = pbMessage.find("New Connection (slot #"); nc != std::string::npos)
	{
		// we have a new connection. find the player's IP and name
		const size_t ipPos = pbMessage.find_first_of("123456789", nc + 24);
		if (ipPos == std::string::npos)
			return;

		const size_t endOfIpPos = pbMessage.find(':', ipPos);

		const std::string& ip = pbMessage.substr(ipPos, endOfIpPos - ipPos);

		const size_t endOfPortPos = pbMessage.find(' ', endOfIpPos);

		const uint16_t port = std::stoi(pbMessage.substr(endOfIpPos + 1, endOfPortPos - endOfIpPos - 1));

		// find their name
		const size_t namePos = pbMessage.find('"', endOfIpPos);
		if (namePos == std::string::npos)
			return;

		const size_t endOfNamePos = pbMessage.find('"', namePos + 1);
		if (endOfNamePos == std::string::npos)
			return;

		const std::string& name = pbMessage.substr(namePos + 1, endOfNamePos - namePos - 1);

		// find the player
		const PlayerMap_t::const_iterator playerIt = m_players.find(name);
		if (playerIt == m_players.end())
		{
			BetteRCon::Internal::g_stdErrLog << "ERROR: Punkbuster sent a new connection for a player that we don't have stored\n";
			return;
		}

		// set their ip
		playerIt->second->ipAddress = ip;
		playerIt->second->port = port;

		// fire an event
		FireEvent({ "bettercon.playerPBConnected", name, ip });
	}
	else if (s_expectPlayerList == true)
	{
		if (pbMessage.size() < sizeof("PunkBuster Server: ") - 1)
		{
			BetteRCon::Internal::g_stdErrLog << "ERROR: PB sent an empty message\n";
			return;
		}

		// find the player's pbguid and IP
		std::stringstream ss(pbMessage.substr(sizeof("PunkBuster Server: ") - 1));

		uint16_t slotId;
		std::string pbGuid;
		std::string ipPort;
		std::string status;
		uint32_t power;
		float authRate;
		uint32_t recentSS;
		std::string OS;
		std::string name;

		// parse the message
		if (!(ss >> slotId) ||
			!(ss >> pbGuid) ||
			!(ss >> ipPort) ||
			!(ss >> status) ||
			!(ss >> power) ||
			!(ss >> authRate) ||
			!(ss >> recentSS) ||
			!(ss >> OS) ||
			(OS == "(" && !(ss >> OS)) || // stupid case where OS is not (V) and is instead ( )
			!(ss >> name))
		{
			BetteRCon::Internal::g_stdErrLog << "ERROR: Failed to parse PB player list message\n";
			return;
		}

		// remove the quotes
		name = name.substr(1, name.size() - 2);

		// find the player
		const PlayerMap_t::const_iterator playerIt = m_players.find(name);
		if (playerIt == m_players.end())
		{
			BetteRCon::Internal::g_stdErrLog << "ERROR: Punkbuster sent player info for a player who is not in the internal player map\n";
			return;
		}

		const size_t ipColon = ipPort.find(':');
		if (ipColon == std::string::npos)
		{
			BetteRCon::Internal::g_stdErrLog << "ERROR: Punkbuster sent an invalid IP-port\n";
			return;
		}

		// save the ip and guid
		const std::shared_ptr<PlayerInfo>& pPlayer = playerIt->second;
		pPlayer->pbGuid = pbGuid.substr(0, 32);
		pPlayer->ipAddress = ipPort.substr(0, ipColon);
		pPlayer->port = static_cast<uint16_t>(std::stoi(ipPort.substr(ipColon + 1)));
	}
}

void Server::HandleMovePlayer(const uint8_t oldTeamId, const uint8_t oldSquadId, const std::shared_ptr<PlayerInfo>& pPlayer, const ErrorCode_t& ec, const std::vector<std::string>& response)
{
	// connection should be closed automatically
	if (ec)
		return;

	if (response.size() != 1)
	{
		// the server is not ok, disconnect
		BetteRCon::Internal::g_stdErrLog << "ERROR: MovePlayer sent an invalid response of size " << response.size() << '\n';
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}

	if (response[0] != "OK")
	{
		// the move failed. move them back to their old team
		BetteRCon::Internal::g_stdErrLog << "ERROR: Failed to move player " << pPlayer->name << ": " << response[0] << '\n';

		// adjust their team and squad internally
		RemovePlayerFromSquad(pPlayer, pPlayer->teamId, pPlayer->squadId);
		AddPlayerToSquad(pPlayer, oldTeamId, oldSquadId);
	}
}

void Server::AddPlayerToSquad(const std::shared_ptr<PlayerInfo>& pPlayer, const uint8_t teamId, const uint8_t squadId)
{
	// add them to the new team and squad
	TeamMap_t::iterator teamIt = m_teams.find(teamId);
	if (teamIt == m_teams.end())
		teamIt = m_teams.emplace(teamId, Team{}).first;

	SquadMap_t::iterator squadIt = teamIt->second.squads.find(squadId);
	if (squadIt == teamIt->second.squads.end())
		squadIt = teamIt->second.squads.emplace(squadId, PlayerMap_t()).first;

	// change their squad and teamId
	pPlayer->teamId = teamId;
	pPlayer->squadId = squadId;

	// increment the team playerCount
	++teamIt->second.playerCount;

	// if they are a commander, increment the count
	if (pPlayer->type == PlayerInfo::TYPE_Commander)
		++teamIt->second.commanderCount;

	squadIt->second.emplace(pPlayer->name, pPlayer);
}

void Server::RemovePlayerFromSquad(const std::shared_ptr<PlayerInfo>& pPlayer, const uint8_t teamId, const uint8_t squadId)
{
	// find them in the team map
	const TeamMap_t::iterator teamIt = m_teams.find(teamId);
	if (teamIt == m_teams.end())
	{
		BetteRCon::Internal::g_stdErrLog << "ERROR: Player " << pPlayer->name << " changed squads/teams but had an invalid team\n";
		return;
	}

	const SquadMap_t::iterator squadIt = teamIt->second.squads.find(squadId);
	if (squadIt == teamIt->second.squads.end())
	{
		BetteRCon::Internal::g_stdErrLog << "ERROR: Player " << pPlayer->name << " changed squads/teams but had an invalid squad\n";
		return;
	}

	const PlayerMap_t::iterator playerIt = squadIt->second.find(pPlayer->name);
	if (playerIt == squadIt->second.end())
	{
		BetteRCon::Internal::g_stdErrLog << "ERROR: Player " << pPlayer->name << " changed squads/teams but was not found in the internal team map\n";
		return;
	}

	// remove them from the team map
	squadIt->second.erase(playerIt);

	// erase the squad if they were alone in their squad
	if (squadIt->second.empty() == true)
		teamIt->second.squads.erase(squadIt);

	// decrement the team's playercount
	--teamIt->second.playerCount;

	// if they are commander, decrement the team's commander count
	if (pPlayer->type == PlayerInfo::TYPE_Commander)
		--teamIt->second.commanderCount;

	// erase the team if it is empty
	if (teamIt->second.squads.empty() == true)
		m_teams.erase(teamIt);
}

void Server::LoadPlugins()
{
	// make sure the plugins directory exists
	if (std::filesystem::is_directory("plugins/") == false)
		return;

	// iterate the plugins directory
	for (const std::filesystem::directory_iterator::value_type& file : std::filesystem::directory_iterator("plugins/"))
	{
		if (file.is_directory() == true)
			continue;

		const std::string pathStr = file.path().string();
		if (pathStr.size() < sizeof(".plugin") - 1)
			continue;

		// invalid extension
		if (pathStr.substr(pathStr.size() - sizeof(".plugin") + 1) != ".plugin")
			continue;

		const auto hPlugin = BLoadLibrary(file.path().string().c_str());

		if (hPlugin == nullptr)
		{
			m_pluginCallback(pathStr, true, false, "Failed to open file");
			continue;
		}

		const PluginFactory_t fnPluginFactory = reinterpret_cast<PluginFactory_t>(BFindFunction(hPlugin, "CreatePlugin"));

		if (fnPluginFactory == nullptr)
		{
			m_pluginCallback(pathStr, true, false, "Failed to find CreatePlugin");
			continue;
		}

		const PluginDestructor_t fnPluginDestructor = reinterpret_cast<PluginDestructor_t>(BFindFunction(hPlugin, "DestroyPlugin"));

		if (fnPluginDestructor == nullptr)
		{
			m_pluginCallback(pathStr, true, false, "Failed to find DestroyPlugin");
			continue;
		}

		// we have a valid plugin. create an instance
		Plugin* pPlugin = fnPluginFactory(this);

		if (pPlugin == nullptr)
		{
			m_pluginCallback(pathStr, true, false, "CreatePlugin returned nullptr");
			continue;
		}

		// add the plugin to the map
		m_plugins.emplace(pPlugin->GetPluginName(), PluginInfo{ hPlugin, pPlugin, fnPluginDestructor });

		// call the callback
		m_pluginCallback(pPlugin->GetPluginName().data(), true, true, "");
	}

	m_finishedLoadingPluginsCallback();
}

void Server::InitializeServer()
{
	// register the event callbacks
	RegisterPrePluginCallback("player.onAuthenticated",
		std::bind(&Server::HandleOnAuthenticated,
			this, std::placeholders::_1));
	RegisterPrePluginCallback("player.onChat",
		std::bind(&Server::HandleOnChat,
			this, std::placeholders::_1));
	RegisterPrePluginCallback("player.onJoin",
		std::bind(&Server::HandleOnJoin,
			this, std::placeholders::_1));
	RegisterPrePluginCallback("player.onKill",
		std::bind(&Server::HandleOnKill,
			this, std::placeholders::_1));
	RegisterPostPluginCallback("player.onLeave",
		std::bind(&Server::HandleOnLeave,
			this, std::placeholders::_1));
	RegisterPrePluginCallback("player.onSpawn",
		std::bind(&Server::HandleOnSpawn,
			this, std::placeholders::_1));
	RegisterPrePluginCallback("player.onSquadChange",
		std::bind(&Server::HandleOnSquadChange,
			this, std::placeholders::_1));
	RegisterPrePluginCallback("player.onTeamChange",
		std::bind(&Server::HandleOnTeamChange,
			this, std::placeholders::_1));
	RegisterPrePluginCallback("server.onRoundOverPlayers",
		std::bind(&Server::HandleOnRoundEnd,
			this, std::placeholders::_1));
	RegisterPrePluginCallback("punkBuster.onMessage",
		std::bind(&Server::HandlePunkbusterMessage,
			this, std::placeholders::_1));

	LoadPlugins();
}

void Server::HandleServerInfo(const ErrorCode_t& ec, const std::vector<std::string>& serverInfo)
{
	// connection should be closed automatically
	if (ec)
		return;

	// parse the result
	if (serverInfo.size() < 1 ||
		serverInfo[0] != "OK")
	{
		// disconnect, the server is not OK
		BetteRCon::Internal::g_stdErrLog << "ServerInfo response not OK: " << serverInfo[0] << '\n';
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}

	if (serverInfo.size() < 24)
	{
		// disconnect, the server is not OK
		BetteRCon::Internal::g_stdErrLog << "ServerInfo size too small: " << serverInfo.size() << '\n';
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}

	try
	{
		// generic info
		m_serverInfo.m_serverName = serverInfo[1];
		m_serverInfo.m_playerCount = stoi(serverInfo[2]);
		m_serverInfo.m_maxPlayerCount = stoi(serverInfo[3]);
		m_serverInfo.m_gameMode = serverInfo[4];
		m_serverInfo.m_map = serverInfo[5];
		m_serverInfo.m_roundsPlayed = stoi(serverInfo[6]);
		m_serverInfo.m_roundsTotal = stoi(serverInfo[7]);

		// parse scores
		size_t offset = 0;
		const size_t numTeams = static_cast<size_t>(stoi(serverInfo[8]));

		// make sure there is space for the team scores
		if (m_serverInfo.m_scores.m_teamScores.size() != numTeams)
			m_serverInfo.m_scores.m_teamScores.resize(numTeams);

		for (; offset < numTeams; ++offset)
		{
			m_serverInfo.m_scores.m_teamScores[offset] = stoi(serverInfo[9 + offset]);
		}
		--offset;

		m_serverInfo.m_scores.m_goalScore = stoi(serverInfo[10 + offset]);

		// more generic info
		m_serverInfo.m_onlineState = serverInfo[11 + offset];
		m_serverInfo.m_ranked = (serverInfo[12 + offset] == "true");
		m_serverInfo.m_punkBuster = (serverInfo[13 + offset] == "true");
		m_serverInfo.m_hasPassword = (serverInfo[14 + offset] == "true");
		m_serverInfo.m_serverUpTime = stoi(serverInfo[15 + offset]);
		m_serverInfo.m_roundTime = stoi(serverInfo[16 + offset]);
		m_serverInfo.m_serverIpAndPort = serverInfo[17 + offset];
		m_serverInfo.m_punkBusterVersion = serverInfo[18 + offset];
		m_serverInfo.m_joinQueueEnabled = (serverInfo[19 + offset] == "true");
		m_serverInfo.m_region = serverInfo[20 + offset];
		m_serverInfo.m_closestPingSite = serverInfo[21 + offset];
		m_serverInfo.m_country = serverInfo[22 + offset];
		m_serverInfo.m_blazePlayerCount = (serverInfo.size() >= 25) ? stoi(serverInfo[23 + offset]) : 0;
		m_serverInfo.m_blazeGameState = serverInfo[(serverInfo.size() >= 25) ? (24 + offset) : (23 + offset)];
	}
	catch (const std::exception& e)
	{
		// they sent bad serverInfo. disconnect
		BetteRCon::Internal::g_stdErrLog << "Error parsing serverInfo: " << e.what() << '\n';
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}

	// fire a serverInfo event
	FireEvent({ "bettercon.serverInfo" });

	// call the serverInfo callback
	m_serverInfoCallback(m_serverInfo);

	m_gotServerInfo = true;
	if (m_initializedServer == false &&
		m_gotServerPlayers == true)
	{
		InitializeServer();
		m_initializedServer = true;
	}

	// reset the timer and wait again
	m_serverInfoTimer.expires_from_now(std::chrono::seconds(30));
	m_serverInfoTimer.async_wait(std::bind(
		&Server::HandleServerInfoTimerExpire,
		this, std::placeholders::_1));
}

void Server::HandleServerInfoTimerExpire(const ErrorCode_t& ec)
{
	// the operation was likely cancelled. stop the loop
	if (ec)
		return;

	SendCommand({ "serverInfo" }, std::bind(
		&Server::HandleServerInfo, this,
		std::placeholders::_1, std::placeholders::_2));
}

void Server::HandlePlayerList(const ErrorCode_t& ec, const std::vector<std::string>& playerInfo)
{
	// connection will be closed automatically
	if (ec)
		return;

	// see if punkbuster is looking for player info yet
	if (m_punkbusterPlayerListTimer.expiry() < std::chrono::steady_clock::now())
	{
		// start the punkbuster playerList loop
		m_punkbusterPlayerListTimer.async_wait(std::bind(
			&Server::HandlePunkbusterPlayerListTimerExpire,
			this, std::placeholders::_1));
	}

	if (playerInfo.size() < 1)
	{
		// the server is not ok, disconnect
		BetteRCon::Internal::g_stdErrLog << "PlayerInfo sent empty response\n";
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}
	else if (playerInfo[0] != "OK")
	{
		// the server is not ok, disconnect
		BetteRCon::Internal::g_stdErrLog << "PlayerInfo sent not OK response: " << playerInfo[0] << '\n';
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}

	HandlePlayerInfo(playerInfo);

	m_gotServerPlayers = true;
	if (m_initializedServer == false &&
		m_gotServerInfo == true)
	{
		InitializeServer();
		m_initializedServer = true;
	}

	// reset the timer and wait again
	m_playerInfoTimer.expires_from_now(std::chrono::seconds(30));
	m_playerInfoTimer.async_wait(std::bind(
		&Server::HandlePlayerListTimerExpire,
		this, std::placeholders::_1));
}

void Server::HandlePlayerListTimerExpire(const ErrorCode_t& ec)
{
	// the oepration was likely canceled. stop the loop
	if (ec)
		return;
	
	SendCommand({ "admin.listPlayers", "all" }, std::bind(
		&Server::HandlePlayerList, this,
		std::placeholders::_1, std::placeholders::_2));
}

void Server::HandlePunkbusterPlayerList(const ErrorCode_t& ec, const std::vector<std::string>& response)
{
	if (ec)
		return;

	if (response.size() < 1)
	{
		// disconnect, the server is not OK
		BetteRCon::Internal::g_stdErrLog << "Punkbuster PlayerList response empty\n";
		ErrorCode_t ec;
		Disconnect(ec);
		return;

	}
	// parse the result
	if (response[0] != "OK")
	{
		// disconnect, the server is not OK
		BetteRCon::Internal::g_stdErrLog << "Punkbuster PlayerList sent not OK response: " << response[0] << '\n';
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}
}

void Server::HandlePunkbusterPlayerListTimerExpire(const ErrorCode_t& ec)
{
	// the oepration was likely canceled. stop the loop
	if (ec)
		return;

	SendCommand({ "punkBuster.pb_sv_command", "pb_sv_plist" }, 
		std::bind(&Server::HandlePunkbusterPlayerList, this,
			std::placeholders::_1, std::placeholders::_2));
}