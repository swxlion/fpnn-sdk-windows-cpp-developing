#include <iostream>
#include "fpnn.h"

using namespace std;
using namespace fpnn;

class ExampleQuestProcessor : public IQuestProcessor
{
    QuestProcessorClassPrivateFields(ExampleQuestProcessor)

public:
    FPAnswerPtr duplexQuest(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
    {
        cout<<"Receive server push."<<endl;
        sendAnswer(FPAWriter::emptyAnswer(quest));

        cout<<"Answer is sent, processor will do other thing, and function will not return."<<endl;
        Sleep(2*1000);
        cout<<"Processor function will return."<<endl;

        return nullptr;
    }

    ExampleQuestProcessor()
    {
        registerMethod("duplex quest", &ExampleQuestProcessor::duplexQuest);
    }

    QuestProcessorClassBasicPublicFuncs
};

int main(int argc, const char** argv)
{
    if (argc != 2)
    {
        cout<<"Usage: "<<argv[0]<<" <endpoint>"<<endl;
        return 0;
    }

    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0)
    {
        cout<<"WSAStartup failed: "<<iResult<<endl;
        return 1;
    }

    TCPClientPtr client = TCPClient::createClient(argv[1]);
    client->setQuestProcessor(std::make_shared<ExampleQuestProcessor>());


    FPQWriter qw(1, "duplex demo");
    qw.param("duplex method", "duplex quest");
    FPQuestPtr quest = qw.take();

    FPAnswerPtr answer = client->sendQuest(quest);
    FPAReader ar(answer);
    if (ar.status() == 0)
        cout<<"Received answer of quest."<<endl;
    else
        cout<<"Received error answer of quest. code is "<<ar.wantInt("code")<<endl;

    Sleep(2*1000);   //-- wait for ExampleQuestProcessor::duplexQuest() print all message.

    return 0;
}