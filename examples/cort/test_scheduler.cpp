#include <iostream>

#include "cort/ioman.h"
#include "cort/hook.h"

const std::string LOG_HEAD = "[task] ";

void test_co_1(){
    std::cout << LOG_HEAD << "tid = " << cort::getThreadId() << ", test_co_1 begin" << std::endl;
    std::cout << LOG_HEAD << "tid = " << cort::getThreadId() << ", test_co_1 over" << std::endl;
}

void test_co_2(){
    std::cout << LOG_HEAD << "tid = " << cort::getThreadId() << ", test_co_2 begin" << std::endl;
    sleep(3);
    std::cout << LOG_HEAD << "tid = " << cort::getThreadId() << ", test_co_3 over" << std::endl;
}

void test_co_3(){
    std::cout << LOG_HEAD << "tid = " << cort::getThreadId() << ", test_co_3 begin" << std::endl;
    std::cout << LOG_HEAD << "tid = " << cort::getThreadId() << ", test_co_3 over" << std::endl;
}

void test_co_4(){
    std::cout << LOG_HEAD << "tid = " << cort::getThreadId() << ", test_co_4 begin" << std::endl;
    std::cout << LOG_HEAD << "tid = " << cort::getThreadId() << ", test_co_4 over" << std::endl;
}

void test_user_co_1(){
    std::cout << "test_user_co_1 begin" << std::endl;

    // cort::scheduler sch;
    cort::ioman sch;
    sch.addTask(test_co_1);
    sch.addTask(test_co_2);

    cort::coroutine::sptr co(new cort::coroutine(&test_co_3));
    sch.addTask(co);

    sch.start();
    sch.stop();

    std::cout << "test_user_co_1 over" << std::endl;
}

void test_user_co_2(){
    std::cout << "test_user_co_2 begin" << std::endl;

    // cort::scheduler sch(3, true);
    cort::ioman sch(3, true);
    sch.addTask(test_co_1);
    sch.addTask(test_co_2);

    cort::coroutine::sptr co(new cort::coroutine(&test_co_3));
    sch.addTask(co);

    sch.start();
    sleep(5);
    sch.stop();

    std::cout << "test_user_co_2 over" << std::endl;
}

int main(){
    test_user_co_1();
    // test_user_co_2();
}