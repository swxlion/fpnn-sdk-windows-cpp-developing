#include <winsock2.h>
#include <errno.h>
#include <atomic>
#include <list>
#include <fcntl.h>
#include <sysinfoapi.h>
#include "Config.h"
#include "FPLog.h"
#include "NetworkUtility.h"
#include "TimeUtil.h"
#include "TCPClient.h"
#include "UDPClient.h"
#include "ClientEngine.h"

#pragma comment(lib, "kernel32.lib")

using namespace fpnn;

/*
	We don't use gc_mutex in ClientEngine business logic, because gc_mutex maybe free before _engine.
	For some compiler, global variable free order maybe stack; but for other compiler, the free order maybe same as the init order.
	e.g. g++ with XCode on MacOS X.
*/
static std::mutex gc_mutex;
static std::atomic<bool> _created(false);
static ClientEnginePtr _engine;

const DWORD IOCPExitTransferredBytes = 2;

ClientEnginePtr ClientEngine::create(const ClientEngineInitParams *params)
{
	if (!_created)
	{
		std::unique_lock<std::mutex> lck(gc_mutex);
		if (!_created)
		{
			_engine.reset(new ClientEngine(params));
			_created = true;
		}
	}
	return _engine;
}

ClientEngine::ClientEngine(const ClientEngineInitParams *params): _running(true),
	_requireLaunchCleanupAction(true)
{
	ClientEngineInitParams defaultParams;
	if (!params)
		params = &defaultParams;

	_logHolder = FPLog::instance();

	_connectTimeout = params->globalConnectTimeoutSeconds * 1000;
	_questTimeout = params->globalQuestTimeoutSeconds * 1000;

	_iocpWorkThreadCount = params->IOCPWorkThreadCount;
	if (_iocpWorkThreadCount == 0)
	{
		SYSTEM_INFO sysInfo;
		GetSystemInfo(&sysInfo);

		_iocpWorkThreadCount = sysInfo.dwNumberOfProcessors;
	}

	_overLappedPools.init(sizeof(FPOverLapped),
		params->overLappedPoolInitParams.initBlocks,
		params->overLappedPoolInitParams.perAppendBlocks,
		params->overLappedPoolInitParams.perfectBlocks,
		0);

	_callbackPool.init(0, 1, params->residentTaskThread, params->maxTaskThreads);

	_timeoutChecker = std::thread(&ClientEngine::timeoutCheckThread, this);

	prepareIOCP();
}

ClientEngine::~ClientEngine()
{
	_running = false;

	clean();

	OVERLAPPED* exitOverlappeds = (OVERLAPPED*)malloc(_iocpWorkThreadCount * sizeof(OVERLAPPED));
	ZeroMemory(exitOverlappeds, _iocpWorkThreadCount * sizeof(OVERLAPPED));

	for(int i = 0; i < _iocpWorkThreadCount; i++)
	{
		LPOVERLAPPED pointer = exitOverlappeds + i * sizeof(OVERLAPPED);
		PostQueuedCompletionStatus(_iocp, sizeof(SOCKET), (ULONG_PTR)INVALID_SOCKET, pointer);
	}

	_timeoutChecker.join();

	CloseHandle(_iocp);

	for(int i = 0; i < _iocpWorkThreadCount; i++)
		_iocpWorkThread[i].join();

	free(exitOverlappeds);

	_callbackPool.release();
	_overLappedPools.release();

	if (_requireLaunchCleanupAction)
	{
		if (_cleanupAction)
			_cleanupAction();
		else
			WSACleanup();
	}
}

void ClientEngine::prepareIOCP()
{
	_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, _iocpWorkThreadCount);
	if (_iocp == NULL)
	{
		LOG_FATAL("IOCP create failed. error code is: %d", GetLastError());
		//-- Beacuse the failed in constructor, so we have to create IOCP working threads for ClientEngine can be normally destroyed.
	}

	for (int i = 0; i < _iocpWorkThreadCount; i++)
		_iocpWorkThread.push_back(std::thread(&ClientEngine::IOCPWorkThread, this));
}

