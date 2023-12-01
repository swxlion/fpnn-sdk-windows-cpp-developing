#ifndef FPNN_Partitioned_Connection_Map_H
#define FPNN_Partitioned_Connection_Map_H

#include <map>
#include <set>
#include <list>
#include <mutex>
#include <vector>
#include <unordered_map>
#include "FPMessage.h"
#include "FPWriter.h"
#include "AnswerCallbacks.h"
#include "TCPClientIOWorker.h"
#include "UDPClientIOWorker.h"

namespace fpnn
{
	class ConnectionMap
	{
		struct TCPClientKeepAliveTimeoutInfo
		{
			TCPClientConnection* conn;
			int timeout;					//-- In milliseconds
		};

		std::mutex _mutex;
		std::unordered_map<SOCKET, BasicConnection*> _connections;

		inline bool sendTCPData(TCPClientConnection* conn, std::string* data)
		{
			/*
			*	Ignoring the returned value will lead to recycle connection not in time when error occurred.
			*	But process this returned value is very complex in logic flows.
			*	If we ignored this returned value, the bad connection will be recycled when the following cases:
			*		1. the next sending: include IOCP triggered or initiative triggered.
			*		2. sever closed connection (e.g. idle), or WSARecv() triggered.
			*		3. keep alive failed (if keep alive enabled).
			*		4. client closed.
			*/
			bool status = conn->send(data);
			if (status == false)
				LOG_WARN("This warning can be ignored safely. "
					"But if this warning triggered frequently, please tell swxlion to optmize the logic flow.");

			return true;
		}

		inline bool sendUDPData(UDPClientConnection* conn, std::string* data, int64_t expiredMS, bool discardable)
		{
			conn->sendData(data, expiredMS, discardable);
			return true;
		}

		inline bool sendQuest(BasicConnection* conn, std::string* data, uint32_t seqNum, BasicAnswerCallback* callback, int timeout, bool discardableUDPQuest)
		{
			if (callback)
				conn->_callbackMap[seqNum] = callback;

			bool status;
			if (conn->connectionType() == BasicConnection::TCPClientConnectionType)
				status = sendTCPData((TCPClientConnection*)conn, data);
			else
				status = sendUDPData((UDPClientConnection*)conn, data, slack_real_msec() + timeout, discardableUDPQuest);

			if (!status && callback)
				conn->_callbackMap.erase(seqNum);
			
			return status;
		}

		void sendTCPClientKeepAlivePingQuest(TCPClientSharedKeepAlivePingDatas& sharedPing, std::list<TCPClientKeepAliveTimeoutInfo>& keepAliveList)
		{
			std::unique_lock<std::mutex> lck(_mutex);

			for (auto& node: keepAliveList)
			{
				std::string* raw = new std::string(*(sharedPing.rawData));
				KeepAliveCallback* callback = new KeepAliveCallback(node.conn->_connectionInfo);

				callback->updateExpiredTime(slack_real_msec() + node.timeout);
				sendQuest(node.conn, raw, sharedPing.seqNum, callback, node.timeout, false);
				
				node.conn->updateKeepAliveMS();
				node.conn->_refCount--;
			}
		}

		bool sendQuestWithBasicAnswerCallback(SOCKET socket, uint64_t token, FPQuestPtr quest, BasicAnswerCallback* callback, int timeout, bool discardableUDPQuest);

	public:
		BasicConnection* takeConnection(SOCKET socket)
		{
			std::unique_lock<std::mutex> lck(_mutex);
			auto it = _connections.find(socket);
			if (it != _connections.end())
			{
				BasicConnection* conn = it->second;
				_connections.erase(it);
				return conn;
			}
			return NULL;
		}

		BasicConnection* takeConnection(const ConnectionInfo* ci)
		{
			std::unique_lock<std::mutex> lck(_mutex);
			auto it = _connections.find(ci->socket);
			if (it != _connections.end())
			{
				BasicConnection* conn = it->second;
				if ((uint64_t)conn == ci->token)
				{
					_connections.erase(it);
					return conn;
				}
			}
			return NULL;
		}

		bool insert(SOCKET socket, BasicConnection* connection)
		{
			std::unique_lock<std::mutex> lck(_mutex);
			auto it = _connections.find(socket);
			if (it == _connections.end())
			{
				_connections[socket] = connection;
				return true;
			}
			return false;
		}

		void remove(SOCKET socket)
		{
			std::unique_lock<std::mutex> lck(_mutex);
			_connections.erase(socket);
		}

		BasicConnection* signConnection(SOCKET socket)
		{
			BasicConnection* connection = NULL;
			std::unique_lock<std::mutex> lck(_mutex);
			auto it = _connections.find(socket);
			if (it != _connections.end())
			{
				connection = it->second;
				connection->_refCount++;
			}
			return connection;
		}

		void waitForEmpty()
		{
			while (_connections.size() > 0)
				Sleep(20);
		}

