#include <muduo/base/Logging.h>

#include "ioman.h"

namespace cort{

// class fdEventContext
eventContext &fdEventContext::getEventContext(uint32_t e){
    switch (e)
    {
    case READ:  return _M_readContext;
    case WRITE: return _M_writeContext;
    }
    throw std::invalid_argument("Invalid value of argument e of type event");
}

void fdEventContext::triggerEvent(uint32_t e){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[fdEventContext] triggerEvent(...)";
    if(!(_M_events & e)){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[fdEventContext] unregisted event";
        return; // 未注册事件
    }
    // 放回调度器任务队列
    eventContext &ctx = getEventContext(e);
    if(ctx.func){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[fdEventContext] schedule function";
        ctx.sch->addTask(ctx.func);
    }
    else{
        // LOG_INFO << "[" << pthread_self() << "]" <<"[fdEventContext] schedule coroutine";
        ctx.sch->addTask(ctx.co);
    }
    // 清理事件
    _M_events = event(_M_events & ~e); //移除事件标志位
    resetEventContext(ctx); // 清理事件上下文
    // LOG_INFO << "[" << pthread_self() << "]" <<"[fdEventContext] triggerEvent(...) over";
}

void fdEventContext::resetEventContext(eventContext &c){
    c.sch = nullptr;
    c.co.reset();
    c.func = nullptr;
}

// class ioman
ioman::ioman(
    size_t thread_num,
    bool as_worker,
    const std::string &name
):
    scheduler(thread_num, as_worker, name)
{
    LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] construction(...)";
    // 创建管道，_M_tickle() -> _M_idle()
    int ret = pipe(_M_tickleFds);
    if(0 != ret) throw std::runtime_error("pipe");

    epoll_event ee;
    memset(&ee, 0, sizeof(epoll_event));
    ee.events = EPOLLIN | EPOLLET;
    ee.data.fd = _M_tickleFds[0];
    ret = fcntl(_M_tickleFds[0], F_SETFL, O_NONBLOCK); // 边沿触发，必须非阻塞
    if(0 != ret) throw std::runtime_error("fcntl set non-block");

    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] initializing epoll ...";
    // 初始化epoll，监听管道读端fd的可读事件
    _M_epollFd = epoll_create(5000);
    ret = epoll_ctl(_M_epollFd, EPOLL_CTL_ADD, _M_tickleFds[0], &ee);
    if(0 != ret) throw std::runtime_error("epoll_ctl executing add");
    _M_expandContext(32);
    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] initialize epoll finish";

    start(); // 多协程处理
    LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] construction(...) over";
}

ioman::~ioman(){
    stop();
    close(_M_epollFd);
    close(_M_tickleFds[0]); 
    close(_M_tickleFds[1]);

    for(auto fdc: _M_fdEventContexts){
        if(fdc) delete fdc;
    }
}


int ioman::addEvent(int fd, uint32_t e, std::function<void()> f){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] addEvent(...)";
    fdEventContext *fdCtx = nullptr;
    rdGuard_type rdGuard(_M_mutex);
    // 找出/创建fd上下文
    if(_M_fdEventContexts.size() > size_t(fd)){
        fdCtx = _M_fdEventContexts[fd];
        rdGuard.unlock();
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] get fdEventContext";
    }
    else{
        rdGuard.unlock();
        wrGuard_type wrGuard(_M_mutex);
        _M_expandContext(fd * 1.5);
        fdCtx = _M_fdEventContexts[fd];
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] expand and get fdEventContext";
    }

    mutexGuard_type guard(fdCtx->_M_mutex);
    if(fdCtx->_M_events & e){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] registed event, addEvent(...) over";
        return -2; // 已注册
    }

    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] adding event ...";
    // 注册事件
    int op = fdCtx->_M_events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event ee;
    ee.events = fdCtx->_M_events | e | EPOLLET;
    ee.data.ptr = fdCtx;
    int ret = epoll_ctl(_M_epollFd, op, fd, &ee);

    if(ret){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] Failed to add event, addEvent(...) over";
        return -1; // 失败
    }

    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] updating fdEventContext ...";
    // 更新
    ++_M_pendingCountor;  // 待处理事件计数器+1，原子操作
    fdCtx->_M_events = event(fdCtx->_M_events | e);
    eventContext &eventCtx = fdCtx->getEventContext(e);
    // fdEventContext::resetEventContext(eventCtx);
    eventCtx.sch = scheduler::getScheduler();
    // 优先使用传入的协程函数
    if(f){
        eventCtx.func.swap(f);
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] scheduler functioin";
    }
    // 无函数时使用当前协程
    else{
        eventCtx.co = coroutine::getRunning();
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] scheduler current coroutine";
        if(coroutine::RUNNING != eventCtx.co->state_now()){
            // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] wrong state, addEvent(...) over";
            return -3;
        }
    }
    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] addEvent(...) over";
    return 0;
}

