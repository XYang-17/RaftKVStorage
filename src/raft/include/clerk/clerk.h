#pragma once
#include <vector>
#include <string>
#include <memory>

#include "kvHelper.h"

namespace raft{

class clerk{
public:
    clerk();

    void init(const std::string configPath);

    std::string get(const std::string key);
    void put(const std::string key, const std::string value);

private:
    static std::string _M_uuid();

protected:
    std::vector<std::shared_ptr<kvHelper>>  _M_servers;
    std::string                             _M_clientId;
    size_t                                  _M_requestId;
    size_t                                  _M_leaderId;
};

}; // namespace raft
