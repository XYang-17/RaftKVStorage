#include <dlfcn.h>
#include <cstdarg>

#include "hook.h"
#include "coroutine.h"
#include "ioman.h"
#include "fdman.h"

namespace cort{

static thread_local bool hook_enable = false;
static int g_tcp_connect_timeout = 5000;

// 宏展开多次使用XX(name)
#define HOOK_FN(XX) \
    XX(sleep)       \
    XX(usleep)      \
    XX(nanosleep)   \
    XX(socket)      \
    XX(connect)     \
    XX(accept)      \
    XX(read)        \
    XX(readv)       \
    XX(recv)        \
    XX(recvfrom)    \
    XX(recvmsg)     \
    XX(write)       \
    XX(writev)      \
    XX(send)        \
    XX(sendto)      \
    XX(sendmsg)     \
    XX(close)       \
    XX(fcntl)       \
    XX(ioctl)       \
    XX(getsockopt)  \
    XX(setsockopt)

void hook_init(){
    static bool is_inited = false;
    if(is_inited) return;
#define XX(name) name##_fn = (name##_t)dlsym(RTLD_NEXT, #name);
    HOOK_FN(XX); // sleep_fn = (sleep_t)dlsym(RTILD_NEXT, 'sleep); ...
#undef XX
}

// 定义静态对象，在main前获取所有符号地址
static uint64_t s_connect_timeout = -1;
struct _hookIniter{
    _hookIniter(){
        hook_init();
        s_connect_timeout = g_tcp_connect_timeout;
    }
};
static _hookIniter s_hook_initer;

// hool_enable
bool get_hook_enable(){return hook_enable;}
void set_hook_enable(const bool flag){ hook_enable = flag;}

struct timer_info{
    int cancelled = 0;
};

template <typename _Fn, typename... _Args>
static ssize_t do_io(
    int fd,
    _Fn fn,
    const char *hook_fn_name,
    uint32_t ev,
    fdContext::timeoutType type,
    _Args&& ...args)
{
    if(!hook_enable) return fn(fd, std::forward<_Args>(args)...);

    fdContext::sptr fdCtx = fdman::getInstance()->get(fd);
    if(!fdCtx) return fn(fd, std::forward<_Args>(args)...);
    if(fdCtx->isClose()){ errno = EBADF; return -1;}
    if(!fdCtx->isSocket() || fdCtx->userNonBlock()){
        return fn(fd, std::forward<_Args>(args)...);
    }

    uint64_t timeout = fdCtx->timeout(type);
    sptr<timer_info> tinfo(new timer_info);

retry:
    ssize_t n = fn(fd, std::forward<_Args>(args)...);
    while(-1 == n && EINTR == errno){
        n = fn(fd, std::forward<_Args>(args)...);
    }
    if(-1 == n && EAGAIN == errno){
        ioman *ioman = ioman::getThis();
        timer::sptr tsptr;
        std::weak_ptr<timer_info> wtinfo(tinfo);
        if(uint64_t(-1) != timeout){
            tsptr = ioman->addConditionTimer(
                timeout,
                [wtinfo, fd, ioman, ev](){
                    auto t = wtinfo.lock();
                    if(!t || t->cancelled) return;
                    t->cancelled = ETIMEDOUT;
                    ioman->cancelEvent(fd, ev);
                },
                wtinfo
            );
        }

        int ret = ioman->addEvent(fd, ev);
        if(ret){
            if(tsptr) tsptr->cancel();
            return -1;
        }
        coroutine::getRunning()->yield();
        if(tsptr) tsptr->cancel();
        if(tinfo->cancelled){
            errno = tinfo->cancelled;
            return -1;
        }
        goto retry;
    }
    return n;
}

extern "C"{
#define XX(name) name##_t name##_fn = nullptr;
    HOOK_FN(XX);
#undef XX

// sleep
unsigned int sleep(unsigned int sec){
    if(!hook_enable) return sleep_fn(sec);
    coroutine::sptr csptr = coroutine::getRunning();
    ioman *ioman = ioman::getThis();
    ioman->addTimer(sec * 1000, [=](){ioman->addTask(csptr, -1);});
    coroutine::getRunning()->yield();
    return 0;
}

int usleep(useconds_t usec){
    // if(!hook_enable){ usleep_fn(usec); return 0;}
    if(!hook_enable) return usleep_fn(usec);
    coroutine::sptr csptr = coroutine::getRunning();
    ioman *ioman = ioman::getThis();
    ioman->addTimer(usec / 1000, [=](){ioman->addTask(csptr, -1);});
    coroutine::getRunning()->yield();
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem){
    if(!hook_enable) return nanosleep_fn(req, rem);
    coroutine::sptr csptr = coroutine::getRunning();
    ioman *ioman = ioman::getThis();
    ioman->addTimer(
        req->tv_sec * 1000 + req->tv_nsec / 1000000,
        [=](){ioman->addTask(csptr, -1);}
    );
    coroutine::getRunning()->yield();
    return 0;
}

// socket
int socket(int domain, int type, int protocol){
    if(!hook_enable) return socket_fn(domain, type, protocol);
    int fd = socket_fn(domain, type, protocol);
    if(-1 != fd) fdman::getInstance()->get(fd, true);
    return fd;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen){
    return connect_with_timeout(sockfd, addr, addrlen, s_connect_timeout);
}

int accept(int sockfd, struct sockaddr * addr, socklen_t *addrlen){
    int fd = do_io(sockfd, accept_fn, "accept", READ, fdContext::RECV, addr, addrlen);
    if(fd >= 0) fdman::getInstance()->get(fd, true);
    return fd;
}

// read
ssize_t read(int fd, void *buf, size_t count){
    return do_io(fd, read_fn, "read", READ, fdContext::RECV, buf, count);
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt){
    return do_io(fd, readv_fn, "readv", READ, fdContext::RECV, iov, iovcnt);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags){
    return do_io(sockfd, recv_fn, "recv", READ, fdContext::RECV, buf, len, flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *addr, socklen_t *addrlen){
    return do_io(sockfd, recvfrom_fn, "recvfrom", READ, fdContext::RECV, buf, len, flags, addr, addrlen);
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags){
    return do_io(sockfd, recvmsg_fn, "recvmsg", READ, fdContext::RECV, msg, flags);
}

// write
ssize_t write(int fd, const void *buf, size_t count){
    return do_io(fd, write_fn, "write", WRITE, fdContext::SEND, buf, count);
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt){
    return do_io(fd, writev_fn, "writev", WRITE, fdContext::SEND, iov, iovcnt);
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags){
    return do_io(sockfd, send_fn, "send", WRITE, fdContext::SEND, buf, len, flags);
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *addr, socklen_t addrlen){
    return do_io(sockfd, sendto_fn, "sendto", WRITE, fdContext::SEND, buf, len, flags, addr, addrlen);
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags){
    return do_io(sockfd, sendmsg_fn, "sendmsg", WRITE, fdContext::SEND, msg, flags);
}

// other
int close(int fd){
    if(hook_enable){
        fdContext::sptr fdCtx = fdman::getInstance()->get(fd);
        if(fdCtx){
            auto ioman = ioman::getThis();
            if(ioman) ioman->cancelAll(fd);
            fdman::getInstance()->del(fd);
        }
    }
    return close_fn(fd);
}

int fcntl(int fd, int cmd, ...){
    va_list va;
    va_start(va, cmd);
    switch(cmd){
        case F_SETFL: {
            int arg = va_arg(va, int);
            va_end(va);
            fdContext::sptr fdCtx = fdman::getInstance()->get(fd);
            printf("fcntl(F_SETFL, ...) [%p]", fdCtx.get());
            if(!fdCtx || fdCtx->isClose() || !fdCtx->isSocket()) return fcntl_fn(fd, cmd, arg);
            
            fdCtx->userNonBlock(arg & O_NONBLOCK);
            if(fdCtx->sysNonBlock()) arg |= O_NONBLOCK;
            else arg &= ~O_NONBLOCK;
            return fcntl_fn(fd, cmd, arg);
        } break;
        case F_GETFL: {
            va_end(va);
            int arg = fcntl_fn(fd, cmd);
            fdContext::sptr fdCtx = fdman::getInstance()->get(fd);
            printf("fcntl(F_GETFL, ...) [%p]", fdCtx.get());
            if(!fdCtx || fdCtx->isClose() || !fdCtx->isSocket()) return arg;
            if(fdCtx->userNonBlock()) return arg | O_NONBLOCK;
            return arg & ~O_NONBLOCK;
        } break;
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
        case F_SETFD:
        case F_SETOWN:
        case F_SETSIG:
        case F_SETLEASE:
        case F_NOTIFY:
#ifdef F_SETPIPE_SZ
        case F_SETPIPE_SZ:
#endif
        {
            int arg = va_arg(va, int);
            va_end(va);
            return fcntl_fn(fd, cmd, arg);
        } break;
        case F_GETFD:
        case F_GETOWN:
        case F_GETSIG:
        case F_GETLEASE:
#ifdef F_GETPIPE_SZ
        case F_GETPIPE_SZ:
#endif
        {
            va_end(va);
            return fcntl_fn(fd, cmd);
        } break;



        case F_SETLK:
        case F_SETLKW:
        case F_GETLK:{
            struct flock *arg = va_arg(va, struct flock*);
            va_end(va);
            return fcntl_fn(fd, cmd, arg);
        } break;
        case F_GETOWN_EX:
        case F_SETOWN_EX: {
            struct f_owner_exlock *arg = va_arg(va, struct f_owner_exlock *);
            va_end(va);
            return fcntl_fn(fd, cmd, arg);
        } break;
        default:
            va_end(va);
            return fcntl_fn(fd, cmd);
    }
}

int ioctl(int fd, unsigned long int request, ...){
    va_list va;
    va_start(va, request);
    void *arg = va_arg(va, void*);
    va_end(va);

    if(FIONBIO == request){
        bool userNonBlock = !!*(int *)arg;
        fdContext::sptr fdCtx = fdman::getInstance()->get(fd);
        printf("ioctl [%p]", fdCtx.get());
        if(!fdCtx || fdCtx->isClose() || !fdCtx->isSocket()){
            return ioctl_fn(fd, request, arg);
        }
        fdCtx->userNonBlock(userNonBlock);
    }
    return ioctl_fn(fd, request, arg);
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen){
    return getsockopt_fn(sockfd, level, optname, optval, optlen);
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen){
    if(!hook_enable) return setsockopt_fn(sockfd, level, optname, optval, optlen);
    if(SOL_SOCKET == level){
        if(SO_RCVTIMEO == optname || SO_SNDTIMEO == optname){
            fdContext::sptr fdCtx = fdman::getInstance()->get(sockfd);
            printf("setsockopt [%p]", fdCtx.get());
            if(fdCtx){
                const timeval *v = (const timeval *)optval;
                fdCtx->timeout(optname, v->tv_sec * 1000 + v->tv_usec / 1000);
            }
        }
    }
    return setsockopt_fn(sockfd, level, optname, optval, optlen);
}

int connect_with_timeout(int fd, const struct sockaddr *addr, socklen_t addrlen, uint64_t timeout_ms){
    if(!hook_enable) return connect_fn(fd, addr, addrlen);
    fdContext::sptr fdCtx = fdman::getInstance()->get(fd);
    printf("connect_with_timeout [%p]", fdCtx.get());
    if(!fdCtx || fdCtx->isClose()){ errno = EBADF; return -1;}
    if(!fdCtx->isSocket() || fdCtx->userNonBlock()) return connect_fn(fd, addr, addrlen);

    int n = connect_fn(fd, addr, addrlen);
    if(0 == n) return 0;
    if(-1 != n || EINPROGRESS != errno) return n;

    ioman *ioman = ioman::getThis();
    timer::sptr tsptr;
    sptr<timer_info> stinfo(new timer_info);
    std::weak_ptr<timer_info> wtinfo(stinfo);
    if(uint64_t(-1) != timeout_ms){
        tsptr = ioman->addConditionTimer(
            timeout_ms,
            [wtinfo, fd, ioman](){
                auto t = wtinfo.lock();
                if(!t || t->cancelled) return;
                t->cancelled = ETIMEDOUT;
                ioman->cancelEvent(fd, WRITE);
            },
            wtinfo
        );
    }

    int ret = ioman->addEvent(fd, WRITE);
    if(0 == ret){
        coroutine::getRunning()->yield();
        if(tsptr) tsptr->cancel();
        if(stinfo->cancelled){ errno = stinfo->cancelled; return -1;}
    }
    else if(tsptr){
        tsptr->cancel();
    }

    int error = 0;
    socklen_t len = sizeof(int);
    if(-1 == getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len)) return -1;
    if(!error) return 0;
    errno = error;
    return -1;
}

}
    
} // namespace cort

