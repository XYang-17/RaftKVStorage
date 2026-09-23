#include <muduo/base/Logging.h>

#include "coroutine.h"
#include "hook.h"

namespace cort{

/*
非主线程中，调度协程就是主协程，调度其他协程
主线程中，调度协程不是主协程；当主线程不参与任务执行，则没有调度协程，只有主协程
主线程的主协程只负责添加任务和停止调度器
*/
static thread_local coroutine *     running_coroutine = nullptr;        // 运行协程
static thread_local coroutine *     scheduler_coroutine = nullptr;      // 调度协程
static thread_local sptr<coroutine> main_coroutine = nullptr;           // 主协程
static thread_local scheduler *     thread_scheduler = nullptr;         // 线程调度器

// class coroutine
coroutine::coroutine(
    std::function<void()> func,
    size_t stackSpace,
    bool inScheduler
):
    _M_stackSize(stackSpace ? stackSpace : _M_default_stackSize),
    _M_stack(static_cast<void*>(malloc(_M_stackSize)), &free)
{
    // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] construction(...) with arguments";
    ++_M_countor;
    if(!_M_stack.get()){
        LOG_ERROR << "[" << pthread_self() << "]" <<"[coroutine] malloc failure";
        throw std::runtime_error("Malloc failure");
    }
    if(0 != getcontext(&_M_context)){
        LOG_ERROR << "[" << pthread_self() << "]" <<"[coroutine] getcontext failure";
        throw std::runtime_error("getcontext failure");
    }
    _M_makecontext();
    _M_func = func;
    _M_cid = _M_new_cid++;
    _M_inScheduler = inScheduler;

    // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] construction(...) with arguments over";
}
    
coroutine::~coroutine(){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] deconstruction(...)";
    --_M_countor;
    if(_M_stack){
        if(TERM != _M_state) LOG_WARN << "[" << pthread_self() << "]" <<"[coroutine] not TERM state when deconstruction?";
    }
    else{
        if(_M_func) LOG_WARN << "[" << pthread_self() << "]" <<"[coroutine] a function in main coroutine?";
        if(RUNNING != _M_state) LOG_WARN << "[" << pthread_self() << "]" <<"[coroutine] not RUNNING state of main coroutine?";
        if(this == running_coroutine){
            _M_setRunningCoroutine(nullptr);
        }
    }
    // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] deconstruction(...) over";
}
    

bool coroutine::reset(std::function<void()> func){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] reset(...)";
    if(!_M_stack.get()){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] no stack, reset(...) over";
        return false;
    }
    if(TERM != _M_state){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] not TERM state, reset(...) over";
        return false;
    }
    if(0 != getcontext(&_M_context)){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] Failed to get context, reset(...) over";
        return false;
    }

    _M_makecontext();
    _M_func = func;
    _M_state = READY;

    // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] reset(...) over";
    return true;
}
    
void coroutine::resume(){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] resume(...)";
    if(READY != _M_state){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] not READY state, resume(...) over";
        return;
    }
    _M_setRunningCoroutine(this);
    _M_state = RUNNING;
    
    // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] swapping context ...";
    // 从调度协程上下文切换到工作协程上下文
    if(_M_inScheduler){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] with scheduler";
        if(0 != swapcontext(&(scheduler::getSchedulerCoroutine()->_M_context), &_M_context)){
            LOG_ERROR << "[" << pthread_self() << "]" <<"[coroutine] Failed to swap context with scheduler";
            throw std::runtime_error("swapcontext");
        }
    }
    else{
        // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] with main coroutine";
        if(0 != swapcontext(&(main_coroutine->_M_context), &_M_context)){
            LOG_ERROR << "[" << pthread_self() << "]" <<"[coroutine] Failed to swap context with main coroutine";
            throw std::runtime_error("swapcontext");
        }
    }
    // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] resume(...) over";
}
    
void coroutine::yield(){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] yield(...)";
    if(READY == _M_state){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] READY state, yield(...) over";
        return;
    }
    
    // 从工作协程上下文切换到调度协程上下文
    // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] swapping context ...";
    if(_M_inScheduler){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] with scheduler";
        _M_setRunningCoroutine(scheduler::getSchedulerCoroutine());
        if(RUNNING == _M_state) _M_state = READY;
        if(0 != swapcontext(&_M_context, &(scheduler::getSchedulerCoroutine()->_M_context))){
            LOG_ERROR << "[" << pthread_self() << "]" <<"[coroutine] Failed to swap context with scheduler";
            throw std::runtime_error("swapcontext");
        }
    }
    else{
        // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] with main coroutine";
        _M_setRunningCoroutine(main_coroutine.get());
        if(RUNNING == _M_state) _M_state = READY;
        if(0 != swapcontext(&_M_context, &(main_coroutine->_M_context))){
            LOG_ERROR << "[" << pthread_self() << "]" <<"[coroutine] Failed to swap context with main coroutine";
            throw std::runtime_error("swapcontext");
        }
    }
    // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] yield(...) over";
}
    
