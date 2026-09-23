#pragma once
#include <vector>
#include <list>
#include <memory>
#include <atomic>
#include <functional>
#include <ucontext.h>

#include "class.h"
#include "thread.h"
#include "mutex.h"

namespace cort{

class coroutine;
class scheduler;


class coroutine: public std::enable_shared_from_this<coroutine>{
public:
    friend class scheduler;

    typedef sptr<coroutine> sptr;

    enum state{/*就绪*/READY, /*运行*/RUNNING, /*结束*/TERM};

    coroutine(std::function<void()> func, size_t stackSpace = 0, bool inScheduler = true);
    ~coroutine();

    uint64_t cid() const{return _M_cid;}
    state state_now() const{return _M_state;}

    bool reset(std::function<void()> func); // 重置复用协程
    void resume();                          // 进入运行，从调度协程获取cpu控制
    void yield();                           // 暂停运行，cpu交由调度协程控制

    static coroutine::sptr getRunning();    // 获取运行协程

private:
    coroutine(); // 仅用于getRunning中创建主协程

    static void _M_setRunningCoroutine(coroutine *co); // 设置运行协程
    static void _M_main();      // 主协程函数

    void _M_makecontext();

protected:
    static std::atomic<uint64_t> _M_new_cid;        // 最新协程id
    static std::atomic<uint64_t> _M_countor;        // 协程计数器
    static const size_t          _M_default_stackSize = 128 * 1024; // 默认协程栈空间容量，128KB
    
    ucontext_t              _M_context;             // 协程上下文，非运行时保存当前状态
    size_t                  _M_stackSize = 0;       // 栈空间容量
    std::unique_ptr<void, decltype(&free)>  _M_stack{nullptr, &free}; // 栈空间地址
    std::function<void()>   _M_func;                // 协程函数
    
    uint64_t                _M_cid = 0;             // 协程id
    state                   _M_state = READY;       // 状态
    bool                    _M_inScheduler;         // 调度器标志

};

class scheduler{
public:
    class task{
    public:
        friend class scheduler;

        task():_M_thread(-1){}
        task(coroutine::sptr co, int t): _M_coroutine(co), _M_thread(t){}
        task(coroutine::sptr *co, int t){
            _M_coroutine.swap(*co);
            _M_thread = t;
        }
        task(std::function<void()> func, int t):
            _M_func(func), _M_thread(t){}
        
        void reset(){
            _M_coroutine = nullptr;
            _M_func = nullptr;
            _M_thread = -1;
        }

    protected:
        coroutine::sptr         _M_coroutine = nullptr; // 可以yield/resume
        std::function<void()>   _M_func = nullptr;      // 此函数中不能使用yield，其后的代码无法执行
        int                     _M_thread;
    };

    typedef sptr<scheduler> sptr;

    scheduler(
        size_t thread_num = 1,
        bool as_worker = true,
        const std::string &name = "scheduler"
    );
    virtual ~scheduler();

    const std::string &getName() const{return _M_name;}

    static scheduler *getScheduler();           // 获取线程调度器
    static coroutine *getSchedulerCoroutine();  // 获取调度协程

    template <typename _Task>
    void addTask(_Task t, int thread = -1){      // 添加新任务
        bool tik = false;
        {
            mutexGuard_type guard(_M_mutex);
            tik = _M_addTask(t, thread);
        }
        if(tik) _M_tickle(); // 唤醒idle协程
    }
    void start();                                   // 启动调度
    void stop();                                    // 结束调度，等待结束

protected:
    virtual void _M_tickle();           // 通知任务
    virtual void _M_idle();             // idle协程函数
    virtual bool _M_stopping();         // 判断已进入停止状态

    void _M_schedule();                 // 调度协程函数
    void _M_setScheduler();             // 设置线程调度器
    bool _M_anyFreeThread() const{      // 判断存在空闲线程
        return _M_idleCountor > 0;
    }

private:
    void _M_construct_not_as_worker(size_t thread_num);    // 非asWorker模式构造
    void _M_construct_as_worker(size_t thread_num);        // asWorker模式构造

    // 添加新任务实际执行函数
    template <typename _Task>
    bool _M_addTask(_Task t, int thread){
        bool tik = _M_tasks.empty();
        task tsk(t, thread);
        if(tsk._M_coroutine || tsk._M_func){
            _M_tasks.emplace_back(tsk);
        }
        return tik;
    }

protected:
    std::string                 _M_name;
    mutex                       _M_mutex;

    std::list<task>             _M_tasks;                       // 任务队列

    std::vector<thread::sptr>   _M_threads;                     // 工作线程池
    std::vector<int>            _M_threadIds;                   // 所有线程Id
    size_t                      _M_threadsNum = 0;              // 工作线程数/线程池容量(caller模式下不包括主线程)
    std::atomic<size_t>         _M_activeCountor{0};            // 活跃线程计数器
    std::atomic<size_t>         _M_idleCountor{0};              // idle线程计数器

    coroutine::sptr             _M_mainSchedulerCoroutine = nullptr;    // 主线程调度协程(仅asWorker模式有效)
    int                         _M_mainThreadId = 0;                    // 主线程Id(仅asWorker模式有效)
    const bool                  _M_asWorker;                            // asWorker模式标志

    bool                        _M_stopped = false;             // 停止标志

};

}; // namespace cort
