#pragma once
#include <mutex>
#include <string>
#include <vector>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/vector.hpp>
#include <google/protobuf/service.h>
#include <muduo/base/Logging.h>

#include "utils.h"
#include "cort/ioman.h"

#include "raftHelper.h"
#include "persister.h"
#include "applyMessage.h"
#include "../config.h"
#include "../rpcProto/raftBase.pb.h"


/*
sendAppendEntries   退回follower后没有持久化
_L_requestVote      退回follower后没有持久化
*/

namespace raft{

namespace protobuf = google::protobuf;

class raft: public raftBase{
public:
    enum identity{FOLLOWER, CANDIDATE, LEADER};
    enum matchLogResult{WRONG_INDEX = -2,MISMATCH = -1, MATCH = 0};

    static const int invalidUpdateNextIndex;
    static const std::map<identity, const char *> identityStr;

    raft(
        int me,
        std::shared_ptr<persister> pstr,
        std::vector<std::shared_ptr<raftHelper>> helpers,
        std::shared_ptr<safequeue<applyMessage>> applyCh,
        size_t readThreadNum
    );

    // 传出当前任期及此节点是否为leader
    std::pair<int, bool> getState(){
        guard_type guard(_M_mutex);
        return {_M_term, LEADER == _M_identity};
    }
    // 获取最近一次持久化的状态文件的大小
    int getRaftStateSize() const{ return _M_persister->loadStateSize(); }
    // 将日志的index转换为logs的下标
    // int raft::getSlicesIndexFromLogIndex(int index){
    //     int lastLogIndex = _M_getLastLogIndex();
    //     if(!_M_valideIndex(_M_lastSnapshotIndex+1, index, lastLogIndex)){
    //         DPrint(stdout, 
    //             "Node_%d(%s) occurs that argument index(%d) is not between last_snapshot_index(%d)+1 and last_log_index(%d)",
    //             _M_me, identityStr.at(_M_identity), _M_lastSnapshotIndex, lastLogIndex
    //         );
    //         return -2;
    //     }
    //     return _M_getSlicesIndexFromLogIndex(index);
    // }

    // 丢弃index及之前的日志，持久化更新后的状态+保存上层应用快照(锁)
    bool persist(int index, const std::string &shot){
        guard_type guard(_M_mutex);
        if(!_M_valideIndex(_M_lastSnapshotIndex+1, index, _M_commitIndex)){
            LOG_WARN << "Node_" << _M_me << "(" << identityStr.at(_M_identity) << ")"
                << " refuses to execute persistence because arugment index(" << index
                << ") is not between last_snapshot_index(" << _M_lastSnapshotIndex
                << ")+1 and commit_index(" << _M_commitIndex << ")";
            return false;
        }

        // 上层引用拍摄快照时最新一条日志的索引及其所在任期，并丢弃此前日志
        int lastPersistedSlicesIndex = _M_getSlicesIndexFromLogIndex(index);
        _M_lastSnapshotIndex = index;
        _M_lastSnapshotTerm = _M_logs[lastPersistedSlicesIndex].term();

        if(_M_logs.size() - 1 == lastPersistedSlicesIndex){
            _M_logs.clear(); // _M_logs全部已被应用
        }
        else{
            // _M_logs前半部分已被应用
            _M_logs.erase(_M_logs.begin(), _M_logs.begin()+lastPersistedSlicesIndex+1);
        }

        // raft与上层应用同步
        if(index > _M_commitIndex){ _M_commitIndex = index; }
        if(index > _M_appliedIndex){ _M_appliedIndex = index; }

        _M_persister->persist(_M_dump(), shot);

        LOG_INFO << "Node_" << _M_me << "(" << identityStr.at(_M_identity) << ")"
            << " successfully execute persistence (last_snapshot_index = " << _M_lastSnapshotIndex
            << ", last_snapshot_term = " << _M_lastSnapshotTerm
            << ", logs_retain_size = " << _M_logs.size() << ")";
        return true;
        
    }

