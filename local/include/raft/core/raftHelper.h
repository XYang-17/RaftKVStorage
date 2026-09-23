#pragma once

#include "../rpcProto/raftBase.pb.h"

namespace raft{

class raftHelper{
public:
    raftHelper(std::string &addr, uint16_t port);
    ~raftHelper();

    bool appendEntries(
        appendEntriesArgs *args,
        appendEntriesReply *reply
    );
    bool installSnapshot(
        installSnapshotArgs *args,
        installSnapshotReply *reply
    );
    bool requestVote(
        requestVoteArgs *args,
        requestVoteReply *reply
    );

protected:
    raftBase_Stub *_M_stub;
};

}; // namespace raft