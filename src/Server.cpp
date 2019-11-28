#include <BetteRCon/Server.h>
#include <MD5.h>

#include <iostream>

using BetteRCon::Server;

int32_t Server::s_lastSequence = 0;

Server::Server() 
	: m_connection(m_worker, 
		std::bind(&Server::HandleEvent, this, 
			std::placeholders::_1, std::placeholders::_2)),
	m_serverInfoTimer(m_worker) 
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
	// make sure we are not already connected
	if (m_connection.IsConnected() == true)
		m_connection.Disconnect();

	// make sure our thread is not already running a server
	if (m_thread.joinable() == true)
		m_thread.join();

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

void Server::Login(const std::string& password, LoginCallback_t&& loginCallback, EventCallback_t&& eventCallback, ServerInfoCallback_t&& serverInfoCallback, PlayerInfoCallback_t&& playerInfoCallback)
{
	// send the login request
	SendCommand({ "login.hashed" }, 
		std::bind(&Server::HandleLoginRecvHash, this,
			std::placeholders::_1, std::placeholders::_2, password, loginCallback));

	// store the callbacks
	m_eventCallback = std::move(eventCallback);
	m_serverInfoCallback = std::move(serverInfoCallback);
	// m_playerInfoCallback = std::move(playerInfoCallback);
}

void Server::Disconnect()
{
	m_connection.Disconnect();
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
	// we don't actually care if this fails, we are ending the server anyways
	ErrorCode_t ec;
	m_connection.Disconnect(ec);

	// kill timers
	m_serverInfoTimer.cancel(ec);

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

void Server::HandleEvent(const ErrorCode_t&, std::shared_ptr<Packet_t> event)
{
	const auto& words = event->GetWords();

	std::cout << "Event " << words.at(0) << " (" << event->GetSequence() << "): ";

	// print out the event for debugging
	for (size_t i = 1; i < words.size(); ++i)
	{
		std::cout << words.at(i) << ' ';
	}
	std::cout << '\n';

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

	if (serverInfo.size() < 25)
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
		m_serverInfo.m_blazePlayerCount = stoi(serverInfo.at(23 + offset));
		m_serverInfo.m_blazeGameState = serverInfo.at(24 + offset);
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