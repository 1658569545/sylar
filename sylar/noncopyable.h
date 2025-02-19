/**
 * @file noncopyable.h
 * @brief 不可拷贝对象封装
 * @author zq
 */
#ifndef __SYLAR_NONCOPYABLE_H__
#define __SYLAR_NONCOPYABLE_H__

namespace sylar {

/**
 * @brief 对象无法拷贝,赋值
 */
class Noncopyable {
public:
    //使用 = default 明确告诉编译器使用默认实现，而不需要开发者提供自定义逻辑
    Noncopyable() = default;
    ~Noncopyable() = default;

    //拷贝构造函数(禁用)
    Noncopyable(const Noncopyable&) = delete;

    //赋值函数(禁用)
    Noncopyable& operator=(const Noncopyable&) = delete;
};

}

#endif
