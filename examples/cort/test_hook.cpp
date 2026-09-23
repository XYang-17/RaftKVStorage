#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <string>
#include <iostream>

#include <muduo/base/LogFile.h>
#include <muduo/base/Logging.h>

#include "cort/ioman.h"

const std::string LOG_HEAD = "[task] ";

muduo::LogFile *g_logfile = nullptr;

void output2file(const char *msg, int len){
    if(g_logfile){
        g_logfile->append(msg, len);
    }
}

void test_sleep(){
    std::cout << LOG_HEAD << "tid = " << cort::getThreadId() << ", test_co_sleep begin" << std::endl;
    cort::ioman *ioman = cort::ioman::getThis();
    ioman->addTask(
        [](){
            while(true){
                std::cout << "befort sleep 5s" << std::endl;
                sleep(5);
                std::cout << "after sleep 5s" << std::endl;
            }
        }
    );
    ioman->addTask(
        [](){
            while(true){
                std::cout << "befort sleep 2s" << std::endl;
                sleep(2);
                std::cout << "after sleep 2s" << std::endl;
            }
        }
    );
    std::cout << LOG_HEAD << "tid = " << cort::getThreadId() << ", test_co_sleep over" << std::endl;
}

void test_sock(){
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; 
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr.s_addr);

    std::cout << "connect begin" << std::endl;
    int ret = connect(sock_fd, (const sockaddr *)&addr, sizeof(addr));
    std::cout << "connect result: ret = " << ret << ", error: " << strerror(errno) << std::endl;
    if(ret) return;

    const char data[] = "GET / HTTP/1.0\r\n\r\n";
    ret = send(sock_fd, data, sizeof(data), 0);
    std::cout << "send result: ret = " << ret << ", error: " << strerror(errno) << std::endl;
    if(ret <= 0) return;

    std::string buf;
    buf.resize(4096);
    ret = recv(sock_fd, &buf[0], buf.size(), 0);
    std::cout << "recv result: ret = " << ret << ", error: " << strerror(errno) << std::endl;
    if(ret <= 0) return;

    buf.resize(ret);
    std::cout << "recv: " << buf << std::endl;
}

int main(){
    // muduo::LogFile logfile("log", 500*1024*1024, false, 0, 0);
    // g_logfile = &logfile;
    // muduo::Logger::setOutput(output2file);
    muduo::Logger::setLogLevel(muduo::Logger::DEBUG);

    cort::ioman ioman;
    ioman.addTask(test_sock);

    test_sleep();
}