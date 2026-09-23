#include <muduo/base/Logging.h>

#include "mprpcConfig.h"

namespace rpc{

void mprpcConfig::load(const char *file_path){
    FILE *pf = fopen(file_path, "r");
    if(nullptr == pf){
        LOG_ERROR << "Failed to open file from '" << file_path << "'";
        exit(EXIT_FAILURE);
    }

    while(!feof(pf)){
        char buf[512] {0};
        fgets(buf, 512, pf);
        std::string data(buf);
        _M_trim(data);

        if(data.empty() || '#' == data[0]){
            continue;
        }
        int eq = data.find('=');
        if(-1 == eq) continue;

        std::string key = data.substr(0, eq), value = data.substr(eq+1);
        _M_trim(key);
        _M_trim(value);
        _M_config.insert({key, value});
    }

    fclose(pf);
}

std::string mprpcConfig::value(const std::string &key){
    auto it = _M_config.find(key);
    if(_M_config.end() == it) return "";
    return it->second;
}

void mprpcConfig::_M_trim(std::string &src){
    int tmp = src.find_last_not_of(' ');
    if(-1 != tmp) src.resize(tmp+1); // 移除尾部空白
    tmp = src.find_first_not_of(' ');
    if(-1 != tmp) src = src.substr(tmp); // 移除头部空白
}

}; // namespace rpc