#pragma once
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include "class.h"
#include "mutex.h"
#include "hook.h"

namespace cort{

class fdContext: public std::enable_shared_from_this<fdContext>{
public:
    typedef sptr<fdContext> sptr;

    enum flag{
        NONE = 0x0,
        ISINIT = 0x001,
        ISSOCK = 0x002,
        SYSNONBLOCK = 0x004,
        USERNONBLOCK = 0x008,
        ISCLOSE = 0x010,
    };

    enum timeoutType{
        RECV = SO_RCVTIMEO,
        SEND = SO_SNDTIMEO
    };

    fdContext(int fd):
        _M_recvTimeout(-1),
        _M_sendTimeout(-1),
        _M_fd(fd),
        _M_flags(NONE)
    {
        _M_init();
    }

    bool isInit() const{return _M_flags & ISINIT;}
    bool isSocket() const{return _M_flags & ISSOCK;}
    bool isClose() const{return _M_flags & ISCLOSE;}
    bool sysNonBlock() const{return _M_flags & SYSNONBLOCK;}
    bool userNonBlock() const{return _M_flags & USERNONBLOCK;}
    uint64_t timeout(int type){
        switch(type){
            case RECV: return _M_recvTimeout;
            case SEND: return _M_sendTimeout;
        }
        throw std::runtime_error("Invalid timeout type");
    }

    void sysNonBlock(bool v){
        if(v){
            _M_set(SYSNONBLOCK);
        }
        else{
            _M_reset(SYSNONBLOCK);
        }
    }
    void userNonBlock(bool v){
        if(v){
            _M_set(USERNONBLOCK);
        }
        else{
            _M_reset(USERNONBLOCK);
        }
    }
    void timeout(int type, uint64_t t){
        switch(type){
            case RECV: _M_recvTimeout = t; return;
            case SEND: _M_sendTimeout = t; return;
        }
        throw std::runtime_error("Invalid timeout type");
    }

private:
    bool _M_init(){
        if(isInit()) return true;

        struct stat fdStat;
        if(-1 == fstat(_M_fd, &fdStat)){
            _M_reset((ISINIT | ISSOCK));
        }
        else if(S_ISSOCK(fdStat.st_mode)){
            _M_set(ISINIT | ISSOCK);
        }
        else{
            _M_set(ISINIT);
        }

        if(isSocket()){
            int flags = fcntl_fn(_M_fd, F_GETFL, 0);
            if(!(flags & O_NONBLOCK)){
                fcntl_fn(_M_fd, F_SETFL, flags | O_NONBLOCK);
            }
            _M_set(SYSNONBLOCK);
        }
        else{
            _M_reset(SYSNONBLOCK);
        }

        return isInit();
    }

    void _M_set(int f){_M_flags |= f;}
    void _M_reset(int f){_M_flags &= ~f;}

protected:
    uint64_t _M_recvTimeout;
    uint64_t _M_sendTimeout;
    int _M_fd;
    int _M_flags;
};

class fdman{
public:
    typedef sptr<fdman> sptr;

    ~fdman()=default;

    static sptr getInstance(){
        static sptr ins(new fdman);
        return ins;
    }

    fdContext::sptr get(int fd, bool auto_create = false);
    void del(int fd);

private:
    fdman()=default;
    fdman(const fdman &)=delete;
    fdman &operator=(const fdman &)=delete;

protected:
    std::unordered_map<int, fdContext::sptr> _M_datas;
    rwMutex                                  _M_mutex;
};

    
} // namespace cort
