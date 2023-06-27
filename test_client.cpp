#include "Msg.h"
#include "NetworkTest.grpc.pb.h"
#include "NetworkTest.pb.h"
#include <algorithm>
#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <bits/types/FILE.h>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <exception>
#include <fcntl.h>
#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/status.h>
#include <memory>
#include <mutex>
#include <ostream>
#include <random>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

class ClientTester {
    friend void RunClientTest(std::shared_ptr<ClientTester> tester);
    using NT = NetworkTest::NT;
    using Stub = NetworkTest::NT::Stub;
    using Result = NetworkTest::Result;
    using runtime_error = std::runtime_error;
    using Context = ::grpc::ClientContext;

    std::unique_ptr<Stub> stub;
    std::default_random_engine re;
    std::uniform_int_distribution<char> AsciiStringGenerator;
    std::uniform_int_distribution<char> BinStringGenerator;
    std::uniform_int_distribution<uint32_t> LenGenerator;
    int fd;
    void QueryStatus(uint64_t idx, Result &response) {
        if (idx < 0)
            runtime_error("No Exist msg Idx<0\n");
        if (idx <= SuccessMaxIdx) {
            response.set_id(SuccessMaxIdx);
            response.set_reason(Success);
            return;
        }
        Context context;
        NetworkTest::Query query;
        query.set_id(idx);
        auto res = stub->ResultQuery(&context, query, &response);
        if (!res.ok())
            runtime_error("Test Error,Please Retry!\n");
        if (response.reason() >= ErrorLevel)
            throw std::runtime_error(
                    ErrorCode2Msg(static_cast<TestStatus>(response.reason())));
        if (response.reason() == Success)
            SuccessMaxIdx = std::max(SuccessMaxIdx, response.id());
    }
    void SendAnswer(const std::string &s) {
        SendSeq++;
        Context context;
        Result response;
        ::NetworkTest::Register answer;
        answer.set_content(s);
        auto res = stub->AnswerRegister(&context, answer, &response);
        if (!res.ok())
            runtime_error("Test Error,Please Retry!\n");
        if (response.reason() >= ErrorLevel)
            throw std::runtime_error(
                    ErrorCode2Msg(static_cast<TestStatus>(response.reason())));
        if (response.reason() == Success)
            SuccessMaxIdx = std::max(SuccessMaxIdx, response.id());
    }
    uint32_t SendSeq = -1;
    uint32_t SuccessMaxIdx = -1;
    static const char *ErrorCode2Msg(TestStatus s) noexcept {
        switch (s) {
            case Success:
                return "Success";
            case Wait:
                return "Wait For Msg";
            case WaitRPC:
                return "Wait For Test";
            case Diff:
                return "Msg is Error";
            case Unknow:
                return "Unknow Error";
            case ErrorLevel:
            case TestError:;
        }
        return "Tester is Error";
    }

    TestStatus Check() {
        using namespace std::chrono_literals;
        Result response;
        QueryStatus(SendSeq, response);
        if (response.id() == SendSeq && response.reason() == Success)
            return Success;
        std::this_thread::sleep_for(3s);
        return (response.id() == SendSeq && response.reason() == Success)
                       ? Success
                       : static_cast<TestStatus>(response.reason());
    }

    void genAsciiMsg(uint64_t size) {
        for (uint64_t i = 0; i < size; i++) {
            auto len = LenGenerator(re);
            auto ch = AsciiStringGenerator(re);
            std::string s(len, ch);
            SendAnswer(s);
            msgs->push(std::move(s));
        }
    }

