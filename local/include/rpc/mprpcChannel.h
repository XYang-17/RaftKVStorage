#pragma once
#include <google/protobuf/service.h>

namespace rpc{

namespace protobuf = google::protobuf;

class mprpcChannel: public protobuf::RpcChannel{
public:
    mprpcChannel(
        const std::string addr,
        uint16_t port,
        bool connectNow
    );

    void CallMethod(
        const protobuf::MethodDescriptor *method,
        protobuf::RpcController *ctrl,
        const protobuf::Message *request,
        protobuf::Message *response,
        protobuf::Closure *done
    ) override;

private:
    int _M_connect();
    void _M_close();
protected:
    int                     _M_clientFd;
    const std::string       _M_addr;
    const uint16_t          _M_port;
};

}; // namespcae rpc