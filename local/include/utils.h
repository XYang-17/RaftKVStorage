#pragma once
#include <utility>
#include <type_traits>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <iostream>
#include <sstream>
#include <map>
#include <queue>
#include <random>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/access.hpp>

#ifndef _CONCAT
#define _CONCAT(a, b) a##b
#endif

/*
延迟调用的函数符，将会在对象被销毁时调用函数
要求函数不接受任何参数；若有返回值将被忽略
*/
template <typename _Fn>
class deferFunction{
public:
    template <typename _F, 
        typename = std::enable_if_t<std::is_constructible_v<_Fn, _F&&>>>
    deferFunction(_F&& func)
    noexcept(std::is_nothrow_constructible_v<_Fn, _F&&>):
        _M_func(std::forward<_F>(func)){}
    ~deferFunction() noexcept(noexcept(_M_func())){_M_func();}

    deferFunction(const deferFunction &) = delete;
    deferFunction &operator=(const deferFunction &) = delete;

protected:
    _Fn _M_func;
};

template <typename _Fn>
deferFunction(_Fn) -> deferFunction<_Fn>;

/*
使用lambda函数构建延迟调用的函数符，使用方法为：DEFER{...}
*/
#define _MAKE_DEFER_(line) deferFunction _CONCAT(deferFunc, line) = [&]()
#undef DEFER
#define DEFER _MAKE_DEFER_(__LINE__)

#ifndef RAFT_KVSTORAGE_DEBUG
#define RAFT_KVSTORAGE_DEBUG false
#endif

/*
字符串输出函数，将格式化字符串输出到指定文件描述符中
*/
inline void dprintf(FILE *fout, const char *format, ...){
    time_t now = time(nullptr);
    struct tm time;
    #if defined(_WIN32)
        localtime_s(&time, &now);
    #else
        localtime_r(&now, &time);
    #endif
    va_list args;
    va_start(args, format);
    std::fprintf(fout, "[%d-%d-%d %d:%d:%d] ",
        time.tm_year+1900, time.tm_mon+1, time.tm_mday,
        time.tm_hour, time.tm_min, time.tm_sec);
    std::vfprintf(fout, format, args);
    std::fprintf(fout, "\n");
    va_end(args);
}
/*
调试信息输出宏函数，仅当RAFT_KVSTORAGE_DEBUG==true时执行格式化输出
*/
#ifndef DPrint
#define DPrint(...) do{if constexpr(RAFT_KVSTORAGE_DEBUG) dprintf(__VA_ARGS__);} while(0)
#endif

/*
运行时断言
*/
inline void conditional_assert(bool condition, const std::string &message){
    if(!condition){
        std::cerr << "Error: " << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

/*
字符串格式化函数
*/
template <typename ..._Args>
inline std::string format(const char* format, _Args&& ...args){
    size_t size = static_cast<size_t>(std::snprintf(nullptr, 0, format, std::forward<_Args>(args)...) + /*\0*/1);
    if(size <= 0){throw std::runtime_error("Failed to format string");}
    std::vector<char> buf(size);
    std::snprintf(buf.data(), size, format, std::forward<_Args>(args)...);
    return std::string(buf.begin(), buf.end());
}

/*
时间函数，获取当前时间
*/
inline std::chrono::_V2::system_clock::time_point now(){
    return std::chrono::high_resolution_clock::now(); 
}

/*
线程睡眠函数
*/
inline void sleep_ms(int n){
    std::this_thread::sleep_for(std::chrono::milliseconds(n));
}

/*
线程安全的队列
*/
template <typename _Value,
    template <typename _V, typename ..._As> typename _Container = std::deque,
    typename ..._Args>
class safequeue{
public:
    using base_container    = _Container<_Value, _Args...>;
    using value_type        = typename base_container::value_type;

    template <typename ..._Ts>
    void push(_Ts&& ...args){
        std::lock_guard<std::mutex> lock(_M_mutex);
        _M_container.push_back(std::forward<_Ts>(args)...);
        _M_condition.notify_one();
    }

    value_type pop(){
        std::unique_lock<std::mutex> lock(_M_mutex);
        while(_M_container.empty()){
            _M_condition.wait(lock);
        }
        value_type val = _M_container.front();
        _M_container.pop_front();
        return val;
    }

    bool pop(value_type &val, std::chrono::milliseconds timeout){
        std::unique_lock<std::mutex> lock(_M_mutex);
        while(_M_container.empty()){
            if(_M_condition.wait_for(lock, timeout) == std::cv_status::timeout){
                return false;
            }
        }
        val = _M_container.front();
        _M_container.pop_front();
        return true;
    }

    bool empty() const{return _M_container.empty();}

private:
    base_container          _M_container;
    std::mutex              _M_mutex;
    std::condition_variable _M_condition;
};


// struct dumper{
//     friend class boost::serialization::access;

//     virtual std::string dump() const = 0;
//     virtual bool load(const std::string &str) = 0;

// private:
//     template <typename _Archive>
//     void serialize(_Archive & ar, const unsigned int version);
// };

/*
带序列化的类型
*/
struct op{
    friend class boost::serialization::access;

    enum operation_t{Invalid=-1, Get=0, Put};
    static const char *operation2String(operation_t opt){
        static const std::map<int, const char *> m{
            {Invalid, "Invalid"},
            {Put, "Put"},
            {Get, "Get"}
        };
        return m.at(opt);
    }
    static operation_t string2Operation(const std::string &str){
        static const std::map<std::string, operation_t> m{
            {"Put", Put},
            {"Get", Get}
        };
        auto it = m.find(str);
        if(m.end() == it) return Invalid;
        return it->second;
    }

    operation_t operation;
    std::string key;
    std::string value;
    std::string clientId;
    int         requestId;

    std::string dump() const{
        std::ostringstream oss;
        boost::archive::text_oarchive oa(oss);
        oa << *this;
        return oss.str();
    }

    bool load(const std::string &str){
        if(str.empty()) return false;
        std::istringstream iss(str);
        boost::archive::text_iarchive ia(iss);
        ia >> *this;
        return true;
    }

    friend std::ostream &operator<<(std::ostream &os, const op &obj){
        os << "op{operation: " << operation2String(obj.operation)
            << ", key: " << obj.key
            << ", value: " << obj.value
            << ", clientId: " << obj.clientId
            << ", requestId: " << obj.requestId;
        return os;
    }

private:
    template <typename _Archive>
    void serialize(_Archive & ar, const unsigned int version){
        if constexpr(_Archive::is_saving::value){
            std::string str = operation2String(operation);
            ar &str;
        }
        else{
            std::string str;
            ar &str;
            operation = string2Operation(str);
        }
        ar &key;
        ar &value;
        ar &clientId;
        ar &requestId;
    }
};


/*
判断端口是否可用
*/
inline bool availablePort(uint16_t port){
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if(0 != ::bind(fd, (sockaddr *)&addr, sizeof(addr))){
        close(fd);
        return false;
    }
    close(fd);
    return true;
}

/*
向后一定范围内搜索可用端口
*/
inline bool findAvailablePort(uint16_t &port){
    uint16_t counter = 0;
    while(!availablePort(port+counter) && counter < 30){
        ++counter;
    }
    if(counter >= 30){return false;}
    counter += counter;
    return true;
}

inline std::string to_hex(const std::string& input){
    std::ostringstream oss;
    for(unsigned char c: input){
        oss << std::hex << std::setw(2) << std::setfill('0') << int(c);
    }
    return oss.str();
}