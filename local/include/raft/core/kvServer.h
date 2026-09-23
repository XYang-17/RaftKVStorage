#pragma once
#include <mutex>
#include <unordered_map>
#include <boost/serialization/unordered_map.hpp>

#include "kvSkipList.h"

#include "raft.h"
#include "../rpcProto/kvServerBase.pb.h"

namespace raft{

namespace protobuf = google::protobuf;

class kvServer: kvServerBase{
public:
    kvServer(int me, int maxRaftStateSize, const std::string &file);

    void get(                               // 远程调用接口，获取数据
        protobuf::RpcController *ctrl,
        const getArgs *args,
        getReply *reply,
        protobuf::Closure *done
    ) override;
    void put(                               // 远程调用接口，存入数据
        protobuf::RpcController *ctrl,
        const putArgs *args,
        putReply *reply,
        protobuf::Closure *done
    ) override;


private:
    friend class boost::serialization::access;

    kvServer() = delete;

    template <typename _Archive>
    void serialize(_Archive &arc, const unsigned int version){
        if constexpr(_Archive::is_saving::value){
            std::string str = _M_data.dump();
            arc &str;
        }
        else{
            std::string str;
            arc &str;
            _M_data.load(str);
        }
        arc &_M_lastRequestId;
    }

    std::string _M_dump(){
        std::ostringstream oss;
        boost::archive::text_oarchive oa(oss);
        oa << *this;
        return oss.str();
    }
    void _M_load(const std::string &str){
        if(str.empty()) return;
        std::istringstream iss(str);
        boost::archive::text_iarchive ia(iss);
        ia >> *this;
    }

    void _L_get(const getArgs *args, getReply *reply);             // get接口实际获取数据的实现(锁)
    void _L_put(const putArgs *args, putReply *reply);             // put接口实际存入数据的实现(锁)

    void _L_popMessageLoop();                       // 持续从命令管道中取出命令，并执行(锁)

    bool _L_duplicateRequest(const std::string &client, int requestId); // 检查该请求是否已经被执行(锁)

    void _L_executeCommand(applyMessage msg);    // 执行管道中取出的命令(锁)
    void _L_installSnapshot(applyMessage msg);   // 安装管道中得取出的快照(锁)

    void _L_executePutOp(op command);                           // 将数据加入跳表(锁)
    std::pair<bool, std::string> _L_executeGetOp(op command);   // 从跳表获取数据(锁)
    void _L_maySnapshot(int index);                             // 可能拍摄快照，并持久化底层raft节点状态
    
    bool _L_tryPush2Wait(op command, int index); // 尝试将命令推入命令执行完毕管道

protected:
    using guard_type = std::lock_guard<std::mutex>;

    std::mutex      _M_mutex;
    
    /*
    get/put —> create —————————————————————————————————————————————————————> pop —> delete —> reply
       |                                                          
       |                           _M_applyChan —> apply —> _M_appliedChan
       |                                 ↑
    ---+---------------------------------+----------------------
       ↓                                 |
    raftNode —> consensus —> commit —> apply
    */
    KVSkipList::kvskiplist<std::string, std::string, 32>    _M_data;            // 数据表
    std::unordered_map<std::string, int>                    _M_lastRequestId;   // 每个用户已执行的最新请求的Id
    std::unordered_map<int, safequeue<op> *>                _M_appliedChan;     // 命令执行完毕管道

    std::shared_ptr<raft>                       _M_raftNode;    // 底层raft节点
    std::shared_ptr<safequeue<applyMessage>>    _M_applyChan;   // 命令管道

    int          _M_me;                     // raft节点id
    int _M_maxRaftStateSize;                // raft节点状态文件大小最大值
    int _M_lastSnapshotCommandIndex = 0;    // 最新安装的快照的最新命令的索引

};
    
} // namespace raft
