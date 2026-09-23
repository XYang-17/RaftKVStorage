#include <string>
#include <vector>
#include <iostream>

#include "friend.pb.h"
#include "rpc/mprpcChannel.h"
#include "rpc/rpcProvider.h"

class friendService: public fixbug::friendServiceRPC{
public:
    std::vector<std::string> getFriendList(uint32_t userId){
        std::vector<std::string> res;
        res.push_back("gao yang");
        res.push_back("liu hong");
        res.push_back("wang shuo");
        return res;
    }

    void getFriendList(
        google::protobuf::RpcController *ctrl,
        const fixbug::getFriendListRequest *req,
        fixbug::getFriendListResponse *resp,
        google::protobuf::Closure *cb)
    {
        uint32_t userId = req->uerid();
        std::vector<std::string> friendList = getFriendList(userId);
        resp->mutable_result()->set_errcode(0);
        resp->mutable_result()->set_errmsg("");
        for(std::string &name: friendList){
            std::string *p = resp->add_friends();
            *p = name;
        }
        cb->Run();
    }
};

int main(){
    std::string addr = "127.0.0.1";
    uint16_t port = 7788;
    std::cout << "run";
    auto stub = new fixbug::friendServiceRPC_Stub(new rpc::mprpcChannel(addr, port, false));

    std::cout << "run";
    rpc::rpcProvider provider;
    provider.notifyService(new friendService());

    std::cout << "run";
    provider.run(1, 7788);
    return 0;
}