    void genBinMsg(uint64_t size) {
        for (uint64_t i = 0; i < size; i++) {
            auto len = LenGenerator(re);
            std::string s;
            for (auto t = 0; t < len; t++)
                s.push_back(BinStringGenerator(re));
            SendAnswer(s);
            msgs->push(std::move(s));
        }
    }
    uint64_t getSeed() {
        fd = open("/dev/urandom", O_RDONLY);
        uint64_t seed;
        auto rc = read(fd, &seed, sizeof(seed));
        if (rc != sizeof(seed))
            throw runtime_error("read /dev/random failed");
        return seed;
    }

public:
    ClientTester(std::string addr)
        : stub(NT::NewStub(
                  grpc::CreateChannel(addr, grpc::InsecureChannelCredentials()))),
          re(getSeed()), msgs(std::make_shared<MsgBuf>()),
          AsciiStringGenerator(' ', '~'), BinStringGenerator(CHAR_MIN, CHAR_MAX),
          LenGenerator(0, 4096) {}
    std::shared_ptr<MsgBuf> msgs;
    void FinishCheck() {
        auto res = Check();
        if (res == Success) {
            puts("Congratulations! You Pass The Test!");
            _exit(0);
        }
        printf("Sorry! You did not pass all Test. Reason:%s  :(\n",
               ErrorCode2Msg(res));
    }
};
void RunClientTest(std::shared_ptr<ClientTester> tester) {
    try {
        using namespace std::chrono_literals;
        tester->genAsciiMsg(1);
        std::this_thread::sleep_for(2s);
        auto reslut = tester->Check();
        if (reslut != Success) {
            printf("QAQ: Failed at 1\n");
        }
        tester->genAsciiMsg(1);
        reslut = tester->Check();
        if (reslut != Success) {
            printf("QAQ: Failed at 2\n");
        }
        tester->genAsciiMsg(1);
        reslut = tester->Check();
        if (reslut != Success) {
            printf("QAQ: Failed at 3\n");
        }
        tester->genBinMsg(1);
        reslut = tester->Check();
        if (reslut != Success) {
            printf("QAQ: Failed at 4\n");
        }
        tester->genBinMsg(1);
        reslut = tester->Check();
        if (reslut != Success) {
            printf("QAQ: Failed at 5\n");
        }
        tester->genBinMsg(1);
        reslut = tester->Check();
        if (reslut != Success) {
            printf("QAQ: Failed at 6\n");
        }
        tester->genAsciiMsg(1024);
        reslut = tester->Check();
        if (reslut != Success) {
            printf("QAQ: Failed at 7\n");
        }
        tester->genBinMsg(1024);
        reslut = tester->Check();
        if (reslut != Success) {
            printf("QAQ: Failed at 8\n");
        }
        tester->genAsciiMsg(1024);
        reslut = tester->Check();
        if (reslut != Success) {
            printf("QAQ: Failed at 9\n");
        }
        tester->genBinMsg(1024);
        reslut = tester->Check();
        if (reslut != Success) {
            printf("QAQ: Failed at 10\n");
        }
        printf("Success Pass All Test\n");
        _exit(0);
    } catch (...) {
        printf("Exception:\n");
    }
}
std::shared_ptr<MsgBuf> InitTestClient(std::string addr) {
    try {
        auto tester = std::make_shared<ClientTester>(addr);
        auto test_thread = std::thread(RunClientTest, tester);
        test_thread.detach();
        return tester->msgs;
    } catch (std::exception &e) {

        printf("Exception: %s\n", e.what());
    }
    return nullptr;
}

struct Message {
    int msgID;
    int partID;
    std::string data;
};
class mess {
    int partid;
    int len;
};



// 输出系统错误信息并退出程序
void sys_error(const char*str)
{
perror(str);
exit(-1);
}

// 向指定文件描述符 fd 写入指定大小为 size 的数据 msg
int writen(int fd, char* msg,int size)
{
// 定义指向 msg 的指针 buf 和数据大小 count
char* buf=msg;
int count =size;
// 循环写入数据，直到写入完毕或出错
while(count>0)
{
// 调用 send 函数向 fd 写入数据，返回值为实际写入的数据长度 len
int len=send(fd,buf,count,0);
// 如果出错，返回 -1
if(len==-1)
{
return -1;
}
// 如果发送缓冲区已满，继续发送
else if(len==0)
{
continue;
}
// 更新指针 buf 和数据大小 count
buf+=len;
count-=len;
}
// 返回写入的数据大小
return size;
}

// 向指定文件描述符 fd 发送指定长度为 len 的数据 msg
int sendMsg(int fd,const char* msg,int len)
{
    // 如果文件描述符为负数或数据为空或长度小于等于 0，返回 -1
    if(fd<0||msg==NULL||len<=0)
    {
        return -1;
    }
    // 分配 len+4 字节的内存空间，用于存放数据
    char* data=(char*)malloc(sizeof(char)*(len+4));
    // 将长度 len 转换成网络字节序并存放在 data 的前 4 个字节中
    int biglen=htonl(len);
    memcpy(data,&biglen,4);
    // 将数据 msg 复制到 data 中，从第 5 个字节开始
    memcpy(data+4,msg,len);
    int ret;
    // 调用 writen 函数向 fd 写入数据，返回值为写入的数据大小
    ret=writen(fd,data,len+4);
    // 如果出错，输出错误信息并关闭文件描述符 fd
    if(ret==-1)
    {
        perror("send error");
        close(fd);
    }
    // 返回写入的数据大小
    return ret;
}

int main() {
    char* buf;
    // 初始化测试客户端，指定 Server 端的监听地址为 "192.168.30.170:1234"
    auto msg = InitTestClient("192.168.30.170:1234");
    // 创建一个 TCP 套接字
    int cfd=socket(AF_INET,SOCK_STREAM,0);
    // 如果创建套接字失败，输出错误信息并退出程序
    if(cfd==-1)
    sys_error("socket error");
    // 定义客户端地址结构体 caddr，并初始化为本地地址和端口
    struct sockaddr_in caddr;
    caddr.sin_family=AF_INET;
    caddr.sin_port=htons(9526);
    inet_pton(AF_INET,"127.0.0.1",&caddr.sin_addr.s_addr);
    // 连接 Server 端，使用 TCP 协议
    connect(cfd,(struct sockaddr*)&caddr,sizeof(caddr));
    // 循环发送数据
    for(;;)
    {
        // 从测试客户端中获取一个字符串
        auto str=msg->pop();
        // 如果字符串为空，跳过本次循环
    if(str.size()==0)
    continue;
    // 将字符串转换为指向字符的指针 buf
    buf=str.data();
    // 输出发送数据的大小
    printf("发送数据的大小为%d\n",str.size());
    // 调用 sendMsg 函数向 Server 端发送数据
    sendMsg(cfd,buf,str.size());
    }
    // Put your code Here!
}