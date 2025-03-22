/**
 * @file scheduler.h
 * @brief 协程调度器封装
 * @author zq
 * @details 可以解决协程模块一个子协程不能运行另一个子协程的缺陷
 * 子协程可以通过向调度器添加调度任务的方式来运行另一个子协程。
 */
#ifndef __SYLAR_SCHEDULER_H__
#define __SYLAR_SCHEDULER_H__

#include <memory>
#include <vector>
#include <list>
#include <iostream>
#include "fiber.h"
#include "thread.h"
#include "log.h"
namespace sylar {


/**
 * @brief 协程调度器
 * @attention 封装的是N-M的协程调度器,调度器内部有一个线程池,支持协程在线程池里面切换,即一个协程调度器对应N个线程，N个线程对应M个协程。
 * @attention 同时每个线程内也会有一个调度协程
 */
class Scheduler {
public:
    typedef std::shared_ptr<Scheduler> ptr;
    typedef Mutex MutexType;

    /**
     * @brief 构造函数
     * @param[in] threads 线程数量
     * @param[in] use_caller 是否将当前线程也作为调度线程，即如果为true，调度协程在main函数所在的线程，如果为false，则调度协程在另外一个线程。即是否将main线程也参与执行任务
     * @param[in] name 协程调度器名称
     */
    Scheduler(size_t threads = 1, bool use_caller = true, const std::string& name = "");

    virtual ~Scheduler();

    //返回协程调度器名称
    const std::string& getName() const { return m_name;}

    //返回协程调度器
    static Scheduler* GetThis();

    //返回当前线程的调度协程，每个线程都独有一份，除了在主线程中，其余的均为主协程
    static Fiber* GetMainFiber();

    /**
     * @brief 启动协程调度器
     * @details 创建一个线程池，并且每个线程池都会绑定run()为线程入口函数，这意味着每个线程都有一个调度协程（且为该线程的主协程），因为只有调度协程才会去执行run()函数来进行协程调度
     */
    void start();

    //停止协程调度器
    void stop();

    /**
     * @brief 调度协程
     * @param[in] fc 协程或函数
     * @param[in] thread 协程执行的线程id,-1代表可以任意线程上跑
     * @details 将一个协程任务分配到适当的线程执行队列中
     */
    template<class FiberOrCb>
    void schedule(FiberOrCb fc, int thread = -1) {
        bool need_tickle = false;
        {
            //SYLAR_LOG_INFO(SYLAR_LOG_ROOT())<<"任务加入";
            MutexType::Lock lock(m_mutex);
            // 将任务加入到队列中，若任务队列原来为空，则tickle
            need_tickle = scheduleNoLock(fc, thread);
            
        }

        if(need_tickle) {
            tickle();
        }
    }

    /**
     * @brief 批量调度协程
     * @param[in] begin 协程数组的开始
     * @param[in] end 协程数组的结束
     * @details 将多个协程任务分配到适当的线程执行队列中
     */
    template<class InputIterator>
    void schedule(InputIterator begin, InputIterator end) {
        bool need_tickle = false;
        {
            MutexType::Lock lock(m_mutex);
            while(begin != end) {
                need_tickle = scheduleNoLock(&*begin, -1) || need_tickle;
                ++begin;
            }
        }
        if(need_tickle) {
            tickle();
        }
    }
    //该方法用于切换到 指定线程 或 调度器 管理的协程中。
    //其主要功能是让当前协程切换到指定的线程，或在需要时主动挂起当前协程。
    void switchTo(int thread = -1);

    //将调度器的状态信息 输出到流 (例如：std::ostream)。
    std::ostream& dump(std::ostream& os);
protected:
    //通知协程调度器有任务了
    virtual void tickle();
    
    /**
     * @brief 协程调度函数，进行协程调度
     * @attention 只有调度协程才会执行该函数
     */
    void run();

    //返回是否可以停止
    virtual bool stopping();