bool ClientEngine::join(BasicConnection* connection)
{
	SOCKET socket = connection->socket();

	if (CreateIoCompletionPort((HANDLE)socket, _iocp, (ULONG_PTR)(socket), 0) == NULL)
	{
		LOG_ERROR("Bind socket %s for %s to IOCP failed. Last error code is: %d",
			socket, connection->_connectionInfo->str().c_str(), GetLastError());
		return false;
	}

	connection->_engine = this;
	_connectionMap.insert(socket, connection);
	return true;
}

void ClientEngine::quit(const BasicConnection* connection)
{
	SOCKET socket = connection->socket();
	_connectionMap.remove(socket);
}

void ClientEngine::sendTCPData(SOCKET socket, uint64_t token, std::string* data)
{
	if (!_connectionMap.sendTCPData(socket, token, data))
	{
		delete data;
		LOG_ERROR("TCP data not send at socket %d. socket maybe closed.", socket);
	}
}

void ClientEngine::sendUDPData(SOCKET socket, uint64_t token, std::string* data, int64_t expiredMS, bool discardable)
{
	if (expiredMS == 0)
		expiredMS = slack_real_msec() + _questTimeout;

	if (!_connectionMap.sendUDPData(socket, token, data, expiredMS, discardable))
	{
		delete data;
		LOG_WARN("UDP data not send at socket %d. socket maybe closed.", socket);
	}
}

void ClientEngine::clearConnectionQuestCallbacks(BasicConnection* connection, int errorCode)
{
	for (auto callbackPair: connection->_callbackMap)
	{
		BasicAnswerCallback* callback = callbackPair.second;
		if (callback->syncedCallback())		//-- check first, then fill result.
			callback->fillResult(NULL, errorCode);
		else
		{
			callback->fillResult(NULL, errorCode);

			BasicAnswerCallbackPtr task(callback);
			_callbackPool.wakeUp(task);
		}
	}
	// connection->_callbackMap.clear(); //-- If necessary.
}

void ClientEngine::closeConnection(BasicConnection* conn, ClientPtr client, int errorCode)
{
	BasicConnection* connection = takeConnection(conn->_connectionInfo.get());
	if (connection != NULL)
	{
		quit(connection);
		clearConnectionQuestCallbacks(connection, errorCode);
		client->willClose(connection, errorCode != FPNN_EC_OK);
	}
}

void ClientEngine::closeUDPConnection(UDPClientConnection* connection)
{
	quit(connection);

	UDPClientPtr client = connection->client();
	if (client)
	{
		client->clearConnectionQuestCallbacks(connection, FPNN_EC_CORE_CONNECTION_CLOSED);
		client->willClose(connection, false);
	}
	else
	{
		clearConnectionQuestCallbacks(connection, FPNN_EC_CORE_CONNECTION_CLOSED);

		std::shared_ptr<ClientCloseTask> task(new ClientCloseTask(connection->questProcessor(), connection, false));
		_callbackPool.wakeUp(task);
		reclaim(task);
	}
}

void ClientEngine::clearConnection(SOCKET socket, int errorCode)
{
	BasicConnection* conn = _connectionMap.takeConnection(socket);
	if (conn == NULL)
		return;

	_connectionMap.remove(socket);
	clearConnectionQuestCallbacks(conn, errorCode);

	if (conn->connectionType() == BasicConnection::TCPClientConnectionType)
	{
		TCPClientPtr client = ((TCPClientConnection*)conn)->client();
		if (client)
		{
			client->willClose(conn, false);
			return;
		}
	}

	if (conn->connectionType() == BasicConnection::UDPClientConnectionType)
	{
		UDPClientPtr client = ((UDPClientConnection*)conn)->client();
		if (client)
		{
			client->willClose(conn, false);
			return;
		}
	}
	
	{
		std::shared_ptr<ClientCloseTask> task(new ClientCloseTask(conn->questProcessor(), conn, false));
		_callbackPool.wakeUp(task);
		reclaim(task);
	}
}

