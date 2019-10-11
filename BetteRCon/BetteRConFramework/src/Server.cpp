#include <BetteRCon/Server.h>

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
	m_thread = std::thread([this]
	{
		// add the main loop to the queue
		m_worker.post(std::bind(&Server::MainLoop, this));

		m_worker.run();
	});
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

	m_thread.join();
}

void Server::HandleEvent(const ErrorCode_t&, std::shared_ptr<Packet_t> event)
{
	// unimplemented
}

void Server::MainLoop()
{
	// unimplemented
}