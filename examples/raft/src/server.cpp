#include <iostream>
#include <unistd.h>
#include <muduo/base/LogFile.h>
#include <muduo/base/Logging.h>

#include "raft/core/kvServer.h"

#include <signal.h>
#include <boost/stacktrace.hpp>

muduo::LogFile *g_logfile = nullptr;
int id;

void output2file(const char *msg, int len){
    if(g_logfile){
        g_logfile->append(msg, len);
    }
}

void segv_handler(int signum){
    ::signal(signum, SIG_DFL);
    std::string file = "./backtrace"+std::to_string(id)+".dump";
    boost::stacktrace::safe_dump_to(file.c_str());
    std::ifstream ifs(file);
    std::cout << boost::stacktrace::stacktrace::from_dump(ifs) << std::endl;
    ::raise(SIGABRT);
}

int main(int args, char **argv){
    if(args < 3){
        std::cout << "command -i <...> -n <...> -f <...>" << std::endl;
        exit(EXIT_FAILURE);
    }

    muduo::LogFile logfile("log", 500*1024*1024, false, 0, 0);
    g_logfile = &logfile;
    muduo::Logger::setOutput(output2file);

    int c, nodeNum, me;
    std::string cfgFile;

    while((c = getopt(args, argv, "i:n:f:")) != -1){
        switch (c)
        {
        case 'i': me = atoi(optarg); break;
        case 'n': nodeNum = atoi(optarg); break;
        case 'f': cfgFile = optarg; break;
        default:
            std::cout << "command -i <...> -n <...> -f <...>" << std::endl;
            exit(EXIT_FAILURE);
        }
    }
    
    std::ofstream ofs(cfgFile, std::ios::out | std::ios::app);
    if(!ofs.is_open()){
        std::cout << "Can not open " << cfgFile << std::endl;
        exit(EXIT_FAILURE);
    }
    ofs.close();

    id = me;
    ::signal(SIGSEGV, &segv_handler);

    // for(int i = 0; i < nodeNum; ++i){
    //     pid_t pid = fork();
    //     if(0 == pid){
    //         // 子进程
    //         auto server = new raft::kvServer(i, 500, cfgFile);
    //         pause();
    //     }
    //     else if(pid > 0){
    //         // 父进程
    //         sleep(1);
    //     }
    //     else{
    //         std::cout << "Fork failure" << std::endl;
    //         exit(EXIT_FAILURE);
    //     }
    // }
    // std::cout << cfgFile << ": " << start_port << std::endl;
    // try{
        auto server = new raft::kvServer(me, 500, cfgFile);
    //     pause();
    // }
    // catch(std::exception e){
    //     std::cout << e.what();
    // }
    pause();
    return 0;
}