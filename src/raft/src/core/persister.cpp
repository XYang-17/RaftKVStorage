#include "utils.h"

#include "core/persister.h"

// 文件损坏问题？
// 文件读写失败？
namespace raft{

persister::persister(unsigned int me):
    _M_stateFile(RAFT_FILE_DIR + ("state_" + std::to_string(me) + "_persist.txt")),
    _M_snapshotFile(RAFT_FILE_DIR + ("snapshot_" + std::to_string(me) + "_persist.txt")),
    _M_stateFileSize(0)
{
    std::ofstream ofs(_M_stateFile);
    if(!ofs.is_open()){
        DPrint(stdout, "Failed to open state file from '%s'", _M_stateFile.c_str());
    }
    ofs.close();
    ofs.open(_M_snapshotFile);
    if(!ofs.is_open()){
        DPrint(stdout, "Failed to open state file from '%s'", _M_snapshotFile.c_str());
    }
    ofs.close();
}

persister::~persister(){
    _M_stateFileSize = 0;
}


bool persister::loadSnapshot(std::string &snapshot){
    std::ifstream ifs;
    DEFER{ if(ifs.is_open()){ ifs.close(); } };

    std::lock_guard<std::mutex> lock(_M_mutex);
    ifs.open(_M_snapshotFile, std::ios::in);
    if(ifs.good()){ ifs >> snapshot; }
    return ifs.good();
}

bool persister::loadState(std::string &state){
    std::ifstream ifs;
    DEFER{ if(ifs.is_open()){ ifs.close(); } };

    std::lock_guard<std::mutex> lock(_M_mutex);
    ifs.open(_M_stateFile, std::ios::in);
    if(ifs.good()){ ifs >> state; }
    return ifs.good();
}


void persister::_M_persistState(const std::string &state){
    std::ofstream ofs;
    DEFER{ if(ofs.is_open()){ ofs.close(); } };
    ofs.open(_M_stateFile, std::ios::trunc);
    ofs << state;
    _M_stateFileSize = state.size();
}

void persister::_M_persistSnapshot(const std::string &snapshot){
    std::ofstream ofs;
    DEFER{ if(ofs.is_open()){ ofs.close(); } };
    ofs.open(_M_snapshotFile, std::ios::trunc);
    ofs << snapshot;
}
    
} // namespace raft