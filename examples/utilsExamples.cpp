#include "utils.h"

int main(){
    DEFER {std::cout << "bye!";};

    dprintf(stdout, format("election time: %d", randomElectionTime().count()).c_str());

    safequeue<int> que;

    std::thread thread1([&que](){
        for(int i = 0; i < 10; ++i) que.push(i);
        dprintf(stdout, "thread1 push over");
    });
    std::thread thread2([&que](){
        for(int i = 10; i < 20; ++i) que.push(i);
        dprintf(stdout, "thread2 push over");
    });
    std::thread thread3([&que](){
        int res;
        std::chrono::milliseconds timeout(10);
        while(que.pop(res, timeout)){
            dprintf(stdout, "%d ", res);
        }


        dprintf(stdout, "timeout(%dms)", timeout);
    });

    if(thread1.joinable()){thread1.join();}
    if(thread2.joinable()){thread2.join();}
    if(thread3.joinable()){thread3.join();}

    return 0;
}