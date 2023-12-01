#include <winsock2.h>
#include <mswsock.h>
#include <sys/types.h>
#include <errno.h>
#include "Decoder.h"
#include "TCPClient.h"
#include "TCPClientIOWorker.h"
#include "ClientEngine.h"

using namespace fpnn;

bool TCPClientConnection::connectedEventCompleted()
{
	if (SOCKET_ERROR == setsockopt(socket(), SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0))
	{
		int lastErrorCode = WSAGetLastError();
		LOG_ERROR("Call setsockopt() with SO_UPDATE_CONNECT_CONTEXT failed, code: %d. %s",
			lastErrorCode, _connectionInfo->str().c_str());
		return false;
	}

	bool isClosed;
	if (beginReceiving(isClosed) == false)
	{
		if (isClosed)
			LOG_ERROR("TCP connection is closed when connectedEventCompleted() calling."
				" Maybe server closed the connection. %s", _connectionInfo->str().c_str());

		return false;
	}

	_sendBuffer.allowSending();

	if (beginSend(0, false, isClosed) == false)
	{
		if (isClosed)
			LOG_ERROR("TCP connection is closed when connectedEventCompleted() calling."
				" Maybe server closed the connection. %s", _connectionInfo->str().c_str());

		return false;
	}

	return true;
}

bool TCPClientConnection::beginSend(DWORD lastSentBytes, bool IOCPCompletedEvent, bool& isClosed, std::string* data)
{
	FPOverLapped* overLapped = _engine->obtainFPOverLapped();
	if (overLapped == NULL)
	{
		LOG_ERROR("TCP connection prepare FPOverLapped buffer for WSASend() API failed. %s", _connectionInfo->str().c_str());
		return false;
	}

	overLapped->connectionUniqueId = _connectionInfo->uniqueId();
	overLapped->action = IOCPActionType::IOCP_Send;

	bool realSent;
	bool rev = _sendBuffer.send(_connectionInfo->socket, (LPWSAOVERLAPPED)overLapped,
		lastSentBytes, IOCPCompletedEvent, realSent, isClosed, data);

	if (realSent)
	{
		//-- _activeTime vaule maybe in confusion after concurrent Sending on one connection.
		//-- But the probability is very low even server with high load. So, it hasn't be adjusted at current.
		_activeTime = time(NULL);
	}
	else
		_engine->returnFPOverLapped(overLapped);
	
	return rev;
}

bool TCPClientConnection::beginReceiving(bool& isClosed)
{
	isClosed = false;

	LPWSABUF wsaBuffer = _receiver->beginRecv();
	if (!wsaBuffer)
	{
		LOG_ERROR("TCP connection prepare WSABUF for WSARecv() API failed. %s", _connectionInfo->str().c_str());
		return false;
	}

	FPOverLapped* overLapped = _engine->obtainFPOverLapped();
	if (overLapped == NULL)
	{
		LOG_ERROR("TCP connection prepare FPOverLapped buffer for WSARecv() API failed. %s", _connectionInfo->str().c_str());
		return false;
	}

	overLapped->connectionUniqueId = _connectionInfo->uniqueId();
	overLapped->action = IOCPActionType::IOCP_Recv;

	DWORD dwFlags = 0;
	int result = WSARecv(_connectionInfo->socket, wsaBuffer, 1, NULL, &dwFlags, (LPWSAOVERLAPPED)overLapped, NULL);
	if (result == SOCKET_ERROR)
	{
		int errorCode = WSAGetLastError();
		if (errorCode == WSA_IO_PENDING)
			return true;

		if (errorCode == WSAECONNRESET || errorCode == WSAEDISCON || errorCode == WSAESHUTDOWN)
		{
			// isClosed = true;
		}
		else
		{
			LOG_ERROR("Error occurred when calling WSARecv() API. WSA error code is: %d. %s",
				errorCode, _connectionInfo->str().c_str());
		}

		isClosed = true;

		_engine->returnFPOverLapped(overLapped);
		return false;
	}
	
	return true;
}

