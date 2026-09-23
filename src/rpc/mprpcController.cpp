#include "mprpcController.h"

namespace rpc{

mprpcController::mprpcController()
    :_M_failed(false), _M_message(){}


void mprpcController::Reset(){
    _M_failed = false;
    _M_message.clear();
}

bool mprpcController::Failed() const{return _M_failed;}

std::string mprpcController::ErrorText() const{return _M_message;}

void mprpcController::SetFailed(const std::string &message){
    _M_failed = true;
    _M_message.assign(message);
}


void mprpcController::StartCancel(){}
bool mprpcController::IsCanceled() const{return false;}
void mprpcController::NotifyOnCancel(google::protobuf::Closure *callback){}

}; // namespace rpc

