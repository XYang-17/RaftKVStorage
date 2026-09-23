#include <muduo/base/Logging.h>

#include "fdman.h"

namespace cort{

fdContext::sptr fdman::get(int fd, bool auto_create){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[fdManger] get(...)";
    if(fd < 0){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[fdManger] invalid file descripter, get(...) over";
        return nullptr;
    }

    rdGuard_type rdGuard(_M_mutex);
    auto it = _M_datas.find(fd);
    if(_M_datas.end() != it){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[fdManger] fd found, get(...) over";
        return it->second;
    }
    if(false == auto_create){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[fdManger] fd not found and not auto_create, get(...) over";
        return nullptr;
    }
    rdGuard.unlock();

    // LOG_INFO << "[" << pthread_self() << "]" <<"[fdManger] adding the new fd into fdContext container ...";
    wrGuard_type wrGuard(_M_mutex);
    fdContext::sptr fdCtx(new fdContext(fd));
    if(!fdCtx->isInit()){
        // LOG_INFO << "[" << pthread_self() << "]" <<"[fdManger] Failed to initialize new fdContext, get(...) over";
        return nullptr;
    }
    _M_datas.insert({fd, fdCtx});

    // LOG_INFO << "[" << pthread_self() << "]" <<"[fdManger] get(...) over";
    return fdCtx;
}

void fdman::del(int fd){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[fdManger] del(...)";
    wrGuard_type wrGuard(_M_mutex);
    auto it = _M_datas.find(fd);
    if(_M_datas.end() == it){
    // LOG_INFO << "[" << pthread_self() << "]" <<"[fdManger] fd not found, del(...) over";
        return;
    }
    _M_datas.erase(it);
    // LOG_INFO << "[" << pthread_self() << "]" <<"[fdManger] del(...) over";
}
    
} // namespace cort
