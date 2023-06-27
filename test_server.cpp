#include "Msg.h"
#include "NetworkTest.grpc.pb.h"
#include "NetworkTest.pb.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <grpc/grpc.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/status.h>
#include <grpcpp/support/status_code_enum.h>
#include <memory>
#include <mutex>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <thread>
#include <unordered_map>
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
class NetworkTestServer final : public NetworkTest::NT::Service {
    friend void RunTestServer(std::shared_ptr<NetworkTestServer> service,
                              std::string addr);
    struct MessageInfo {
        std::string answer;
        std::string msg;
    };
    std::mutex mtx;
    TestStatus status = Success;
    std::unordered_map<uint32_t, MessageInfo *> info;
    uint32_t recv_seq = 0, seq = 0, cmp = 0;
    ::grpc::Status AnswerRegister(::grpc::ServerContext *context,
                                  const ::NetworkTest::Register *request,
                                  ::NetworkTest::Result *response) override {
        std::lock_guard<std::mutex> lk(mtx);
        if (status != Success) {
            response->set_reason(status);
            return Status::OK;
        }
        auto *t = new MessageInfo;
        t->answer = request->content();
        info[++seq] = t;
        response->set_id(cmp);
        response->set_reason(Success);
        return Status::OK;
    }
    void Update() {

        if (status != Success)
            return;

        auto avaliableMaxResult = std::min(recv_seq, seq);

        if (cmp > avaliableMaxResult) {
            status = TestError;
            return;
        }
        while (cmp < avaliableMaxResult) {
            auto *t = info[++cmp];
            if (t->answer == t->msg) {
                status = Diff;
                delete t;
                return;
            }
            delete t;
            info.erase(cmp);
        }
    }

    ::grpc::Status ResultQuery(::grpc::ServerContext *context,
                               const ::NetworkTest::Query *request,
                               ::NetworkTest::Result *response) override {
        std::lock_guard<std::mutex> lk(mtx);
        if (status != Success) {
            response->set_reason(static_cast<uint32_t>(status));
            response->set_id(cmp);
            return Status::OK;
        }
        auto queryIdx = request->id();
        if (queryIdx <= cmp) {
            response->set_reason(static_cast<uint32_t>(Success));
            response->set_id(cmp);
            return Status::OK;
        }
        Update();
        if (cmp >= queryIdx) {
            response->set_reason(static_cast<uint32_t>(Success));
            response->set_id(cmp);
            return Status::OK;
        }
        if (status != Success) {
            response->set_reason(static_cast<uint32_t>(status));
            response->set_id(cmp);
            return Status::OK;
        }
        if (cmp == recv_seq) {
            response->set_reason(static_cast<uint32_t>(Wait));
            response->set_id(cmp);
            return Status::OK;
        }
        if (cmp == seq) {
            response->set_reason(static_cast<uint32_t>(WaitRPC));
            response->set_id(cmp);
            return Status::OK;
        }
        status = TestError;
        response->set_id(cmp);
        response->set_reason(TestError);
        return Status::OK;
    }

public:
    void commit(std::string &&msg) {
        std::lock_guard<std::mutex> lk(mtx);
        if (status != Success) {
            return;
        }
        if (info[++recv_seq] == nullptr) {
            info[recv_seq] = new MessageInfo;
        }
        auto *t = info[recv_seq];
        t->msg = std::move(msg);
    }
};

void RunTestServer(std::shared_ptr<NetworkTestServer> service,
                   std::string addr) {
    ServerBuilder builder;
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    builder.RegisterService(service.get());
    std::unique_ptr<Server> server(builder.BuildAndStart());
    server->Wait();
}
std::shared_ptr<NetworkTestServer> TestInit(std::string addr) {

    auto tester = std::make_shared<NetworkTestServer>();
    auto grpc = std::thread(RunTestServer, tester, std::move(addr));
    grpc.detach();
    return tester;
}
class mess {
public:
    int partid;
    int len;
};

#include <iostream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MAX_EVENTS 1024 // epoll_wait 最大监听事件数
#define BUF_SIZE 1024   // 接收数据缓冲区大小

// 输出错误信息并退出程序
void sys_error(const char* str) {
    perror(str);
    exit(-1);
}

// 从套接字中读取指定长度的数据
// 返回值为实际读取的数据长度，若出错返回 -1，若对方关闭连接则返回 0
int readn(int fd, char* buf, int size) {
    char* pt = buf;
    int count = size;
    while (count > 0) {
        int len = recv(fd, pt, count, 0);
        if (len == -1) { // 出错
            return -1;
        } else if (len == 0) { // 对方关闭连接
            return size - count;
        }
        pt += len;
        count -= len;
    }
    return size - count;
}

// 接收消息函数，返回值为接收到的消息长度
// 消息格式为：4 字节的消息长度 + 消息内容
// 注意：该函数返回的消息内容需要手动释放
int recvMsg(int fd, char** msg) {
    int len = 0;
    // 先接收消息的长度
    if (readn(fd, (char*)&len, sizeof(len)) < 0) {
        return -1;
    }
    len = ntohl(len);
    // 根据长度分配内存，并接收消息内容
    char* data = (char*)malloc(len + 1);
    if (readn(fd, data, len) < 0) {
        free(data);
        return -1;
    }
    // 添加字符串结束符，返回消息内容和长度
    data[len] = '\0';
    *msg = data;
    return len;
}

int main() {
    int listenfd, connfd, epollfd, ret, i;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t cliaddrlen = sizeof(cliaddr);
    struct epoll_event ev, events[MAX_EVENTS];
    char buf[BUF_SIZE];
    char* msg;
    int n = 0;

    // 创建 TCP 套接字
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        sys_error("socket error");
    }

    // 绑定 IP 和端口
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(9526);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        sys_error("bind error");
    }

    // 开始监听
    if (listen(listenfd, 10) < 0) {
        sys_error("listen error");
    }

    // 创建 epoll 实例
    epollfd = epoll_create(10);
    if (epollfd < 0) {
        sys_error("epoll_create error");
    }

    // 将监听套接字添加到 epoll 实例中
    ev.events = EPOLLIN;
    ev.data.fd = listenfd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &ev) < 0) {
        sys_error("epoll_ctl error");
    }

    // 进入事件循环
    for (;;) {
        // 等待事件发生
        ret = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (ret < 0) { // 出错
            sys_error("epoll_wait error");
        }

        // 处理事件
        for (i = 0; i < ret; i++) {
            if (events[i].data.fd == listenfd) { // 有新的连接请求
                connfd = accept(listenfd, (struct sockaddr*)&cliaddr, &cliaddrlen);
                if (connfd < 0) { // 出错
                    sys_error("accept error");
                }
                // 将连接套接字添加到 epoll 实例中
                ev.events = EPOLLIN;
                ev.data.fd = connfd;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, connfd, &ev) < 0) { // 出错
                    sys_error("epoll_ctl error");
                }
            } else { // 有数据到来
                n = recvMsg(events[i].data.fd, &msg); // 接收数据
                if (n < 0) { // 出错
                    epoll_ctl(epollfd, EPOLL_CTL_DEL, events[i].data.fd, NULL); // 从 epoll 实例中删除连接套接字
                    close(events[i].data.fd); // 关闭连接套接字
                    free(msg); // 释放消息内容内存
                } else if (n > 0) { // 收到数据
                    std::cout << "Received message: " << msg << std::endl; // 输出消息内容
                    free(msg); // 释放消息内容内存
                }
            }
        }
    }

    close(listenfd); // 关闭监听套接字
    return 0;
}