coroutine::sptr coroutine::getRunning(){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] getRunning(...)";
    if(!running_coroutine){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] create main coroutine";
        main_coroutine = sptr(new coroutine);
    }
    // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] getRunning(...) over";
    return running_coroutine->shared_from_this();
}
    

coroutine::coroutine(){
    if(0 != getcontext(&_M_context)){
        LOG_ERROR << "[" << pthread_self() << "]" <<"[coroutine]  getcontext failure";
        throw std::runtime_error("getcontext failure");
    }
    _M_cid = _M_new_cid++;
    _M_setRunningCoroutine(this);
    _M_state = RUNNING;
    ++_M_countor;
}

    
void coroutine::_M_setRunningCoroutine(coroutine *co){
    running_coroutine = co;
}
    
void coroutine::_M_main(){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] _M_main(...)";
    sptr cur = getRunning();
    cur->_M_func();
    cur->_M_func = nullptr;
    cur->_M_state = TERM;
    
    // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] _M_main yield";
    // 任务结束，协程退出
    auto cur_pointer = cur.get();
    cur.reset();
    cur_pointer->yield();

    // LOG_INFO << "[" << pthread_self() << "]" <<"[coroutine] _M_main(...) over";
}

void coroutine::_M_makecontext(){
    _M_context.uc_link = nullptr;
    _M_context.uc_stack.ss_sp = _M_stack.get();
    _M_context.uc_stack.ss_size = _M_stackSize;
    makecontext(&_M_context, &_M_main, 0);
}


// class scheduler
scheduler::scheduler(
    size_t thread_num,
    bool as_worker,
    const std::string &name
):
    _M_asWorker(as_worker), _M_name(name)
{
    LOG_INFO << "[" << pthread_self() << "]" <<"[schseduler] construction(...)";
    if(0 == thread_num){
        // LOG_WARN << "[" << pthread_self() << "]" <<"[scheduler]  0 of Thread_num is invalid, replace with 1";
        thread_num = 1;
    }
    
    if(_M_asWorker){
        _M_construct_as_worker(thread_num);
    }
    else{
        _M_construct_not_as_worker(thread_num);
    }
    LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] construction(...) over";
}

scheduler::~scheduler(){
    stop();
    if(this == getScheduler()){
        _M_setScheduler();
    }
}

scheduler *scheduler::getScheduler(){return thread_scheduler;}
coroutine *scheduler::getSchedulerCoroutine(){return scheduler_coroutine;}

void scheduler::start(){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] start(...)";
    mutexGuard_type guard(_M_mutex);
    if(_M_stopped){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] stopped, start(...) over";
        return;
    }
    if(!_M_threads.empty()){
        // LOG_WARN << "[" << pthread_self() << "]" <<"[scheduler] non-empty thread pool, start(...) over";
        return;
    }

    // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] initializing thread pool(" << _M_threadsNum << " threads) ...";
    _M_threads.resize(_M_threadsNum);
    for(size_t i = 0; i < _M_threadsNum; ++i){
        _M_threads[i].reset(new thread([this](){_M_schedule();}, _M_name + "-" + std::to_string(i)));
        _M_threadIds.emplace_back(_M_threads[i]->tid());
    }
    // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] initialze thread pool finish, start(...) over";
}

void scheduler::stop(){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] stop(...)";
    // stop()应当由主线程使用
    if(_M_asWorker ^ (this == getScheduler())){
        // LOG_WARN << "[" << pthread_self() << "]" <<"[scheduler] coroutine not in main thread is trying to stop scheduler, stop(...) over";
        // throw std::runtime_error("A incorrect thread is trying to stop scheduler");
        return;
    }
    if(_M_stopping()){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] stopped, stop(...) over";
        return; // 已进入停止状态
    }

    // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] tickling all thread";
    _M_stopped = true;
    for(size_t i = 0; i < _M_threadsNum; ++i){
        _M_tickle();
    }
    // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] tickle finish";

    // asWorker模式下，切回调度协程完成任务
    if(_M_mainSchedulerCoroutine){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] tickle main thread";
        _M_tickle();
        _M_mainSchedulerCoroutine->resume();
    }

    // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] join all thread";
    // 置换出全部线程，等待执行结束
    std::vector<thread::sptr> thds;
    {
        mutexGuard_type guard(_M_mutex);
        thds.swap(_M_threads);
    }
    for(auto &t: thds){
        t->join();
    }
    // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] stop(...) over";
}


