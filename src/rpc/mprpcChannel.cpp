#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cerrno>
#include <muduo/base/Logging.h>

#include "utils.h"

#include "rpcHeader.pb.h"
#include "mprpcController.h"
#include "mprpcChannel.h"

namespace rpc{

mprpcChannel::mprpcChannel(
    const std::string addr,
    uint16_t port,
    bool connectNow
):
    _M_addr(addr),
    _M_port(port),
    _M_clientFd(-1)
{
    if(!connectNow) return;
    int counter = 3;
    while(0 != _M_connect() && counter--){}
}


void mprpcChannel::CallMethod(
    const protobuf::MethodDescriptor *method,
    protobuf::RpcController *ctrl,
    const protobuf::Message *request,
    protobuf::Message *response,
    protobuf::Closure *done)
{
    if(-1 == _M_clientFd){
        int res;
        if(0 != (res = _M_connect())){
            ctrl->SetFailed(format(
                "Failed to connect %s:%d (errno = %d)",
                _M_addr.c_str(), _M_port, res));
            return;
        }
    }

    const protobuf::ServiceDescriptor *sd = method->service();
    const std::string serviceName = sd->name(), methodName = method->name();

    // request序列化
    std::string args_str;
    uint32_t args_size;
    if(!request->SerializeToString(&args_str)){
        ctrl->SetFailed("Failed to serialize request.");
        return;
    }
    args_size = args_str.size();
    rpc::rpcHeader rpcHeader;
    rpcHeader.set_service_name(serviceName);
    rpcHeader.set_method_name(methodName);
    rpcHeader.set_args_size(args_size);

    // RPC请求头序列化
    std::string rpcHeader_str;
    if(!rpcHeader.SerializeToString(&rpcHeader_str)){
        ctrl->SetFailed("Failed to serialize RPC header.");
        return;
    }

    // 组合RPC请求头和request
    std::string rpcRequest;
    {
        protobuf::io::StringOutputStream sos(&rpcRequest);
        protobuf::io::CodedOutputStream cos(&sos);

        cos.WriteVarint32(static_cast<uint32_t>(rpcHeader_str.size()));
        cos.WriteString(rpcHeader_str);
    }
    rpcRequest += args_str;

    // 发送RPC请求
    int res;
    while(-1 == send(_M_clientFd, rpcRequest.c_str(), rpcRequest.size(), 0)){
        DPrint(stdout, "Failed to send to %s:%d (errno = %d)", _M_addr.c_str(), _M_port, errno);
        _M_close();
        if(0 != (res = _M_connect())){
            ctrl->SetFailed(format(
                "Failed to connect %s:%d (errno = %d)",
                _M_addr.c_str(), _M_port, res));
            return;
        }
    }

    // 接收RPC响应
    char recvBuf[1024+/*\0*/1] {0};
    int recv_size = 0;
    if(-1 == (recv_size = recv(_M_clientFd, recvBuf, 1024, 0))){
        int en = errno;
        _M_close();
        ctrl->SetFailed(format(
            "Failed to receive from %s:%d (errno = %d)",
            _M_addr.c_str(), _M_port, en
        ));
        return;
    }

    if(!response->ParseFromArray(recvBuf, recv_size)){
        ctrl->SetFailed(format(
            "Failed to parse RPC response: %s", recvBuf
        ));
    }
}


int mprpcChannel::_M_connect(){
    _M_clientFd = socket(AF_INET, SOCK_STREAM, 0);
    if(-1 == _M_clientFd){
        int en = errno;
        DPrint(stdout, "Failed to create socker because (errno = %d) %s", en, strerror(en));
        return en;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(_M_port);
    server_addr.sin_addr.s_addr = inet_addr(_M_addr.c_str());
    if(-1 == connect(_M_clientFd, (struct sockaddr*)&server_addr, sizeof(server_addr))){
        int en = errno;
        _M_close();
        DPrint(stdout, "Failed to connect %s:%d because (errno = %d) %s", _M_addr.c_str(), _M_port, en, strerror(en));
        return en;
    }
    DPrint(stdout, "Successfully connect %s:%d", _M_addr.c_str(), _M_port);
    return 0;
}

void mprpcChannel::_M_close(){
    close(_M_clientFd);
    _M_clientFd = -1;
}

}; // namespcae rpc

