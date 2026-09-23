#pragma once
#include <string>
#include <memory>
#include <thread>
#include <functional>

#include "class.h"

namespace cort{

pid_t getThreadId();

class thread;

static thread_local thread *    current_thread = nullptr;
static thread_local std::string current_thread_name = "unknow";

class thread{
public:
    typedef sptr<thread> sptr;

    thread(std::function<void()> func, const std::string &name = "unkown");
    ~thread();
    
    pid_t tid() const{return _M_tid;}
    const std::string &name() const{return _M_name;}
    
    void join();
    static thread *current_thread(){
        return cort::current_thread;
    }
    static const std::string &current_thread_name(){
        return cort::current_thread_name;
    }
    static void set_current_thread_name(const std::string &name){
        if(name.empty()) return;
        if(cort::current_thread){
            cort::current_thread->_M_name = name;
        }
        cort::current_thread_name = name;
    }

private:
    thread(const thread &) = delete;
    thread(thread &&) = delete;
    thread &operator=(const thread &) = delete;

    static void *_M_run(void *arg);

protected:
    pid_t           _M_tid;
    std::string     _M_name;
    pthread_t       _M_thread;
    std::function<void()>   _M_func;
    bool            _M_detachable = false;
};
    
}; // namespace cort