bool ioman::delEvent(int fd, uint32_t e){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] delEvent(...)";
    rdGuard_type rdGuard(_M_mutex);
    if(_M_fdEventContexts.size() <= size_t(fd)){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] invalid file descripter, delEvent(...) over";
        return false; // 无效文件描述符
    }
    fdEventContext *fdCtx = _M_fdEventContexts[fd];
    rdGuard.unlock();
    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] get fdEventContext";

    mutexGuard_type guard(fdCtx->_M_mutex);
    if(!(fdCtx->_M_events & e)){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] unregisted event, delEvent(...) over";
        return false; // 未注册(默认为NONE)
    }

    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] deleting event ...";
    // 取消事件
    event new_events = event(fdCtx->_M_events & ~e);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event ee;
    ee.events = new_events | EPOLLET;
    ee.data.ptr =fdCtx;
    int ret = epoll_ctl(_M_epollFd, op, fd, &ee);

    if(ret){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] Failed to delete event, delEvent(...) over";
        return false; // 失败
    }

    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] updating fdEventContext ...";
    // 更新
    --_M_pendingCountor;  // 待处理事件计数器-1，原子操作
    fdCtx->_M_events = new_events;
    // eventContext &eventCtx = fdCtx->getEventContext(e);
    fdCtx->resetEventContext(fdCtx->getEventContext(e));

    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] delEvent(...) over";
    return true;
}

bool ioman::cancelEvent(int fd, uint32_t e){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] cancelEvent(...)";
    rdGuard_type rdGuard(_M_mutex);
    if(_M_fdEventContexts.size() <= size_t(fd)){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] invalid file descripter, cancelEvent(...) over";
        return false; // 无效文件描述符
    }
    fdEventContext *fdCtx = _M_fdEventContexts[fd];
    rdGuard.unlock();
    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] get feEventContext";

    mutexGuard_type guard(fdCtx->_M_mutex);
    if(!(fdCtx->_M_events & e)){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] unregisted event, cancelEvent(...) over";
        return false; // 未注册(默认为NONE)
    }

    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] canceling event ...";
    // 取消事件
    event new_events = event(fdCtx->_M_events & ~e);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event ee;
    ee.events = new_events | EPOLLET;
    ee.data.ptr =fdCtx;
    int ret = epoll_ctl(_M_epollFd, op, fd, &ee);

    if(ret){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] Failed to cancel event, cancelEvent(...) over";
        return false; // 失败
    }

    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] updating fdEventContext ...";
    fdCtx->triggerEvent(e); // 触发事件
    --_M_pendingCountor; // 待处理事件计数器-1，原子操作

    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] cancelEvent(...) over";
    return true;
}

