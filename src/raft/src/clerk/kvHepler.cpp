#include <muduo/base/Logging.h>

#include "clerk/kvHelper.h"

namespace raft{

kvHelper::kvHelper(std::string addr, uint16_t port):
    _M_stub(new kvServerBase_Stub(new rpc::mprpcChannel(addr, port, false))){}

kvHelper::~kvHelper(){delete _M_stub;}


bool kvHelper::get(getArgs *args, getReply *reply){
    rpc::mprpcController ctrl;
    _M_stub->get(&ctrl, args, reply, nullptr);
    if(ctrl.Failed()){
        LOG_WARN << "Failed to execute because: " << ctrl.ErrorText();
    }
    return !ctrl.Failed();
}

bool kvHelper::put(putArgs *args, putReply *reply){
    rpc::mprpcController ctrl;
    _M_stub->put(&ctrl, args, reply, nullptr);
    if(ctrl.Failed()){
        LOG_WARN << "Failed to execute because: " << ctrl.ErrorText();
    }
    return !ctrl.Failed();
}

}; // namespace raft