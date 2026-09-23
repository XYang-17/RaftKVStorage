#include <iostream>
#include <vector>

#include "cort/thread.h"

void func1(){
    std::cout << "thread: " << cort::thread::current_thread_name() << "(" << cort::thread::current_thread() << ")";
}

void func2(){
    std::cout << "thread: " << cort::thread::current_thread_name() << "(" << cort::thread::current_thread() << ")";
}

int main(){
    std::vector<cort::thread::sptr> threads;
    for(int i = 0; i < 5; ++i){
        cort::thread::sptr t(new cort::thread(&func1, "name_"+std::to_string(i)));
        threads.push_back(t);
    }
    for(int i = 0; i < 5; ++i){
        threads[i]->join();
    }
    std::cout << "end";
}