bool ioman::cancelAll(int fd){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] cancelAll(...)";
    rdGuard_type rdGuard(_M_mutex);
    if(_M_fdEventContexts.size() <= size_t(fd)){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] invalid file descripter, cancelAll(...) over";
        return false; // 无效文件描述符
    }
    fdEventContext *fdCtx = _M_fdEventContexts[fd];
    rdGuard.unlock();
    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] got fdEventContext";

    mutexGuard_type guard(fdCtx->_M_mutex);
    if(!(fdCtx->_M_events)){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] no registed event, cancelAll(...) over";
        return false; // 未注册任何事件(默认为NONE)
    }

    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] cancel all event ...";
    // 取消事件
    int op = EPOLL_CTL_DEL;
    epoll_event ee;
    ee.events = 0;
    ee.data.ptr =fdCtx;
    int ret = epoll_ctl(_M_epollFd, op, fd, &ee);

    if(ret){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] Failed to cancel all event, cancelAll(...) over";
        return false; // 失败
    }

    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] updating feEventContext ...";
    if(fdCtx->_M_events & READ){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] canceling READ ...";
        fdCtx->triggerEvent(READ); // 触发事件
        --_M_pendingCountor; // 待处理事件计数器-1，原子操作
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] cancel READ finish";
    }
    if(fdCtx->_M_events & WRITE){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] canceling WRITE ...";
        fdCtx->triggerEvent(WRITE); // 触发事件
        --_M_pendingCountor; // 待处理事件计数器-1，原子操作
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] cancel WRITE finish";
    }


    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] cancelAll(...) over";
    return true;
}


ioman *ioman::getThis(){
    return dynamic_cast<ioman *>(scheduler::getScheduler());
}


void ioman::_M_tickle(){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] _M_tickle(...)";
    if(_M_anyFreeThread()){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] free thread exist";
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] tickle through pipe ...";
        // 通过管道告知idle协程，事件就绪，切入工作协程
        // 写入的数据无意义，只是为了触发管道在_M_epollFd中注册的读事件
        int ret = write(_M_tickleFds[1], "\0", 1);
        if(1 != ret){
            LOG_ERROR << "[" << pthread_self() << "]" <<"[ioManger] Failed to tickle, _M_tickle(...) over";
            throw std::runtime_error("Failed to tickle");
        }
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] tickle finish";
    }
    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] _M_tickle(...) over";
}

bool ioman::_M_stopping(){
    uint64_t duration;
    return _M_stopping(duration);
}

