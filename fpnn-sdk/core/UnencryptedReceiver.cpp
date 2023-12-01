#include <io.h>
#include "FPLog.h"
#include "Config.h"
#include "Decoder.h"
#include "Receiver.h"

using namespace fpnn;

int UnencryptedReceiver::remainDataLen()
{
	//-- check Magic Header
	if (FPMessage::isTCP((char *)_header))
		return (int)(sizeof(FPMessage::Header) + FPMessage::BodyLen((char *)_header)) - _curr;
	else
		return -1;
}

LPWSABUF UnencryptedReceiver::beginRecv()
{
	if (_curr < FPMessage::_HeaderLength)
	{
		_wsaBuffer.buf = (char*)(_header + _curr);
		_wsaBuffer.len = FPMessage::_HeaderLength - _curr;
	}
	else
	{
		_wsaBuffer.buf = (char*)(_bodyBuffer + _curr);
		_wsaBuffer.len = _total - _curr;
	}

	return &_wsaBuffer;
}
void* UnencryptedReceiver::transferRecvBuffer()
{
	void* buf;
	if (_curr < FPMessage::_HeaderLength)
	{
		buf = _header;
		_header = NULL;
	}
	else
	{
		buf = _bodyBuffer;
		_bodyBuffer = NULL;
	}
	return buf;
}
bool UnencryptedReceiver::receivedData(DWORD receivedBytes, FPQuestPtr& quest, FPAnswerPtr& answer)
{
	quest = nullptr;
	answer = nullptr;
	_curr += (int)receivedBytes;
	if (_curr == _total)
	{
		if (_total == FPMessage::_HeaderLength)
		{
			int length = remainDataLen();

			if (length > 0)
			{
				if (_bodyBuffer)
					free(_bodyBuffer);
				
				_bodyBuffer = (uint8_t*)malloc(length + FPMessage::_HeaderLength);
				_total += length;
				if (_total > Config::_max_recv_package_length)
				{
					LOG_ERROR("Recv huge TCP data. Connection will be closed by framework.");
					return false;
				}

				memcpy(_bodyBuffer, _header, FPMessage::_HeaderLength);
			}
			else
			{
				LOG_ERROR("Received Error data (Not available FPNN-TCP-Message).");
				return false;
			}
		}
		else
		{
			bool rev = false;
			const char *desc = "unknown";
			try
			{
				if (FPMessage::isQuest((char *)_bodyBuffer))
				{
					desc = "TCP quest";
					quest = Decoder::decodeQuest((char *)_bodyBuffer, _total);
				}
				else
				{
					desc = "TCP answer";
					answer = Decoder::decodeAnswer((char *)_bodyBuffer, _total);
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

			free(_bodyBuffer);
			_bodyBuffer = NULL;

			_curr = 0;
			_total = FPMessage::_HeaderLength;

			return rev;
		}
	}
	return true;	
}
bool UnencryptedReceiver::embed_receivedData(uint64_t connectionId, DWORD receivedBytes, EmbedRecvNotifyDelegate delegate)
{
	_curr += (int)receivedBytes;
	if (_curr == _total)
	{
		if (_total == FPMessage::_HeaderLength)
		{
			int length = remainDataLen();

			if (length > 0)
			{
				if (_bodyBuffer)
					free(_bodyBuffer);
				
				_bodyBuffer = (uint8_t*)malloc(length + FPMessage::_HeaderLength);
				_total += length;
				if (_total > Config::_max_recv_package_length)
				{
					LOG_ERROR("Recv huge TCP data. Connection will be closed by framework.");
					return false;
				}

				memcpy(_bodyBuffer, _header, FPMessage::_HeaderLength);
			}
			else
			{
				LOG_ERROR("Received Error data (Not available FPNN-TCP-Message).");
				return false;
			}
		}
		else
		{
			delegate(connectionId, _bodyBuffer, _total);

			if (Config::_embed_receiveBuffer_freeBySDK)
				free(_bodyBuffer);

			_bodyBuffer = NULL;

			_curr = 0;
			_total = FPMessage::_HeaderLength;
		}
	}
	return true;	
}