    std::pair<int, int> execute(op opt);            // leader对外接口，将命令写入集群(锁)
    template<typename F, typename ...Args>
    void executeGet(F&& f, Args&& ...args){
        _M_readThreads->push(std::move(f), std::forward<Args>(args)...);
    }                                               // 对外接口，放入线程池执行读请求

    bool waitApplied();                             // 对外接口，等待日志应用到指定index后，或超时返回(锁)

    // 远程调用接口，向该节点推送日志
    void appendEntries(
        protobuf::RpcController *ctrl,
        const appendEntriesArgs *args,
        appendEntriesReply *reply,
        protobuf::Closure *done
    ) override { _L_appendEntries(args, reply); done->Run(); }
    // 远程调用接口，向该节点征集选票
    void requestVote(
        protobuf::RpcController *ctrl,
        const requestVoteArgs *args,
        requestVoteReply *reply,
        protobuf::Closure *done
    ) override{ _L_requestVote(args, reply); done->Run(); }
    // 远程调用接口，将快照安装在该节点
    void installSnapshot(
        protobuf::RpcController *ctrl,
        const installSnapshotArgs *args,
        installSnapshotReply *reply,
        protobuf::Closure *done
    ) override{ _L_installSnapshot(args, reply); done->Run(); }

protected:
    // 持久化内容包括：当前任期、投票信息、日志、上层引用拍摄快照时最新一条日志的索引及其所在任期
    class persistNode{
    public:
        friend class boost::serialization::access;

        std::vector<std::string>                logs;
        int                                     term;
        int                                     vote4;
        int                                     lastSnapshotIndex;
        int                                     lastSnapshotTerm;

        static std::string serializeAsString_fn(const logEntry &entry){return entry.SerializeAsString();}

        template <typename _Archive>
        void serialize(_Archive &arc, const unsigned int version){
            arc &logs;
            arc &term;
            arc &vote4;
            arc &lastSnapshotIndex;
            arc &lastSnapshotTerm;
        }
    };

private:
    // 判断日志索引在有效范围内
    static bool _M_valideIndex(int min, int index, int max)
    { return min <= index && index <= max; }
    // 获取发起选举前的随机等待时间
    static std::chrono::milliseconds _M_randomWaitTimeBeforeElection(){
        static std::random_device dev;
        static std::mt19937 rng(dev());
        static std::uniform_int_distribution<int> dist(
            RAFT_MIN_WAIT_TIME_BEFORE_ELECTION,
            RAFT_MAX_WAIT_TIME_BEFORE_ELECTION
        );
        return std::chrono::milliseconds(dist(rng));
    }
    
    // 判断是否接受投票请求
    bool _M_receiveVote(int index, int term){
        // 获取最新一条日志的索引及所在任期
        int lastLogIndex = _M_getLastLogIndex();
        int lastLogTerm = _M_getLastLogTerm();
        // 要求任期比自己的更新，或同一任期下拥有更新的日志
        return term > lastLogTerm || (term == lastLogTerm && index >= lastLogIndex);
    }

    // std::pair<int, int> _M_getLastLogIndexAndTerm();           // 传出最新一条[已持久化的]日志的索引及所在任期
    // 获取最新一条[已持久化的]日志的索引
    int _M_getLastLogIndex()
    { return _M_logs.empty() ? _M_lastSnapshotIndex : _M_logs[_M_logs.size() - 1].index(); }
    // 获取最新一条[已持久化的]日志所在任期
    int _M_getLastLogTerm()
    { return _M_logs.empty() ? _M_lastSnapshotTerm : _M_logs[_M_logs.size() - 1].term(); }
    // 验证logs中索引为index的日志的term
    raft::matchLogResult _M_matchLogTerm(int index, int term){
        const int lastTerm = _M_getLogTerm(index);
        if(lastTerm == WRONG_INDEX){ return WRONG_INDEX; }
        return (lastTerm == term) ? MATCH : MISMATCH;
    }

