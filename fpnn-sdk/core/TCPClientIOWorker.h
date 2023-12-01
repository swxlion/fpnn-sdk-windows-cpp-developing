#ifndef FPNN_TCP_Client_IO_Worker_H
#define FPNN_TCP_Client_IO_Worker_H

#include "IOWorker.h"
#include "IOBuffer.h"
#include "Receiver.h"

namespace fpnn
{
	class TCPClient;
	typedef std::shared_ptr<TCPClient> TCPClientPtr;

	class TCPClientConnection;
	typedef std::shared_ptr<TCPClientConnection> TCPClientConnectionPtr;

	//=================================================================//
	//- TCP Client Keep Alive Structures
	//=================================================================//
	struct TCPClientKeepAliveParams
	{
		int pingTimeout;			//-- In milliseconds
		int pingInterval;			//-- In milliseconds
		int maxPingRetryCount;

		virtual void config(const TCPClientKeepAliveParams* params)
		{
			pingTimeout = params->pingTimeout;
			pingInterval = params->pingInterval;
			maxPingRetryCount = params->maxPingRetryCount;
		}
		virtual ~TCPClientKeepAliveParams() {}
	};

	struct TCPClientKeepAliveInfos: public TCPClientKeepAliveParams
	{
	private:
		int unreceivedThreshold;
		int64_t lastReceivedMS;
		int64_t lastPingSentMS;

	public:
		TCPClientKeepAliveInfos(): lastPingSentMS(0)
		{
			lastReceivedMS = slack_real_msec();
		}
		virtual ~TCPClientKeepAliveInfos() {}

		virtual void config(const TCPClientKeepAliveParams* params)
		{
			TCPClientKeepAliveParams::config(params);
			unreceivedThreshold = pingTimeout * maxPingRetryCount + pingInterval;
		}

		inline void updateReceivedMS() { lastReceivedMS = slack_real_msec(); }
		inline void updatePingSentMS() { lastPingSentMS = slack_real_msec(); }
		inline int isRequireSendPing()	//-- If needed, return timeout; else, return 0.
		{
			int64_t now = slack_real_msec();
			if ((now >= lastReceivedMS + pingInterval) && (now >= lastPingSentMS + pingTimeout))
				return pingTimeout;
			else
				return 0;
		}

		inline bool isLost()
		{
			return (slack_real_msec() > (lastReceivedMS + unreceivedThreshold));
		}
	};

	class KeepAliveCallback: public AnswerCallback
	{
		ConnectionInfoPtr _connectionInfo;

	public:
		KeepAliveCallback(ConnectionInfoPtr ci): _connectionInfo(ci) {}

		virtual void onAnswer(FPAnswerPtr) {}
		virtual void onException(FPAnswerPtr answer, int errorCode)
		{
			LOG_ERROR("Keep alive ping for %s failed. Code: %d, infos: %s",
				_connectionInfo->str().c_str(), errorCode, answer ? answer->json().c_str() : "<N/A>");
		}
	};

	struct TCPClientSharedKeepAlivePingDatas
	{
		FPQuestPtr quest;
		std::string* rawData;
		uint32_t seqNum;

		TCPClientSharedKeepAlivePingDatas(): rawData(0) {}
		~TCPClientSharedKeepAlivePingDatas() { if (rawData) delete rawData; }

		void build()
		{
			if (!quest)
			{
				quest = FPQWriter::emptyQuest("*ping");
				rawData = quest->raw();
				seqNum = quest->seqNumLE();
			}
		}
	};

	//===============[ TCPClientConnection ]=====================//
	class TCPClientConnection: public BasicConnection
	{
	private:
		std::weak_ptr<TCPClient> _client;
		TCPClientKeepAliveInfos* _keepAliveInfos;

	public:
		Receiver* _receiver;
		SendBuffer _sendBuffer;
		EmbedRecvNotifyDelegate _embedRecvNotifyDeleagte;

		bool _socketConnected;
		int64_t _connectingExpiredMS;

