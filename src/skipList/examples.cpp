// #include "include/skipList.h"
#include "include/kvSkipList.h"
#include <iostream>
#include <fstream>

int main(){
    KVSkipList::kvskiplist<std::string, std::string, 32> sl;
    sl["0"] = "1";
    sl["1"] = "1";
    sl["0"] = "1";
    sl.insert("1", "1");
    sl.insert("3", "10");
    sl.insert("4", "100");
    sl.insert("1", "1000");
    sl.insert("2", "1000");
    sl.insert("10", "10000");
    std::cout << "sl:\n" << sl << std::endl;

    // std::string str = sl.dump();
    // KVSkipList::kvskiplist<int, int, 5> sl1;
    // sl1.load(str);
    // std::cout << "sl1:\n" << sl1 << std::endl;
    // sl1[1] = 10;
    // sl1[100];
    // std::cout << "sl1:\n" << sl1 << std::endl;

    // for(auto it = sl.begin(); it != sl.end(); ++it){
    //     std::cout << (*it).first << ": " << (*it).second << " ";
    // }
    // std::cout <<std::endl;
    // (*(sl.begin())).second = 555;
    // sl.remove(sl.begin()++);
    // for(auto it = sl.begin(); it != sl.end(); ++it){
    //     std::cout << (*it).first << ": " << (*it).second << " ";
    // }
    // std::cout <<std::endl;
}