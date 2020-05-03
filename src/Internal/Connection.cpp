#include <BetteRCon/Internal/Connection.h>
#include <BetteRCon/Internal/Log.h>

using BetteRCon::Internal::Connection;
using BetteRCon::Internal::Packet;

Connection::Connection(Worker_t& worker) 
	: m_worker(worker), m_connected(false),
	m_socket(m_worker), m_timeoutTimer(m_worker) {}

void Connection::AsyncConnect(const Endpoint_t& endpoint, ConnectCallback_t&& connectCallback, 
	DisconnectCallback_t&& disconnectCallback, RecvCallback_t&& eventCallback) noexcept
{
	// make sure we are not already connected
	if (m_connected == true)
		return connectCallback(asio::error::make_error_code(asio::error::already_connected));
	// save the disconnect and event callbacks
	m_disconnectCallback = std::move(disconnectCallback);
	m_eventCallback = std::move(eventCallback);
	// try to connect
	ErrorCode_t ec;
	m_socket.async_connect(endpoint, [this, connectCallback = std::move(connectCallback)]
		(const ErrorCode_t& ec)
		{
			if (ec)
				return connectCallback(ec);

			// we are successfully connected.
			m_connected = true;

			// start the 2-minute connection timeout
			m_timeoutTimer.expires_from_now(std::chrono::minutes(2));
			m_timeoutTimer.async_wait(std::bind(&Connection::HandleTimeout, this, std::placeholders::_1));

			// read the first 8 bytes from the socket, which will include the size of the packet
			m_incomingBuf.resize(sizeof(int32_t) * 2);

			asio::async_read(m_socket, asio::buffer(m_incomingBuf),
				std::bind(&Connection::HandleReadHeader, this,
					std::placeholders::_1, std::placeholders::_2));

			connectCallback(ec);
		});
}

void Connection::Disconnect() noexcept
{
	// make sure we are not connected already
	if (IsConnected() == false)
		return;
	// post the functor to allow this operation to be called from any thread
	m_worker.post(std::bind(&Connection::CloseConnection, this, ErrorCode_t{}));
}

bool Connection::IsConnected() const noexcept
{
	return m_connected == true;
}

void Connection::SendPacket(const Packet& packet, RecvCallback_t&& callback)
{
	// make sure we are connected
	if (IsConnected() == false)
		return callback(asio::error::make_error_code(asio::error::not_connected), std::nullopt);
	// serialize the data into our buffer
	std::vector<char> sendBuf;
	packet.Serialize(sendBuf);
	// insert the buffer into the queue
	m_sendQueue.push(std::move(sendBuf));
	// ours is the only one. otherwise, a callback will handle sending our data
	if (m_sendQueue.size() == 1)
		SendUnsentBuffers();
	// save the callback
	auto it = m_recvCallbacks.emplace(packet.GetSequence(), std::move(callback));
	BetteRCon::Internal::g_stdOutLog << "Queued packet with " << packet.GetWords().front() << " with seq " << packet.GetSequence() << " and response success " << it.second << '\n';
}

Connection::~Connection()
{
	// if it is destructed, it must be from the thread that created it.
	if (IsConnected() == true)
		CloseConnection(ErrorCode_t{});
}

void Connection::CloseConnection(const ErrorCode_t& ec)
{
	ErrorCode_t ignored;
	// disconnect the socket and call the handler
	m_socket.shutdown(Socket_t::shutdown_both, ignored);
	m_socket.close(ignored);
	// cancel the timer
	m_timeoutTimer.cancel(ignored);
	// update connected status
	m_connected = false;
	// call the disconnect callback
	m_disconnectCallback(ec);
}

void Connection::HandleReadHeader(const ErrorCode_t& ec, const size_t bytes_transferred)
{
	if (ec)
	{
		if (ec != asio::error::operation_aborted)
			CloseConnection(ec);
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
		if (ec != asio::error::operation_aborted)
			CloseConnection(ec);
		return;
	}
	// update the 2-minute connection timeout
	m_timeoutTimer.expires_from_now(std::chrono::minutes(2));
	m_timeoutTimer.async_wait(std::bind(&Connection::HandleTimeout, this, std::placeholders::_1));
	// parse the packet
	std::optional<Packet> receivedPacket;
	try
	{
		receivedPacket = std::make_optional<Packet>(m_incomingBuf);
	}
	catch (const Packet::ErrorCode_t& ec)
	{
		// we got a bad packet. disconnect
		return CloseConnection(asio::error::make_error_code(asio::error::invalid_argument));
	}
	// is this a response or an event?
	if (receivedPacket->IsResponse() == true &&
		receivedPacket->GetWords().size() > 0)
	{
		BetteRCon::Internal::g_stdOutLog << "Received response for seq " << receivedPacket->GetSequence() << '\n';
		// return the response to the caller
		auto callbackFnIt = m_recvCallbacks.find(receivedPacket->GetSequence());
		if (callbackFnIt == m_recvCallbacks.end())
		{
			// this should not happen. abort
			return CloseConnection(asio::error::make_error_code(asio::error::invalid_argument));
		}
		const auto fn = callbackFnIt->second;
		// remove the callback
		m_recvCallbacks.erase(callbackFnIt);
		// call the callback
		fn(ErrorCode_t{}, receivedPacket);
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
	CloseConnection(asio::error::make_error_code(asio::error::timed_out));
}

void Connection::HandleWrite(const ErrorCode_t& ec, const size_t bytes_transferred)
{
	if (ec)
	{
		if (ec != asio::error::operation_aborted)
			CloseConnection(ec);
		return;
	}
	// pop the buffer, we don't need it any more
	m_sendQueue.pop();
	// send more packets if there are some lined up
	if (m_sendQueue.empty() == false)
		SendUnsentBuffers();
}

void Connection::SendUnsentBuffers()
{
	// get the first buffer
	const std::vector<char>& frontBuf = m_sendQueue.front();
	// there is no send in progress
	asio::async_write(m_socket, asio::buffer(frontBuf),
		std::bind(&Connection::HandleWrite, this,
			std::placeholders::_1, std::placeholders::_2));
}