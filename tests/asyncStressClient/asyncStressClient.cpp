#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include "TCPClient.h"
#include "UDPClient.h"
#include "CommandLineUtil.h"

using std::cout;
using std::cerr;
using std::endl;
using namespace fpnn;

class QuestProcessor: public IQuestProcessor
{
    QuestProcessorClassPrivateFields(QuestProcessor)
    
public:
    virtual void connected(const ConnectionInfo& ci, bool connected)
    {
        cout<<"client "<<ci.token<<" socket "<<ci.socket<<" connect "<<(connected ? "successful" : "failed")<<"."<<endl;
    }
    virtual void connectionWillClose(const ConnectionInfo& ci, bool closeByError)
    {
        cout<<"client "<<ci.token<<" socket "<<ci.socket<<" closed by "<<(closeByError ? "error" : "normal")<<"."<<endl;
    }

    QuestProcessorClassBasicPublicFuncs
};

FPQuestPtr QWriter(const char* method, bool oneway, FPMessage::FP_Pack_Type def_ptype){
    FPQWriter qw(6,method, oneway, def_ptype);
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

class Tester
{
    std::string _ip;
    int _port;
    int _thread_num;
    int _qps;
    static LARGE_INTEGER _frequency;

    std::atomic<int64_t> _send;
    std::atomic<int64_t> _recv;
    std::atomic<int64_t> _sendError;
    std::atomic<int64_t> _recvError;
    std::atomic<int64_t> _timecost;

    std::vector<std::thread> _threads;

    void test_worker(int qps);

public:
    Tester(const char* ip, int port, int thread_num, int qps): _ip(ip), _port(port), _thread_num(thread_num), _qps(qps),
        _send(0), _recv(0), _sendError(0), _recvError(0), _timecost(0)
    {
        QueryPerformanceFrequency(&_frequency);
    }

    ~Tester()
    {
        stop();
    }

    inline void incRecv() { _recv++; }
    inline void incRecvError() { _recvError++; }
    inline void addTimecost( int64_t cost) { _timecost.fetch_add(cost); }

    void launch()
    {
        int pqps = _qps / _thread_num;
        if (pqps == 0)
            pqps = 1;
        int remain = _qps - pqps * _thread_num;
        if (remain < 0)
            remain = 0;

        for(int i = 0 ; i < _thread_num; i++)
            _threads.push_back(std::thread(&Tester::test_worker, this, pqps));

        if (remain)
            _threads.push_back(std::thread(&Tester::test_worker, this, remain));
    }

    void stop()
    {
        for(size_t i = 0; i < _threads.size(); i++)
            _threads[i].join();
    }

    void showStatistics()
    {
        const int sleepSeconds = 3;

        int64_t send = _send;
        int64_t recv = _recv;
        int64_t sendError = _sendError;
        int64_t recvError = _recvError;
        int64_t timecost = _timecost;


        while (true)
        {
            LARGE_INTEGER start;
            QueryPerformanceCounter(&start);

            Sleep(sleepSeconds * 1000);

            int64_t s = _send;
            int64_t r = _recv;
            int64_t se = _sendError;
            int64_t re = _recvError;
            int64_t tc = _timecost;

            LARGE_INTEGER ent;
            QueryPerformanceCounter(&ent);

            int64_t ds = s - send;
            int64_t dr = r - recv;
            int64_t dse = se - sendError;
            int64_t dre = re - recvError;
            int64_t dtc = tc - timecost;

            send = s;
            recv = r;
            sendError = se;
            recvError = re;
            timecost = tc;

            int64_t real_time = (ent.QuadPart - start.QuadPart) * 1000 * 1000 / _frequency.QuadPart;

            ds = ds * 1000 * 1000 / real_time;
            dr = dr * 1000 * 1000 / real_time;
            //dse = dse * 1000 * 1000 / real_time;
            //dre = dre * 1000 * 1000 / real_time;
            if (dr)
                dtc = dtc / dr;

            cout<<"time interval: "<<(real_time / 1000.0)<<" ms, send error: "<<dse<<", recv error: "<<dre<<endl;
            cout<<"[QPS] send: "<<ds<<", recv: "<<dr<<", per quest time cost: "<<dtc<<" usec"<<endl;
        }
    }
};

/*void usleep(long long usec)
{
	HANDLE timer = CreateWaitableTimer(NULL, TRUE, NULL);
	LARGE_INTEGER interval;
	interval.QuadPart = -10LL * usec;
	SetWaitableTimer(timer, &interval, 0, NULL, NULL, 0);
	WaitForSingleObject(timer, INFINITE);
	CloseHandle(timer);
}*/

LARGE_INTEGER Tester::_frequency;
void Tester::test_worker(int qps)
{
    Tester* ins = this;

    int intervalMs = 30;
    int batchCount = 0;
    while (batchCount == 0)
    {
        batchCount = qps * intervalMs / 1000;
        if (batchCount == 0)
            intervalMs += 5;
    }

    cout<<"-- qps: "<<qps<<", batch: "<<batchCount<<", interval: "<<intervalMs<<" ms"<<endl;

    std::shared_ptr<Client> client;
    if (CommandLineParser::exist("udp"))
    {
        client = Client::createUDPClient(_ip, _port);       
        client->setQuestProcessor(std::make_shared<QuestProcessor>());
    }
    else
    {
        client = Client::createTCPClient(_ip, _port);
        client->connect();
    }

    int64_t adjustMs = 0;
    LARGE_INTEGER batch_start_time, batch_stop_time;
    LARGE_INTEGER sleep_start_time, sleep_stop_time;
    while (true)
    {
        QueryPerformanceCounter(&batch_start_time);
        for (int i = 0; i < batchCount; i++)
        {
            FPQuestPtr quest = QWriter("two way demo", false, FPMessage::FP_PACK_MSGPACK);
            LARGE_INTEGER send_time;
            QueryPerformanceCounter(&send_time);
            try{
                bool status = client->sendQuest(quest, [send_time, ins](FPAnswerPtr answer, int errorCode)
                    {
                        if (errorCode != FPNN_EC_OK)
                        {
                            ins->incRecvError();
                            if (errorCode == FPNN_EC_CORE_TIMEOUT)
                                cout<<"Timeouted occurred when recving."<<endl;
                            else
                                cout<<"error occurred when recving."<<endl;
                            return;
                        }

                        ins->incRecv();
                        
                        LARGE_INTEGER recv_time;
                        QueryPerformanceCounter(&recv_time);
                        int64_t diff = (recv_time.QuadPart - send_time.QuadPart) * 1000 * 1000 / _frequency.QuadPart;
                        ins->addTimecost(diff);
                    });
                
                if (status)
                    _send++;
                else
                    _sendError++;
            }
            catch (...)
            {
                _sendError++;
                cerr<<"error occurred when sending"<<endl;
            }
        }
        QueryPerformanceCounter(&batch_stop_time);

        int64_t batchCostMs = (batch_stop_time.QuadPart - batch_start_time.QuadPart) * 1000 / _frequency.QuadPart;
        int64_t sleepMs = intervalMs - batchCostMs - adjustMs;
        if (sleepMs > 0)
        {
            QueryPerformanceCounter(&sleep_start_time);
            Sleep((DWORD)sleepMs);
            QueryPerformanceCounter(&sleep_stop_time);

            int realSleepMs = (int)((sleep_stop_time.QuadPart - sleep_start_time.QuadPart) * 1000 / _frequency.QuadPart);
            adjustMs = realSleepMs - sleepMs;
        }
        else
            adjustMs = 0;
            //adjustMs = -sleepMs;
    }
    client->close();
}

int main(int argc, char* argv[])
{
    CommandLineParser::init(argc, argv);
    std::vector<std::string> mainParams = CommandLineParser::getRestParams();
    if (mainParams.size() != 4)
    {
        cout<<"Usage: "<<argv[0]<<" ip port connections total-qps [-udp]"<<endl;
        return 0;
    }

    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0)
    {
        cout<<"WSAStartup failed: "<<iResult<<endl;
        return 1;
    }

    Tester tester(mainParams[0].c_str(), std::stoi(mainParams[1]), std::stoi(mainParams[2]), std::stoi(mainParams[3]));

    tester.launch();
    tester.showStatistics();

    WSACleanup();

    return 0;
}
