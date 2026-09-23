#pragma once

#include "rpc/mprpcChannel.h"
#include "rpc/mprpcController.h"
#include "rpc/rpcProvider.h"
#include "../rpcProto/kvServerBase.pb.h"

namespace raft{

class kvHelper{
public:
    kvHelper(std::string addr, uint16_t port);
    ~kvHelper();

    bool get(getArgs *args, getReply *reply);
    bool put(putArgs *args, putReply *reply);

protected:
    kvServerBase_Stub *_M_stub;
};

}; // namespace raft