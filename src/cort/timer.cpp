#include <muduo/base/Logging.h>

#include "timer.h"

namespace cort{

// class timer
bool timer::cancel(){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] cancel(...)";
    wrGuard_type guard(_M_manager->_M_rwMutex);
    if(_M_cb){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] delete timer ...";
        // 清除超时回调，从manager中移除该定时器
        _M_cb = nullptr;
        auto it = _M_manager->_M_timers.find(shared_from_this());
        _M_manager->_M_timers.erase(it);
        // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] delete(...) over";
        return true;
    }
    // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] cancel(...) over";
    return false;
}

bool timer::refresh(){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] refresh(...)";
    wrGuard_type guard(_M_manager->_M_rwMutex);
    if(_M_cb){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] no callback function, refresh(...) over";
        return false;
    }
    auto it = _M_manager->_M_timers.find(shared_from_this());
    if(_M_manager->_M_timers.end() == it){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] not found in manager, refresh(...) over";
        return false;
    }
    _M_manager->_M_timers.erase(it);

    _M_next = now_ms() + _M_period;
    // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] reinserting to manager after refresh timer ...";
    _M_manager->_M_timers.insert(shared_from_this());
    // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] refresh(...) over";
    return true;
}

bool timer::reset(uint64_t period, bool from_now){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] reset(...)";
    if(period == _M_period && !from_now){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] new period is equal to _M_period and not from_now, reset(...) over";
        return true;
    }
    wrGuard_type guard(_M_manager->_M_rwMutex);
    if(!_M_cb){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] no callback function, reset(...) over";
        return true;
    }
    auto it = _M_manager->_M_timers.find(shared_from_this());
    if(_M_manager->_M_timers.end() == it){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] not found in manager, reset(...) over";
        return false;
    }
    _M_manager->_M_timers.erase(it);
    
    if(from_now) _M_next = now_ms();
    else _M_next += period - _M_period;
    _M_period = period;
    // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] reinserting to manager after reset timer ...";
    _M_manager->_M_addTimer(shared_from_this(), guard);
    // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] reset(...) over";
}



timer::timer(
    uint64_t period,
    std::function<void()> cb,
    bool recurring,
    timerManager *manager
):
    _M_recurring(recurring),
    _M_period(period),
    _M_cb(cb),
    _M_manager(manager),
    _M_next(now_ms() + _M_period){}

timer::timer(uint64_t next):
    _M_next(next){}


// class timer::comparator
bool timer::comparator::operator()(
    const sptr &lhs, const sptr &rhs)
const{
    if(!rhs) return false;
    if(!lhs) return true;
    if(lhs->_M_next < rhs->_M_next) return true;
    if(rhs->_M_next < lhs->_M_next) return false;
    return lhs.get() < rhs.get();
}

// class timerManager
timerManager::timerManager():
    _M_previousTime(now_ms()){}

timerManager::~timerManager(){}


timer::sptr timerManager::addTimer(
    uint64_t period, 
    std::function<void()> cb,
    bool recurring)
{
    timer::sptr timerPtr(new timer(period, cb, recurring, this));
    wrGuard_type guard(_M_rwMutex);
    _M_addTimer(timerPtr, guard);
    return timerPtr;
}

timer::sptr timerManager::addConditionTimer(
    uint64_t period,
    std::function<void()> cb,
    std::weak_ptr<void> cond,
    bool recurring)
{
    // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] addConditionTimer(...)";
    return addTimer(
        period,
        [cond, cb](){
            if(cond.lock()){
                cb();
            }
        },
        recurring
    );
    // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] addConditionTimer(...) over";
}


uint64_t timerManager::getNextDuration(){
    rdGuard_type guard(_M_rwMutex);
    _M_tickled = false;
    if(_M_timers.empty()) return noNext;
    const timer::sptr &next = *_M_timers.begin();
    uint64_t now_ms = cort::now_ms();
    if(now_ms >= next->_M_next) return 0;
    return next->_M_next - now_ms;
}

void timerManager::timeoutCallbacks(
    std::vector<std::function<void()>> &cbs)
{
    // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] timeoutCallbacks(...)";
    {
        // 读请求更容易获取到锁，快速判断是否存在计时器
        rdGuard_type guard(_M_rwMutex);
        if(_M_timers.empty()){
            // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] no timer, timeoutCallbacks(...) over";
            return;
        }
    }

    const uint64_t now_ms = cort::now_ms();
    std::vector<std::function<void()>> temp_cbs;
    {    
        wrGuard_type guard(_M_rwMutex);
        if(_M_timers.empty()){
            // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] no timer, timeoutCallbacks(...) over";
            return;
        }

        bool rollover = _M_clockRollover(now_ms);
        if(!rollover && ((*_M_timers.begin())->_M_next > now_ms)){
            // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] not rollover and next timeout later than now, timeoutCallback(...) over";
            return;
        }

        timer::sptr nowTimer(new timer(now_ms));
        auto timeoutEndIt = rollover ? _M_timers.end() : _M_timers.lower_bound(nowTimer);
        while(_M_timers.end() != timeoutEndIt
            && now_ms == (*timeoutEndIt)->_M_next)
        {++timeoutEndIt;}

        timer_container_type::iterator it = _M_timers.begin();
        while(it != timeoutEndIt){
            temp_cbs.emplace_back((*it)->_M_cb);
            auto node = _M_timers.extract(it++);
            if(node.value()->_M_recurring){
                node.value()->_M_next = now_ms + node.value()->_M_period;
                _M_timers.insert(std::move(node));
            }
            else{
                node.value()->_M_cb = nullptr;
            }
        }

        cbs.swap(temp_cbs);
    }
    // LOG_INFO << "[" << pthread_self() << "]" <<"[timer] timeoutCallbacks(...) over";
}


bool timerManager::empty() const{return _M_timers.empty();}


void timerManager::_M_addTimer(timer::sptr timer, wrGuard_type &guard){
    auto it = _M_timers.insert(timer).first;
    if((_M_timers.begin() != it) || _M_tickled){
        guard.unlock();
        return;
    }
    _M_tickled = true;
    guard.unlock();
    _M_afterAddTimerAtFront();
}


bool timerManager::_M_clockRollover(uint64_t now_ms){
    bool rollover = false;
    // 当前时间 小于 前一次记录时间
    if(now_ms < _M_previousTime && now_ms < (_M_previousTime - 60 * 60 * 1000)){
        rollover = true;
    }
    _M_previousTime = now_ms;
    return rollover;
}

} // namespace cort
