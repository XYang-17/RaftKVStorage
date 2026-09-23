#pragma once
#include <memory>

namespace cort{

template <typename _T>
using sptr = std::shared_ptr<_T>;

class noncopyable{
public:
    noncopyable() = default;
    ~noncopyable() = default;
private:
    noncopyable(const noncopyable &) = delete;
    noncopyable &operator=(const noncopyable &) = delete;
};
    
} // namespace cort