#pragma once
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

namespace cort{

bool get_hook_enable();
void set_hook_enable(const bool flag);

extern "C"{

// sleep
typedef unsigned int (*sleep_t)(unsigned int sec);
typedef int (*usleep_t)(useconds_t usec);
typedef int (*nanosleep_t)(const struct timespec *req, struct timespec *rem);

extern sleep_t        sleep_fn;
extern usleep_t       usleep_fn;
extern nanosleep_t    nanosleep_fn;

// socket
typedef int (*socket_t)(int domain, int type, int protocal);
typedef int (*connect_t)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
typedef int (*accept_t)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

extern socket_t   socket_fn;
extern connect_t  connect_fn;
extern accept_t   accept_fn;

// read
typedef ssize_t (*read_t)(int fd, void *buf, size_t count);
typedef ssize_t (*readv_t)(int fd, const struct iovec *iov, int iovcnt);
typedef ssize_t (*recv_t)(int sockfd, void *buf, size_t len, int flags);
typedef ssize_t (*recvfrom_t)(int sockfd, void *buf, size_t len, int flags, struct sockaddr *addr, socklen_t *addrlen);
typedef ssize_t (*recvmsg_t)(int sockfd, struct msghdr *msg, int flags);

extern read_t         read_fn;
extern readv_t        readv_fn;
extern recv_t         recv_fn;
extern recvfrom_t     recvfrom_fn;
extern recvmsg_t      recvmsg_fn;

//write
typedef ssize_t (*write_t)(int fd, const void *buf, size_t count);
typedef ssize_t (*writev_t)(int fd, const struct iovec *iov, int iovcnt);
typedef ssize_t (*send_t)(int sockfd, const void *buf, size_t len, int flags);
typedef ssize_t (*sendto_t)(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *addr, socklen_t addrlen);
typedef ssize_t (*sendmsg_t)(int sockfd, const struct msghdr *msg, int flags);

extern write_t        write_fn;
extern writev_t       writev_fn;
extern send_t         send_fn;
extern sendto_t       sendto_fn;
extern sendmsg_t      sendmsg_fn;

// other
typedef int (*close_t)(int fd);
typedef int (*fcntl_t)(int fd, int cmd, ...);
typedef int (*ioctl_t)(int fd, unsigned long int request, ...);
typedef int (*getsockopt_t)(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
typedef int (*setsockopt_t)(int sockfd, int level, int optname, const void *optval, socklen_t optlen);

extern close_t        close_fn;
extern fcntl_t        fcntl_fn;
extern ioctl_t        ioctl_fn;
extern getsockopt_t   getsockopt_fn;
extern setsockopt_t   setsockopt_fn;

extern int connect_with_timeout(int fd, const struct sockaddr *addr, socklen_t addrlen, uint64_t timeout_ms);

}
    
} // namespace cort