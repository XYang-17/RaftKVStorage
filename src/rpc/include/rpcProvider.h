#pragma once
#include <string>
#include <unordered_map>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>
#include <google/protobuf/service.h>

namespace rpc{

namespace protobuf = google::protobuf;

class rpcProvider{
public:
    ~rpcProvider();

    void notifyService(protobuf::Service *service);
    void run(int nodeIdx, uint16_t port);

private:
    void _M_onConnection(const muduo::net::TcpConnectionPtr &conn);
    void _M_onMessage(
        const muduo::net::TcpConnectionPtr &conn,
        muduo::net::Buffer *buf,
        muduo::Timestamp
    );
    void _M_senRPCResponse(
        const muduo::net::TcpConnectionPtr &conn,
        protobuf::Message *response
    );

protected:
    struct serviceInfo{
        protobuf::Service *_M_service;
        std::unordered_map<std::string, const protobuf::MethodDescriptor*> _M_methods;
    };

    muduo::net::EventLoop                           _M_eventLoop;
    std::shared_ptr<muduo::net::TcpServer>          _M_tcp;
    std::unordered_map<std::string, serviceInfo>    _M_services;
};

}; // namespace rpc