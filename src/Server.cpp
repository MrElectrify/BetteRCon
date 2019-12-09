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
	: m_connection(m_worker, 
		std::bind(&Server::HandleEvent, this, 
			std::placeholders::_1, std::placeholders::_2)),
	m_serverInfoTimer(m_worker), m_playerInfoTimer(m_worker)
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

void Server::Login(const std::string& password, LoginCallback_t&& loginCallback, DisconnectCallback_t&& disconnectCallback, PluginCallback_t&& pluginCallback, EventCallback_t&& eventCallback, ServerInfoCallback_t&& serverInfoCallback, PlayerInfoCallback_t&& playerInfoCallback)
{
	// send the login request
	SendCommand({ "login.hashed" }, 
		std::bind(&Server::HandleLoginRecvHash, this,
			std::placeholders::_1, std::placeholders::_2, password, loginCallback));

	// store the callbacks
	m_disconnectCallback = std::move(disconnectCallback);
	m_eventCallback = std::move(eventCallback);
	m_pluginCallback = std::move(pluginCallback);
	m_serverInfoCallback = std::move(serverInfoCallback);
	m_playerInfoCallback = std::move(playerInfoCallback);

	// register the event callbacks
	RegisterCallback("player.onAuthenticated",
		std::bind(&Server::HandleOnAuthenticated,
			this, std::placeholders::_1));
}