    // 使用persistNode持久化当前状态
    void _M_persist(){ auto data = _M_dump(); _M_persister->persist(data); }
    // 将序列化字符串反序列化为持久化节点，并加载为当前状态
    void _M_load(const std::string &data){
        if(data.empty()) return;
        std::istringstream iss(data);
        boost::archive::text_iarchive ia(iss);
        persistNode node;
        ia >> node;

        _M_term = node.term;
        _M_vote4 = node.vote4;
        _M_lastSnapshotIndex = node.lastSnapshotIndex;
        _M_lastSnapshotTerm = node.lastSnapshotTerm;
        _M_logs.clear();
        _M_logs.resize(node.logs.size());
        for(int i = 0; i < _M_logs.size(); ++i){
            _M_logs[i].ParseFromString(node.logs[i]);
        }
    }
    // 将当前状态封装为持久化节点，并导出为序列化字符串
    std::string _M_dump(){
        persistNode node;
        node.term = _M_term;
        node.vote4 = _M_vote4;
        node.lastSnapshotIndex = _M_lastSnapshotIndex;
        node.lastSnapshotTerm = _M_lastSnapshotTerm;
        node.logs.resize(_M_logs.size());
        std::transform(_M_logs.begin(), _M_logs.end(), node.logs.begin(), &persistNode::serializeAsString_fn);

        std::ostringstream oss;
        boost::archive::text_oarchive oa(oss);
        oa << node;
        return oss.str();
    }

    // 转变身份
    void _M_identityChange(identity iden, int term, int vote){
        _M_identity = iden;
        _M_term = term;
        _M_vote4 = vote;
        _M_persist();
    }
    // 传出leader已向该节点发送的日志的索引和所在任期
    std::pair<int, int> _M_getPreviousLogInfo(int server){
        // leader应向该节点发送的下一条日志的索引 位于logs队首
        if(_M_nextIndex[server] == _M_lastSnapshotIndex + 1){
            LOG_INFO << "nextIndex[" << server << "] = " << _M_nextIndex[server] << " (if)";
            return {_M_lastSnapshotIndex, _M_lastSnapshotTerm};
        }
        else{
            int index = _M_nextIndex[server] - 1;
            LOG_INFO << "nextIndex[" << server << "] = " << _M_nextIndex[server] << " (else)";
            return {index, _M_logs[_M_getSlicesIndexFromLogIndex(index)].term()};
        }
    }          
    // 将日志的index转换为logs的有效下标
    int _M_getSlicesIndexFromLogIndex(int index){ return index - _M_lastSnapshotIndex - 1; }
    // 获取logs中索引为index的日志的term
    int _M_getLogTerm(int index){
        if(index == _M_lastSnapshotIndex) return _M_lastSnapshotTerm;

        int lastLogIndex = _M_getLastLogIndex();
        if(!_M_valideIndex(_M_lastSnapshotIndex, index, lastLogIndex)){
            DPrint(stdout, 
                "Node_%d(%s) occurs that argument index(%d) is not between last_snapshot_index(%d)+1 and last_log_index(%d)",
                _M_me, identityStr.at(_M_identity), _M_lastSnapshotIndex, lastLogIndex
            );
            return -2;
        }

        return _M_logs[_M_getSlicesIndexFromLogIndex(index)].term();
    }
    // 获取新日志的索引
    int _M_getNewCommandIndex(){ return _M_getLastLogIndex() + 1; }

    // 将已提交的命令推入上层命令管道
    void _M_pushMessage(const applyMessage msg){ _M_applyChan->push(msg); }
    std::vector<applyMessage> _M_getApplyMessages();                    // 传出已提交未应用日志中的命令

    void _L_applierTicker();                // 持续将已提交未应用的日志中的命令加入上层命令管道(锁)
    void _L_daemonTicker();                 // 守护线程，leader持续向其它节点发送心跳，非leader持续等待leader的心跳(锁)

    void _L_heartBeat();                    // leader向其它节点发送一次心跳(锁)
    void _L_stand4Election();               // 非leader发起选举(锁)

