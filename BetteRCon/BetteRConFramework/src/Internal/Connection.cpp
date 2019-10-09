#include <BetteRCon/Internal/Connection.h>

using BetteRCon::Internal::Connection;

Connection::Connection(Worker_t& worker) : m_socket(worker) {}

void Connection::Connect(const Endpoint_t& endpoint)
{
	ErrorCode_t ec;

	m_socket.connect(endpoint, ec);

	// we failed to connect
	if (ec)
		throw ec;
}

void Connection::Connect(const Endpoint_t& endpoint, ErrorCode_t& ec) noexcept
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