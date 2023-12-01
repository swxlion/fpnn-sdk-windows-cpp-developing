#include <io.h>
#include <errno.h>
#include "Endian.h"
#include "IOBuffer.h"

using namespace fpnn;

void SendBuffer::encryptData()
{
	if (_sentPackage > 0)
		_encryptor->encrypt(_currBuffer);
	else
	{
		if (!_encryptAfterFirstPackage)
			_encryptor->encrypt(_currBuffer);
	}
}

bool SendBuffer::realSend(SOCKET socket, LPWSAOVERLAPPED overLapped, DWORD lastSentBytes, bool& realSent, bool& isClosed)
{
	_offset += lastSentBytes;
	_sentBytes += lastSentBytes;

	if (_currBuffer != NULL)
	{
		if (_offset == _currBuffer->length())
		{
			delete _currBuffer;
			_currBuffer = NULL;
			_offset = 0;
			_sentPackage += 1;
		}
	}

	if (_currBuffer == NULL)
	{
		CurrBufferProcessFunc currBufferProcess;
		{
			std::unique_lock<std::mutex> lck(*_mutex);
			if (_outQueue.size() == 0)
			{
				_sendToken = true;
				return true;
			}

			_currBuffer = _outQueue.front();
			_outQueue.pop();
			_offset = 0;

			currBufferProcess = _currBufferProcess;
		}

		if (currBufferProcess)
			(this->*currBufferProcess)();
	}

	_wsaBuffer.len = _currBuffer->length() - _offset;
	_wsaBuffer.buf = (char*)(_currBuffer->data() + _offset);

	DWORD dwFlags = 0;
	int rev = WSASend(socket, &_wsaBuffer, 1, NULL, dwFlags, overLapped, NULL);
	if (rev == SOCKET_ERROR)
	{
		int errorCode = WSAGetLastError();
		if (errorCode == WSA_IO_PENDING)
		{
			realSent = true;
			//-- Keep token
			return true;
		}

		if (errorCode == WSAECONNRESET || errorCode == WSAEDISCON || errorCode == WSAESHUTDOWN)
		{
			//isClosed = true;
		}
		else
		{
			LOG_ERROR("Error occurred when calling WSASend() API. WSA error code is: %d, socket: %d.", errorCode, socket);
		}

		isClosed = true;

		//-- Release token is NOT necessary, but for the futuer changing, release it.
		std::unique_lock<std::mutex> lck(*_mutex);
		_sendToken = true;

		return false;
	}
	
	realSent = true;
	//-- Keep token
	return true;
}

bool SendBuffer::send(SOCKET socket, LPWSAOVERLAPPED overLapped, DWORD lastSentBytes, bool IOCPCompletedEvent, bool& realSent, bool& isClosed, std::string* data)
{
	realSent = false;
	isClosed = false;
	if (data && data->empty())
	{
		delete data;
		data = NULL;
	}

	{
		std::unique_lock<std::mutex> lck(*_mutex);
		if (data)
			_outQueue.push(data);

		if (IOCPCompletedEvent)
		{
			if (_sendToken)
			{
				LOG_ERROR("Call SendBuffer::send() when IOCPCompleted == true, but _sendToken == true. Socket: %d", socket);
				_sendToken = false;
			}
		}
		else
		{
			if (!_sendToken)
				return true;

			_sendToken = false;
		}
	}

	//-- Token will be return in realSend() function.
	return realSend(socket, overLapped, lastSentBytes, realSent, isClosed);
}

bool SendBuffer::entryEncryptMode(uint8_t *key, size_t key_len, uint8_t *iv, bool streamMode)
{
	if (_encryptor)
		return false;

	Encryptor* encryptor = NULL;
	if (streamMode)
		encryptor = new StreamEncryptor(key, key_len, iv);
	else
		encryptor = new PackageEncryptor(key, key_len, iv);

	{
		std::unique_lock<std::mutex> lck(*_mutex);
		if (_sentBytes) return false;
		if (_sendToken == false) return false;

		_encryptor = encryptor;
		_currBufferProcess = &SendBuffer::encryptData;
	}

	return true;
}

void SendBuffer::appendData(std::string* data)
{
	if (data && data->empty())
	{
		delete data;
		return;
	}

	std::unique_lock<std::mutex> lck(*_mutex);
	if (data)
		_outQueue.push(data);
}
