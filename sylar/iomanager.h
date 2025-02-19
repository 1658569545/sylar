/**
 * @file iomanager.h
 * @brief 基于Epoll的IO协程调度器
 * @author zq
 */
#ifndef __SYLAR_IOMANAGER_H__
#define __SYLAR_IOMANAGER_H__

#include "scheduler.h"
#include "timer.h"

namespace sylar {

/**
 * @brief 基于Epoll的IO协程调度器
 * @details IO协程调度关注的是FdContext信息，也就是文件描述符-事件-回调函数三元组，IOManager需要保存所有关注的三元组，并且在epoll_wait检测到描述符事件就绪时执行对应的回调函数。
 */
class IOManager : public Scheduler, public TimerManager {
public:
    typedef std::shared_ptr<IOManager> ptr;
    typedef RWMutex RWMutexType;

    //IO事件
    enum Event {
        /// 无事件
        NONE    = 0x0,
        /// 读事件(EPOLLIN)
        READ    = 0x1,
        /// 写事件(EPOLLOUT)
        WRITE   = 0x4,
    };
private:

    //Socket事件上下文类
    //每个socket fd都有一个FdContext，FdContext中会包括fd的值，fd上的所有事件，以及fd的所有的事件上下文
    struct FdContext {
        typedef Mutex MutexType;

        //事件上下文类
        //fd的每个事件都有一个事件上下文，保存这个事件的回调函数/协程以及调度器
        //对fd事件做了简化，只预留了读事件和写事件，所有的事件都被归类到这两类事件中
        struct EventContext {
            /// 调度器
            Scheduler* scheduler = nullptr;
            /// 事件回调协程
            Fiber::ptr fiber;
            /// 事件回调函数
            std::function<void()> cb;
        };

        /**
         * @brief 获取事件上下文类
         * @param[in] event 事件类型
         * @return 返回对应事件的上下文
         */
        EventContext& getContext(Event event);

        /**
         * @brief 重置事件上下文
         * @param[in, out] ctx 待重置的上下文类
         */
        void resetContext(EventContext& ctx);

        /**
         * @brief 触发事件
         * @param[in] event 事件类型
         */
        void triggerEvent(Event event);

        /// 读事件上下文
        EventContext read;
        /// 写事件上下文
        EventContext write;
        /// 事件关联的句柄
        int fd = 0;
        /// 当前的事件
        Event events = NONE;
        /// 事件的Mutex
        MutexType mutex;
    };

public:
    /**
     * @brief 构造函数
     * @param[in] threads 线程数量
     * @param[in] use_caller 是否将当前线程也作为调度线程，即如果为true，调度协程在main函数所在的线程，如果为false，则调度协程在一个新开的线程中。即是否将main线程也参与执行任务
     * @param[in] name 调度器的名称
     */
    IOManager(size_t threads = 1, bool use_caller = true, const std::string& name = "");

    /**
     * @brief 析构函数
     */
    ~IOManager();

    /**
     * @brief 在fd上添加事件
     * @param[in] fd socket句柄
     * @param[in] event 事件类型
     * @param[in] cb 事件回调函数
     * @return 添加成功返回0,失败返回-1
     */
    int addEvent(int fd, Event event, std::function<void()> cb = nullptr);

    /**
     * @brief 删除事件
     * @param[in] fd socket句柄
     * @param[in] event 事件类型
     * @attention 不会触发事件
     * @return 是否删除成功
     */
    bool delEvent(int fd, Event event);

    /**
     * @brief 取消事件
     * @param[in] fd socket句柄
     * @param[in] event 事件类型
     * @attention 如果该事件被注册过回调，那就触发一次回调事件
     * @return 是否删除成功
     */
    bool cancelEvent(int fd, Event event);

    /**
     * @brief 取消所有事件
     * @param[in] fd socket句柄
     */
    bool cancelAll(int fd);

    /**
     * @brief 返回当前的IOManager
     */
    static IOManager* GetThis();
protected:

    /**
     * @brief 通知调度协程、也就Scheduler::run()从idle中推出
     * @details Scheduler::run()每次从idle协程中退出之后，都会重新把任务队列里的所有任务执行完了再重新进入idle，如果没有调度协程处理于idle状态，那也就没必要发通知了
     */
    void tickle() override;

    /**
     * @brief 判断是否可以停止
     * @details 停止条件为：定时器为空 && 等待执行的事件数量为0 && scheduler可以stop
     */
    bool stopping() override;

    /**
     * @brief idle协程
     * @details 对于IO协程调度来说，应阻塞在等待IO事件上，idle退出的时机是epoll_wait返回，对应的操作是tickle或注册的IO事件发生
     */
    void idle() override;

    /**
     * @brief 当有定时器插入到头部时，要重新更新epoll_wait的超时时间，这里是唤醒idle协程以便于使用新的超时时间
     */
    void onTimerInsertedAtFront() override;

    /**
     * @brief 重置socket句柄上下文的容器大小
     * @param[in] size 容量大小
     */
    void contextResize(size_t size);

    /**
     * @brief 判断是否可以停止
     * @param[out] timeout 最近要出发的定时器事件间隔
     * @return 返回是否可以停止
     */
    bool stopping(uint64_t& timeout);
private:
    /// epoll 文件句柄，即内核事件表句柄
    int m_epfd = 0;
    /// pipe 文件句柄，fd[0]表示读端，fd[1] 表示写端，用来通知调度协程
    int m_tickleFds[2];
    /// 当前等待执行的事件数量
    std::atomic<size_t> m_pendingEventCount = {0};
    /// IOManager的Mutex
    RWMutexType m_mutex;
    /// socket事件上下文的容器
    std::vector<FdContext*> m_fdContexts;
};

}

#endif