	public:
		TCPClientPtr client() { return _client.lock(); }
		inline bool entryEncryptMode(uint8_t *key, size_t key_len, uint8_t *iv, bool streamMode)
		{
			delete _receiver;
			if (streamMode)
				_receiver = new EncryptedStreamReceiver(key, key_len, iv);
			else
				_receiver = new EncryptedPackageReceiver(key, key_len, iv);

			if (_sendBuffer.entryEncryptMode(key, key_len, iv, streamMode) == false)
			{
				LOG_ERROR("Entry encrypt mode failed. Connection has bytes sending. Connection will be closed by server. %s", _connectionInfo->str().c_str());
				return false;
			}
			_connectionInfo->_encrypted = true;
			return true;
		}
		inline void encryptAfterFirstPackageSent() { _sendBuffer.encryptAfterFirstPackage(); }
		inline bool isEncrypted() { return _connectionInfo->_encrypted; }

		bool beginReceiving(bool& isClosed);
		bool receivedData(DWORD receivedBytes, FPQuestPtr& quest, FPAnswerPtr& answer);
		bool beginSend(DWORD lastSentBytes, bool IOCPCompletedEvent, bool& isClosed, std::string* data = NULL);
		inline bool send(std::string* data)
		{
			bool isClosed;
			return beginSend(0, false, isClosed, data);
		}
		
		TCPClientConnection(TCPClientPtr client, ConnectionInfoPtr connectionInfo, IQuestProcessorPtr questProcessor):
			BasicConnection(connectionInfo), _client(client), _keepAliveInfos(NULL),
			_sendBuffer(NULL), _embedRecvNotifyDeleagte(NULL), _socketConnected(false)
		{
			_questProcessor = questProcessor;
			_receiver = new UnencryptedReceiver();

			_connectionInfo->token = (uint64_t)this;
			resetMutex(&_mutex);	//-- if use Virtual Derive, must redo this in subclass constructor.
			_activeTime = time(NULL);
		}

		void resetMutex(std::mutex* mutex)
		{
			_connectionInfo->_mutex = mutex;
			_sendBuffer.setMutex(mutex);
		}

		virtual ~TCPClientConnection()
		{
			if (_keepAliveInfos)
				delete _keepAliveInfos;

			_transferredReceivingBuffer = _receiver->transferRecvBuffer();
			delete _receiver;

			_transferredSendingBuffer = _sendBuffer.transferSendBuffer();
		}
		virtual enum ConnectionType connectionType() { return TCPClientConnectionType; }

		virtual void embed_configRecvNotifyDelegate(EmbedRecvNotifyDelegate delegate)
		{
			_embedRecvNotifyDeleagte = delegate;
		}

		void configKeepAlive(const TCPClientKeepAliveParams* params)
		{
			if (_keepAliveInfos == NULL)
				_keepAliveInfos = new TCPClientKeepAliveInfos;
			
			_keepAliveInfos->config(params);
		}

		int isRequireKeepAlive(bool& isLost)
		{
			if (!_keepAliveInfos)
			{
				isLost = false;
			}
			else
			{
				isLost = _keepAliveInfos->isLost();
				if (!isLost)
					return _keepAliveInfos->isRequireSendPing();
			}

			return 0;
		}

		inline void updateKeepAliveMS()
		{
			_keepAliveInfos->updatePingSentMS();
		}

		inline void updateReceivedMS()
		{
			if (_keepAliveInfos)
				_keepAliveInfos->updateReceivedMS();
		}

		inline void disableIOOperations()
		{
			_sendBuffer.disableSending();
		}
		bool connectedEventCompleted();
	};

	//===============[ TCPClientIOProcessor ]=====================//
	class TCPClientIOProcessor
	{
		static bool deliverAnswer(TCPClientConnection * connection, FPAnswerPtr answer);
		static bool deliverQuest(TCPClientConnection * connection, FPQuestPtr quest);
		static void closeConnection(TCPClientConnection * connection, bool normalClosed);

	public:
		static void processConnectedEvent(TCPClientConnection * connection);
		static void processReceiveEvent(TCPClientConnection * connection, DWORD dwBytesTransferred);
		static void processSendEvent(TCPClientConnection * connection, DWORD dwBytesTransferred);
	};
}

#endif
