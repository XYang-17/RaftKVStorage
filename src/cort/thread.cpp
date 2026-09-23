#include <iostream>
#include <sys/syscall.h>
#include <muduo/base/Logging.h>

#include "thread.h"

namespace cort{

pid_t getThreadId(){ return syscall(SYS_gettid); }

thread::thread(std::function<void()> func, const std::string &name):
    _M_func(func), _M_name(name.empty() ? "unknow" : name)
{
    LOG_INFO << "[" << pthread_self() << "]" <<"[thread] construction(...)";
    if(pthread_create(&_M_thread, nullptr, &thread::_M_run, this)){
        LOG_ERROR << "[" << pthread_self() << "]" <<"[thread] Failed to exerute pthread_create" << _M_name;
        throw std::logic_error("pthread_create");
    }
    LOG_INFO << "[" << pthread_self() << "]" <<"[thread] construction(...) over";
}

thread::~thread(){
    if(_M_thread){
        pthread_detach(_M_thread);
    }
}


void thread::join(){
    LOG_INFO << "[" << pthread_self() << "]" <<"[thread] join(...)";
    if(_M_thread){
        if(pthread_join(_M_thread, nullptr)){
            LOG_ERROR << "[" << pthread_self() << "]" <<"[thread] Failed to execute pthread_join in thread " << _M_name;
            throw std::logic_error("pthread_join");
        }
        _M_thread = 0;
    }
    LOG_INFO << "[" << pthread_self() << "]" <<"[thread] join(...) over";
}


void *thread::_M_run(void *arg){
    thread *t = (thread *)arg;
    cort::current_thread = t;
    cort::current_thread_name = t->_M_name;
    t->_M_tid = getThreadId();

    pthread_setname_np(pthread_self(), t->_M_name.substr(0, 15).c_str());
    std::function<void()> func;
    func.swap(t->_M_func);
    
    // LOG_INFO << "[" << pthread_self() << "]" <<"[thread] executing thread function ...";
    func();
    // LOG_INFO << "[" << pthread_self() << "]" <<"[thread] execute(...) over";
    return 0;
}
    
} // namespace cort