void scheduler::_M_tickle(){}

void scheduler::_M_schedule(){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] _M_schedule(...)";
    set_hook_enable(true);
    _M_setScheduler();
    if(_M_mainThreadId != getThreadId()){
        scheduler_coroutine = coroutine::getRunning().get();
    }

    coroutine::sptr idle_co(new coroutine([this](){_M_idle();}));
    coroutine::sptr work_co;

    task tsk;
    bool tik = false;
    while(true){
        tsk.reset();

        tik = false;
        {
            mutexGuard_type guard(_M_mutex);
            auto tsk_it = _M_tasks.begin();

            // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] contending for task ...";
            while(_M_tasks.end() != tsk_it){
                // 此任务指定其它线程执行，该循环结束后通知其它线程
                if(-1 != tsk_it->_M_thread && getThreadId() != tsk_it->_M_thread){
                    ++tsk_it;
                    tik = true;
                    continue;
                }

                // 此任务存在问题
                if(!tsk_it->_M_coroutine && !tsk_it->_M_func){
                    LOG_WARN << "[" << pthread_self() << "]" <<"[scheduler] invalid task?";
                }
                if(tsk_it->_M_coroutine){
                    if(coroutine::READY != tsk_it->_M_coroutine->state_now()){
                        LOG_WARN << "[" << pthread_self() << "]" <<"[scheduler] not ready task in deque?";
                    }
                }

                // 取出可执行任务
                tsk = *tsk_it;
                _M_tasks.erase(tsk_it++);
                ++_M_activeCountor;

                // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] get task";
                break;
            }
            tik |= (_M_tasks.end() != tsk_it); // 存在指定线程的任务 或 后续还有任务，通知其它线程
        }
        if(tik) _M_tickle();

        if(tsk._M_coroutine){
            // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] resume exist coroutine";
            // 已有协程，继续执行
            tsk._M_coroutine->resume();
            --_M_activeCountor;
            // tsk.reset();
        }
        else if(tsk._M_func){
            // 复用已有协程/创建新协程，并开始执行
            if(work_co){
                // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] reuse exist coroutine";
                work_co->reset(tsk._M_func);
            }
            else{
                // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] create new coroutine";
                work_co.reset(new coroutine(tsk._M_func));
            }

            // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] resume coroutine";
            // tsk.reset();
            work_co->resume();
            --_M_activeCountor;
            work_co.reset();
        }
        else{
            // 未取到可执行任务
            // idle协程结束，此调度协程也结束
            if(coroutine::TERM == idle_co->state_now()){
                // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] TERM state of idle coroutine";
                break;
            }

            // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] resume idle coroutine";
            // idle
            ++_M_idleCountor;
            idle_co->resume();
            --_M_idleCountor;
        }
    }
    // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] _M_scheduler(...) over";
}

void scheduler::_M_idle(){
    while(!_M_stopping()){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] idle coroutine yield";
        // 未进入停止状态，idle协程自动yield
        coroutine::getRunning()->yield();
    }
}

bool scheduler::_M_stopping(){
    mutexGuard_type guard(_M_mutex);
    // 设置停止标志、无待执行任务、无活跃线程
    return _M_stopped && _M_tasks.empty() && 0 == _M_activeCountor;
}

void scheduler::_M_setScheduler(){
    thread_scheduler = this;
}


void scheduler::_M_construct_not_as_worker(size_t thread_num){
    LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] construct_not_as_worker(...)";
    _M_mainThreadId = -1;
    _M_threadsNum = thread_num;
    // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] construct_not_as_worker(...) over";
}

void scheduler::_M_construct_as_worker(size_t thread_num){
    LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] construct_as_worker(...)";
    if(nullptr != getScheduler()){
        LOG_ERROR << "[" << pthread_self() << "]" <<"[scheduler] scheduler existed";
        throw std::runtime_error("Scheduler existed");
    }
    _M_setScheduler();

    coroutine::getRunning();    // 初始化主线程的主协程
    _M_mainSchedulerCoroutine.reset(new coroutine([this](){_M_schedule();}, 0, false));
    thread::set_current_thread_name(_M_name);
    scheduler_coroutine = _M_mainSchedulerCoroutine.get();
    _M_mainThreadId = getThreadId();
    _M_threadIds.emplace_back(_M_mainThreadId);
    _M_threadsNum = thread_num - 1;

    // LOG_INFO << "[" << pthread_self() << "]" <<"[scheduler] construct_as_worker(...) over";
}


std::atomic<uint64_t> coroutine::_M_new_cid{0};
std::atomic<uint64_t> coroutine::_M_countor{0};
    
} // namespace cort