bool TCPClientConnection::receivedData(DWORD receivedBytes, FPQuestPtr& quest, FPAnswerPtr& answer)
{
	if (_embedRecvNotifyDeleagte == NULL)
	{
		bool status = _receiver->receivedData(receivedBytes, quest, answer);
		if (status == false)
		{
			LOG_ERROR("Client receiving & decoding data error. Connection will be closed soon. %s", _connectionInfo->str().c_str());
			return false;
		}
	}
	else
	{
		bool status = _receiver->embed_receivedData(_connectionInfo->uniqueId(), receivedBytes, _embedRecvNotifyDeleagte);
		if (status == false)
		{
			LOG_ERROR("Client receiving data in embedded mode error. Connection will be closed soon. %s", _connectionInfo->str().c_str());
			return false;
		}
	}

	return true;
}

bool TCPClientIOProcessor::deliverAnswer(TCPClientConnection * connection, FPAnswerPtr answer)
{
	TCPClientPtr client = connection->client();
	if (client)
	{
		client->dealAnswer(answer, connection->_connectionInfo);
		return true;
	}
	else
	{
		return false;
	}
}

bool TCPClientIOProcessor::deliverQuest(TCPClientConnection * connection, FPQuestPtr quest)
{
	TCPClientPtr client = connection->client();
	if (client)
	{
		client->dealQuest(quest, connection->_connectionInfo);
		return true;
	}
	else
	{
		LOG_ERROR("Duplex client is destroyed. Connection will be closed. %s", connection->_connectionInfo->str().c_str());
		return false;
	}
}

void TCPClientIOProcessor::processConnectedEvent(TCPClientConnection * connection)
{
	connection->_socketConnected = true;

	TCPClientPtr client = connection->client();
	if (client)
	{
		client->socketConnected(connection, true);
		connection->_refCount--;
	}
	else
		closeConnection(connection, false);
}

void TCPClientIOProcessor::processReceiveEvent(TCPClientConnection * connection, DWORD dwBytesTransferred)
{
	if (dwBytesTransferred == 0)
	{
		closeConnection(connection, true);
		return;
	}

	connection->updateReceivedMS();

	FPQuestPtr quest;
	FPAnswerPtr answer;
	if (connection->receivedData(dwBytesTransferred, quest, answer) == false)
	{
		closeConnection(connection, false);
		return;
	}

	if (quest)
	{
		if (deliverQuest(connection, quest) == false)
		{
			closeConnection(connection, false);
			return;
		}
	}
	if (answer)
	{
		if (deliverAnswer(connection, answer) == false)
		{
			closeConnection(connection, false);
			return;
		}
	}

	bool isClosed;
	if (connection->beginReceiving(isClosed) == false)
	{
		if (isClosed)
			LOG_ERROR("TCP connection is closed when processReceiveEvent() calling."
				" Maybe server closed the connection. %s", connection->_connectionInfo->str().c_str());

		closeConnection(connection, true);
		return;
	}

	connection->_refCount--;
}

void TCPClientIOProcessor::processSendEvent(TCPClientConnection * connection, DWORD dwBytesTransferred)
{
	bool isClosed;
	bool status = connection->beginSend(dwBytesTransferred, true, isClosed);
	if (isClosed)
	{
		closeConnection(connection, true);
		return;
	}
	if (status)
	{
		connection->_refCount--;
	}
	else
	{
		LOG_ERROR("TCP connection is closed when processSendEvent() calling."
			" Maybe server closed the connection. %s", connection->_connectionInfo->str().c_str());

		closeConnection(connection, false);
	}
}

void TCPClientIOProcessor::closeConnection(TCPClientConnection * connection, bool normalClosed)
{
	bool closedByError = !normalClosed;
	int errorCode = normalClosed ? FPNN_EC_CORE_CONNECTION_CLOSED : FPNN_EC_CORE_INVALID_CONNECTION;

	ClientEngine* clientEngine = connection->_engine;
	if (clientEngine->takeConnection(connection->socket()) == NULL)
	{
		connection->_refCount--;
		return;
	}

	clientEngine->quit(connection);
	// connection->_refCount--;

	TCPClientPtr client = connection->client();
	if (client)
	{
		client->clearConnectionQuestCallbacks(connection, errorCode);
		client->willClose(connection, closedByError);
	}
	else
	{
		clientEngine->clearConnectionQuestCallbacks(connection, errorCode);
		
		std::shared_ptr<ClientCloseTask> task(new ClientCloseTask(connection->questProcessor(), connection, closedByError));
		clientEngine->execute(task);
		clientEngine->reclaim(task);
	}

	connection->_refCount--;
}
