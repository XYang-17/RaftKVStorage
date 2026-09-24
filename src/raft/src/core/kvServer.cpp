#include <muduo/base/Logging.h>

#include "rpc/rpcProvider.h"
#include "rpc/mprpcConfig.h"

#include "config.h"
#include "core/kvServer.h"

namespace raft{

kvServer::kvServer(int me, int maxRaftStateSize, const std::string &file):
    _M_me(me),
    _M_maxRaftStateSize(maxRaftStateSize),
    _M_applyChan(std::make_shared<safequeue<applyMessage>>())
{
    // 加载rpc配置
    rpc::mprpcConfig cfg;
    cfg.load(file.c_str());
    std::vector<std::pair<std::string, uint16_t>> addr_port;
    for(int i = 0; i < UINT16_MAX; ++i){
        std::string node = "node" + std::to_string(i);
        std::string addr = cfg.value(node+"ip");
        if(addr.empty()) break;
        std::string port = cfg.value(node+"port");
        addr_port.emplace_back(addr, atoi(port.c_str()));
    }
    if(_M_me >= addr_port.size()){
        throw std::runtime_error("Bad me");
    }

    // 加载raft配置
    std::vector<std::shared_ptr<raftHelper>> helpers(addr_port.size(), nullptr);
    for(int i = 0; i < addr_port.size(); ++i){
        if( i == _M_me){ continue; }
        helpers[i].reset(new raftHelper(addr_port[i].first, addr_port[i].second));
    }
    sleep(addr_port.size());

    std::shared_ptr<persister> pstr = std::make_shared<persister>(_M_me);
    // 初始化底层raft节点
    _M_raftNode.reset(new raft(_M_me, pstr, helpers, _M_applyChan, RAFT_READ_THREAD_NUM));

    // 加载快照
    std::string shot;
    if(pstr->loadSnapshot(shot) && !shot.empty()){
        _M_load(shot);
    }

    // 开启rpc，监听远程调用请求
    uint16_t myPort = addr_port[_M_me].second;
    std::thread t([this, myPort](){
        rpc::rpcProvider provider;
        provider.notifyService(this);                       // 注册KVServer服务
        provider.notifyService(this->_M_raftNode.get());    // 注册raft服务
        provider.run(this->_M_me, myPort);
    });
    t.detach();
    sleep(5); // 等待rpc连接完成
    
    // 另起线程，持续从命令管道中取出命令，并执行
    std::thread t2(&kvServer::_L_popMessageLoop, this);
    t2.join();
}


void kvServer::get(
    protobuf::RpcController *ctrl,
    const getArgs *args,
    getReply *reply,
    protobuf::Closure *done)
{
    _M_raftNode->executeGet(&kvServer::_L_get, this, ctrl, args, reply, done);
    // _L_get(ctrl, args, reply, done);
    // done->Run(); // 改由_L_get在线程池中执行
}

void kvServer::put(
    protobuf::RpcController *ctrl,
    const putArgs *args,
    putReply *reply,
    protobuf::Closure *done)
{
    _L_put(args, reply);
    done->Run();
}


// void kvServer::_L_get(const getArgs *args, getReply *reply){
//     op command{op::Get, args->key(), "", args->clientid(), args->requestid()};
//
//     // int index = -1, _;
//     // bool asLeader = false;
//     // _M_raftNode->execute(command, &index, &_, &asLeader);
//     auto [index, term] = _M_raftNode->execute(command);
//
//     if(-1 == index || -1 == term){
//         reply->set_err(RAFT_ERR_WRONG_LEADER);
//         return;
//     }
//
//     // 获取/创建命令执行完毕管道
//     std::unique_lock<std::mutex> lock(_M_mutex);
//     if(_M_appliedChan.end() == _M_appliedChan.find(index)){
//         _M_appliedChan.insert({index, new safequeue<op>});
//     }
//     auto que = _M_appliedChan[index];
//     lock.unlock();  // 解锁，让其它线程/协程执行完成命令后，将命令推入命令执行完毕管道
//
//     // 等待获取已完成命令
//     op raftCommitOp;
//     if(!que->pop(raftCommitOp, std::chrono::milliseconds(RAFT_CONSENSUS_TIME))){
//         // 超时失败
//         // int _;
//         // bool asLeader = false;
//         // _M_raftNode->getState(&_, &asLeader);
//         auto [_, asLeader] = _M_raftNode->getState();
//      
//         // leader节点，且已经被执行过
//         // 未被执行过的新命令执行超时失败，此时不知道此节点中的数据是否是集群中已达成共识的数据，为保证一致性，不能返回数据
//         if(asLeader && _L_duplicateRequest(command.clientId, command.requestId)){
//             auto res = _L_executeGetOp(command);
//             if(!res.first){
//                 reply->set_err(RAFT_ERR_NO_KEY);
//             }
//             else{
//                 reply->set_err(RAFT_OK);
//                 reply->set_value(res.second);
//             }
//         }
//         else{
//             reply->set_err(RAFT_TIMEOUT);
//         }
//     }
//     // 可能因leader变更导致日志覆盖，需要确定被执行的是我提交的这一条
//     // 日志/命令匹配(term+index)，回去数据并返回
//     else if(raftCommitOp.clientId == command.clientId
//         && raftCommitOp.requestId == command.requestId)
//     {
//         auto res = _L_executeGetOp(command);
//         if(!res.first){
//             reply->set_err(RAFT_ERR_NO_KEY);
//             reply->set_err("");
//         }
//         else{
//             reply->set_err(RAFT_OK);
//             reply->set_value(res.second);
//         }
//     }
//     // 匹配失败，回复失败
//     else{
//         reply->set_err(RAFT_ERR_WRONG_LEADER);
//     }
//
//     // 移除命令执行完毕管道
//     lock.lock();
//     que = _M_appliedChan[index];
//     _M_appliedChan.erase(index);
//     delete que;
// }

// void kvServer::_L_get(const getArgs *args, getReply *reply){
//     if(!_M_raftNode->waitApplied()){
//         reply->set_err(RAFT_TIMEOUT);
//         return;
//     }
// 
//     op command{op::Get, args->key(), "", args->clientid(), args->requestid()};
//     auto res = _L_executeGetOp(command);
//     if(!res.first){
//         reply->set_err(RAFT_ERR_NO_KEY);
//         return;
//     }
//     
//     reply->set_value(res.second);
//     reply->set_err(RAFT_OK);
// }

void kvServer::_L_get(
    protobuf::RpcController *ctrl,
    const getArgs *args,
    getReply *reply,
    protobuf::Closure *done
)
{
    if(!_M_raftNode->waitApplied()){
        reply->set_err(RAFT_TIMEOUT);
        return;
    }

    op command{op::Get, args->key(), "", args->clientid(), args->requestid()};
    auto res = _L_executeGetOp(command);
    if(!res.first){
        reply->set_err(RAFT_ERR_NO_KEY);
        return;
    }
    
    reply->set_value(res.second);
    reply->set_err(RAFT_OK);

    done->Run();
}

void kvServer::_L_put(const putArgs *args, putReply *reply){
    op command{
        op::Put,
        args->key(),
        args->value(),
        args->clientid(),
        args->requestid()
    };

    // int index = -1, _;
    // bool asLeader = false;
    // _M_raftNode->execute(command, &index, &_, &asLeader);
    auto [index, term] = _M_raftNode->execute(command);
    // 底层raft节点不是leader，禁止写入
    if(-1 == index || -1 == term){
        // LOG_INFO << "not leader";
        reply->set_err(RAFT_ERR_WRONG_LEADER);
        return;
    }
    
    // 获取/创建命令执行完毕管道
    std::unique_lock<std::mutex> lock(_M_mutex);
    if(_M_appliedChan.end() == _M_appliedChan.find(index)){
        _M_appliedChan.insert({index, new safequeue<op>});
    }
    auto que = _M_appliedChan[index];
    lock.unlock(); // 解锁，让其它线程/协程执行完成命令后，将命令推入命令执行完毕管道
    LOG_INFO << "create queue over";

    // 等待获取已完成命令
    op raftCommitOp;
    // 超时失败
    if(!que->pop(raftCommitOp, std::chrono::milliseconds(RAFT_CONSENSUS_TIME))){
        LOG_INFO << "pop failure";
        // 已经执行过的请求，回复成功
        if(_L_duplicateRequest(command.clientId, command.requestId)){
            reply->set_err(RAFT_OK);
        }
        // 失败回复
        else{
            reply->set_err(RAFT_ERR_WRONG_LEADER);
        }
    }
    // 可能因leader变更导致日志覆盖，需要确定被执行的是我提交的这一条
    // 日志/命令匹配(term+index)，回复成功
    else if(raftCommitOp.clientId == command.clientId
        && raftCommitOp.requestId == command.requestId)
    {
        LOG_INFO << "ok";
        reply->set_err(RAFT_OK);
    }
    // 匹配失败，回复失败
    else{
        LOG_INFO << "term change";
        reply->set_err(RAFT_ERR_WRONG_LEADER);
    }

    // 移除命令执行完毕管道
    lock.lock();
    que = _M_appliedChan[index];
    _M_appliedChan.erase(index);
    delete que;
}


void kvServer::_L_popMessageLoop(){
    while(true){
        auto msg = _M_applyChan->pop();
        if(msg.validCommand){
            _L_executeCommand(msg); // 执行命令
        }
        if(msg.validSnapshot){
            _L_installSnapshot(msg); // 安装快照
        }
    }
}


bool kvServer::_L_duplicateRequest(const std::string &client, int requestId){
    guard_type guard(_M_mutex);
    auto it = _M_lastRequestId.find(client);
    if(_M_lastRequestId.end() == it) return false; // 记录中找不到该用户，没有执行过
    return requestId <= it->second; // 比记录新就表示没有执行过
}


void kvServer::_L_executeCommand(applyMessage msg){
    if(msg.commandIndex <= _M_lastSnapshotCommandIndex) return; // 命令索引 小于 快照最新命令的索引，忽略

    op command;
    command.load(msg.command);

    // 请求没有被执行过，执行Put
    if(!_L_duplicateRequest(command.clientId, command.requestId)){
        if(op::Put == command.operation){
            _L_executePutOp(command);
        }
    }
    // 可能拍摄快照
    _L_maySnapshot(msg.commandIndex);
    
    // 尝试将命令推入命令执行完毕管道
    _L_tryPush2Wait(command, msg.commandIndex);
}

void kvServer::_L_installSnapshot(applyMessage msg){
    guard_type guard(_M_mutex);
    _M_load(msg.snapshot);
    _M_lastSnapshotCommandIndex = msg.snapshotIndex;
}


void kvServer::_L_executePutOp(op command){
    guard_type guard(_M_mutex);
    _M_data[command.key] = command.value; // 数据插入跳表
    _M_lastRequestId[command.clientId] = command.requestId; // 更新用户执行记录
}

std::pair<bool, std::string>
kvServer::_L_executeGetOp(op command){
    guard_type guard(_M_mutex);
    // 查询结果
    auto it = _M_data.find(command.key);
    _M_lastRequestId[command.clientId] = command.requestId; // 更新用户执行记录
    if(_M_data.end() == it) return{false, ""};
    return {true, (*it).second};
}

void kvServer::_L_maySnapshot(int index){
    if(-1 == _M_maxRaftStateSize) return;
    if(_M_raftNode->getRaftStateSize() > _M_maxRaftStateSize){
        guard_type guard(_M_mutex);
        _M_raftNode->persist(index, _M_dump());
    }
}


bool kvServer::_L_tryPush2Wait(op command, int index){
    guard_type guard(_M_mutex);
    auto it = _M_appliedChan.find(index);
    if(_M_appliedChan.end() == it){ return false; }
    it->second->push(command);
    return true;
}

}; // namespace raft