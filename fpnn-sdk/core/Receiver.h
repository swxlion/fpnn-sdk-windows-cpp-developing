#ifndef FPNN_Receiver_h
#define FPNN_Receiver_h

#include <ws2def.h>
#include "Encryptor.h"
#include "FPMessage.h"
#include "embedTypes.h"

namespace fpnn
{
	//================================//
	//--     Receiver Interface     --//
	//================================//
	class Receiver
	{
	protected:
		int _curr;
		int _total;

		WSABUF _wsaBuffer;

	public:
		Receiver(): _curr(0), _total(FPMessage::_HeaderLength) {}
		virtual ~Receiver() {}

		virtual LPWSABUF beginRecv() = 0;
		virtual void* transferRecvBuffer() = 0;
		virtual bool receivedData(DWORD receivedBytes, FPQuestPtr& quest, FPAnswerPtr& answer) = 0;
		virtual bool embed_receivedData(uint64_t connectionId, DWORD receivedBytes, EmbedRecvNotifyDelegate delegate) = 0;
	};

	//================================//
	//--    UnencryptedReceiver     --//
	//================================//
	class UnencryptedReceiver: public Receiver
	{
		uint8_t* _header;
		uint8_t* _bodyBuffer;

		/**
			Only called when protocol is TCP, and header has be read.
			if return -1, mean unsupported protocol.
		*/
		int remainDataLen();
		
	public:
		UnencryptedReceiver(): Receiver(), _bodyBuffer(NULL)
		{
			_header = (uint8_t*)malloc(FPMessage::_HeaderLength);
		}
		virtual ~UnencryptedReceiver()
		{
			if (_bodyBuffer)
				free(_bodyBuffer);

			if (_header)
				free(_header);
		}

		virtual LPWSABUF beginRecv();
		virtual void* transferRecvBuffer();
		virtual bool receivedData(DWORD receivedBytes, FPQuestPtr& quest, FPAnswerPtr& answer);
		virtual bool embed_receivedData(uint64_t connectionId, DWORD receivedBytes, EmbedRecvNotifyDelegate delegate);
	};

	//================================//
	//--  EncryptedStreamReceiver   --//
	//================================//
	class EncryptedStreamReceiver: public Receiver
	{
		StreamEncryptor _encryptor;
		uint8_t* _header;
		uint8_t* _decHeader;
		uint8_t* _bodyBuffer;

		/**
			Only called when protocol is TCP, and header has be read.
			if return -1, mean unsupported protocol.
		*/
		int remainDataLen();
		
	public:
		EncryptedStreamReceiver(uint8_t *key, size_t key_len, uint8_t *iv): Receiver(), _encryptor(key, key_len, iv), _bodyBuffer(NULL)
		{
			_header = (uint8_t*)malloc(FPMessage::_HeaderLength);
			_decHeader = (uint8_t*)malloc(FPMessage::_HeaderLength);
		}
		virtual ~EncryptedStreamReceiver()
		{
			if (_bodyBuffer)
				free(_bodyBuffer);

			if (_header)
				free(_header);

			free(_decHeader);
		}

		virtual LPWSABUF beginRecv();
		virtual void* transferRecvBuffer();
		virtual bool receivedData(DWORD receivedBytes, FPQuestPtr& quest, FPAnswerPtr& answer);
		virtual bool embed_receivedData(uint64_t connectionId, DWORD receivedBytes, EmbedRecvNotifyDelegate delegate);
	};

	//================================//
	//--  EncryptedPackageReceiver  --//
	//================================//
	class EncryptedPackageReceiver: public Receiver
	{
		PackageEncryptor _encryptor;
		uint32_t* _packageLen;
		uint8_t* _dataBuffer;
		bool _getLength;

	public:
		EncryptedPackageReceiver(uint8_t *key, size_t key_len, uint8_t *iv):
			Receiver(), _encryptor(key, key_len, iv), _dataBuffer(NULL), _getLength(false)
		{
			_packageLen = (uint32_t*)malloc(sizeof(uint32_t));
			_total = sizeof(uint32_t);
		}
		virtual ~EncryptedPackageReceiver()
		{
			if (_packageLen)
				free(_packageLen);
			
			if (_dataBuffer)
				free(_dataBuffer);
		}

		virtual LPWSABUF beginRecv();
		virtual void* transferRecvBuffer();
		virtual bool receivedData(DWORD receivedBytes, FPQuestPtr& quest, FPAnswerPtr& answer);
		virtual bool embed_receivedData(uint64_t connectionId, DWORD receivedBytes, EmbedRecvNotifyDelegate delegate);
	};
}

#endif
