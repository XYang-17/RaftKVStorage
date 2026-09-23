#include <iostream>

#include "raft/clerk/clerk.h"

int main(){
    printf("1");
    raft::clerk client;
    client.init("./node.conf");
    int count = 500;
    while(count--){
        client.put("x", std::to_string(count));
        
        std::cout << "x = " << client.get("x") << std::endl;
    }
    return 0;
}