#include "rpc/mprpcConfig.h"

#include "utils.h"

#include "clerk/clerk.h"
#include "config.h"

namespace raft{
    
clerk::clerk():
    _M_clientId(_M_uuid()),
    _M_requestId(0),
    _M_leaderId(0){}


void clerk::init(const std::string configPath){
    rpc::mprpcConfig cfg;
    cfg.load(configPath.c_str());
    
    std::vector<std::pair<std::string, uint16_t>> addr_port;
    // 获取所有raft节点地址
    for(size_t i = 0; i < UINT16_MAX; ++i){
        std::string node = "node" + std::to_string(i);
        std::string addr = cfg.value(node + "ip");
        if(addr.empty()) break;
        std::string port = cfg.value(node + "port");
        addr_port .emplace_back(addr, atoi(port.c_str()));
    }
    // 连接并记录
    _M_servers.assign(addr_port.size(), nullptr);
    for(size_t i = 0; i < addr_port.size(); ++i){
        _M_servers[i].reset(new kvHelper(
            addr_port[i].first, addr_port[i].second));
    }
}


std::string clerk::get(const std::string key){
    getArgs args;
    args.set_key(key);
    args.set_clientid(_M_clientId);
    args.set_requestid(++_M_requestId);

    size_t leaderId = _M_leaderId;
    while(true){
        getReply reply;
        if(!_M_servers[leaderId]->get(&args, &reply)
            || RAFT_KVSTORAGE_ERR_WRONG_LEADER == reply.err())
        {
            // 失败，或者对方不是leader，尝试下一个节点
            leaderId = (leaderId + 1) % _M_servers.size();
            continue;
        }
        if(RAFT_KVSTORAGE_ERR_NO_KEY == reply.err())
        { break; } // key不存在
        if(RAFT_KVSTORAGE_OK == reply.err()){
            // 成功
            _M_leaderId = leaderId;
            return reply.value();
        }
    }

    return "";
}

void clerk::put(const std::string key, const std::string value){
    putArgs args;
    args.set_key(key);
    args.set_value(value);
    args.set_clientid(_M_clientId);
    args.set_requestid(++_M_requestId);
    
    size_t leaderId = _M_leaderId;
    while(true){
        putReply reply;
        if(!_M_servers[leaderId]->put(&args, &reply)){
            // 失败，尝试下一个节点
            DPrint(stdout, "Failed to put to leaderId = %u because of RPC failure", leaderId);
            leaderId = (leaderId + 1) % _M_servers.size();
            continue;
        }
        if(RAFT_KVSTORAGE_OK == reply.err()){
            // 成功
            _M_leaderId = leaderId;
            return;
        }
        if(RAFT_KVSTORAGE_ERR_WRONG_LEADER == reply.err()){
            // 对方不是leader，尝试下一个节点
            DPrint(stdout, "Failed to put to leaderId = %u because of non-leader", leaderId);
            leaderId = (leaderId + 1) % _M_servers.size();
        }
    }
}


std::string clerk::_M_uuid(){
    return std::to_string(rand()) + std::to_string(rand()) + std::to_string(rand()) + std::to_string(rand());
}

}; // namespace raft