#ifndef UDP_Connection_h
#define UDP_Connection_h

#include "IOWorker.h"
#include "UDPIOBuffer.h"

namespace fpnn
{
	class UDPClient;
	typedef std::shared_ptr<UDPClient> UDPClientPtr;

	//===============[ UDPClientConnection ]=====================//
	class UDPClientConnection: public BasicConnection
	{
		WSABUF _wsaRecvBuffer;
		UDPIOBuffer _ioBuffer;
		std::weak_ptr<UDPClient> _client;

	public:
		UDPClientConnection(UDPClientPtr client, ConnectionInfoPtr connectionInfo, IQuestProcessorPtr questProcessor, int MTU):
			BasicConnection(connectionInfo), _ioBuffer(NULL, connectionInfo->socket, MTU), _client(client)
		{
			_questProcessor = questProcessor;
			_connectionInfo->token = (uint64_t)this;	//-- if use Virtual Derive, must redo this in subclass constructor.
			_connectionInfo->_mutex = &_mutex;
			_ioBuffer.initMutex(&_mutex);
			_ioBuffer.updateEndpointInfo(_connectionInfo->endpoint());

			_wsaRecvBuffer.len = FPNN_UDP_MAX_DATA_LENGTH;
			if ((int)(_wsaRecvBuffer.len) > Config::_max_recv_package_length)
				_wsaRecvBuffer.len = Config::_max_recv_package_length;

			_wsaRecvBuffer.buf = (CHAR*)malloc(_wsaRecvBuffer.len);
		}

		virtual ~UDPClientConnection()
		{
			_transferredReceivingBuffer = _wsaRecvBuffer.buf;
		}

		virtual enum ConnectionType connectionType() { return BasicConnection::UDPClientConnectionType; }
		UDPClientPtr client() { return _client.lock(); }

		inline void enableKeepAlive() { _ioBuffer.enableKeepAlive(); }
		inline bool isRequireClose() { return (_ioBuffer.isRequireClose() ? true : _ioBuffer.isTransmissionStopped()); }
		inline void setUntransmittedSeconds(int untransmittedSeconds) { _ioBuffer.setUntransmittedSeconds(untransmittedSeconds); }
		void sendCachedData();
		void sendData(std::string* data, int64_t expiredMS, bool discardable);
		void sendCloseSignal();

		bool beginReceiving(bool& isClosed);
		bool receivedData(DWORD receivedBytes, std::list<FPQuestPtr>& questList, std::list<FPAnswerPtr>& answerList);
		bool IOCPSendCompleted(DWORD lastSentBytes);

		virtual void embed_configRecvNotifyDelegate(EmbedRecvNotifyDelegate delegate)
		{
			_ioBuffer.configEmbedInfos(_connectionInfo->uniqueId(), delegate);
		}
	};

	//===============[ UDPClientIOProcessor ]=====================//
	class UDPClientIOProcessor
	{
		static bool deliverAnswer(UDPClientConnection * connection, FPAnswerPtr answer);
		static bool deliverQuest(UDPClientConnection * connection, FPQuestPtr quest);
		static void closeConnection(UDPClientConnection * connection, bool normalClosed);

	public:
		static void processReceiveEvent(UDPClientConnection * connection, DWORD dwBytesTransferred);
		static void processSendEvent(UDPClientConnection * connection, DWORD dwBytesTransferred);
	};
}

#endif