/**
 * @file fiber.h
 * @brief 协程封装
 * @details 基于ucontext_t实现非对称协程。子协程只能和线程主协程切换，而不能和另一个子协程切换
 * @author zq
 */
#ifndef __SYLAR_FIBER_H__
#define __SYLAR_FIBER_H__

#include <memory>
#include <functional>
#include <ucontext.h>

namespace sylar {

class Scheduler;

/**
 *  协程又称为用户态线程，只执行于用户态，不会陷入内核态中,同时还是线程内部调度的基本单位，单线程下协程并不是并发执行，而是顺序执行的。
 *  同样是单线程环境下，一个协程的yield，必然对应另一个协程的resume。
 *  
 *  协程可以暂停执行并返回，并且可以从暂停点恢复然后继续执行
 * 
 *  非对称在于程序控制流转移到被调协程时使用的是 call/resume 操作，而当被调协程让出 CPU 时使用的却是 return/yield 操作。
 *  此外，协程间的地位也不对等，caller 与 callee 关系是确定的，不可更改的，非对称协程只能返回最初调用它的协程。
 * 
 *  对称协程（symmetric coroutines）则不一样，启动之后就跟启动之前的协程没有任何关系了。
 *  协程的切换操作，一般而言只有一个操作 — yield，用于将程序控制流转移给另外的协程。
 * 
 *  协程能够半路yield、再重新resume的关键是协程存储了函数在yield时间点的执行状态，这个状态称为协程上下文。
 *  协程上下文包含了函数在当前执行状态下的全部CPU寄存器的值，这些寄存器值记录了函数栈帧、代码的执行位置等信息，
 *  如果将这些寄存器的值重新设置给CPU，就相当于重新恢复了函数的运行。
 *  在Linux系统里这个上下文用ucontext_t结构体来表示，通过getcontext()来获取。 
 */

//协程类
//std::enable_shared_from_this：支持Fiber在自身的成员函数中通过shared_from_this获取自身的 std::shared_ptr。
class Fiber : public std::enable_shared_from_this<Fiber> {
friend class Scheduler;
public:
    typedef std::shared_ptr<Fiber> ptr;

    //协程状态
    enum State {
        INIT,   //初始化状态
        HOLD,   //暂停状态
        EXEC,   //执行中状态
        TERM,   //结束状态
        READY,  //可执行状态
        EXCEPT  //异常状态
    };
private:
    /**
     * @brief 无参构造函数
     * @attention 每个线程第一个协程的构造
     * @details 同时，private的构造函数是为了防止编译器自动生成无参构造函数，且不会为主协程分配栈空间
     */
    Fiber();

public:
    /**
     * @brief 构造函数
     * @param[in] cb 协程执行的函数
     * @param[in] stacksize 协程栈大小
     * @param[in] use_caller 是否在MainFiber上调度，感觉没啥大用，只是为了演示非对称模型
     */
    Fiber(std::function<void()> cb, size_t stacksize = 0, bool use_caller = false);
    
    ~Fiber();

    /**
     * @brief 重置协程执行函数,并设置状态
     * @pre getState() 为 INIT, TERM, EXCEPT
     * @post getState() = INIT
     */
    void reset(std::function<void()> cb);

    /**
     * @brief 将当前协程切换到运行状态, 调度协程 ---> 当前协程
     * @pre getState() != EXEC
     * @post getState() = EXEC
     * @details 即谁调用该函数，谁就会被放到前台来运行。
     */
    void swapIn();

    /**
     * @brief 将当前协程切换到后台, 当前协程 ---> 调度协程
     */
    void swapOut();

    /**
     * @brief 将当前线程切换到执行状态, 当前线程的主协程 ---> 当前协程
     * @pre 执行的为当前线程的主协程
     */
    void call();

    /**
     * @brief 将当前线程切换到后台, 当前协程 ---> 当前线程的主协程
     * @pre 执行的为该协程
     * @post 返回到线程的主协程
     */
    void back();

    //返回协程id
    uint64_t getId() const { return m_id;}

    //返回协程状态
    State getState() const { return m_state;}
public:

    /**
     * @brief 设置当前线程的运行协程
     * @param[in] f 运行协程
     */
    static void SetThis(Fiber* f);

    //返回当前所在的协程，第一次调用时，会创建一个主协程
    static Fiber::ptr GetThis();

    //将当前协程切换到后台,并设置为READY状态
    static void YieldToReady();

    //将当前协程切换到后台,并设置为HOLD状态             
    static void YieldToHold();

    //返回当前协程的总数量
    static uint64_t TotalFibers();

    /**
     * @brief 协程执行函数
     * @post 执行完成返回到调度协程
     * @details 当use_caller=false时，不使用主协程
     */
    static void MainFunc();

    /**
     * @brief 协程执行函数
     * @post 执行完成返回到主协程
     * @details 当use_caller=true时，使用主协程，感觉没啥大用，只是为了演示非对称模型
     */
    static void CallerMainFunc();

    //获取当前协程的id
    static uint64_t GetFiberId();
private:
    uint64_t m_id = 0;              // 协程id
    
    uint32_t m_stacksize = 0;       // 协程运行栈大小
    
    State m_state = INIT;           // 协程状态
    
    ucontext_t m_ctx;               // 协程上下文
    
    void* m_stack = nullptr;        // 协程运行栈指针
    
    std::function<void()> m_cb;     // 协程运行函数

};

}

#endif
