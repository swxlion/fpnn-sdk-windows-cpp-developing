#ifndef FPNN_Client_Engine_H
#define FPNN_Client_Engine_H

#include <mutex>
#include <atomic>
#include <memory>
#include <thread>
#include <set>
#include "FPLog.h"
#include "IOWorker.h"
#include "MemoryPool.h"
#include "TaskThreadPool.h"
#include "TCPClientIOWorker.h"
#include "IQuestProcessor.h"
#include "ConnectionMap.h"
#include "FPOverLapped.h"
#include "ConcurrentSenderInterface.h"

namespace fpnn
{
	class ClientEngine;
	typedef std::shared_ptr<ClientEngine> ClientEnginePtr;

	class Client;
	typedef std::shared_ptr<Client> ClientPtr;

	struct MemoryPoolInitParams
	{
		int32_t initBlocks;
		int32_t perAppendBlocks;
		int32_t perfectBlocks;
	};
	struct ClientEngineInitParams
	{
		int globalConnectTimeoutSeconds;
		int globalQuestTimeoutSeconds;
		int residentTaskThread;
		int maxTaskThreads;
		int IOCPWorkThreadCount;
		struct MemoryPoolInitParams overLappedPoolInitParams;

		ClientEngineInitParams(): globalConnectTimeoutSeconds(5), globalQuestTimeoutSeconds(5),
			residentTaskThread(4), maxTaskThreads(64), IOCPWorkThreadCount(0)
		{
			overLappedPoolInitParams.initBlocks = 2;
			overLappedPoolInitParams.perAppendBlocks = 2;
			overLappedPoolInitParams.perfectBlocks = 10;
		}
	};

	class ClientEngine: virtual public IConcurrentSender
	{
	private:
		std::mutex _mutex;
		FPLogPtr _logHolder;

		int _connectTimeout;
		int _questTimeout;
		std::atomic<bool> _running;

		HANDLE _iocp;
		bool _requireLaunchCleanupAction;
		std::function<void ()> _cleanupAction;

		int _iocpWorkThreadCount;
		std::vector<std::thread> _iocpWorkThread;

		ConnectionMap _connectionMap;
		TaskThreadPool _callbackPool;
		MemoryPool _overLappedPools;

		std::set<IReleaseablePtr> _reclaimedConnections;

		std::thread _timeoutChecker;

		ClientEngine(const ClientEngineInitParams *params = NULL);

		void prepareIOCP();
		void closeUDPConnection(UDPClientConnection* connection);
		void clearConnection(SOCKET socket, int errorCode);
		void reclaimConnections();
		void clearTimeoutQuest();
		void clean();
		void IOCPWorkThread();
		void timeoutCheckThread();

	public:
		static ClientEnginePtr create(const ClientEngineInitParams* params = NULL);
		static ClientEnginePtr instance() { return create(); }
		virtual ~ClientEngine();

		inline static void setQuestTimeout(int seconds)
		{
			instance()->_questTimeout = seconds * 1000;
		}
		inline static int getQuestTimeout(){
			return instance()->_questTimeout / 1000;
		}
		inline static void setConnectTimeout(int seconds)
		{
			instance()->_connectTimeout = seconds * 1000;
		}
		inline static int getConnectTimeout(){
			return instance()->_connectTimeout / 1000;
		}

		inline static bool runTask(std::shared_ptr<ITaskThreadPool::ITask> task)
		{
			return instance()->_callbackPool.wakeUp(task);
		}

		inline static bool runTask(std::function<void ()> task)
		{
			return instance()->_callbackPool.wakeUp(std::move(task));
		}

		inline bool execute(std::shared_ptr<ITaskThreadPool::ITask> task)
		{
			return _callbackPool.wakeUp(task);
		}

		void clearConnectionQuestCallbacks(BasicConnection*, int errorCode);
		void closeConnection(BasicConnection*, ClientPtr, int errorCode);

		inline BasicConnection* takeConnection(const ConnectionInfo* ci)  //-- !!! Using for other case. e.g. TCPCLient.
		{
			return _connectionMap.takeConnection(ci);
		}
		inline BasicConnection* takeConnection(SOCKET socket)
		{
			return _connectionMap.takeConnection(socket);
		}
		inline BasicAnswerCallback* takeCallback(SOCKET socket, uint32_t seqNum)
		{
			return _connectionMap.takeCallback(socket, seqNum);
		}

		bool join(BasicConnection* connection);
		void quit(const BasicConnection* connection);

