#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fstream>
#include <muduo/base/Logging.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/io/coded_stream.h>

#include "utils.h"

#include "rpcProvider.h"
#include "rpcHeader.pb.h"

namespace rpc{
rpcProvider::~rpcProvider(){
    _M_eventLoop.quit();
}


void rpcProvider::notifyService(protobuf::Service *service){
    serviceInfo info;
    const protobuf::ServiceDescriptor *sd = service->GetDescriptor();
    int methodNum = sd->method_count();
    for(int i = 0; i < methodNum; ++i){
        const protobuf::MethodDescriptor *md = sd->method(i);
        info._M_methods.insert({md->name(), md});
    }
    info._M_service = service;
    _M_services.insert({sd->name(), info});
}

void rpcProvider::run(int nodeIdx, uint16_t port){
    // 获取IP地址
    char *addr;
    char host_name[128];
    gethostname(host_name, sizeof(host_name));
    struct hostent *hent = gethostbyname(host_name);
    for(int i = 0; hent->h_addr_list[i]; ++i){
        addr = inet_ntoa(*(struct in_addr*)(hent->h_addr_list[i]));
    }
    // 获取可用端口
    if(!findAvailablePort(port)){
        LOG_ERROR << "Failed to find a available port from " << port;
        exit(EXIT_FAILURE);
    }

    // std::string node = "node" + std::to_string(nodeIdx);
    // std::ofstream ofs("test.conf", std::ios::app);
    // if(!ofs.is_open()){
    //     LOG_WARN << "Failed to open test.conf";
    //     exit(EXIT_FAILURE);
    // }
    // ofs << node << "ip=" << addr << "\n"
    //     << node << "port=" << port << std::endl;
    // ofs.close();

    // 创建TCP对象，设置回调
    // muduo::net::InetAddress addr_port(addr, port);
    _M_tcp = std::make_shared<muduo::net::TcpServer>(
        &_M_eventLoop,
        muduo::net::InetAddress{addr, port},
        "RPCProvider");
    _M_tcp->setConnectionCallback(
        [this](const muduo::net::TcpConnectionPtr &conn){
        return this->_M_onConnection(conn);
    });
    _M_tcp->setMessageCallback(
        [this](
            const muduo::net::TcpConnectionPtr &conn,
            muduo::net::Buffer *buf,
            muduo::Timestamp t)
        {
            return this->_M_onMessage(conn, buf, t);
        }
    );
    _M_tcp->setThreadNum(4);

    // 启动TCP，监听请求
    _M_tcp->start();
    DPrint(stdout, "Successfully started RPCProvider in %s:%d", addr, port);
    // 启动事件循环，等待处理事件
    _M_eventLoop.loop();
}


void rpcProvider::_M_onConnection(const muduo::net::TcpConnectionPtr &conn){
    if(!conn->connected()){
        conn->shutdown();
    }
}

void rpcProvider::_M_onMessage(
    const muduo::net::TcpConnectionPtr &conn,
    muduo::net::Buffer *buf,
    muduo::Timestamp)
{
    std::string recvBuf = buf->retrieveAllAsString();
    protobuf::io::ArrayInputStream ais(recvBuf.data(), recvBuf.size());
    protobuf::io::CodedInputStream cis(&ais);

    // 安全读取数据，写入缓冲区
    uint32_t header_size;
    std::string rpcHeaderStr;
    cis.ReadVarint32(&header_size);
    protobuf::io::CodedInputStream::Limit lim = cis.PushLimit(header_size);
    cis.ReadString(&rpcHeaderStr, header_size);
    cis.PopLimit(lim);

    // 数据反序列化
    rpc::rpcHeader rpcHeader;
    std::string serviceName, methodName;
    uint32_t args_size;
    if(!rpcHeader.ParseFromString(rpcHeaderStr)){
        LOG_WARN << "Failed to parse RPC header: " << rpcHeaderStr;
        return;
    }
    serviceName = rpcHeader.service_name();
    methodName = rpcHeader.method_name();
    args_size = rpcHeader.args_size();

    // 读取参数
    std::string argsStr;
    if(!cis.ReadString(&argsStr, args_size)){
        LOG_WARN << "Failed to read args";
        return;
    }

    // 获取service、method
    auto service_it = _M_services.find(serviceName);
    if(_M_services.end() == service_it){
        DPrint(stdout, "Failed to find service named %s", serviceName);
        return;
    }
    auto method_it = service_it->second._M_methods.find(methodName);
    if(service_it->second._M_methods.end() == method_it){
        DPrint(stdout, "Failed to find method named %s", methodName);
        return;
    }
    protobuf::Service *service = service_it->second._M_service;
    const protobuf::MethodDescriptor *method = method_it->second;

    // 生成RPC的请求、响应、回调
    protobuf::Message *req = service->GetRequestPrototype(method).New(),
        *resp = service->GetResponsePrototype(method).New();
    if(!req->ParseFromString(argsStr)){
        DPrint(stdout, "Failed to parse request: %s", argsStr);
        return;
    }
    protobuf::Closure *cb= protobuf::NewCallback
        <rpcProvider, const muduo::net::TcpConnectionPtr&, protobuf::Message*>
        (this, &rpcProvider::_M_senRPCResponse, conn, resp); // 请求处理完毕后，执行响应的序列化和发送

    service->CallMethod(method, nullptr, req, resp, cb);
}

void rpcProvider::_M_senRPCResponse(
    const muduo::net::TcpConnectionPtr &conn,
    protobuf::Message *resp)
{
    std::string respStr;
    if(resp->SerializeToString(&respStr)){
        conn->send(respStr);
    }
    else{
        LOG_WARN << "Failed to serialize response";
    }
}


}; // namespace rpc