    //协程无任务可调度时执行idle协程
    virtual void idle();

    //设置当前的协程调度器
    void setThis();

    //是否有空闲线程
    bool hasIdleThreads() { return m_idleThreadCount > 0;}
private:

    //将协程任务 fc 添加到调度器的任务队列 m_fibers 中，且不加锁。
    //fc可以是回调函数也可以是协程
    template<class FiberOrCb>
    bool scheduleNoLock(FiberOrCb fc, int thread) {
        //如果原来没有，则需要tickle()来通知调度协程，让其退出idle状态
        bool need_tickle = m_fibers.empty();
        FiberAndThread ft(fc, thread);
        if(ft.fiber || ft.cb) {
            //m_fibers.push_back(std::move(ft)); 
            m_fibers.push_back(ft);
        }
        return need_tickle;
    }

private:
    // 协程/函数 - 线程id 的二元组模式
    struct FiberAndThread {
        // 协程
        Fiber::ptr fiber;   
        // 协程执行函数
        std::function<void()> cb;   
        // 线程id 用于制定协程/函数在哪个线程上进行执行，如果为-1,则代表任意线程都可以
        int thread; 

        /**
         * @brief 构造函数
         * @param[in] f 协程
         * @param[in] thr 线程id
         * @details 确定协程在哪个线程上跑，-1代表任意线程都可以
         */
        FiberAndThread(Fiber::ptr f, int thr)
            :fiber(f), thread(thr) {
        }

        /**
         * @brief 构造函数
         * @param[in] f 协程指针
         * @param[in] thr 线程id
         * @post *f = nullptr
         * @details 通过swap将传入的 fiber 置空，使其引用计数-1
         */
        FiberAndThread(Fiber::ptr* f, int thr)
            :thread(thr) {
            fiber.swap(*f);
        }

        /**
         * @brief 构造函数
         * @param[in] f 协程执行函数
         * @param[in] thr 线程id
         * @details 确定回调在哪个线程上跑
         */
        FiberAndThread(std::function<void()> f, int thr)
            :cb(f), thread(thr) {
        }

        /**
         * @brief 构造函数
         * @param[in] f 协程执行函数指针
         * @param[in] thr 线程id
         * @post *f = nullptr
         * @details 通过swap将传入的 cb 置空，使其引用计数-1
         */
        FiberAndThread(std::function<void()>* f, int thr)
            :thread(thr) {
            cb.swap(*f);
        }

        //无参构造函数
        FiberAndThread()
            :thread(-1) {
        }

        //重置数据
        void reset() {
            fiber = nullptr;
            cb = nullptr;
            thread = -1;
        }
    };

private: 
    // Mutex
    MutexType m_mutex;      
    // 线程池
    std::vector<Thread::ptr> m_threads;     
    // 待执行的协程（任务）队列
    std::list<FiberAndThread> m_fibers;     
    // use_caller为true时，为main函数所在线程的调度协程
    Fiber::ptr m_rootFiber;     
    // 协程调度器名称
    std::string m_name;     

protected:
    // 线程池的线程ID数组
    std::vector<int> m_threadIds;  
    // 线程数量（不包含user_caller的数量） 
    size_t m_threadCount = 0;       
    // 工作线程数量
    std::atomic<size_t> m_activeThreadCount = {0};     
    // 空闲线程数量 
    std::atomic<size_t> m_idleThreadCount = {0};    
    // 是否正在停止（即是否调用了stop函数）   
    bool m_stopping = true;     
    // 是否自动停止
    bool m_autoStop = false;    
    // 主线程id，即main函数所在线程的id
    int m_rootThread = 0;       
};

//切换调度器（Scheduler）上下文的类
class SchedulerSwitcher : public Noncopyable {
public:
    SchedulerSwitcher(Scheduler* target = nullptr);
    ~SchedulerSwitcher();
private:
    Scheduler* m_caller;
};

}

#endif
