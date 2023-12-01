#pragma once

#include <stdint.h>
#include <winsock2.h>
#include <minwinbase.h>

#pragma comment(lib, "Ws2_32.lib")

namespace fpnn
{

	enum class IOCPActionType
	{
		IOCP_Connect,
		IOCP_Send,
		IOCP_Recv,
	};

	struct FPOverLapped
	{
		union
		{
			OVERLAPPED overLapped;
			WSAOVERLAPPED wsaOverLapped;
		} fpOverLapped;

		uint64_t connectionUniqueId;
		enum class IOCPActionType action;
	};
}