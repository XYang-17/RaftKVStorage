#pragma once
#include <cmath>
#include <cstdlib>
#include <memory>
#include <ostream>
#include <functional>
#include <stdexcept>
#include <string.h>
#include <bits/allocator.h>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/vector.hpp>


#define SKIPLIST_STORE_FILE "store/dumpfile"

namespace KVSkipList{

using layer_size_type = unsigned short;

namespace __detail{
template <typename _Key, typename _Value, typename _Alloc>
struct node{
    using key_type      = _Key;
    using value_type    = _Value;

    using self_type     = node<key_type, value_type, _Alloc>;
    using self_pointer  = self_type*;
    using alloc_type    = typename std::allocator_traits<_Alloc>::template rebind_alloc<self_pointer>;

    template <typename _K, typename ..._Args>
    node(layer_size_type l, _K &&k, _Args&& ...args){
        static_assert(std::is_constructible_v<value_type, _Args...>, 
              "value_type cannot be constructed from provided arguments.");
        if(0 == l) throw std::runtime_error("Level must be a integer greater than 0.");
        ::new(std::addressof(key)) key_type(std::forward<_K>(k));
        ::new(std::addressof(value)) value_type(std::forward<_Args>(args)...);
        level = l;
        forward = alloc.allocate(level);
        std::fill_n(forward, level, nullptr);
    }
    ~node(){
        if(nullptr != forward)
            alloc.deallocate(forward, level);
    }

    key_type                    key;
    value_type                  value;
    layer_size_type             level;
    self_pointer                *forward;
    alloc_type                  alloc;

    template <bool _Const>
    class iterator{
    public:
        using pointer       = std::conditional_t<_Const, const node::value_type *, node::value_type *>;
        using reference     =std::pair<const node::key_type &, std::conditional_t<_Const, const node::value_type &, node::value_type &>>;

        iterator(node *n = nullptr): _M_node(n){}
        iterator(const iterator &) = default;
        iterator &operator=(const iterator &) = default;

        reference operator*() const{
            return {_M_node->key, _M_node->value};
        }
        pointer operator->() const{
            return std::addressof(operator*());
        }
        iterator &operator++(){
            _M_node = _M_node->forward[0];
            return *this;
        }
        iterator operator++(int){
            iterator rtn(*this);
            ++*this;
            return rtn;
        }

        friend bool operator==(const iterator &lhs, const iterator &rhs){
            return lhs._M_node == rhs._M_node;
        }
        friend bool operator!=(const iterator &lhs, const iterator &rhs){
            return lhs._M_node != rhs._M_node;
        }
    private:
        node *_M_node;
    };

private:
    node(const self_type &n)=delete;
    node(self_type&& n)=delete;
    self_type &operator=(const self_type &n)=delete;
    self_type &operator=(self_type&& n)=delete;
};

template <typename _Key, typename _Value>
class dumper{
public:
    friend class boost::serialization::access;

    template <typename _Archive>
    void serialize(_Archive &arc, const unsigned int version){
        arc &key;
        arc &value;
    }

    template <typename _Alloc>
    void push(const node<_Key, _Value, _Alloc> *node){
        key.emplace_back(node->key);
        value.emplace_back(node->value);
    }

    void clear(){
        key.clear();
        value.clear();
    }

    std::vector<_Key> key;
    std::vector<_Value> value;
};

};

template <typename _Key, typename _Value, layer_size_type _MaxLevel,
    template <typename _T> typename _Comp = std::greater,
    typename _Alloc = std::allocator<_Value>>
class kvskiplist{
public:
    using node_type             = __detail::node<_Key, _Value, _Alloc>;
    using node_alloc_type       = typename std::allocator_traits<_Alloc>::template rebind_alloc<node_type>;

    using key_type              = typename node_type::key_type;
    using value_type            = typename node_type::value_type;

    using iterator              = typename node_type::iterator<false>;
    using const_iterator        = typename node_type::iterator<true>;

    using size_type             = size_t;

    kvskiplist():
        _M_node_alloc(),
        _M_head(nullptr),
        _M_level_now(1),
        _M_counter(0)
    {
        _M_head = _M_constructNode(_MaxLevel, key_type{});
    }
    ~kvskiplist(){
        clear();
        _M_destroyNode(_M_head);
    }

    template <typename _K>
    value_type &operator[](_K &&k){
        node_type *pre [_MaxLevel];
        _M_findPrecursors(k, pre);

        // 不存在相同节点，插入默认值
        if(nullptr == pre[0]->forward[0] || _Comp<_Key>{}(pre[0]->forward[0]->key, k)){
            _M_insert(pre, std::forward<_K>(k), value_type{});
        }
        return pre[0]->forward[0]->value;
    }

    template <typename _K, typename ..._Args>
    iterator insert(_K &&k, _Args&& ...args){
        node_type *pre [_MaxLevel];
        _M_findPrecursors(k, pre);

        // 不存在相同节点，插入
        if(nullptr == pre[0]->forward[0] || _Comp<_Key>{}(pre[0]->forward[0]->key, k))
            return _M_insert(pre, std::forward<_K>(k), std::forward<_Args>(args)...);
        return nullptr;
    }
    template <typename _K>
    void remove(_K &&k){
        node_type *pre [_MaxLevel] {0};
        _M_findPrecursors(k, pre);
        
        // 不存在相同节点，结束
        node_type *rm = pre[0]->forward[0];
        if(nullptr == rm || _Comp<_Key>{}(rm->key, k)){
            return;
        }

        for(layer_size_type l = 0; l < _M_level_now; ++l){
            if(pre[l]->forward[l] != rm) break;
            pre[l]->forward[l] = rm->forward[l];
        }
        --_M_counter;

        while(_M_level_now > 1 && nullptr == _M_head->forward[_M_level_now-1]){
            --_M_level_now;
        }

        _M_destroyNode(rm);
    }
    void remove(iterator it){remove((*it).first);}
    void clear(){
        if(_M_level_now > 1)
            std::fill_n(&_M_head->forward[1], _M_level_now-1, nullptr);
        node_type *rm;
        while(nullptr != _M_head->forward[0]){
            rm = _M_head->forward[0];
            _M_head->forward[0] = rm->forward[0];
            _M_node_alloc.deallocate(rm, 1);
        }
        _M_level_now = 1;
        _M_counter = 0;
    }

