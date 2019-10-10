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

bool Connection::IsConnected() const noexcept
{
	return m_socket.is_open();
}

void Connection::SendPacket(const Packet& packet)
{
	// make sure we are connected
	if (m_socket.is_open() == false)
		throw asio::error::make_error_code(asio::error::not_connected);

	// serialize the data into our buffer
	packet.Serialize(m_outgoingBuf);

	// write the data to the socket
	asio::async_write(m_socket, asio::buffer(m_outgoingBuf), 
		std::bind(&Connection::HandleWrite, this,
			std::placeholders::_1, std::placeholders::_2));
}

void Connection::SendPacket(const Packet& packet, ErrorCode_t& ec) noexcept
{
	try
	{
		SendPacket(packet);
	}
	catch (const ErrorCode_t& e)
	{
		ec = e;
	}
}

void Connection::CloseConnection()
{
	ErrorCode_t ec;

	// close the connection
	m_socket.shutdown(asio::socket_base::shutdown_both, ec);
	m_socket.close(ec);
}

void Connection::HandleWrite(const ErrorCode_t& ec, const size_t bytes_transferred)
{
	if (ec)
	{
		// we failed to read the data, we are no longer connected
		CloseConnection();
	}
}