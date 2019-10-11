#include <BetteRCon/Server.h>
#include <BetteRCon/Internal/MD5.h>

using BetteRCon::Server;

int32_t Server::s_lastSequence = 0;

Server::Server() 
	: m_connection(m_worker, 
		std::bind(&Server::HandleEvent, this, 
			std::placeholders::_1, std::placeholders::_2)) {}

void Server::Connect(const Endpoint_t& endpoint)
{
	// make sure we are not already connected
	if (m_connection.IsConnected() == true)
	{
		m_connection.Disconnect();
	}

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

void Server::Login(const std::string& password, LoginCallback_t&& loginCallback)
{
	// send the login request
	SendCommand({ "login.hashed" }, 
		std::bind(&Server::HandleLoginRecvHash, this,
			std::placeholders::_1, std::placeholders::_2, password, loginCallback));
}

void Server::Disconnect()
{
	m_connection.Disconnect();
}

void Server::Disconnect(ErrorCode_t& ec)
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

	// wait for the thread
	if (m_thread.joinable() == true)
		m_thread.join();
}

void Server::HandleEvent(const ErrorCode_t&, std::shared_ptr<Packet_t> event)
{
	// unimplemented
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
		// login is successful. start the main loop
		m_worker.post(std::bind(&Server::MainLoop, this));

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

void Server::MainLoop()
{
	
}