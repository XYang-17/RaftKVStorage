#pragma once
#include <string>
#include <unordered_map>

namespace rpc{

class mprpcConfig{
public:
    void load(const char *file_path);
    std::string value(const std::string &key);

private:
    void _M_trim(std::string &src);

protected:
    std::unordered_map<std::string, std::string>    _M_config;
};

}; // namespace rpc