		inline void keepAlive(int socket, bool keepAlive)		//-- Only for ARQ UDP
		{
			_connectionMap.keepAlive(socket, keepAlive);
		}
		inline void setUDPUntransmittedSeconds(SOCKET socket, int untransmittedSeconds)		//-- Only for ARQ UDP
		{
			_connectionMap.setUDPUntransmittedSeconds(socket, untransmittedSeconds);
		}
		inline void executeConnectionAction(SOCKET socket, std::function<void (BasicConnection* conn)> action)		//-- Only for ARQ UDP
		{
			_connectionMap.executeConnectionAction(socket, std::move(action));
		}

		virtual void sendTCPData(SOCKET socket, uint64_t token, std::string* data);
		virtual void sendUDPData(SOCKET socket, uint64_t token, std::string* data, int64_t expiredMS, bool discardable);
		
		/**
			All SendQuest():
				If return false, caller must free quest & callback.
				If return true, don't free quest & callback.
		*/
		virtual FPAnswerPtr sendQuest(SOCKET socket, uint64_t token, std::mutex* mutex, FPQuestPtr quest, int timeout = 0)
		{
			if (timeout == 0) timeout = _questTimeout;
			return _connectionMap.sendQuest(socket, token, mutex, quest, timeout, quest->isOneWay());
		}
		virtual bool sendQuest(SOCKET socket, uint64_t token, FPQuestPtr quest, AnswerCallback* callback, int timeout = 0)
		{
			if (timeout == 0) timeout = _questTimeout;
			return _connectionMap.sendQuest(socket, token, quest, callback, timeout, quest->isOneWay());
		}
		virtual bool sendQuest(SOCKET socket, uint64_t token, FPQuestPtr quest, BasicAnswerCallback* callback, int timeout = 0)
		{
			if (timeout == 0) timeout = _questTimeout;
			return _connectionMap.sendQuest(socket, token, quest, callback, timeout, quest->isOneWay());
		}
		virtual bool sendQuest(SOCKET socket, uint64_t token, FPQuestPtr quest, std::function<void (FPAnswerPtr answer, int errorCode)> task, int timeout = 0)
		{
			if (timeout == 0) timeout = _questTimeout;
			return _connectionMap.sendQuest(socket, token, quest, std::move(task), timeout, quest->isOneWay());
		}

		//-- For UDP Client
		virtual FPAnswerPtr sendQuest(SOCKET socket, uint64_t token, std::mutex* mutex, FPQuestPtr quest, int timeout, bool discardableUDPQuest)
		{
			if (timeout == 0) timeout = _questTimeout;
			return _connectionMap.sendQuest(socket, token, mutex, quest, timeout, discardableUDPQuest);
		}
		virtual bool sendQuest(SOCKET socket, uint64_t token, FPQuestPtr quest, AnswerCallback* callback, int timeout, bool discardableUDPQuest)
		{
			if (timeout == 0) timeout = _questTimeout;
			return _connectionMap.sendQuest(socket, token, quest, callback, timeout, discardableUDPQuest);
		}
		virtual bool sendQuest(SOCKET socket, uint64_t token, FPQuestPtr quest, std::function<void (FPAnswerPtr answer, int errorCode)> task, int timeout, bool discardableUDPQuest)
		{
			if (timeout == 0) timeout = _questTimeout;
			return _connectionMap.sendQuest(socket, token, quest, std::move(task), timeout, discardableUDPQuest);
		}

		inline void reclaim(IReleaseablePtr object)
		{
			std::unique_lock<std::mutex> lck(_mutex);
			_reclaimedConnections.insert(object);
		}

		//-- Windows ONLY
		FPOverLapped* obtainFPOverLapped();
		inline void returnFPOverLapped(FPOverLapped*& overlapped) { _overLappedPools.recycle((void**)(&overlapped)); }
		inline void setDestructorCleanupAction(std::function<void ()> cleanupAction)
		{
			_cleanupAction = std::move(cleanupAction);
		}
		inline void enableDestructorCleanupAction(bool enable) { _requireLaunchCleanupAction = enable; }

	};

	class ClientCloseTask: virtual public ITaskThreadPool::ITask, virtual public IReleaseable
	{
		bool _error;
		bool _executed;
		BasicConnection* _connection;
		IQuestProcessorPtr _questProcessor;

	public:
		ClientCloseTask(IQuestProcessorPtr questProcessor, BasicConnection* connection, bool error):
			_error(error), _executed(false), _connection(connection), _questProcessor(questProcessor)
			{
				connection->connectionDiscarded();
			}

		virtual ~ClientCloseTask()
		{
			if (!_executed)
				run();

			delete _connection;
		}

		virtual bool releaseable() { return (_connection->_refCount == 0); }
		virtual void run();
	};
}

#endif
