#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
#include <string.h>

#include <cort/ioman.h>

static int sock_fd = -1;
void watch_io_read();

void do_io_write(){
    std::cout << "write callback" << std::endl;
    int err;
    socklen_t len = sizeof(err);
    getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, &err, &len);
    if(err){
        std::cout << "connect failure" << std::endl;
    }
    else{
        std::cout << "connect success" << std::endl;
    }
}

void do_io_read(){
    std::cout << "read callback" << std::endl;
    char buf[1024] {0};
    int ret = read(sock_fd, buf, sizeof(buf));
    if(ret < 0){
        std::cout << "read error: " << strerror(errno) << std::endl;
        close(sock_fd);
        return;
    }
    buf[ret] = '\0';
    std::cout << "read: " << buf << std::endl;
    if(0 == ret) close(sock_fd);

    cort::ioman::getThis()->addTask(watch_io_read);
}

void watch_io_read(){
    std::cout << "watch_io_read";
    cort::ioman::getThis()->addEvent(sock_fd, cort::READ, do_io_read);
}

void test_io(){
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(sock_fd < 0) throw std::runtime_error("socket error");

    fcntl(sock_fd, F_SETFL, O_NONBLOCK);
    
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr.s_addr);

    int ret = connect(sock_fd, (const sockaddr *)&addr, sizeof(addr));
    if(0 == ret){
        std::cout << "connect: " << strerror(errno) << std::endl;
        return;
    }

    if(EINPROGRESS == errno){
        std::cout << "EINPROGRESS" << std::endl;
        cort::ioman::getThis()->addEvent(sock_fd, cort::WRITE, do_io_write);
        cort::ioman::getThis()->addEvent(sock_fd, cort::READ, do_io_read);
    }
    else{
        std::cout << "connect error: " << strerror(errno) << std::endl;
    }
}

int main(){
    cort::ioman ioman;
    ioman.addTask(test_io);

    return 0;
}