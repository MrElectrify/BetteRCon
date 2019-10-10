#include <BetteRCon/Internal/Connection.h>

#include <iostream>

using BetteRCon::Internal::Connection;
using BetteRCon::Internal::Packet;

Connection::Connection(Worker_t& worker) : m_socket(worker) {}

void Connection::Connect(const Endpoint_t& endpoint)
{
	ErrorCode_t ec;

	m_socket.connect(endpoint, ec);

	// we failed to connect
	if (ec)
		throw ec;

	// read the first 8 bytes from the socket, which will include the size of the packet
	m_incomingBuf.resize(sizeof(int32_t) * 2);

	asio::async_read(m_socket, asio::buffer(m_incomingBuf),
		std::bind(&Connection::HandleReadHeader, this,
			std::placeholders::_1, std::placeholders::_2));
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

void Connection::Disconnect()
{
	// make sure we are not connected already
	if (IsConnected() == false)
		throw asio::error::make_error_code(asio::error::not_connected);

	// calling from the same thread, this is OK
	CloseConnection();
}

void Connection::Disconnect(ErrorCode_t& ec) noexcept
{
	try
	{
		Disconnect();
	}
	catch (const ErrorCode_t& e)
	{
		ec = e;
	}
}

Connection::ErrorCode_t Connection::GetLastErrorCode() const noexcept
{
	return m_lastErrorCode;
}

bool Connection::IsConnected() const noexcept
{
	return m_connected == true;
}

void Connection::SendPacket(const Packet& packet)
{
	// make sure we are connected
	if (IsConnected() == false)
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

std::shared_ptr<Packet> Connection::RecvResponse(const int32_t sequence)
{
	// make sure we are connected
	if (IsConnected() == false)
		throw asio::error::make_error_code(asio::error::not_connected);

	// search for the packet
	{
		std::lock_guard responseGuard(m_incomingResponseMutex);

		auto responseIt =  m_incomingResponses.find(sequence);
		if (responseIt != m_incomingResponses.end())
		{
			// we found the response, remove it and return it
			auto pResponse = responseIt->second;

			m_incomingResponses.erase(responseIt);

			return pResponse;
		}
	}
	return nullptr;
}

std::shared_ptr<Packet> Connection::RecvResponse(const int32_t sequence, ErrorCode_t& ec) noexcept
{
	try
	{
		return RecvResponse(sequence);
	}
	catch (const ErrorCode_t& e)
	{
		ec = e;
		return nullptr;
	}
}

void Connection::CloseConnection()
{
	ErrorCode_t ec;

	// close the connection
	m_socket.shutdown(asio::socket_base::shutdown_both, ec);
	m_socket.close(ec);

	m_connected = false;
}

void Connection::HandleReadHeader(const ErrorCode_t& ec, const size_t bytes_transferred)
{
	if (ec)
	{
		// store the error code if it is not the result of a cancelled operation
		if (ec != asio::error::operation_aborted)
		{
			m_lastErrorCode = ec;
			CloseConnection();
		}
		return;
	}

	const auto packetSize = *reinterpret_cast<const int32_t*>(&m_incomingBuf[sizeof(int32_t)]);

	// resize the buffer to fit the whole packet
	m_incomingBuf.resize(packetSize);

	// receive the rest of the packet
	asio::async_read(m_socket, asio::buffer(m_incomingBuf.data() + sizeof(int32_t) * 2, packetSize - sizeof(int32_t) * 2),
		std::bind(&Connection::HandleReadBody, this,
			std::placeholders::_1, std::placeholders::_2));
}

void Connection::HandleReadBody(const ErrorCode_t& ec, const size_t bytes_transferred)
{
	if (ec)
	{
		// store the error code if it is not the result of a cancelled operation
		if (ec != asio::error::operation_aborted)
		{
			m_lastErrorCode = ec;
			CloseConnection();
		}
		return;
	}

	// parse the packet
	auto receivedPacket = std::make_shared<Packet>(m_incomingBuf);

	// is this a response or an event?
	if (receivedPacket->IsResponse() == true)
	{
		// this is a response. place it with the other responses
		std::lock_guard responseGuard(m_incomingResponseMutex);

		// move the packet
		m_incomingResponses.emplace(receivedPacket->GetSequence(), std::move(receivedPacket));
	}
	else
	{
		// this is an event. place it with the other events
		std::lock_guard eventGuard(m_incomingEventMutex);

		// move the event
		m_incomingEvents.emplace(receivedPacket->GetSequence(), std::move(receivedPacket));
	}

	// read the first 8 bytes from the socket, which will include the size of the packet
	m_incomingBuf.resize(sizeof(int32_t) * 2);

	asio::async_read(m_socket, asio::buffer(m_incomingBuf),
		std::bind(&Connection::HandleReadHeader, this,
			std::placeholders::_1, std::placeholders::_2));
}

void Connection::HandleWrite(const ErrorCode_t& ec, const size_t bytes_transferred)
{
	if (ec)
	{
		// store the error code if it is not the result of a cancelled operation
		if (ec != asio::error::operation_aborted)
		{
			m_lastErrorCode = ec;
			CloseConnection();
		}
	}
}