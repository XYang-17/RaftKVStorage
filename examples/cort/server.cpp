#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
#include <stack>

#include <muduo/base/LogFile.h>
#include <muduo/base/Logging.h>

#include "cort/ioman.h"
#include "cort/hook.h"
#include "cort/fdman.h"

static int listen_sock_fd = -1;

muduo::LogFile *g_logfile = nullptr;

void output2file(const char *msg, int len){
    if(g_logfile){
        g_logfile->append(msg, len);
    }
}

void test_accept();

void watch_io_read(){
    printf("watch_io_read\n");
    cort::ioman::getThis()->addEvent(listen_sock_fd, cort::READ, test_accept);
}

void test_accept(){
    // auto ctx = cort::fdMan::getInstance()->get(listen_sock_fd);
    // std::cout << "userNonBlock: " << ctx->userNonBlock();
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    socklen_t addrlen;
    int fd;
    // while(true){
        addrlen = sizeof(addr);
        if(0 <= (fd = accept(listen_sock_fd, (struct sockaddr *)&addr, &addrlen))){
            // fcntl(fd, F_SETFL, O_NONBLOCK);
            int ret = cort::ioman::getThis()->addEvent(
                fd,
                cort::READ,
                [fd](){
                    char buf[1024]{0};
                    while(true){
                        int ret = recv(fd, buf, sizeof(buf), 0);
                        if(ret > 0){
                            LOG_INFO << "recv client: " << buf;
                            ret = send(fd, buf, ret, 0); // 回显
                        }
                        else{
                            if(EAGAIN == errno){
                                LOG_INFO << "recv EAGAIN";
                                break;
                            }
                            LOG_INFO << "recv errno = " << errno;
                            cort::ioman::getThis()->delEvent(fd, cort::READ);
                            close(fd);
                            break;
                        }
                    }
                }
            );
            if(-1 == ret) std::cout << "addEvent failure" << std::endl;
        }
    // }
    cort::ioman::getThis()->addTask(watch_io_read);
}

void test_ioman(){
    muduo::LogFile logfile("log", 500*1024*1024*1024, false, 0, 0);
    g_logfile = &logfile;
    muduo::Logger::setOutput(output2file);

    int port = 8080;
    struct sockaddr_in addr;
    listen_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(listen_sock_fd < 0){
        std::cout << "socket error";
        return;
    }
    
    int opt = -1;
    setsockopt(listen_sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset((char *)&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if(0 > bind(listen_sock_fd, (struct sockaddr *)&addr, sizeof(addr))){
        std::cout << "bind error";
        return;
    }

    if(0 > listen(listen_sock_fd, 1024)){
        std::cout << "listen error";
        return;
    }
    else{
        std::cout << "listen successfully on port: " << port << std::endl;
    }

    // fcntl(listen_sock_fd, F_SETFL, O_NONBLOCK);
    cort::fdman::getInstance()->get(listen_sock_fd, true);
    cort::ioman ioman;
    ioman.addEvent(listen_sock_fd, cort::READ, test_accept);
}

int main(){
    test_ioman();
    return 0;
}