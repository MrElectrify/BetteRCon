#include <BetteRCon/Server.h>

using BetteRCon::Server;

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

bool Server::IsConnected() const noexcept
{
	return m_connection.IsConnected() == true;
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