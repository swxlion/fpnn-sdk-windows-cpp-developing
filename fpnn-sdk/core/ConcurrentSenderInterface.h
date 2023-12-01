#ifndef FPNN_Concurrent_Sender_Interface_H
#define FPNN_Concurrent_Sender_Interface_H

#include <string>
#include <winsock2.h>
#include "AnswerCallbacks.h"

namespace fpnn
{
	class IConcurrentSender
	{
	public:
		virtual void sendTCPData(SOCKET socket, uint64_t token, std::string* data) = 0;
		virtual void sendUDPData(SOCKET socket, uint64_t token, std::string* data, int64_t expiredMS, bool discardable) = 0;

		/**
			All SendQuest():
				If throw exception or return false, caller must free quest & callback.
				If return true, or FPAnswerPtr is NULL, don't free quest & callback.
		*/
		virtual FPAnswerPtr sendQuest(SOCKET socket, uint64_t token, std::mutex* mutex, FPQuestPtr quest, int timeout = 0) = 0;
		virtual bool sendQuest(SOCKET socket, uint64_t token, FPQuestPtr quest, AnswerCallback* callback, int timeout = 0) = 0;
		virtual bool sendQuest(SOCKET socket, uint64_t token, FPQuestPtr quest, std::function<void (FPAnswerPtr answer, int errorCode)> task, int timeout = 0) = 0;
	};
}

#endif
