#include <muduo/base/Logging.h>

#include "rpc/mprpcController.h"
#include "rpc/mprpcChannel.h"

#include "core/raftHelper.h"

namespace raft{

raftHelper::raftHelper(std::string &addr, uint16_t port):
    _M_stub(new raftBase_Stub(new rpc::mprpcChannel(addr, port, true))){}

raftHelper::~raftHelper(){delete _M_stub;}


bool raftHelper::appendEntries(
    appendEntriesArgs *args,
    appendEntriesReply *reply)
{
    rpc::mprpcController ctrl;
    _M_stub->appendEntries(&ctrl, args, reply, nullptr);
    if(ctrl.Failed()){
        LOG_WARN << "Failed to execute to because: " << ctrl.ErrorText();
    }
    return !ctrl.Failed();
}

bool raftHelper::installSnapshot(
    installSnapshotArgs *args,
    installSnapshotReply *reply)
{
    rpc::mprpcController ctrl;
    _M_stub->installSnapshot(&ctrl, args, reply, nullptr);
    if(ctrl.Failed()){
        LOG_WARN << "Failed to execute because: " << ctrl.ErrorText();
    }
    return !ctrl.Failed();
}

bool raftHelper::requestVote(
    requestVoteArgs *args,
    requestVoteReply *reply)
{
    rpc::mprpcController ctrl;
    _M_stub->requestVote(&ctrl, args, reply, nullptr);
    if(ctrl.Failed()){
        LOG_WARN << "Failed to execute because: " << ctrl.ErrorText();
    }
    return !ctrl.Failed();
}

}; // namespace raft