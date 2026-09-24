#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <exception>
#include <muduo/base/Logging.h>

#include "config.h"
#include "core/raft.h"

namespace raft{

    
raft::raft(
    int me,
    std::shared_ptr<persister> pstr,
    std::vector<std::shared_ptr<raftHelper>> helpers,
    std::shared_ptr<safequeue<applyMessage>> applyCh,
    size_t readThreadNum
):
    _M_helpers(helpers),
    _M_lastResetElectionTime(now()),
    _M_lastResetHeartBeatTime(_M_lastResetElectionTime),
    _M_persister(pstr),
    _M_applyChan(applyCh),
    _M_commitIndex(0),
    _M_appliedIndex(0),
    _M_lastSnapshotIndex(0),
    _M_lastSnapshotTerm(0),
    _M_me(me),
    _M_term(0),
    _M_vote4(-1),
    _M_identity(FOLLOWER)
{
    _M_nextIndex.assign(_M_helpers.size(), 0);
    _M_matchIndex.assign(_M_helpers.size(), 0);

    std::string state;
    _M_persister->loadState(state); // 从文件中读取序列化状态字符串
    _M_load(state);
    if(_M_lastSnapshotIndex > 0){
        _M_appliedIndex = _M_lastSnapshotIndex;
    }

    _M_readThreads.reset(new threadPool(readThreadNum));
    _M_readThreads->run(); // 启动读线程池

    // 初始化IO管理工具，多协程执行心跳和选举
    // _M_ioman.reset(new cort::ioman(RAFT_COROUTINE_THREAD_NUM, RAFT_COROUTINE_AS_WORKER));
    // _M_ioman->addTask([this](){this->_L_electionTimeoutTicker();});
    // _M_ioman->addTask([this](){this->_L_leaderHeartBeatTicker();});
    // std::thread ticker([this](){this->_L_electionTimeoutTicker();});
    // ticker.detach();

    std::thread daemonTicker(&raft::_L_daemonTicker, this);
    daemonTicker.detach();

    std::thread t(&raft::_L_applierTicker, this);
    t.detach();
}


std::pair<int, int> raft::execute(op opt){
    guard_type guard(_M_mutex);
    // leader
    if(LEADER == _M_identity){
        int new_index = _M_getNewCommandIndex();
        _M_logs.emplace_back();
        auto &entry = _M_logs.back();
        entry.set_command(opt.dump());
        entry.set_term(_M_term);
        entry.set_index(new_index);
        _M_persist(); // 立刻持久化状态
        LOG_INFO << "logs_size = " << _M_logs.size();
        
        return {entry.index(), entry.term()};
    }

    return {-1, -1};
}

bool raft::waitApplied(){
    std::unique_lock<std::mutex> lock(_M_mutex);
    int wait4 = _M_commitIndex;
    _M_readCondtion.wait_for(
        lock, std::chrono::microseconds(RAFT_MAX_WAIT_TIME_FOR_READ),
        [this, &wait4]{ return this->_M_appliedIndex >= wait4; }
    );
    return _M_appliedIndex >= wait4;
}


std::vector<applyMessage> raft::_M_getApplyMessages(){
    LOG_ERROR << "commit_index = " << _M_commitIndex
        << ", lastLogIndex = " << _M_getLastLogIndex()
        << ", logsize = " << _M_logs.size()
        << ", snapshotIndex = " << _M_lastSnapshotIndex;
    // 已提交日志的索引 不能超过 logs中的最新日志的索引
    if(_M_commitIndex > _M_getLastLogIndex()){
        throw std::runtime_error(__FILE__+std::to_string(__LINE__));
    }

    if(_M_appliedIndex > _M_commitIndex){
        throw std::runtime_error(__FILE__+std::to_string(__LINE__));
    }

    // 将已提交未应用的日志的命令写入外部数组
    std::vector<applyMessage> applyMsgs;
    int tempSlicesIndex = -1;
    
    while(_M_appliedIndex < _M_commitIndex){
        ++_M_appliedIndex;
        if(_M_logs[(tempSlicesIndex = _M_getSlicesIndexFromLogIndex(_M_appliedIndex))].index() != _M_appliedIndex){}

        applyMsgs.emplace_back(true, _M_appliedIndex, _M_logs[tempSlicesIndex].command());
    }
    return applyMsgs;
}


void raft::_L_applierTicker(){
    std::unique_lock<std::mutex> lock(_M_mutex, std::defer_lock);
    while(true){
        lock.lock();
        auto applyMsgs = _M_getApplyMessages(); // 锁内获取未应用命令，推进应用进度
        lock.unlock();

        if(!applyMsgs.empty()){
            // 锁外将命令推入上层命令管道
            for(auto &msg: applyMsgs){
                _M_applyChan->push(msg);
            }
        }
        sleep_ms(RAFT_APPLY_INTERVAL);
    }
}

void raft::_L_daemonTicker(){
    while(true){
        // leaderHeartBeat
        while(LEADER == _M_identity){
            // while(LEADER != _M_identity){
            //     usleep(RAFT_HEARTBEAT_SLEEP_TIME * 1000);
            // }

            std::chrono::duration<signed long int, std::ratio<1, 1'000'000'000>> sleepTime;
            std::chrono::system_clock::time_point timeNow;
            {
                guard_type guard(_M_mutex);
                timeNow = now();
                sleepTime = _M_lastResetHeartBeatTime + std::chrono::milliseconds(RAFT_HEARTBEAT_SLEEP_TIME) - timeNow;
            }

            // 等待到发送心跳
            if(std::chrono::duration<double, std::milli>(sleepTime).count() > 1){
                auto start = std::chrono::steady_clock::now();
                usleep(std::chrono::duration_cast<std::chrono::microseconds>(sleepTime).count());
            }
            
            // 等待期间定时器被重置，通过其它手段发送了心跳，则重新等到下一次需要发送心跳的时间，避免频繁发送
            if(std::chrono::duration<double , std::milli>(_M_lastResetHeartBeatTime - timeNow).count() > 0){
                continue;
            }

            _L_heartBeat();
        }

        // electionTimeout
        while(LEADER != _M_identity){
            // while(LEADER == _M_identity){
            //     usleep(RAFT_HEARTBEAT_SLEEP_TIME);
            // }

            std::chrono::duration<signed long int, std::ratio<1, 1'000'000'000>> sleepTime;
            std::chrono::system_clock::time_point timeNow;
            {
                guard_type guard(_M_mutex);
                timeNow = now();
                sleepTime = _M_lastResetElectionTime + _M_randomWaitTimeBeforeElection() - timeNow;
                /*
                |
                |
                |   👈  _M_lastResetElectionTime        ----    ----
                |                                                ↑
                |                                                |
                |   👈 now                              ----    random wait time
                |                                        ↑       |
                |                                     sleepTime  |
                |                                        ↓       ↓
                |   👈 stand for lection                ----    ----
                |
                ↓
                time
                */
            }

            // 等待到发起选举
            if(std::chrono::duration<double, std::milli>(sleepTime).count() > 1){
                auto start = std::chrono::steady_clock::now();
                usleep(std::chrono::duration_cast<std::chrono::microseconds>(sleepTime).count());
            }
            
            // 等待期间定时器被重置，收到leader的心跳，则不能发起选举，避免无意义的选举
            if(std::chrono::duration<double , std::milli>(_M_lastResetElectionTime - timeNow).count() > 0){
                continue;
            }
            
            _L_stand4Election(); // 干票大的！！！
        }
    }
}

// void raft::_L_electionTimeoutTicker(){
//     while(LEADER != _M_identity){
//         // while(LEADER == _M_identity){
//         //     usleep(RAFT_HEARTBEAT_SLEEP_TIME);
//         // }

//         std::chrono::duration<signed long int, std::ratio<1, 1'000'000'000>> sleepTime;
//         std::chrono::system_clock::time_point timeNow;
//         {
//             guard_type guard(_M_mutex);
//             timeNow = now();
//             sleepTime = _M_lastResetElectionTime + _M_randomWaitTimeBeforeElection() - timeNow;
//             /*
//             |
//             |
//             |   👈  _M_lastResetElectionTime        ----    ----
//             |                                                ↑
//             |                                                |
//             |   👈 now                              ----    random wait time
//             |                                        ↑       |
//             |                                     sleepTime  |
//             |                                        ↓       ↓
//             |   👈 stand for lection                ----    ----
//             |
//             ↓
//             time
//             */
//         }

//         // 等待到发起选举
//         if(std::chrono::duration<double, std::milli>(sleepTime).count() > 1){
//             auto start = std::chrono::steady_clock::now();
//             usleep(std::chrono::duration_cast<std::chrono::microseconds>(sleepTime).count());
//         }
        
//         // 等待期间定时器被重置，收到leader的心跳，则不能发起选举，避免无意义的选举
//         if(std::chrono::duration<double , std::milli>(_M_lastResetElectionTime - timeNow).count() > 0){
//             continue;
//         }
        
//         _L_stand4Election(); // 干票大的！！！
//     }
//     std::thread heartBeatTicker(&raft::_L_leaderHeartBeatTicker, this);
//     heartBeatTicker.detach();
// }


void raft::_L_heartBeat(){
    guard_type guard(_M_mutex);
    if(LEADER == _M_identity){
        auto appendNum = std::make_shared<int>(1);

        // 向其它节点发送心跳
        for(int i = 0; i < _M_helpers.size(); ++i){
            if(i == _M_me) continue;

            // 需要发送的日志已被丢弃，直接将上层应用快照发送给对方
            if(_M_nextIndex[i] <= _M_lastSnapshotIndex){
                std::thread t(&raft::_L_sendSnapshot, this, i);
                t.detach();
                continue;
            }

            auto [prevLogIndex, prevLogTerm] = _M_getPreviousLogInfo(i);

            auto args = std::make_shared<appendEntriesArgs>();
            args->set_term(_M_term);
            args->set_leader_id(_M_me);
            args->set_prev_log_index(prevLogIndex);
            args->set_prev_log_term(prevLogTerm);
            args->clear_entries();
            args->set_leader_commit(_M_commitIndex);

            // 将 leader已向该节点发送的日志 后的日志全部写入参数中
            if(prevLogIndex == _M_lastSnapshotIndex){
                for(const auto &log: _M_logs){ *(args->add_entries()) = log; }
            }
            else{
                for(int j = _M_getSlicesIndexFromLogIndex(prevLogIndex) + 1; j < _M_logs.size(); ++j){
                    *(args->add_entries()) = _M_logs[j];
                }
            }
            
            const auto reply = std::make_shared<appendEntriesReply>();
            reply->set_state(DISCONNECTED); // 对方应该将该值改为CONNECTED

            std::thread t(&raft::_L_sendAppendEntries, this, i, args, reply, appendNum); 
            t.detach();
        }
        _M_lastResetHeartBeatTime = now(); // 更新最新一次重置心跳的时间
    }
}

void raft::_L_stand4Election(){
    guard_type guard(_M_mutex);
    if(LEADER != _M_identity){

        // 成为candidate，将自己的term加1,并给自己投票
        _M_identityChange(CANDIDATE, _M_term + 1, _M_me);

        // 向其它节点征集选票
        std::shared_ptr<int> voteNum = std::make_shared<int>(1);
        _M_lastResetElectionTime = now();
        for(int i = 0; i < _M_helpers.size(); ++i){
            if(i == _M_me) continue;
            
            int lastLogIndex = _M_getLastLogIndex();
            int lastLogTerm = _M_getLastLogTerm();

            // 封装选举需要的信息
            auto args = std::make_shared<requestVoteArgs>();
            args->set_term(_M_term);
            args->set_candidate_id(_M_me);
            args->set_last_log_index(lastLogIndex);
            args->set_last_log_term(lastLogTerm);

            auto reply = std::make_shared<requestVoteReply>();
            std::thread t(&raft::_L_sendRequestVote, this, i, args, reply, voteNum);
            t.detach();
        }
    }
}


bool raft::_L_sendAppendEntries(
    int server,
    std::shared_ptr<appendEntriesArgs> args,
    std::shared_ptr<appendEntriesReply> reply, 
    std::shared_ptr<int> appendNum)
{
    LOG_INFO << "HeartBeat to node_" << server << " with "
        << "prev_log_index = " << args->prev_log_index() << ", "
        << "leader_commit = " << args->leader_commit() << ", "
        << "entries size = " << args->entries().size() << ", "
        << "log_size = " << _M_logs.size();
        
    // 远程调用对方的方法，推送日志
    // 操作失败，直接返回，交由上层处理
    if(!_M_helpers[server]->appendEntries(args.get(), reply.get())){
        return false;
    }
    // 网络状态异常，直接返回，交由上层处理
    if(DISCONNECTED == reply->state()){ return true; }

    guard_type guard(_M_mutex);
    // 对方回复了一个更新的任期，当前节点自动退为follower
    if(reply->term() > _M_term){
        _M_identityChange(FOLLOWER, reply->term(), -1);
        _M_lastResetElectionTime = now();
        return true;
    }
    // 回复已经过期，或此刻当前节点已不是leader，直接返回
    // 之前的任期结束，不代表当前身份不是leader
    if(reply->term() < _M_term || LEADER != _M_identity){ return true; }

    // 当前节点仍是leader
    // 推送的不是对方需要的下一条期望收到的日志，对方回复失败
    if(!reply->success()){
        // 根据对方回复的下一条期望收到的日志的索引 更新自己掌握的信息
        if(invalidUpdateNextIndex != reply->update_next_index()){
            // LOG_INFO << "_M_nextIndex[" << server << "] = " << _M_nextIndex[server];
            _M_nextIndex[server] = reply->update_next_index();
            // LOG_INFO << "_M_nextIndex[" << server << "] = " << _M_nextIndex[server];
        }
        return true;
    }

    // 推送成功
    ++(*appendNum); // 推送节点数+1
    LOG_INFO << "append success: " << *appendNum;
    // 更新自己掌握的信息，args中包含此次推送前匹配的索引，和此次推送的日志条数
    _M_matchIndex[server] = std::max(_M_matchIndex[server], args->prev_log_index() + args->entries_size());
    // LOG_INFO << "_M_nextIndex[" << server << "] = " << _M_nextIndex[server];
    _M_nextIndex[server] = _M_matchIndex[server] + 1;
    // LOG_INFO << "_M_nextIndex[" << server << "] = " << _M_nextIndex[server];

    int lastLogIndex = _M_getLastLogIndex();
    // leader应向其它节点发送的下一条日志的索引 不可能会超过 自己的logs中最新一条日志的下一个索引
    if(_M_nextIndex[server] > lastLogIndex + 1){
        throw std::runtime_error(std::to_string(__LINE__));
    }
    
    // 超过一半的节点已经成功接收推送
    if(*appendNum >= (_M_helpers.size() >> 1) + 1){
        LOG_INFO << "over half: " << *appendNum;
        *appendNum = 0; // 置0,不会出现第二次大多数，因而只会进入一次
        // 确认有效(推送非空，且任期没有发生变化)
        int tmp;
        if(args->entries_size() > 0
            && (tmp = args->entries(args->entries_size()-1).term()) == _M_term)
        {
            // 更新 最新已提交日志索引
            LOG_INFO << "_M_commitIndex = " << _M_commitIndex;
            const int oldCommitIndex = _M_commitIndex;
            _M_commitIndex = std::max(_M_commitIndex, args->prev_log_index() + args->entries_size());
            LOG_INFO << "_M_commitIndex = " << _M_commitIndex;
            
        }
        else{
            LOG_INFO << "no commit "
                << "args->entries_size() = " << args->entries_size() << ", "
                << "args->entries(args->entries_size()-1).term() = " << tmp << ", "
                << "_M_term = " << _M_term;
        }
    }
    
    // 最新已提交日志索引 不可能超过 自己的logs中最新一条日志的索引
    if(_M_commitIndex > lastLogIndex){
        throw std::runtime_error(std::to_string(__LINE__));
    }

    return true;
}

bool raft::_L_sendRequestVote(
    int server,
    std::shared_ptr<requestVoteArgs> args,
    std::shared_ptr<requestVoteReply> reply, 
    std::shared_ptr<int> voteNum)
{
    LOG_INFO << "Request vote from node_" << server << " with "
        << "term = " << _M_term
        << "lastLogIndex = " << args->last_log_index();

    // 远程调用对方的方法，征集选票
    // 操作失败，直接返回，交由上层处理
    if(!_M_helpers[server]->requestVote(args.get(), reply.get())){
        return false;
    }

    guard_type guard(_M_mutex);
    // 对方回复了一个更新的任期，当前节点自动退为follower
    if(reply->term() > _M_term){
        _M_identityChange(FOLLOWER, reply->term(), -1);
        return true;
    }
    // 回复已经过期，或对方拒绝了投票，直接返回
    // 回复的term比当前节点的term小(旧)，表示当前节点已经发现一个拥有更新任期的节点，自动退为follower
    if(reply->term() < _M_term || !reply->vote_granted())
    { return true; }
    
    // 对方同意投票
    ++(*voteNum); // 选票数+1
    // 当选票数超过一半，当前节点成为leader
    if(*voteNum >= (_M_helpers.size() >> 1) + 1){
        *voteNum = 0;
        if(LEADER == _M_identity) return true; // 已经得到足够多的选票成为leader
        _M_identity = LEADER;

        int lastLogIndex = _M_getLastLogIndex();
        // 设置 应向其它所有节点发送的下一条日志的索引 为 所掌握的最新一条[已持久化的]日志的索引+1
        std::string nextIndexData = "";
        // for(auto &n: _M_nextIndex){nextIndexData += std::to_string(n) + " "; }
        // LOG_INFO << "[" << nextIndexData << "]";
        _M_nextIndex.assign(_M_helpers.size(), lastLogIndex + 1);
        nextIndexData = "";
        // for(auto &n: _M_nextIndex){nextIndexData += std::to_string(n) + " "; }
        // LOG_INFO << "[" << nextIndexData << "]";

        // 设置 掌握的其它所有节点已成功复制的最新日志的索引
        _M_matchIndex.assign(_M_helpers.size(), 0);
        _M_matchIndex[_M_me] = lastLogIndex;

        std::string msg = format("Node_%i(term = %d, last_log_index = %d) won election", _M_me, _M_term, lastLogIndex);
        std::cout << msg << std::endl;
        LOG_INFO << msg;

        // 另起一个线程，向其它节点发送一次心跳(持有锁，等当前函数执行完毕才能够执行)
        // 同时，会传递最新已提交日志索引，让其它节点也更新提交
        std::thread t(&raft::_L_heartBeat, this);
        t.detach();

        _M_persist();
    }
    return true;
}

void raft::_L_sendSnapshot(int server){
    installSnapshotArgs args;
    {
        guard_type guard(_M_mutex);
        // 读取快照，并写入参数中
        std::string buf;
        _M_persister->loadSnapshot(buf);
        args.set_data(buf);
        // 当前leader及任期 写入参数中
        args.set_leader_id(_M_me);
        args.set_term(_M_term);
        // 上层应用拍摄快照时最新一条日志的索引及其所在任期 写入参数中
        args.set_last_index(_M_lastSnapshotIndex);
        args.set_last_term(_M_lastSnapshotTerm);
    }
    
    installSnapshotReply reply;
    // 远程调用，将快照安装到该节点
    // 失败，没救了
    if(!_M_helpers[server]->installSnapshot(&args, &reply)){ return; }

    guard_type guard(_M_mutex);
    // 该节点记录的任期已经大于自身的，自动退为follwer
    if(reply.term() > _M_term){
        _M_identityChange(FOLLOWER, reply.term(), -1);
        _M_lastResetElectionTime = now();   // 重置选举
        return;
    }
    // 此时身份可能已经改变，则不再执行后续操作
    if(LEADER != _M_identity || args.term() != _M_term){ return; }
    // 仍然为leader(至少当前自己这么认为)，更新掌握的信息
    _M_matchIndex[server] = args.last_index();
    // LOG_INFO << "_M_nextIndex[" << server << "] = " << _M_nextIndex[server];
    _M_nextIndex[server] = _M_matchIndex[server] + 1;
    // LOG_INFO << "_M_nextIndex[" << server << "] = " << _M_nextIndex[server];
}


void raft::_L_appendEntries(const appendEntriesArgs *args, appendEntriesReply *reply){
    guard_type guard(_M_mutex);
    reply->set_state(CONNECTED); // 确认网络状态

    // args的term比自己的小，已过期，拒绝，并回复最新任期
    if(args->term() < _M_term){
        reply->set_success(false);
        reply->set_term(_M_term);
        reply->set_update_next_index(invalidUpdateNextIndex); // leader及时更新
        LOG_INFO << "Node_" << _M_me << "(term = " << _M_term
            << ") refuses appending entries from "
            << "leader_node_" << args->leader_id() << "(term = " << args->term() << ") "
            << "because of expire term";
        return;
    }
    
    DEFER{_M_persist();}; // 结束后自动持久化

    // 跟随
    if(args->term() > _M_term){ _M_identityChange(FOLLOWER, args->term(), -1); }
    _M_identity = FOLLOWER; // 强制降为follower
    _M_lastResetElectionTime = now(); // 重置选举
    reply->set_term(_M_term);

    int tempLastLogIndex = -1;
    // leader认为的我已接收的最新一条日志 太新，传来的日志与自身的logs断开了，回复所需要的下一条日志的索引
    if(args->prev_log_index() > (tempLastLogIndex = _M_getLastLogIndex())){
        reply->set_success(false);
        reply->set_update_next_index(tempLastLogIndex + 1);
        LOG_INFO << "Node_" << _M_me << "(term = " << _M_term
            << ") refuses appending entries from "
            << "leader_node_" << args->leader_id() << "(term = " << args->term() << ") "
            << "because of too new logs";
        return;
    }
    // leader认为的我已接收的最新一条日志 太旧，传来的日志与自身的logs断开了，回复所需要的下一条日志的索引
    if(args->prev_log_index() < _M_lastSnapshotIndex){
        reply->set_success(false);
        reply->set_update_next_index(_M_lastSnapshotIndex + 1);
        LOG_INFO << "Node_" << _M_me << "(term = " << _M_term
            << ") refuses appending entries from "
            << "leader_node_" << args->leader_id() << "(term = " << args->term() << ") "
            << "because of too old logs";
        return;
    }

    // leader认为的我已接收的最新一条日志的任期 与 我的记录 不符
    // 我的记录与leader冲突，要求leader从该任期开始的位置重新发送
    if(MATCH != _M_matchLogTerm(args->prev_log_index(), args->prev_log_term())){
        // 将期望的日志 回退到 leader传过来的任期内的第一条日志
        // 实在已经找不到第一条日志的记录了，只能保持下一条期望收到的日志为 leader认为的我已接收的最新一条日志的任期
        // 即回退一步
        reply->set_update_next_index(args->prev_log_index());
        for(int index = args->prev_log_index(); index >= _M_lastSnapshotIndex; --index){
            if(_M_getLogTerm(index) != _M_getLogTerm(args->prev_log_index())){
                reply->set_update_next_index(index + 1);
                break;
            }
        }
        reply->set_success(false);
        LOG_INFO << "Node_" << _M_me << "(term = " << _M_term
            << ") refuses appending entries from "
            << "leader_node_" << args->leader_id() << "(term = " << args->term() << ") "
            << " and need rollback";
    }
    // 没有问题，更新logs
    else{
        // DEFER{_M_persist();}; // 结束后自动持久化
        for(int i = 0; i < args->entries_size(); ++i){
            auto log = args->entries(i);
            // 前面已经确定了传来的日志没有与自身的logs断开
            // 此条日志比logs中最新的还新，直接加到logs的尾部
            if(log.index() > _M_getLastLogIndex()){
                _M_logs.push_back(log); // 前面定义了DEFER，函数以任何方式结束都会持久化
                LOG_INFO << "Node_" << _M_me << "push a new log";
            }
            else{
                int slicesIndex = _M_getSlicesIndexFromLogIndex(log.index());
                if(_M_logs[slicesIndex].term() == log.term()){
                    if(_M_logs[slicesIndex].command() != log.command()){
                        // 异常，同一任期内产生的同一日志，命令却不一致
                        // std::string log_command_in_leader = log.command();
                        // std::string log_command = _M_logs[slicesIndex].command();
                        std::string errMsg = format(
"Error occured because different commands in node_%d(%s) and leader_node_%d(%s) within same term(%d) and same log_index(%d)",
                            _M_me, _M_logs[slicesIndex].command().c_str(),
                            args->leader_id(), log.command().c_str(),
                            log.term(), log.index()
                        );
                        LOG_ERROR << errMsg;
                        throw std::runtime_error(errMsg);
                    }
                }
                else{
                    // 任期不一致，更新日志
                    _M_logs[slicesIndex] = log;
                    LOG_INFO << "Node_" << _M_me << "update a log";
                }
            }
        }

        if((tempLastLogIndex = _M_getLastLogIndex()) < args->prev_log_index() + args->entries_size()){
            LOG_WARN << "Node_" << _M_me << "(last_log_index = " << tempLastLogIndex
                << ") receive a expire log from "
                << "leader_node_" << args->leader_id()
                << "(previous_log_index = " << args->prev_log_index()
                << ", entries_size = " << args->entries_size() << ")";
        }

        LOG_INFO << "my commit index: " << _M_commitIndex << "\n"
                << "leader's commit index: " << args->leader_commit();
        // 更新提交进度
        
        if(args->leader_commit() > _M_commitIndex){
            _M_commitIndex = std::min(args->leader_commit(), tempLastLogIndex);
        }

        reply->set_success(true);
    }

}

void raft::_L_requestVote(const requestVoteArgs *args, requestVoteReply *reply){
    guard_type guard(_M_mutex);
    DEFER{ _M_persist(); };

    // 候选人的term已经过时，拒绝投票并回复自己已知的最新的term
    if(args->term() < _M_term){
        reply->set_term(_M_term);
        reply->set_state(EXPIRE);
        reply->set_vote_granted(false);
        return;
    }
    // 候选人的term比自己的更新，本节点成为follwer
    if(args->term() > _M_term){
        _M_identityChange(FOLLOWER, args->term(), -1);
    }

    // 候选人的日志太旧，不能接受，拒绝投票
    if(!_M_receiveVote(args->last_log_index(), args->last_log_term())){
        reply->set_term(_M_term);
        reply->set_state(VOTED);
        reply->set_vote_granted(false);
    }
    // 已投票给其他候选人，拒绝再次投票
    else if(-1 != _M_vote4 && args->candidate_id() != _M_vote4){
        reply->set_term(_M_term);
        reply->set_state(VOTED);
        reply->set_vote_granted(false);
    }
    // 可以投票给该候选人
    else{
        _M_vote4 = args->candidate_id();
        _M_lastResetElectionTime = now();
        // _M_persist();

        reply->set_term(_M_term);
        reply->set_state(NORMAL);
        reply->set_vote_granted(true);
    }
}

void raft::_L_installSnapshot(const installSnapshotArgs *args, installSnapshotReply *reply){
    guard_type guard(_M_mutex);
    // 快照已经过期，回复最新任期
    if(args->term() < _M_term){ reply->set_term(_M_term); return; }

    // 跟随
    if(args->term() > _M_term){ _M_identityChange(FOLLOWER, args->term(), -1); }
    _M_identity = FOLLOWER; // 正在竞选同一个任期，或其它情况，强制降为follower
    _M_lastResetElectionTime = now(); // 收到leader的心跳，重置选举

    // 本节点的快照状态 比 args快照更新，视为过期
    if(args->last_index() <= _M_lastSnapshotIndex){
        // reply->set_term(_M_term);
        return;
    }
    // 清除logs中包含在args快照中的日志条目
    if(_M_getLastLogIndex() > args->last_index()){
        _M_logs.erase(_M_logs.begin(), _M_logs.begin()+_M_getSlicesIndexFromLogIndex(args->last_index())+1);
    }
    else{
        _M_logs.clear();
    }
    // 更新进度
    
    _M_commitIndex = std::max(_M_commitIndex, args->last_index());
    

    
    _M_appliedIndex = std::max(_M_appliedIndex, args->last_index());
    
    _M_lastSnapshotIndex = args->last_index();
    _M_lastSnapshotTerm = args->last_term();

    reply->set_term(_M_term);

    _M_persister->persist(_M_dump(), args->data());

    // 将快照推入上层命令管道，由上层应用安装
    applyMessage applyMsg(true, args->last_index(), args->last_term(), args->data());
    std::thread t(&raft::_M_pushMessage, this, applyMsg);
    t.detach();

}


const int raft::invalidUpdateNextIndex = -2;
const std::map<raft::identity, const char *> raft::identityStr{
    {FOLLOWER, "follower"},
    {CANDIDATE, "candidate"},
    {LEADER, "leader"}
};

} // namespace raft