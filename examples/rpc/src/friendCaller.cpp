#include <iostream>

#include "rpc/mprpcChannel.h"
#include "rpc/mprpcController.h"
#include "rpc/rpcProvider.h"
#include "friend.pb.h"

int main(int args, char *argv []){
    if(args < 3){
        std::cout << "Execute by ./file addr port";
        exit(EXIT_FAILURE);
    }

    std::string addr = argv[1];
    uint16_t port = std::atoi(argv[2]);

    fixbug::friendServiceRPC_Stub stub(new rpc::mprpcChannel(addr, port, true));
    fixbug::getFriendListRequest req;
    req.set_uerid(1000);

    fixbug::getFriendListResponse resp;
    rpc::mprpcController ctrl;

    int count = 10;
    while(count--){
        stub.getFriendList(&ctrl, &req, &resp, nullptr);

        if(ctrl.Failed()){
            std::cout << ctrl.ErrorText() << std::endl;
        }
        else{
            if(0 == resp.result().errcode()){
                std::cout << "  Success get" << std::endl;
                int size = resp.friends_size();
                for(int i = 0;i <size; ++i){
                    std::cout << "  index: " << (i+1) << " name: " << resp.friends(i) << std::endl;
                }
            }
            else{
                std::cout << "Failed to get friendList" << std::endl;
            }
        }
        sleep(1);
    }
    return 0;
}