		void getAllSocket(std::set<SOCKET>& fdSet)
		{
			std::unique_lock<std::mutex> lck(_mutex);
			for (auto& cmp: _connections)
				fdSet.insert(cmp.first);
		}

		bool sendTCPData(SOCKET socket, uint64_t token, std::string* data)
		{
			std::unique_lock<std::mutex> lck(_mutex);
			auto it = _connections.find(socket);
			if (it != _connections.end())
			{
				TCPClientConnection* connection = (TCPClientConnection*)(it->second);
				if (token == (uint64_t)connection)
					return sendTCPData(connection, data);
			}
			return false;
		}

		bool sendUDPData(SOCKET socket, uint64_t token, std::string* data, int64_t expiredMS, bool discardable)
		{
			std::unique_lock<std::mutex> lck(_mutex);
			auto it = _connections.find(socket);
			if (it != _connections.end())
			{
				UDPClientConnection* connection = (UDPClientConnection*)(it->second);
				if (token == (uint64_t)connection)
					return sendUDPData(connection, data, expiredMS, discardable);
			}
			return false;
		}

	protected:
		bool sendQuest(SOCKET socket, uint64_t token, std::string* data, uint32_t seqNum, BasicAnswerCallback* callback, int timeout, bool discardableUDPQuest)
		{
			std::unique_lock<std::mutex> lck(_mutex);
			auto it = _connections.find(socket);
			if (it != _connections.end())
			{
				BasicConnection* connection = it->second;
				if (token == (uint64_t)connection)
					return sendQuest(connection, data, seqNum, callback, timeout, discardableUDPQuest);
			}
			return false;
		}

	public:
		void keepAlive(SOCKET socket, bool keepAlive)
		{
			std::unique_lock<std::mutex> lck(_mutex);
			auto it = _connections.find(socket);
			if (it != _connections.end())
			{
				BasicConnection* connection = it->second;
				if (keepAlive && connection->connectionType() == BasicConnection::UDPClientConnectionType)
				{
					((UDPClientConnection*)connection)->enableKeepAlive();
				}
			}
		}

		void setUDPUntransmittedSeconds(SOCKET socket, int untransmittedSeconds)
		{
			std::unique_lock<std::mutex> lck(_mutex);
			auto it = _connections.find(socket);
			if (it != _connections.end())
			{
				BasicConnection* connection = it->second;
				if (connection->connectionType() == BasicConnection::UDPClientConnectionType)
				{
					((UDPClientConnection*)connection)->setUntransmittedSeconds(untransmittedSeconds);
				}
			}
		}

		void executeConnectionAction(SOCKET socket, std::function<void (BasicConnection* conn)> action)
		{
			std::unique_lock<std::mutex> lck(_mutex);
			auto it = _connections.find(socket);
			if (it != _connections.end())
			{
				BasicConnection* connection = it->second;
				action(connection);
			}
		}

		void periodUDPSendingCheck(std::unordered_set<UDPClientConnection*>& invalidOrExpiredConnections);

	public:
		void TCPClientKeepAlive(std::list<TCPClientConnection*>& invalidConnections, std::list<TCPClientConnection*>& connectExpiredConnections);

	public:
		BasicAnswerCallback* takeCallback(SOCKET socket, uint32_t seqNum);
		void extractTimeoutedCallback(int64_t threshold, std::list<std::map<uint32_t, BasicAnswerCallback*> >& timeouted);
		void extractTimeoutedConnections(int64_t threshold, std::list<BasicConnection*>& timeouted);

		/**
			All SendQuest():
				If return false, caller must free quest & callback.
				If return true, don't free quest & callback.
		*/
		FPAnswerPtr sendQuest(SOCKET socket, uint64_t token, std::mutex* mutex, FPQuestPtr quest, int timeout, bool discardableUDPQuest = false);

		inline bool sendQuest(SOCKET socket, uint64_t token, FPQuestPtr quest, AnswerCallback* callback, int timeout, bool discardableUDPQuest = false)
		{
			return sendQuestWithBasicAnswerCallback(socket, token, quest, callback, timeout, discardableUDPQuest);
		}
		inline bool sendQuest(SOCKET socket, uint64_t token, FPQuestPtr quest, std::function<void (FPAnswerPtr answer, int errorCode)> task, int timeout, bool discardableUDPQuest = false)
		{
			BasicAnswerCallback* t = new FunctionAnswerCallback(std::move(task));
			if (sendQuestWithBasicAnswerCallback(socket, token, quest, t, timeout, discardableUDPQuest))
				return true;
			else
			{
				delete t;
				return false;
			}
		}

		/*===============================================================================
		  Call by framwwork.
		=============================================================================== */
		inline bool sendQuest(SOCKET socket, uint64_t token, FPQuestPtr quest, BasicAnswerCallback* callback, int timeout, bool discardableUDPQuest = false)
		{
			return sendQuestWithBasicAnswerCallback(socket, token, quest, callback, timeout, discardableUDPQuest);
		}
	};
}

#endif

