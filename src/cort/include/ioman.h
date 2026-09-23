#pragma once
#include <fcntl.h>
#include <string.h>
#include <sys/epoll.h>

#include "class.h"
#include "timer.h"
#include "coroutine.h"

namespace cort{

/*
事件标志位
*/
enum event{
    NONE = 0x0,
    READ = EPOLLIN,
    WRITE = EPOLLOUT,
};

/*
事件上下文，包含需要该事件的协程/函数及其所属调度器
*/
struct eventContext{
    coroutine::sptr         co;
    std::function<void()>   func;
    scheduler *             sch= nullptr;
};

/*
文件描述符上下文，包含fd、待处理事件及I/O事件上下文
*/
class fdEventContext{
public:
    friend class ioman;

    eventContext &getEventContext(uint32_t e);             // 获取事件对应的事件上下文
    void triggerEvent(uint32_t e);                         // 触发事件

    static void resetEventContext(eventContext &c);     // 清理事件上下文

protected:
    // 事件上下文固定，可改为unordered_map<event, eventContext>?
    eventContext        _M_readContext;
    eventContext        _M_writeContext;
    uint32_t            _M_events = NONE;
    mutex               _M_mutex;           // 供ioman使用
    int                 _M_fd;
};

class ioman: public scheduler, public timerManager{
public:
    typedef cort::sptr<ioman> sptr;

    ioman(
        size_t thread_num = 1,
        bool as_worker = true,
        const std::string &name = "IOManager"
    );
    ~ioman();

    int addEvent(int fd, uint32_t e, std::function<void()> f = nullptr);   // 注册事件
    bool delEvent(int fd, uint32_t e);                                     // 移除事件(不触发)
    bool cancelEvent(int fd, uint32_t e);                                  // 取消事件(触发)
    bool cancelAll(int fd);                                                 // 取消全部事件

    static ioman *getThis();

protected:
    void _M_tickle() override;      // 通知调度器，任务到达
    bool _M_stopping() override;    // 判断已进入停止状态
    void _M_idle() override;        // idle协程函数

    void _M_afterAddTimerAtFront() override;
    
    bool _M_stopping(uint64_t &duration);            // 判断已进入停止状态，并将距下一个超时时间点的时长传出
    void _M_expandContext(size_t size);             // 扩充fd上下文数量

protected:
    // 使用vector，未注册的事件仍存在，只是_M_event=NONE，可改为map<fd, fdEventContext*>?
    std::vector<fdEventContext*>   _M_fdEventContexts;    // 待处理事件的fd上下文
    rwMutex                         _M_mutex;
    std::atomic<size_t>             _M_pendingCountor{0};   // 待处理事件计数器
    int                             _M_tickleFds [2];       // 管道读/写端文件描述符，调用_M_tickle()使线程从idle协程切到工作协程
    int                             _M_epollFd = 0;         // 内核epoll描述符

};
    
} // namespace cort