void ioman::_M_idle(){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] _M_idle(...)";
    const uint64_t MAX_EVENTS = 256;
    // epoll_event *events = new epoll_event[MAX_EVENTS] {};
    cort::sptr<epoll_event> events(
        new epoll_event[MAX_EVENTS] {},
        [](epoll_event *p){delete[] p;}
    );
    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] create epoll_event array finish";

    uint64_t next_duration;
    while(!_M_stopping(next_duration)){ // 未进入停止状态，并获取距下一个超时时间点的时长
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] got duration to next timeout";
        int ret = 0;
        do{
            // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] waiting timeout ...";
            static const uint64_t MAX_TIMEOUT = 5000;
            if(noNext == next_duration){
                next_duration = MAX_TIMEOUT;
            }
            else{
                next_duration = std::min(next_duration, MAX_TIMEOUT);
            }
            // 监听事件，放入events数组中
            // next_duration要求epoll在下一次计时器超时的时候退出监听，处理超时
            ret = epoll_wait(_M_epollFd, events.get(), MAX_EVENTS, int(next_duration));
            // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] wait finish";

            if(ret < 0){
                if(EINTR == errno){
                    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] EINTER signal, continue listen";
                    continue; // wait被信号中断，重新监听
                }
                LOG_WARN << "[" << pthread_self() << "]" <<"[ioManger] idle coroutine " << getName() << "break because errno(" << std::to_string(errno) << ") from epoll_fd = " << _M_epollFd;
                break;
            }
            else{
            //     LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] event come";
                break; // 监听到事件，暂停监听
            }
        }while(true);

        // 超时回调函数放入队列
        {
            // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] getting timeout callback functions ...";
            std::vector<std::function<void()>> cbs;
            timeoutCallbacks(cbs); // 获取超时的计数器的回调函数
            // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] got";
            if(!cbs.empty()){
                // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] adding task ...";
                for(const auto &cb: cbs){
                    addTask(cb); // 加入任务队列
                }
                // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] add finish";
            }
        }

        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] dealing triggered events ...";
        // 在超时之后，避免超时回调为取消事件前事件已被处理
        for(int i = 0; i < ret; ++i){
            epoll_event &e = events.get()[i];
            // e.data为fd和一个void*指针的联合体
            if(e.data.fd == _M_tickleFds[0]){
                // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] from _M_tickle";
                // 此事件中data中为管道读端fd，表示管道注册的读事件，由_M_tickle()触发，数据无意义，丢弃
                uint8_t dummy[256];
                while(read(_M_tickleFds[0], dummy, sizeof(dummy)) > 0);
                continue;
            }

            // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] events triggered";
            // 处理真正的事件
            fdEventContext *fdCtx = (fdEventContext *)e.data.ptr;
            mutexGuard_type guard(fdCtx->_M_mutex);

            // 错误事件、挂起事件(对端关闭)
            if(e.events & (EPOLLERR | EPOLLHUP)){
                // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] EPOLLERR | EPOLLHUP";
                e.events |= (EPOLLIN | EPOLLOUT) & fdCtx->_M_events;
            }

            // 统计需要触发的事件
            int triggered = NONE;
            if(e.events & EPOLLIN)  triggered |= READ;
            if(e.events & EPOLLOUT) triggered |= WRITE;
            if(NONE == fdCtx->_M_events & triggered){
                // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] unregisted events";
                continue; // 与已注册事件无重叠
            }

            // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] delete triggered events";
            // 移除事件
            int left = fdCtx->_M_events & ~triggered;
            int op = left ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            e.events = left | EPOLLET;
            int ctl_ret = epoll_ctl(_M_epollFd, op, fdCtx->_M_fd, &e);

            // 失败
            if(ctl_ret){
                // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] idle coroutine " << getName() << "continue because errno(" << std::to_string(errno) << ") from epoll_fd = " << _M_epollFd;
                continue;
            }
            
            // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] triggering events in fdEventContext ...";
            // 触发事件
            if(READ & triggered){
                // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] triggering READ ...";
                fdCtx->triggerEvent(READ);
                --_M_pendingCountor;
                // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] trigger finish";
            }
            if(WRITE & triggered){
                // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] triggering WRITE ...";
                fdCtx->triggerEvent(WRITE);
                --_M_pendingCountor;
                // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] trigger finish";
            }
        }
        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] deal triggered events(...) over";

        // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] yield";
        // 此时任务队列中已存在该线程可执行的任务，idle协程yield
        coroutine::sptr cur = coroutine::getRunning();
        auto raw_ptr = cur.get();
        cur.reset();
        raw_ptr->yield();
    }
    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] idle coroutine " << getName() << " exit because stopping";
}


void ioman::_M_afterAddTimerAtFront(){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] _M_afterAddTimerAtFront(...)";
    // 计时器加入集合最前方，触发管道可读事件，让idle协程更新距下一个超时时间点的时长
    _M_tickle();
    // 若此刻未在执行idle协程，而是工作协程，则这个可读事件无法被及时感知
    // 只能等待下一次执行idle时才会被执行
    // LOG_INFO << "[" << pthread_self() << "]" <<"[ioman] _M_afterAddTimerAtFront(...) over";
}


bool ioman::_M_stopping(uint64_t &duration){
    duration = getNextDuration();
    // 无定时器(无等待中任务)、无待处理事件、调度器已进入停止状态
    return noNext == duration && 0 ==  _M_pendingCountor && scheduler::_M_stopping();
}

void ioman::_M_expandContext(size_t size){
    if(size > _M_fdEventContexts.size()){
        size_t i = _M_fdEventContexts.size();
        _M_fdEventContexts.resize(size);
        while(i < size){
            _M_fdEventContexts[i] = new fdEventContext;
            _M_fdEventContexts[i]->_M_fd = i;
            ++i;
        }
    }
}

} // namespace cort
