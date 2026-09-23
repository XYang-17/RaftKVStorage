#pragma once
#include <set>
#include <vector>
#include <memory>
#include <functional>

#include "class.h"
#include "mutex.h"

namespace cort{

/*
获取当前时间点的毫秒数
*/
static uint64_t now_ms(){
    struct timespec ts {0};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

class timer;
class timerManager;

class timer: public std::enable_shared_from_this<timer>{
public:
    friend class timerManager;

    typedef sptr<timer> sptr;

    bool cancel(); // 取消计时器
    bool refresh(); // 刷新计时器
    bool reset(uint64_t period, bool from_now); // 重置计时器

private:
    timer(
        uint64_t period,
        std::function<void()> cb,
        bool recurring,
        timerManager *manager
    );
    timer(uint64_t next);

protected:
    struct comparator{
        bool operator()(const sptr &lhs, const sptr &rhs) const;
    };

    uint64_t                _M_next = 0;
    bool                    _M_recurring = false;   // 循环计时
    uint64_t                _M_period = 0;          // 循环周期
    std::function<void()>   _M_cb;
    timerManager            *_M_manager = nullptr;
};

class timerManager{
public:
    friend class timer;

    static const uint64_t noNext = ~0ull;

    timerManager();
    virtual ~timerManager();

    timer::sptr addTimer(
        uint64_t period, 
        std::function<void()> cb,
        bool recurring = false
    );
    timer::sptr addConditionTimer(
        uint64_t period,
        std::function<void()> cb,
        std::weak_ptr<void> cond,
        bool recurring = false
    );

    uint64_t getNextDuration();                                     // 获取距下一个超时时间点的时长
    void timeoutCallbacks(std::vector<std::function<void()>> &cbs); // 获取所有已超时计时器的回调函数

    bool empty() const;

protected:
    virtual void _M_afterAddTimerAtFront() = 0;
    void _M_addTimer(timer::sptr timer, wrGuard_type &lock);

private:
    bool _M_clockRollover(uint64_t now);                            // 检测时钟被回拨

protected:
    typedef std::set<timer::sptr, timer::comparator> timer_container_type;

    rwMutex                 _M_rwMutex;
    timer_container_type    _M_timers;
    bool                    _M_tickled = false;
    uint64_t                _M_previousTime = 0; // 前一次记录的时间，用于检测时钟回拨
};
    
} // namespace cort
