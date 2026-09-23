#pragma once
#include <list>
#include <mutex>
#include <thread>
#include <memory>
#include <pthread.h>

#include "class.h"


namespace cort{

class semaphore: noncopyable{
public:
    semaphore(uint32_t count = 0);
    ~semaphore();

    void wait();
    void notify();
};

class mutex: noncopyable{
public:
    mutex(){
        pthread_mutex_init(&_M_mutex, nullptr);
    }
    ~mutex(){
        pthread_mutex_destroy(&_M_mutex);
    }

    void lock(){
        pthread_mutex_lock(&_M_mutex);
    }
    void unlock(){
        pthread_mutex_unlock(&_M_mutex);
    }

protected:
    pthread_mutex_t _M_mutex;
};

class rwMutex: noncopyable{
public:
    rwMutex(){
        pthread_rwlock_init(&_M_mutex, nullptr);
    }
    ~rwMutex(){
        pthread_rwlock_destroy(&_M_mutex);
    }

    void rdlock(){
        pthread_rwlock_rdlock(&_M_mutex);
    }
    void wrlock(){
        pthread_rwlock_wrlock(&_M_mutex);
    }
    void unlock(){
        pthread_rwlock_unlock(&_M_mutex);
    }


protected:
    pthread_rwlock_t _M_mutex;
};

template <typename _Mutex,
    typename = decltype(std::declval<_Mutex>().lock())>
class scopedGuard{
public:
    scopedGuard(_Mutex &mutex):
        _M_mutex(mutex), _M_locked(false){lock();}
    ~scopedGuard(){unlock();}

    void lock(){
        if(!_M_locked){
            _M_mutex.lock();
            _M_locked = true;
        }
    }
    void unlock(){
        if(_M_locked){
            _M_mutex.unlock();
            _M_locked = false;
        }
    }

protected:
    _Mutex  &_M_mutex;
    bool    _M_locked;
};

template <typename _RMutex,
    typename = decltype(std::declval<_RMutex>().rdlock())>
class readScopedGuard{
public:
    readScopedGuard(_RMutex &mutex):
        _M_mutex(mutex), _M_locked(false){rdlock();}
    ~readScopedGuard(){unlock();}

    void rdlock(){
        if(!_M_locked){
            _M_mutex.rdlock();
            _M_locked = true;
        }
    }
    void unlock(){
        if(_M_locked){
            _M_mutex.unlock();
            _M_locked = false;
        }
    }

protected:
    _RMutex  &_M_mutex;
    bool    _M_locked;
};

template <typename _WMutex,
    typename = decltype(std::declval<_WMutex>().wrlock())>
class writeScopedGuard{
public:
    writeScopedGuard(_WMutex &mutex):
        _M_mutex(mutex), _M_locked(false){wrlock();}
    ~writeScopedGuard(){unlock();}

    void wrlock(){
        if(!_M_locked){
            _M_mutex.wrlock();
            _M_locked = true;
        }
    }
    void unlock(){
        if(_M_locked){
            _M_mutex.unlock();
            _M_locked = false;
        }
    }

protected:
    _WMutex  &_M_mutex;
    bool    _M_locked;
};

typedef scopedGuard<mutex>          mutexGuard_type;
typedef readScopedGuard<rwMutex>    rdGuard_type;
typedef writeScopedGuard<rwMutex>   wrGuard_type;
} // namespace cort
