#pragma once
#include <string>
#include <google/protobuf/service.h>

namespace rpc{

namespace protobuf = google::protobuf;

class mprpcController: public protobuf::RpcController{
public:
    mprpcController();

    void Reset() override;
    bool Failed() const override;
    std::string ErrorText() const override;
    void SetFailed(const std::string &message) override;

    void StartCancel() override;
    bool IsCanceled() const override;
    void NotifyOnCancel(protobuf::Closure *callback) override;

protected:
    bool        _M_failed;
    std::string _M_message;
};

}; // namespace rpc