#ifndef FPNN_IO_Buffer_H
#define FPNN_IO_Buffer_H

#include <atomic>
#include <string>
#include <queue>
#include <mutex>
#include <memory>
#include <winsock2.h>
#include "FPMessage.h"
#include "Encryptor.h"

namespace fpnn
{
	class SendBuffer
	{
		typedef void (SendBuffer::* CurrBufferProcessFunc)();

	private:
		std::mutex* _mutex;		//-- only using for sendBuffer and sendToken
		bool _sendToken;

		size_t _offset;
		std::string* _currBuffer;
		std::queue<std::string*> _outQueue;
		uint64_t _sentBytes;		//-- Total Bytes
		uint64_t _sentPackage;
		bool _encryptAfterFirstPackage;
		Encryptor* _encryptor;

		CurrBufferProcessFunc _currBufferProcess;

		WSABUF _wsaBuffer;

		void encryptData();
		bool realSend(SOCKET socket, LPWSAOVERLAPPED overLapped, DWORD lastSentBytes, bool& realSent, bool& isClosed);

	public:
		SendBuffer(std::mutex* mutex): _mutex(mutex), _sendToken(true), _offset(0), _currBuffer(0),
			_sentBytes(0), _sentPackage(0), _encryptAfterFirstPackage(false), _encryptor(NULL), _currBufferProcess(NULL) {}
		~SendBuffer()
		{
			while (_outQueue.size())
			{
				std::string* data = _outQueue.front();
				_outQueue.pop();
				delete data;
			}

			if (_currBuffer)
				delete _currBuffer;

			if (_encryptor)
				delete _encryptor;
		}

		inline void setMutex(std::mutex* mutex)
		{
			_mutex = mutex;
		}

		inline std::string* transferSendBuffer()
		{
			std::string* v = _currBuffer;
			_currBuffer = NULL;
			return v;
		}

		bool send(SOCKET socket, LPWSAOVERLAPPED overLapped, DWORD lastSentBytes, bool IOCPCompletedEvent, bool& realSent, bool& isClosed, std::string* data = NULL);
		bool entryEncryptMode(uint8_t *key, size_t key_len, uint8_t *iv, bool streamMode);
		void encryptAfterFirstPackage() { _encryptAfterFirstPackage = true; }
		void appendData(std::string* data);

		//-- ONLY for connection connecting completed.
		inline void disableSending()
		{
			std::unique_lock<std::mutex> lck(*_mutex);
			_sendToken = false;
		}
		inline void allowSending()
		{
			std::unique_lock<std::mutex> lck(*_mutex);
			_sendToken = true;
		}
	};
}

#endif
