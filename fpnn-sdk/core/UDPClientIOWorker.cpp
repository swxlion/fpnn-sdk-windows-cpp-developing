#include <time.h>
#include "ClientEngine.h"
#include "UDPClientIOWorker.h"
#include "UDPClient.h"

using namespace fpnn;

//====================================================//
//--                 UDPClientConnection                  --//
//====================================================//
void UDPClientConnection::sendCachedData()
{
	FPOverLapped* overLapped = _engine->obtainFPOverLapped();
	if (overLapped == NULL)
	{
		LOG_ERROR("UDP connection prepare FPOverLapped buffer for sendCachedData() API failed. %s", _connectionInfo->str().c_str());
		return;
	}

	overLapped->connectionUniqueId = _connectionInfo->uniqueId();
	overLapped->action = IOCPActionType::IOCP_Send;

	bool realSent = false;
	bool blockByFlowControl = false;
	_ioBuffer.sendCachedData((LPWSAOVERLAPPED)overLapped, realSent, blockByFlowControl);
	_activeTime = time(NULL);

	if (!realSent)
		_engine->returnFPOverLapped(overLapped);
}

void UDPClientConnection::sendData(std::string* data, int64_t expiredMS, bool discardable)
{
	FPOverLapped* overLapped = _engine->obtainFPOverLapped();
	if (overLapped == NULL)
	{
		LOG_ERROR("UDP connection prepare FPOverLapped buffer for sendData() API failed. %s", _connectionInfo->str().c_str());
		return;
	}

	overLapped->connectionUniqueId = _connectionInfo->uniqueId();
	overLapped->action = IOCPActionType::IOCP_Send;

	bool realSent = false;
	bool blockByFlowControl = false;
	_ioBuffer.sendData((LPWSAOVERLAPPED)overLapped, realSent, blockByFlowControl, data, expiredMS, discardable);
	_activeTime = time(NULL);

	if (!realSent)
		_engine->returnFPOverLapped(overLapped);
}

void UDPClientConnection::sendCloseSignal()
{
	FPOverLapped* overLapped = _engine->obtainFPOverLapped();
	if (overLapped == NULL)
	{
		LOG_ERROR("UDP connection prepare FPOverLapped buffer for sendCloseSignal() API failed. %s", _connectionInfo->str().c_str());
		return;
	}

	overLapped->connectionUniqueId = _connectionInfo->uniqueId();
	overLapped->action = IOCPActionType::IOCP_Send;

	bool realSent = false;
	_ioBuffer.sendCloseSignal((LPWSAOVERLAPPED)overLapped, realSent);

	if (!realSent)
		_engine->returnFPOverLapped(overLapped);
}

bool UDPClientConnection::IOCPSendCompleted(DWORD lastSentBytes)
{
	FPOverLapped* overLapped = _engine->obtainFPOverLapped();
	if (overLapped == NULL)
	{
		LOG_ERROR("UDP connection prepare FPOverLapped buffer for IOCPSendCompleted() API failed. %s", _connectionInfo->str().c_str());
		return false;
	}

	overLapped->connectionUniqueId = _connectionInfo->uniqueId();
	overLapped->action = IOCPActionType::IOCP_Send;

	bool realSent = false;
	_ioBuffer.IOCPSendCompleted((int)lastSentBytes, (LPWSAOVERLAPPED)overLapped, realSent);
	
	if (!realSent)
		_engine->returnFPOverLapped(overLapped);

	return true;
}

bool UDPClientConnection::beginReceiving(bool& isClosed)
{
	isClosed = false;

	FPOverLapped* overLapped = _engine->obtainFPOverLapped();
	if (overLapped == NULL)
	{
		LOG_ERROR("UDP connection prepare FPOverLapped buffer for WSARecv() API failed. %s", _connectionInfo->str().c_str());
		return false;
	}

	overLapped->connectionUniqueId = _connectionInfo->uniqueId();
	overLapped->action = IOCPActionType::IOCP_Recv;

	DWORD dwFlags = 0;
	int result = WSARecv(_connectionInfo->socket, &_wsaRecvBuffer, 1, NULL, &dwFlags, (LPWSAOVERLAPPED)overLapped, NULL);
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

bool UDPClientConnection::receivedData(DWORD receivedBytes, std::list<FPQuestPtr>& questList, std::list<FPAnswerPtr>& answerList)
{
	//-- _ioBuffer.getRecvToken();  //-- No necessary.
	bool status = _ioBuffer.recvData(_wsaRecvBuffer.buf, (int)receivedBytes);
	questList.swap(_ioBuffer.getReceivedQuestList());
	answerList.swap(_ioBuffer.getReceivedAnswerList());

	/*
	*  '_activeTime' is not used in current, and '_activeTime' is invalid in embed mode.
	*  If you want to use '_activeTime' in embed mode, please modify the following codes,
	*  adding the processing codes for '_activeTime'.
	*/
	if (questList.size() || answerList.size())
		_activeTime = time(NULL);

	_ioBuffer.returnRecvToken();

	return status;
}

//====================================================//
//--              UDPClientIOWorker                 --//
//====================================================//
bool UDPClientIOProcessor::deliverAnswer(UDPClientConnection * connection, FPAnswerPtr answer)
{
	UDPClientPtr client = connection->client();
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

bool UDPClientIOProcessor::deliverQuest(UDPClientConnection * connection, FPQuestPtr quest)
{
	UDPClientPtr client = connection->client();
	if (client)
	{
		client->dealQuest(quest, connection->_connectionInfo);
		return true;
	}
	else
	{
		LOG_ERROR("UDP duplex client is destroyed. Connection will be closed. %s", connection->_connectionInfo->str().c_str());
		return false;
	}
}

void UDPClientIOProcessor::processReceiveEvent(UDPClientConnection * connection, DWORD dwBytesTransferred)
{
	if (dwBytesTransferred == 0)
	{
		closeConnection(connection, true);
		return;
	}

	std::list<FPQuestPtr> questList;
	std::list<FPAnswerPtr> answerList;

	if (connection->receivedData(dwBytesTransferred, questList, answerList) == false)
	{
		closeConnection(connection, false);
		return;
	}

	for (auto& answer: answerList)
		if (!deliverAnswer(connection, answer))
		{
			closeConnection(connection, false);
			return;
		}
	
	for (auto& quest: questList)
		if (!deliverQuest(connection, quest))
		{
			closeConnection(connection, false);
			return;
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

	if (isClosed)
	{
		closeConnection(connection, false);
		return;
	}

	//-- For UNA, acks, sync, etc ...
	connection->sendCachedData();

	if (connection->isRequireClose())
	{
		closeConnection(connection, false);
		return;
	}

	connection->_refCount--;
}

void UDPClientIOProcessor::processSendEvent(UDPClientConnection * connection, DWORD dwBytesTransferred)
{
	bool status = connection->IOCPSendCompleted(dwBytesTransferred);
	if (!status)
	{
		closeConnection(connection, false);
		return;
	}

	if (connection->isRequireClose())
	{
		closeConnection(connection, true);
		return;
	}

	connection->_refCount--;
}

void UDPClientIOProcessor::closeConnection(UDPClientConnection * connection, bool normalClosed)
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

	UDPClientPtr client = connection->client();
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
