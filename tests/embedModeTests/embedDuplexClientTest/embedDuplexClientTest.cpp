#include <iostream>
#include <vector>
#include <thread>
#include <assert.h>
#include "..\DemoBridgeClient.h"
#include "CommandLineUtil.h"

using namespace std;
using namespace fpnn;

FPQuestPtr QWriter(const char* method, bool oneway){
    FPQWriter qw(6,method, oneway);
    qw.param("quest", "one");
    qw.param("int", 2); 
    qw.param("double", 3.3);
    qw.param("boolean", true);
    qw.paramArray("ARRAY",2);
    qw.param("first_vec");
    qw.param(4);
    qw.paramMap("MAP",5);
    qw.param("map1","first_map");
    qw.param("map2",true);
    qw.param("map3",5);
    qw.param("map4",5.7);
    qw.param("map5","中文");
    return qw.take();
}

int sendCount = 0;
static LARGE_INTEGER gc_frequency;

void sProcess(const char* ip, int port){
    DemoBridgeClient client(ip, port, !CommandLineParser::exist("udp"));

    client.setConnectedNotify([](uint64_t connectionId, const std::string& endpoint,
        bool isTCP, bool encrypted, bool connected) {

        cout<<"client connected. ID: "<<connectionId<<", endpoint: "<<endpoint<<", isTCP: "<<isTCP<<", encrypted: "<<encrypted<<", connect : "<<(connected ? "successful" : "failed")<<endl;
    });

    client.setConnectionClosedNotify([](uint64_t connectionId, const std::string& endpoint,
        bool isTCP, bool encrypted, bool closedByError) {

        cout<<"client Closed. ID: "<<connectionId<<", endpoint: "<<endpoint<<", isTCP: "<<isTCP<<", encrypted: "<<encrypted<<", closedByError: "<<closedByError<<endl;
    });

    client.registerServerPush("duplex quest", [](uint64_t connectionId, const std::string& endpoint,
        bool isTCP, bool encrypted, FPQuestPtr quest) {
        
        cout<<"Tow way method demo is called. connection ID: "<<connectionId<<", endpoint: "<<endpoint<<", isTCP: "<<isTCP<<", encrypted: "<<encrypted<<endl;
        cout<<"quest data: "<<(quest->json())<<endl;

        return FPAWriter(2, quest)("Simple","one")("duplex", 2).take();
    });

    std::mutex locker;
    std::mutex* lockPtr = &locker;

    int64_t start = slack_real_msec();
    int64_t resp = 0;
    for (int i = 1; i<=sendCount; i++)
    {
        FPQuestPtr quest = QWriter((i%2) ? "duplex demo" : "delay duplex demo", false);
        LARGE_INTEGER start_time, stop_time;
        QueryPerformanceCounter(&start_time);
        try{
            locker.lock();
            client.sendQuest(quest, [lockPtr](FPAnswerPtr answer, int errorCode){
                lockPtr->unlock();
            });

            locker.lock();
            locker.unlock();
            if (quest->isTwoWay()){
                QueryPerformanceCounter(&stop_time);
                int64_t diff = (stop_time.QuadPart - start_time.QuadPart) * 1000 * 1000 / gc_frequency.QuadPart;
                resp += diff;
            }
        }
        catch (...)
        {
            cout<<"error occurred when sending"<<endl;
        }

        if(i % 10000 == 0){
            int64_t end = slack_real_msec();
            cout<<"Speed:"<<(10000*1000/(end-start))<<" Response Time:"<<resp/10000<<endl;
            start = end;
            resp = 0;
        }

        Sleep(((i%2) ? 1 : 3) * 1000);
    }
    client.close();
}

int main(int argc, char* argv[])
{
    CommandLineParser::init(argc, argv);
    std::vector<std::string> mainParams = CommandLineParser::getRestParams();

    if (mainParams.size() != 4)
    {
        cout<<"Usage: "<<argv[0]<<" ip port threadNum sendCount [-udp]"<<endl;
        return 0;
    }

    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0)
    {
        cout<<"WSAStartup failed: "<<iResult<<endl;
        return 1;
    }

    QueryPerformanceFrequency(&gc_frequency);

    demoGlobalClientManager.start();

    int thread_num = atoi(mainParams[2].c_str());
    sendCount = atoi(mainParams[3].c_str());
    vector<thread> threads;
    for(int i=0;i<thread_num;++i){
        threads.push_back(thread(sProcess, mainParams[0].c_str(), atoi(mainParams[1].c_str())));
    }   

    for(unsigned int i=0; i<threads.size();++i){
        threads[i].join();
    } 

    WSACleanup();
    return 0;
}