    iterator begin(){return _M_head->forward[0];}
    const_iterator begin() const{return _M_head->forward[0];}
    const_iterator cbegin() const{return _M_head->forward[0];}
    iterator end(){return nullptr;}
    const_iterator end() const{return nullptr;}
    const_iterator cend() const{return nullptr;}

    template <typename _K>
    const_iterator find(_K &&k) const{
        node_type *pre = _M_head;
        layer_size_type l = _M_level_now - 1;
        do{
            while(nullptr != pre->forward[l] && _Comp<_Key>{}(k, pre->forward[l]->key)){
                pre = pre->forward[l];
            }
        }while(l-- > 0);
        if(nullptr == pre->forward[0] || _Comp<_Key>{}(pre->forward[0]->key, k))
            return end();
        return pre->forward[0];
        
    }
    template <typename _K>
    iterator find(_K &&k){
        node_type *pre = _M_head;
        layer_size_type l = _M_level_now - 1;
        do{
            while(nullptr != pre->forward[l] && _Comp<_Key>{}(k, pre->forward[l]->key)){
                pre = pre->forward[l];
            }
        }while(l-- > 0);
        if(nullptr == pre->forward[0] || _Comp<_Key>{}(pre->forward[0]->key, k))
            return end();
        return pre->forward[0];
        
    }
    size_type size() const{return _M_counter;}

    std::string dump() const{
        node_type *node = _M_head->forward[0];
        _M_dumper.clear();
        while(nullptr != node){
            _M_dumper.push(node);
            node = node->forward[0];
        }
        std::ostringstream oss;
        boost::archive::text_oarchive oa(oss);
        oa << _M_dumper;
        return oss.str();
    }
    void load(const std::string &str){
        if(str.empty()) return;
        _M_dumper.clear();
        std::istringstream iss(str);
        boost::archive::text_iarchive ia(iss);
        ia >> _M_dumper;
        for(size_type i = 0; i < _M_dumper.key.size(); ++i){
            insert(_M_dumper.key[i], _M_dumper.value[i]);
        }
    }
    
    friend std::ostream &operator<<(std::ostream &os, const kvskiplist &sl){
        for(layer_size_type l = 0; l < sl._M_level_now; ++l){
            node_type * node = sl._M_head->forward[l];
            while(nullptr != node){
                os << node->key << ": " << node->value << ", ";
                node = node->forward[l];
            }
            os << std::endl;
        }
        return os;
    }

private:
    template <typename ..._Args>
    node_type *_M_constructNode(layer_size_type level, _Args&& ...args){
        node_type *node = _M_node_alloc.allocate(1);
        _M_node_alloc.construct(node, level, std::forward<_Args>(args)...);
        return node;
    }
    void _M_destroyNode(node_type *node){
        _M_node_alloc.destroy(node);
        _M_node_alloc.deallocate(node, 1);
    }

    layer_size_type _M_randomLevel(){
        static bool seeded = false;
        if (!seeded) {
            // 混合时间和时钟计数，增加种子随机性
            unsigned seed = static_cast<unsigned>(time(nullptr)) ^ static_cast<unsigned>(clock());
            srand(seed);
            seeded = true;
        }

        layer_size_type level = 1;
        // 几何分布：以 0.5 概率增加层数，直到达到最大层
        while ((rand() & 1) && level < _MaxLevel){ ++level; }
        return level;
    }

    void _M_findPrecursors(const key_type &k, node_type **pre){
        node_type *cur = _M_head;
        layer_size_type l = _M_level_now - 1;
        do{
            while(nullptr != cur->forward[l] && _Comp<_Key>{}(k, cur->forward[l]->key)){
                cur = cur->forward[l];
            }
            pre[l] = cur;
        }while(l-- > 0);
    }

    template <typename _K, typename ..._Args>
    iterator _M_insert(node_type **pre, _K &&k, _Args&& ...args){
        // 生成随机层数
        layer_size_type node_level = _M_randomLevel();
        node_type *isrt = _M_constructNode(node_level, std::forward<_K>(k), std::forward<_Args>(args)...);

        if(node_level > _M_level_now){
            for(layer_size_type l = _M_level_now; l < node_level; ++l){
                pre[l] = _M_head;
            }
            _M_level_now = node_level;
        }
        for(layer_size_type l = 0; l < node_level; ++l){
            isrt->forward[l] = pre[l]->forward[l];
            pre[l]->forward[l] = isrt;
        }
        ++_M_counter;

        return isrt;
    }

private:
    node_alloc_type     _M_node_alloc;
    node_type           *_M_head;
    layer_size_type     _M_level_now;
    size_type           _M_counter;

    static __detail::dumper<_Key, _Value> _M_dumper;
};

template <typename _Key, typename _Value, layer_size_type _MaxLevel,
    template <typename _T> typename _Comp, typename _Alloc>
__detail::dumper<_Key, _Value>
kvskiplist<_Key, _Value, _MaxLevel, _Comp, _Alloc>::_M_dumper;

}; // namespace KVSkipList

