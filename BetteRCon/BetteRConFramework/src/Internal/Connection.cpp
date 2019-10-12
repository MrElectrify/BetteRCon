#include <BetteRCon/Internal/Connection.h>

using BetteRCon::Internal::Connection;
using BetteRCon::Internal::Packet;

Connection::Connection(Worker_t& worker, RecvCallback_t&& eventCallback) : m_connected(false), m_eventCallback(std::move(eventCallback)), m_socket(worker), m_timeoutTimer(worker) {}

void Connection::Connect(const Endpoint_t& endpoint)
{
	ErrorCode_t ec;

	m_socket.connect(endpoint, ec);

	// we failed to connect
	if (ec)
		throw ec;

	m_connected = true;

	// start the 2-minute connection timeout
	m_timeoutTimer.expires_from_now(std::chrono::minutes(2));
	m_timeoutTimer.async_wait(std::bind(&Connection::HandleTimeout, this, std::placeholders::_1));

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

void Connection::SendPacket(const Packet& packet, RecvCallback_t&& callback)
{
	// make sure we are connected
	if (IsConnected() == false)
		return callback(asio::error::make_error_code(asio::error::not_connected), nullptr);

	// serialize the data into our buffer
	packet.Serialize(m_outgoingBuf);

	// save the callback
	m_recvCallbacks.emplace(packet.GetSequence(), std::move(callback));

	// write the data to the socket
	asio::async_write(m_socket, asio::buffer(m_outgoingBuf),
		std::bind(&Connection::HandleWrite, this,
			std::placeholders::_1, std::placeholders::_2));
}

void Connection::CloseConnection()
{
	ErrorCode_t ec;

	m_connected = false;

	// close the connection
	m_socket.shutdown(asio::socket_base::shutdown_both, ec);
	m_socket.close(ec);

	// cancel the timer
	m_timeoutTimer.cancel(ec);
}

void Connection::HandleReadHeader(const ErrorCode_t& ec, const size_t bytes_transferred)
{
	if (ec)
	{
		// store the error code if it is not the result of a cancelled operation
		if (ec != asio::error::operation_aborted &&
			m_connected == true)
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
		if (ec != asio::error::operation_aborted &&
			m_connected == true)
		{
			m_lastErrorCode = ec;
			CloseConnection();
		}
		return;
	}

	// start the 2-minute connection timeout
	m_timeoutTimer.expires_from_now(std::chrono::minutes(2));
	m_timeoutTimer.async_wait(std::bind(&Connection::HandleTimeout, this, std::placeholders::_1));

	// parse the packet
	std::shared_ptr<Packet> receivedPacket;
	
	try
	{
		receivedPacket = std::make_shared<Packet>(m_incomingBuf);
	}
	catch (const Packet::ErrorCode_t& ec)
	{
		// we got a bad packet. disconnect
		m_lastErrorCode = asio::error::make_error_code(asio::error::invalid_argument);
		CloseConnection();
		return;
	}

	// is this a response or an event?
	if (receivedPacket->IsResponse() == true)
	{
		// return the response to the caller
		auto callbackFnIt = m_recvCallbacks.find(receivedPacket->GetSequence());
		if (callbackFnIt == m_recvCallbacks.end())
		{
			// this should not happen. abort
			CloseConnection();
			m_lastErrorCode = asio::error::make_error_code(asio::error::service_not_found);
			return;
		}

		// call the callback
		callbackFnIt->second(ErrorCode_t{}, receivedPacket);

		// remove the callback
		m_recvCallbacks.erase(callbackFnIt);
	}
	else
	{
		// move the event
		m_eventCallback(ErrorCode_t{}, receivedPacket);
	}

	// read the first 8 bytes from the socket, which will include the size of the packet
	m_incomingBuf.resize(sizeof(int32_t) * 2);

	asio::async_read(m_socket, asio::buffer(m_incomingBuf),
		std::bind(&Connection::HandleReadHeader, this,
			std::placeholders::_1, std::placeholders::_2));
}

void Connection::HandleTimeout(const ErrorCode_t& ec)
{
	// we reset the timer
	if (ec == asio::error::operation_aborted)
		return;

	// 2 minutes have passed since we got a transmission from them. assume they are frozen, shutdown
	m_lastErrorCode = asio::error::make_error_code(asio::error::timed_out);
	CloseConnection();
}

void Connection::HandleWrite(const ErrorCode_t& ec, const size_t bytes_transferred)
{
	if (ec)
	{
		// store the error code if it is not the result of a cancelled operation
		if (ec != asio::error::operation_aborted && 
			m_connected == true)
		{
			m_lastErrorCode = ec;
			CloseConnection();
		}
	}
}