void Server::Disconnect()
{
	m_connection.Disconnect();

	ErrorCode_t ec;

	// kill timers
	m_serverInfoTimer.cancel(ec);

	// disable plugins
	for (const auto& plugin : m_plugins)
	{
		// make a copy, because the string_view's pointer is no longer valid once we free the library
		const std::string pluginName(plugin.second.pPlugin->GetPluginName());

		plugin.second.pPlugin->Disable();
		plugin.second.pDestructor(plugin.second.pPlugin);

		// free the module
		BFreeLibrary(plugin.second.hPluginModule);

		// call the unload callback
		m_pluginCallback(pluginName.data(), false, true, "");
	}

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

Server::ErrorCode_t Server::GetLastErrorCode() const noexcept
{
	return m_connection.GetLastErrorCode();
}

void Server::SendCommand(const std::vector<std::string>& command, RecvCallback_t&& recvCallback)
{
	// create our packet
	Packet_t packet(command, s_lastSequence++);

	// send the packet
	m_connection.SendPacket(packet, [ recvCallback{ std::move(recvCallback) }](const Connection_t::ErrorCode_t& ec, std::shared_ptr<Packet_t> packet)
	{
		// make sure we don't have an error
		if (ec)
			return recvCallback(ec, std::vector<std::string>{});
		
		// return the words to the outside callback
		recvCallback(ec, packet->GetWords());
	});
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

void Server::SendResponse(const std::vector<std::string>& response, const int32_t sequence)
{
	// create our packet
	Packet_t packet(response, sequence, true);

	// send the packet
	m_connection.SendPacket(packet, [](const Connection_t::ErrorCode_t&, std::shared_ptr<Packet_t>) {});
}

void Server::RegisterCallback(const std::string& eventName, EventCallback_t&& eventCallback)
{
	// add the event callback to the list of callbacks for the event name
	m_eventCallbacks.emplace(eventName, std::move(eventCallback));
}

bool Server::EnablePlugin(const std::string& pluginName)
{
	auto pluginIt = m_plugins.find(pluginName);

	if (pluginIt == m_plugins.end())
		return false;

	pluginIt->second.pPlugin->Enable();
	
	return true;
}

bool Server::DisablePlugin(const std::string& pluginName)
{
	auto pluginIt = m_plugins.find(pluginName);

	if (pluginIt == m_plugins.end())
		return false;

	pluginIt->second.pPlugin->Disable();

	return true;
}

void Server::ScheduleAction(TimedAction_t&& timedAction, const std::chrono::system_clock::duration& timeFromNow)
{
	// create the timer
	auto pTimer = std::make_shared<asio::steady_timer>(m_worker);
	pTimer->expires_from_now(timeFromNow);

	pTimer->async_wait(
		[timedAction = std::move(timedAction), pTimer] (const ErrorCode_t& ec)
	{
		if (!ec)
			timedAction();
	});
}

void Server::ScheduleAction(TimedAction_t&& timedAction, const size_t millisecondsFromNow)
{
	ScheduleAction(std::move(timedAction), std::chrono::milliseconds(millisecondsFromNow));
}

void Server::HandleEvent(const ErrorCode_t& ec, std::shared_ptr<Packet_t> event)
{
	if (ec)
		return m_disconnectCallback(ec);

	if (event == nullptr)
		return m_disconnectCallback(ec);

	// call each event handler
	auto eventRange = m_eventCallbacks.equal_range(event->GetWords().front());

	for (auto it = eventRange.first; it != eventRange.second; ++it)
		it->second(event->GetWords());

	// call each plugin's event handler
	for (const auto& plugin : m_plugins)
	{
		// make sure the plugin is enabled
		if (plugin.second.pPlugin->IsEnabled() == false)
			continue;

		const auto& handlers = plugin.second.pPlugin->GetEventHandlers();

		// call their handler
		auto handlerIt = handlers.find(event->GetWords().front());

		if (handlerIt != handlers.end())
			handlerIt->second(event->GetWords());
	}

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

	// did we get an OK?
	if (response.at(0) == "OK")
	{
		// decode the salt into bytes
		const auto& saltHex = response.at(1);
		std::string salt;

		for (size_t i = 0; i < saltHex.size(); i += 2)
		{
			auto byte = static_cast<char>(stoi(saltHex.substr(i, 2), nullptr, 16));
			salt.push_back(byte);
		}

		// md5 the password appended to salt
		auto hashResult = MD5(salt + password).hexdigest();

		// send the hashed password
		SendCommand({ "login.hashed", hashResult }, 
			std::bind(&Server::HandleLoginRecvResponse, this, 
				std::placeholders::_1, std::placeholders::_2, loginCallback));
	}
	else if (response.at(0) == "PasswordNotSet")
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

	// did we get an OK?
	if (response.at(0) == "OK")
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

		// load the plugins
		LoadPlugins();

		// call the login callback
		return loginCallback(LoginResult_OK);
	}
	else if (response.at(0) == "InvalidPasswordHash")
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

void Server::LoadPlugins()
{
	// make sure the plugins directory exists
	if (std::filesystem::is_directory("plugins/") == false)
		return;

	// iterate the plugins directory
	for (const auto& file : std::filesystem::directory_iterator("plugins/"))
	{
		if (file.is_directory() == true)
			continue;

		auto pathStr = file.path().string();
		if (pathStr.size() < sizeof(".plugin") - 1)
			continue;

		// invalid extension
		if (pathStr.substr(pathStr.size() - sizeof(".plugin") + 1) != ".plugin")
			continue;

		auto hPlugin = BLoadLibrary(file.path().string().c_str());

		if (hPlugin == nullptr)
		{
			m_pluginCallback(pathStr, true, false, "Failed to open file");
			continue;
		}

		auto fnPluginFactory = reinterpret_cast<PluginFactory_t>(BFindFunction(hPlugin, "CreatePlugin"));

		if (fnPluginFactory == nullptr)
		{
			m_pluginCallback(pathStr, true, false, "Failed to find CreatePlugin");
			continue;
		}

		auto fnPluginDestructor = reinterpret_cast<PluginDestructor_t>(BFindFunction(hPlugin, "DestroyPlugin"));

		if (fnPluginDestructor == nullptr)
		{
			m_pluginCallback(pathStr, true, false, "Failed to find DestroyPlugin");
			continue;
		}

		// we have a valid plugin. create an instance
		auto pPlugin = fnPluginFactory(this);

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
}

void Server::HandleServerInfo(const ErrorCode_t& ec, const std::vector<std::string>& serverInfo)
{
	// connection should be closed automatically
	if (ec)
		return;

	// parse the result
	if (serverInfo.at(0) != "OK")
	{
		// disconnect, the server is not OK
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}

	if (serverInfo.size() < 24)
	{
		// disconnect, the server is not OK
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}

	try
	{
		// generic info
		m_serverInfo.m_serverName = serverInfo.at(1);
		m_serverInfo.m_playerCount = stoi(serverInfo.at(2));
		m_serverInfo.m_maxPlayerCount = stoi(serverInfo.at(3));
		m_serverInfo.m_gameMode = serverInfo.at(4);
		m_serverInfo.m_map = serverInfo.at(5);
		m_serverInfo.m_roundsPlayed = stoi(serverInfo.at(6));
		m_serverInfo.m_roundsTotal = stoi(serverInfo.at(7));

		// parse scores
		size_t offset = 0;
		auto numTeams = static_cast<size_t>(stoi(serverInfo.at(8)));
		for (; offset < numTeams; ++offset)
		{
			m_serverInfo.m_scores.m_teamScores.push_back(stoi(serverInfo.at(9 + offset)));
		}
		--offset;

		m_serverInfo.m_scores.m_goalScore = stoi(serverInfo.at(10 + offset));

		// more generic info
		m_serverInfo.m_onlineState = serverInfo.at(11 + offset);
		m_serverInfo.m_ranked = (serverInfo.at(12 + offset) == "true");
		m_serverInfo.m_punkBuster = (serverInfo.at(13 + offset) == "true");
		m_serverInfo.m_hasPassword = (serverInfo.at(14 + offset) == "true");
		m_serverInfo.m_serverUpTime = stoi(serverInfo.at(15 + offset));
		m_serverInfo.m_roundTime = stoi(serverInfo.at(16 + offset));
		m_serverInfo.m_serverIpAndPort = serverInfo.at(17 + offset);
		m_serverInfo.m_punkBusterVersion = serverInfo.at(18 + offset);
		m_serverInfo.m_joinQueueEnabled = (serverInfo.at(19 + offset) == "true");
		m_serverInfo.m_region = serverInfo.at(20 + offset);
		m_serverInfo.m_closestPingSite = serverInfo.at(21 + offset);
		m_serverInfo.m_country = serverInfo.at(22 + offset);
		m_serverInfo.m_blazePlayerCount = (serverInfo.size() >= 25) ? stoi(serverInfo.at(23 + offset)) : 0;
		m_serverInfo.m_blazeGameState = serverInfo.at((serverInfo.size() >= 25) ? (24 + offset) : (23 + offset));
	}
	catch (const std::exception& e)
	{
		// they sent bad serverInfo. disconnect
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}

	// call the serverInfo callback
	m_serverInfoCallback(m_serverInfo);

	// reset the timer and wait again
	m_serverInfoTimer.expires_from_now(std::chrono::seconds(15));
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

	/// TODO: Make this whole section work independently of variable count per player
	if (playerInfo.at(0) != "OK" ||
		playerInfo.at(1) != "10")
	{
		// the server is not ok, disconnect
		ErrorCode_t ec;
		Disconnect(ec);
		return;
	}

	/// TODO: Handle team/squad changes, disconnects, and round events that may affect players in teams
	// process each player
	constexpr size_t offset = 13;
	constexpr size_t numVar = 10;
	size_t playerCount = std::stoi(playerInfo.at(12));

	for (size_t i = 0; i < playerCount; ++i)
	{
		std::string playerName = playerInfo.at(offset + numVar * i);
		std::shared_ptr<PlayerInfo> pPlayer;

		auto playerIt = m_players.find(playerName);
		if (playerIt == m_players.end())
			pPlayer = m_players.emplace(playerName, std::make_shared<PlayerInfo>()).first->second;
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
		pPlayer->type = static_cast<uint16_t>(std::stoi(playerInfo.at(offset + numVar * i + 9)));

		// check if they are in the team and squad list
		auto teamIt = m_teams.find(pPlayer->teamId);
		if (teamIt == m_teams.end())
			teamIt = m_teams.emplace(pPlayer->teamId, std::unordered_map<uint8_t, PlayerMap_t>()).first;

		auto squadIt = teamIt->second.find(pPlayer->squadId);
		if (squadIt == teamIt->second.end())
			squadIt = teamIt->second.emplace(pPlayer->squadId, PlayerMap_t()).first;

		if (squadIt->second.find(playerName) == squadIt->second.end())
			squadIt->second.emplace(std::move(playerName), std::move(pPlayer));
	}

	// call the playerInfo callback
	m_playerInfoCallback(m_players, m_teams);

	// reset the timer and wait again
	m_playerInfoTimer.expires_from_now(std::chrono::seconds(15));
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