void ClientEngine::clean()
{
	std::set<SOCKET> fdSet;
	_connectionMap.getAllSocket(fdSet);
	
	for (int socket: fdSet)
		clearConnection(socket, FPNN_EC_CORE_CONNECTION_CLOSED);

	_connectionMap.waitForEmpty();
}

void ClientEngine::IOCPWorkThread()
{
	DWORD dwBytesTransferred;
	SOCKET socket;
	FPOverLapped* fpOverlapped;

	while (_running)
	{
		fpOverlapped = NULL;
		dwBytesTransferred = 0;
		socket = INVALID_SOCKET;
		BOOL iocpStatus = GetQueuedCompletionStatus(_iocp, &dwBytesTransferred, (PULONG_PTR)&socket,
			(LPOVERLAPPED*)&fpOverlapped, INFINITE);
		
		if (!_running)
			return;

		if (iocpStatus == FALSE)
		{
			int lastErrorCode = GetLastError();
			if (lastErrorCode == ERROR_ABANDONED_WAIT_0)
				return;

			if (fpOverlapped)
			{
				const char* optAction = "Connect";
				if (fpOverlapped->action == IOCPActionType::IOCP_Send)
					optAction = "sending";
				else if (fpOverlapped->action == IOCPActionType::IOCP_Recv)
					optAction = "receiving";

				LOG_ERROR("GetQueuedCompletionStatus() failed and lpOverlapped != NULL. "
					"Socket is %d, operation action is %s, transferred bytes = %d, last error code: %d."
					" Connection will be closed.",
					socket, optAction, dwBytesTransferred, lastErrorCode);

				clearConnection(socket, FPNN_EC_CORE_UNKNOWN_ERROR);
				returnFPOverLapped(fpOverlapped);
			}
			else
			{
				LOG_ERROR("GetQueuedCompletionStatus() failed with lpOverlapped == NULL, last error code: %d", lastErrorCode);
			}

			continue;
		}

		BasicConnection* conn = _connectionMap.signConnection(socket);
		if (!conn)
		{
			//-- socket is closed.
			returnFPOverLapped(fpOverlapped);
			continue;
		}

		if (conn->_connectionInfo->uniqueId() != fpOverlapped->connectionUniqueId)
		{
			LOG_WARN("Closed socket is reused. Socket: %d", socket);
			returnFPOverLapped(fpOverlapped);
			conn->_refCount--;
			continue;
		}

		enum class IOCPActionType action = fpOverlapped->action;
		returnFPOverLapped(fpOverlapped);

		if (action == IOCPActionType::IOCP_Send)
		{
			if (conn->connectionType() == BasicConnection::TCPClientConnectionType)
				TCPClientIOProcessor::processSendEvent((TCPClientConnection*)conn, dwBytesTransferred);
			else
				UDPClientIOProcessor::processSendEvent((UDPClientConnection*)conn, dwBytesTransferred);
		}
		else if (action == IOCPActionType::IOCP_Recv)
		{
			if (conn->connectionType() == BasicConnection::TCPClientConnectionType)
				TCPClientIOProcessor::processReceiveEvent((TCPClientConnection*)conn, dwBytesTransferred);
			else
				UDPClientIOProcessor::processReceiveEvent((UDPClientConnection*)conn, dwBytesTransferred);
		}
		else
		{
			TCPClientIOProcessor::processConnectedEvent((TCPClientConnection*)conn);
		}
	}
}

void ClientEngine::reclaimConnections()
{
	std::set<IReleaseablePtr> deleted;
	{
		std::unique_lock<std::mutex> lck(_mutex);
		for (IReleaseablePtr object: _reclaimedConnections)
		{
			if (object->releaseable())
				deleted.insert(object);
		}
		for (IReleaseablePtr object: deleted)
			_reclaimedConnections.erase(object);
	}
	deleted.clear();
}

