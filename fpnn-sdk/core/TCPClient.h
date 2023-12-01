#ifndef FPNN_TCP_Client_H
#define FPNN_TCP_Client_H

#include "StringUtil.h"
#include "NetworkUtility.h"
#include "TCPClientIOWorker.h"
#include "KeyExchange.h"
#include "ClientInterface.h"

//-- Important info: https://blog.csdn.net/bingqingsuimeng/article/details/73970028

namespace fpnn
{
	class TCPClient;
	typedef std::shared_ptr<TCPClient> TCPClientPtr;

	//=================================================================//
	//- TCP Client:
	//=================================================================//
	class TCPClient: public Client, public std::enable_shared_from_this<TCPClient>
	{
	private:
		int _AESKeyLen;
		bool _packageEncryptionMode;
		std::string _eccCurve;
		std::string _serverPublicKey;
		std::string _encryptionKeyId;
		//----------
		TCPClientKeepAliveParams* _keepAliveParams;
		//----------
		int _connectTimeout;
		std::list<AsyncQuestCacheUnit*> _questCache;

	private:
		TCPClientConnection* buildConnection(SOCKET socket, ConnectionInfoPtr currConnInfo, ConnectionInfoPtr& newConnInfo);
		bool configEncryptedConnection(TCPClientConnection* connection, std::string& publicKey);
		void sendEncryptHandshake(TCPClientConnection* connection, const std::string& publicKey);
		struct sockaddr* perpareIPv4Address(ConnectionInfoPtr currConnInfo, SOCKET& socket, int& addressLen);
		struct sockaddr* perpareIPv6Address(ConnectionInfoPtr currConnInfo, SOCKET& socket, int& addressLen);
		bool perpareConnection(SOCKET socket, struct sockaddr *address, int addressLen, ConnectionInfoPtr currConnInfo);
		void cacheSendQuest(FPQuestPtr quest, BasicAnswerCallback* callback, int timeout);
		void dumpCachedSendData(ConnectionInfoPtr connInfo);
		void triggerConnectingFailedEvent(ConnectionInfoPtr connInfo, int errorCode);

		TCPClient(const std::string& host, int port, bool autoReconnect = true);

	public:
		virtual ~TCPClient()
		{
			if (_keepAliveParams)
				delete _keepAliveParams;
		}

		/*===============================================================================
		  Call by framwwork.
		=============================================================================== */
		void dealQuest(FPQuestPtr quest, ConnectionInfoPtr connectionInfo);
		void socketConnected(TCPClientConnection* conn, bool connected);
		void connectFailed(ConnectionInfoPtr connInfo, int errorCode);
		bool connectSuccessed(TCPClientConnection* conn);

		/*===============================================================================
		  Call by Developer. Configure Function.
		=============================================================================== */
		inline void enableEncryptor(const std::string& curve, const std::string& peerPublicKey, bool packageMode = true, bool reinforce = false, const std::string& keyId = std::string())
		{
			_eccCurve = curve;
			_serverPublicKey = peerPublicKey;
			_packageEncryptionMode = packageMode;
			_AESKeyLen = reinforce ? 32 : 16;
			_encryptionKeyId = keyId;
		}

		bool enableEncryptorByDerData(const std::string &derData, bool packageMode = true, bool reinforce = false, const std::string& keyId = std::string());
		bool enableEncryptorByPemData(const std::string &PemData, bool packageMode = true, bool reinforce = false, const std::string& keyId = std::string());
		bool enableEncryptorByDerFile(const char *derFilePath, bool packageMode = true, bool reinforce = false, const std::string& keyId = std::string());
		bool enableEncryptorByPemFile(const char *pemFilePath, bool packageMode = true, bool reinforce = false, const std::string& keyId = std::string());

		virtual void keepAlive();
		void setKeepAlivePingTimeout(int seconds);
		void setKeepAliveInterval(int seconds);
		void setKeepAliveMaxPingRetryCount(int count);

		inline void setConnectTimeout(int seconds)
		{
			_connectTimeout = seconds * 1000;
		}
		inline int getConnectTimeout()
		{
			return _connectTimeout / 1000;
		}
		/*===============================================================================
		  Call by Developer.
		=============================================================================== */
		virtual bool connect();
		virtual bool asyncConnect();
		virtual void close();

		/**
			All SendQuest():
				If return false, caller must free quest & callback.
				If return true, don't free quest & callback.

			timeout in seconds.
		*/
		virtual FPAnswerPtr sendQuest(FPQuestPtr quest, int timeout = 0);
		virtual bool sendQuest(FPQuestPtr quest, AnswerCallback* callback, int timeout = 0);
		virtual bool sendQuest(FPQuestPtr quest, std::function<void (FPAnswerPtr answer, int errorCode)> task, int timeout = 0);

		inline static TCPClientPtr createClient(const std::string& host, int port, bool autoReconnect = true)
		{
			return TCPClientPtr(new TCPClient(host, port, autoReconnect));
		}
		inline static TCPClientPtr createClient(const std::string& endpoint, bool autoReconnect = true)
		{
			std::string host;
			int port;

			if (!parseAddress(endpoint, host, port))
				return nullptr;

			return TCPClientPtr(new TCPClient(host, port, autoReconnect));
		}

		/*===============================================================================
		  Interfaces for embed mode.
		=============================================================================== */
		/*
		* If 'true' is returned, 'rawData' will be token over by C++ SDK, caller/developer DO NOT to release (delete) it.
		* If 'false' is returned, caller/developer will delete it when necessary.
		*/
		virtual bool embed_sendData(std::string* rawData);
	};
}

#endif
