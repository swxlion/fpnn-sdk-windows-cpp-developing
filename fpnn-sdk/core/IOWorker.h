#ifndef FPNN_IO_Worker_H
#define FPNN_IO_Worker_H

#include <time.h>
#include <io.h>
#include "IQuestProcessor.h"
#include "FPOverLapped.h"
#include "embedTypes.h"

namespace fpnn
{
	//===============[ IReleaseable ]=====================//
	class IReleaseable
	{
	public:
		virtual bool releaseable() = 0;
		virtual ~IReleaseable() {}
	};
	typedef std::shared_ptr<IReleaseable> IReleaseablePtr;

	//===============[ Connection Status ]=====================//
	/*
	*	Designing thought:
	*		1. ANY unnecessary CPU instructions will consume CPU resources;
	*		2. Do NOT do ANY unnecessary processing, even if it will relust in incomplete logic or incomprehensible codes.
	*	So,
	*		The logic of this structure is not complete, but it is sufficient for the C++ SDK using, and ensure the processes are correct.
	*/
	struct ConnectionEventStatus
	{
		//-- Status:
		//--	0: undo;
		//--	1: doing;
		//--	2: done.
		
		unsigned int _connectedEventStatus:2;
		unsigned int _closeEventStatus:2;
		unsigned int _connectionDiscarded:1;
		unsigned int _requireCallCloseEvent:1;

		ConnectionEventStatus(): _connectedEventStatus(0), _closeEventStatus(0),
			_connectionDiscarded(0), _requireCallCloseEvent(0) {}

		inline void connectionDiscarded() { _connectionDiscarded = 1; }
		inline bool getConnectedEventCallingPermission(bool& requireCallConnectionCannelledEvent)
		{
			requireCallConnectionCannelledEvent = false;
			if (_connectedEventStatus == 0)
			{
				if (_connectionDiscarded == 1)
					requireCallConnectionCannelledEvent = true;

				_connectedEventStatus = 1;
				return true;
			}

			return false;
		}
		inline void connectedEventCalled(bool& requireCallCloseEvent)
		{
			_connectedEventStatus = 2;
			requireCallCloseEvent = this->_requireCallCloseEvent == 1;
		}
		inline bool getCloseEventCallingPermission(bool& requireCallConnectionCannelledEvent)
		{
			requireCallConnectionCannelledEvent = false;

			if (_closeEventStatus != 0)
				return false;

			if (_connectedEventStatus == 1)
			{
				_requireCallCloseEvent = 1;
				return false;
			}
			if (_connectedEventStatus == 0)
			{
				_closeEventStatus = 1;
				_connectionDiscarded = 1;
				_connectedEventStatus = 1;
				requireCallConnectionCannelledEvent = true;
				return false;
			}
			if (_connectedEventStatus == 2)
			{
				_closeEventStatus = 1;
				return true;
			}
			return true;
		}
	};

	//===============[ Basic Connection ]=====================//
	class ClientEngine;

	class BasicConnection: public IReleaseable
	{
	public:
		enum ConnectionType
		{
			TCPClientConnectionType,
			UDPClientConnectionType
		};

	protected:
		std::mutex _mutex;
		IQuestProcessorPtr _questProcessor;
		ConnectionEventStatus _connectionEventStatus;

		void* _transferredReceivingBuffer;
		std::string* _transferredSendingBuffer;

	public:
		class ClientEngine* _engine;
		ConnectionInfoPtr _connectionInfo;

		int64_t _activeTime;
		std::atomic<int> _refCount;

		std::unordered_map<uint32_t, BasicAnswerCallback*> _callbackMap;

	public:
		BasicConnection(ConnectionInfoPtr connectionInfo): _transferredReceivingBuffer(NULL),
			_transferredSendingBuffer(NULL), _connectionInfo(connectionInfo), _refCount(0)
		{
			_connectionInfo->token = (uint64_t)this;	//-- if use Virtual Derive, must redo this in subclass constructor.
			_activeTime = time(NULL);
		}

		virtual ~BasicConnection()
		{
			closesocket(_connectionInfo->socket);

			Sleep(0);

			if (_transferredReceivingBuffer)
				free(_transferredReceivingBuffer);

			if (_transferredSendingBuffer)
				delete _transferredSendingBuffer;
		}

		virtual enum ConnectionType connectionType() = 0;
		virtual bool releaseable() { return (_refCount == 0); }

		inline SOCKET socket() const { return _connectionInfo->socket; }
		inline IQuestProcessorPtr questProcessor() { return _questProcessor; }
		virtual void embed_configRecvNotifyDelegate(EmbedRecvNotifyDelegate delegate) = 0;

		//--------------- Connection event status ------------------//
		inline void connectionDiscarded()
		{
			std::unique_lock<std::mutex> lck(_mutex);
			_connectionEventStatus.connectionDiscarded();
		}
		inline bool getConnectedEventCallingPermission(bool& requireCallConnectionCannelledEvent)
		{
			std::unique_lock<std::mutex> lck(_mutex);
			return _connectionEventStatus.getConnectedEventCallingPermission(requireCallConnectionCannelledEvent);
		}
		inline void connectedEventCalled(bool& requireCallCloseEvent)
		{
			std::unique_lock<std::mutex> lck(_mutex);
			_connectionEventStatus.connectedEventCalled(requireCallCloseEvent);
		}
		inline bool getCloseEventCallingPermission(bool& requireCallConnectionCannelledEvent)
		{
			std::unique_lock<std::mutex> lck(_mutex);
			return _connectionEventStatus.getCloseEventCallingPermission(requireCallConnectionCannelledEvent);
		}
	};
}

#endif