    void _M_leaderUpdateCommitIndex();      // leader推进提交进度
    bool _L_sendAppendEntries(                         // leader向其它节点推送日志，返回值仅表示远程调用是否成功(锁)
        int server,
        std::shared_ptr<appendEntriesArgs> args,
        std::shared_ptr<appendEntriesReply> reply, 
        std::shared_ptr<int> appendNum
    );
    bool _L_sendRequestVote(                           // candidate向其它节点征集选票，返回值仅表示远程调用是否成功(锁)
        int server,
        std::shared_ptr<requestVoteArgs> args,
        std::shared_ptr<requestVoteReply> reply, 
        std::shared_ptr<int> voteNum
    );
    void _L_sendSnapshot(int server);                  // leader向节点发送快照，仅当leader应向其发送的下一条日志已被丢弃时使用(锁)

    void _L_appendEntries(const appendEntriesArgs *args, appendEntriesReply *reply);        // appendEntries接口实际向本节点推送日志的实现(锁)
    void _L_requestVote(const requestVoteArgs *args, requestVoteReply *reply);              // requestVote接口实际执行征集本节点选票的实现(锁)
    void _L_installSnapshot(const installSnapshotArgs *args, installSnapshotReply *reply);  // installSnapshot接口实际向本节点安装快照的实现(锁)

protected:
    using guard_type = std::lock_guard<std::mutex>;

    std::mutex   _M_mutex;

    std::vector<std::shared_ptr<raftHelper>>    _M_helpers; // RPC工具
    std::deque<logEntry>                        _M_logs;    // 日志(必须持久化变量)

    /*   
    |   👈 lastSnapshotIndex，此条及以前的日志已被logs丢弃，命令早已被持久化到上层应用，并拍摄快照
    |   👈 logs.begin()(=lastSnapshotIndex+1)
    ...
    |   👈 appliedIndex                     // 已经被leader的上层执行的日志的索引
    ...                                     // 这段日志中的命令被放入_M_applyChan中等待上层执行
    |   👈 commitIndex                      // 已经提交的日志的索引，超过一半的节点都已经成功复制此条及以前的日志
    ...
    |   👈 matchIndex[i]                    // leader掌握的该节点已成功复制的日志的索引，至少一半节点满足matchIndex[i] >= commitIndex
    |   👈 nextIndex[i](=matchIndex[i]+1)   // leader将会向该节点发送的日志的索引
    ...
    |   👈 logs.end()
    */
    std::vector<int>     _M_nextIndex;      // leader应向其它节点发送的下一条日志的索引
    std::vector<int>     _M_matchIndex;     // leader掌握的其它节点已成功复制的最新日志的索引

    std::chrono::_V2::system_clock::time_point  _M_lastResetElectionTime;   // 最新一次重置选举的时间，当收到leader时设置该值
    std::chrono::_V2::system_clock::time_point  _M_lastResetHeartBeatTime;  // 最新一次重置心跳的时间，当发送心跳时设置该值

    std::unique_ptr<cort::ioman>                _M_ioman = nullptr;         // 协程IO调度工具
    std::shared_ptr<persister>                  _M_persister = nullptr;     // 持久化工具
    std::shared_ptr<safequeue<applyMessage>>    _M_applyChan = nullptr;     // 上层命令管道

    int      _M_commitIndex;    // 最新已提交日志索引
    int      _M_appliedIndex;   // 最新被应用日志索引
    int  _M_lastSnapshotIndex;  // 上层引用拍摄快照时最新一条日志的索引
    int  _M_lastSnapshotTerm;   // _M_lastSnapshotIndex对应所在的任期

    // 自身信息
    int         _M_me;          // 节点id
    int         _M_term;        // 当前任期(必须持久化变量)
    int         _M_vote4;       // 投票信息(必须持久化变量)
    identity    _M_identity;    // 当前身份

    std::shared_ptr<threadPool> _M_readThreads = nullptr;       // 读请求线程池
    std::condition_variable     _M_readCondtion;                    // 读请求条件变量
};

    
} // namespace raft
