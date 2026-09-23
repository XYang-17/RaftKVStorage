#pragma once
#include <cmath>
#include <cstdlib>
#include <memory>
#include <functional>
#include <stdexcept>
#include <string.h>
#include <bits/allocator.h>

#define SKIPLIST_STORE_FILE "store/dumpfile"

namespace skipList{

using layer_size_type = size_t;

template <typename _Value, typename _Alloc>
struct node{
    using value_type    = _Value;

    using self_type     = node<value_type, _Alloc>;
    using self_pointer  = self_type*;
    using alloc_type    = typename std::allocator_traits<_Alloc>::template rebind_alloc<self_pointer>;

    node(layer_size_type l, const value_type &v){
        if(0 == l) throw std::runtime_error("Level must be a integer greater than 0.");
        _M_construct(l, v);
    }
    node(layer_size_type l, value_type &&v){
        if(0 == l) throw std::runtime_error("Level must be a integer greater than 0.");
        _M_construct(l, std::move(v));
    }
    template <typename ..._Args>
    node(layer_size_type l, _Args&& ...args){
        if(0 == l) throw std::runtime_error("Level must be a integer greater than 0.");
        _M_construct(l, std::forward<_Args>(args)...);
    }
    ~node(){
        if(nullptr != forward)
            alloc.deallocate(forward, level);
    }

    value_type                  value;
    layer_size_type             level;
    self_pointer                *forward;
    alloc_type                  alloc;
private:
    node(const self_type &n)=delete;
    node(self_type&& n)=delete;
    self_type &operator=(const self_type &n)=delete;
    self_type &operator=(self_type&& n)=delete;

    template <typename ..._Args>
    void _M_construct(layer_size_type l, _Args&& ...args){
        ::new(std::addressof(value)) value_type(std::forward<_Args>(args)...);
        level = l;
        forward = alloc.allocate(level);
        std::fill_n(forward, level, nullptr);
    }
};

template <typename _Value, layer_size_type _MaxLevel,
    typename _Comp = std::greater<_Value>,
    typename _Alloc = std::allocator<_Value>>
class skiplist{
public:
    using node_type             = node<_Value, _Alloc>;
    using node_alloc_type       = typename std::allocator_traits<_Alloc>::template rebind_alloc<node_type>;

    using value_type            = typename node_type::value_type;

    using iterator              = void;
    using size_type             = size_t;

    skiplist():
        _M_node_alloc(),
        _M_head(nullptr),
        _M_level_now(0),
        _M_counter(0)
    {
        _M_head = _M_constructNode(_MaxLevel);
    }
    ~skiplist(){
        clear();
        _M_destroyNode(_M_head);
    }

    template <typename ..._Args>
    iterator insert(_Args&& ...args){
        value_type val(std::forward<_Args>(args)...);
        return insert(val);
    }
    iterator insert(const value_type &val){
        node_type *pre [std::max(_M_level_now, layer_size_type(1))];
        _M_findPrecursor(val, pre);

        // 存在相同节点，结束
        if(nullptr != pre[0]->forward[0] && !_Comp{}(pre[0]->forward[0]->value, val)){
            return;
        }

        // 生成随机层数
        layer_size_type node_level = _M_randomLevel();
        if(node_level > _M_level_now){
            for(layer_size_type l = _M_level_now; l < node_level; ++l){
                pre[l] = _M_head;
            }
            _M_level_now = node_level;
        }

        node_type *isrt = _M_constructNode(node_level, val);
        for(layer_size_type l = 0; l < node_level; ++l){
            isrt->forward[l] = pre[l]->forward[l];
            pre[l]->forward[l] = isrt;
        }
        ++_M_counter;
    }
    iterator insert(value_type &&val){
        node_type *pre [std::max(_M_level_now, layer_size_type(1))];
        _M_findPrecursor(val, pre);

        // 存在相同节点，结束
        if(nullptr != pre[0]->forward[0] && !_Comp{}(pre[0]->forward[0]->value, val)){
            return;
        }

        // 生成随机层数
        layer_size_type node_level = _M_randomLevel();
        if(node_level > _M_level_now){
            for(layer_size_type l = _M_level_now; l < node_level; ++l){
                pre[l] = _M_head;
            }
            _M_level_now = node_level;
        }

        node_type *isrt = _M_constructNode(node_level, std::move(val));
        for(layer_size_type l = 0; l < node_level; ++l){
            isrt->forward[l] = pre[l]->forward[l];
            pre[l]->forward[l] = isrt;
        }
        ++_M_counter;
    }

    void remove(const value_type &value){
        node_type *pre [std::max(_M_level_now, layer_size_type(1))] {0};
        _M_findPrecursor(value, pre);
        
        // 不存在相同节点，结束
        node_type *rm = pre[0]->forward[0];
        if(nullptr == rm || _Comp{}(rm->value, value)){
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

    bool find(const value_type &val){
        node_type *cur = _M_head;
        layer_size_type l = _M_level_now - 1;
        do{
            while(nullptr != cur->forward[l] && _Comp{}(val, cur->forward[l]->value)){
                cur = cur->forward[l];
            }
        }while(l-- > 0);
        return nullptr != cur->forward[0] && !_Comp{}(cur->forward[0]->value, val);
    }
    void clear(){
        if(_M_level_now > 1)
            std::fill_n(&_M_head->forward[1], _M_level_now-1, nullptr);
        node_type *rm;
        while(nullptr != _M_head->forward[0]){
            rm = _M_head->forward[0];
            _M_head->forward[0] = rm->forward[0];
            _M_node_alloc.deallocate(rm, 1);
        }
        _M_level_now = 0;
        _M_counter = 0;
    }
    size_type size() const{return _M_counter;}

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
        if(!seeded){
            srand(static_cast<unsigned>(time(nullptr)));
            seeded = true;
        }
        return static_cast<layer_size_type>(1 + rand() % (
            _MaxLevel == _M_level_now ? _M_level_now : _M_level_now + 1
        ));
    }

    void _M_findPrecursor(const value_type &val, node_type **pre){
        node_type *cur = _M_head;
        layer_size_type l = std::max(_M_level_now, layer_size_type(1)) - 1;
        do{
            while(nullptr != cur->forward[l] && _Comp{}(val, cur->forward[l]->value)){
                cur = cur->forward[l];
            }
            pre[l] = cur;
        }
        while(l-- > 0);
    }

private:
    node_alloc_type     _M_node_alloc;
    node_type           *_M_head;
    layer_size_type     _M_level_now;
    size_type           _M_counter;
};

}; // namespace skipList

