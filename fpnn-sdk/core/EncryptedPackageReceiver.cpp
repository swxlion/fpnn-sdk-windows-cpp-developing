#include <io.h>
#include "Endian.h"
#include "FPLog.h"
#include "Config.h"
#include "Decoder.h"
#include "Receiver.h"

using namespace fpnn;

LPWSABUF EncryptedPackageReceiver::beginRecv()
{
	if (_getLength)
		_wsaBuffer.buf = (char*)(_dataBuffer + _curr);
	else
		_wsaBuffer.buf = (char*)(_packageLen + _curr);

	_wsaBuffer.len = _total - _curr;

	return &_wsaBuffer;
}
void* EncryptedPackageReceiver::transferRecvBuffer()
{
	void* buf;
	if (_getLength)
	{
		buf = _dataBuffer;
		_dataBuffer = NULL;
	}
	else
	{
		buf = _packageLen;
		_packageLen = NULL;
	}
	return buf;
}
bool EncryptedPackageReceiver::receivedData(DWORD receivedBytes, FPQuestPtr& quest, FPAnswerPtr& answer)
{
	quest = nullptr;
	answer = nullptr;
	_curr += (int)receivedBytes;
	if (_curr == _total)
	{
		if (_getLength == false)
		{
			_total = (int)le32toh(*_packageLen);
			if (_total > Config::_max_recv_package_length)
			{
				LOG_ERROR("Recv huge TCP data. Connection will be closed by framework.");
				return false;
			}

			_curr = 0;
			_getLength = true;

			if (_dataBuffer)
				free(_dataBuffer);

			_dataBuffer = (uint8_t*)malloc(_total);
		}
		else
		{
			char* buf = (char*)malloc(_total);
			_encryptor.decrypt((uint8_t *)buf, _dataBuffer, _total);

			bool rev = false;
			const char *desc = "unknown";
			try
			{
				if (FPMessage::isQuest(buf))
				{
					desc = "TCP quest";
					quest = Decoder::decodeQuest(buf, _total);
				}
				else
				{
					desc = "TCP answer";
					answer = Decoder::decodeAnswer(buf, _total);
				}
				rev = true;
			}
			catch (const FpnnError& ex)
			{
				LOG_ERROR("Decode %s error. Connection will be closed by server. Code: %d, error: %s.", desc, ex.code(), ex.what());
			}
			catch (...)
			{
				LOG_ERROR("Decode %s error. Connection will be closed by server.", desc);
			}

			free(buf);
			free(_dataBuffer);
			_dataBuffer = NULL;

			_curr = 0;
			_total = (int)sizeof(uint32_t);
			_getLength = false;

			return rev;
		}
	}
	return true;
}
bool EncryptedPackageReceiver::embed_receivedData(uint64_t connectionId, DWORD receivedBytes, EmbedRecvNotifyDelegate delegate)
{
	_curr += (int)receivedBytes;
	if (_curr == _total)
	{
		if (_getLength == false)
		{
			_total = (int)le32toh(*_packageLen);
			if (_total > Config::_max_recv_package_length)
			{
				LOG_ERROR("Recv huge TCP data. Connection will be closed by framework.");
				return false;
			}

			_curr = 0;
			_getLength = true;

			if (_dataBuffer)
				free(_dataBuffer);

			_dataBuffer = (uint8_t*)malloc(_total);
		}
		else
		{
			char* buf = (char*)malloc(_total);
			_encryptor.decrypt((uint8_t *)buf, _dataBuffer, _total);

			delegate(connectionId, buf, _total);

			if (Config::_embed_receiveBuffer_freeBySDK)
				free(buf);

			free(_dataBuffer);
			_dataBuffer = NULL;

			_curr = 0;
			_total = (int)sizeof(uint32_t);
			_getLength = false;
		}
	}
	return true;	
}