void ClientEngine::clearTimeoutQuest()
{
	int64_t current = TimeUtil::curr_msec();
	std::list<std::map<uint32_t, BasicAnswerCallback*> > timeouted;

	_connectionMap.extractTimeoutedCallback(current, timeouted);
	for (auto bacMap: timeouted)
	{
		for (auto bacPair: bacMap)
		{
			if (bacPair.second)
			{
				BasicAnswerCallback* callback = bacPair.second;
				if (callback->syncedCallback())		//-- check first, then fill result.
					callback->fillResult(NULL, FPNN_EC_CORE_TIMEOUT);
				else
				{
					callback->fillResult(NULL, FPNN_EC_CORE_TIMEOUT);

					BasicAnswerCallbackPtr task(callback);
					_callbackPool.wakeUp(task);
				}
			}
		}
	}
}

void ClientEngine::timeoutCheckThread()
{
	while (_running)
	{
		//-- Step 1: UDP period sending check

		int cyc = 100;
		int udpSendingCheckSyc = 5;
		while (_running && cyc--)
		{
			udpSendingCheckSyc -= 1;
			if (udpSendingCheckSyc == 0)
			{
				udpSendingCheckSyc = 5;
				std::unordered_set<UDPClientConnection*> invalidOrExpiredConnections;
				_connectionMap.periodUDPSendingCheck(invalidOrExpiredConnections);

				for (UDPClientConnection* conn: invalidOrExpiredConnections)
					closeUDPConnection(conn);
			}

			Sleep(10);
		}


		//-- Step 2: TCP client keep alive

		std::list<TCPClientConnection*> invalidConnections;
		std::list<TCPClientConnection*> connectExpiredConnections;

		_connectionMap.TCPClientKeepAlive(invalidConnections, connectExpiredConnections);
		for (auto conn: invalidConnections)
		{
			quit(conn);
			clearConnectionQuestCallbacks(conn, FPNN_EC_CORE_INVALID_CONNECTION);

			TCPClientPtr client = conn->client();
			if (client)
			{
				client->willClose(conn, true);
			}
			else
			{
				std::shared_ptr<ClientCloseTask> task(new ClientCloseTask(conn->questProcessor(), conn, true));
				_callbackPool.wakeUp(task);
				reclaim(task);
			}
		}

		for (auto conn: connectExpiredConnections)
		{
			quit(conn);
			clearConnectionQuestCallbacks(conn, FPNN_EC_CORE_INVALID_CONNECTION);

			TCPClientPtr client = conn->client();
			if (client)
			{
				client->connectFailed(conn->_connectionInfo, FPNN_EC_CORE_INVALID_CONNECTION);
				client->willClose(conn, true);
			}
			else
			{
				std::shared_ptr<ClientCloseTask> task(new ClientCloseTask(conn->questProcessor(), conn, true));
				_callbackPool.wakeUp(task);
				reclaim(task);
			}
		}

		//-- Step 3: clean timeouted callbacks

		clearTimeoutQuest();
		reclaimConnections();
	}
}

FPOverLapped* ClientEngine::obtainFPOverLapped()
{
	void * overlapped = _overLappedPools.gain();
	if (overlapped)
		ZeroMemory(overlapped, sizeof(struct FPOverLapped));

	return (struct FPOverLapped*)overlapped;
}

void ClientCloseTask::run()
{
	_executed = true;

	if (_questProcessor)
	try
	{
		if (_connection->connectionType() == BasicConnection::TCPClientConnectionType)
		{
			bool requireCallConnectionCannelledEvent;
			bool callCloseEvent = _connection->getCloseEventCallingPermission(requireCallConnectionCannelledEvent);

			if (callCloseEvent)
			{
				_questProcessor->connectionWillClose(*(_connection->_connectionInfo), _error);
			}
			else if (requireCallConnectionCannelledEvent)
			{
				_questProcessor->connected(*(_connection->_connectionInfo), false);
			}
		}
		else
		{
			_questProcessor->connectionWillClose(*(_connection->_connectionInfo), _error);
		}
	}
	catch (const FpnnError& ex){
		LOG_ERROR("ClientCloseTask::run() error:(%d)%s. %s", ex.code(), ex.what(), _connection->_connectionInfo->str().c_str());
	}
	catch (...)
	{
		LOG_ERROR("Unknown error when calling ClientCloseTask::run() function. %s", _connection->_connectionInfo->str().c_str());
	}
}
