#pragma once
#include <mutex>
#include <fstream>

namespace raft{

#ifndef RAFT_FILE_DIR
#define RAFT_FILE_DIR ""
#endif

class persister{
public:
    explicit persister(unsigned int me);
    ~persister();

    void persist(const std::string &state){
        std::lock_guard<std::mutex> lock(_M_mutex);
        _M_persistState(state);
    }
    void persist(const std::string &state, const std::string &snapshot){
        std::lock_guard<std::mutex> lock(_M_mutex);
        _M_persistState(state);
        _M_persistSnapshot(snapshot);
    }

    long long loadStateSize() const{return _M_stateFileSize;}
    bool loadSnapshot(std::string &snapshot);
    bool loadState(std::string &state);

private:
    void _M_persistState(const std::string &state);
    void _M_persistSnapshot(const std::string &snapshot);

protected:
    std::mutex          _M_mutex;
    size_t              _M_stateFileSize; // 记录最近一次保存的状态文件的大小
    const std::string   _M_stateFile;     // 状态文件路径
    const std::string   _M_snapshotFile;  // 快照文件路径
};
    
} // namespace raft
