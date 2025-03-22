#include "iomanager.h"
#include "macro.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <string.h>
#include <unistd.h>

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

enum EpollCtlOp {
};

// 将 EpollCtlOp 类型的值格式化为可读字符串，并输出到 std::ostream 中。
static std::ostream& operator<< (std::ostream& os, const EpollCtlOp& op) {
    switch((int)op) {
#define XX(ctl) \
        case ctl: \
            return os << #ctl;
        XX(EPOLL_CTL_ADD);
        XX(EPOLL_CTL_MOD);
        XX(EPOLL_CTL_DEL);
        default:
            return os << (int)op;
    }
#undef XX
}
// 将 EPOLL_EVENTS 类型的值格式化为可读字符串，并输出到 std::ostream 中。
static std::ostream& operator<< (std::ostream& os, EPOLL_EVENTS events) {
    if(!events) {
        return os << "0";
    }
    bool first = true;
#define XX(E) \
    if(events & E) { \
        if(!first) { \
            os << "|"; \
        } \
        os << #E; \
        first = false; \
    }
    XX(EPOLLIN);
    XX(EPOLLPRI);
    XX(EPOLLOUT);
    XX(EPOLLRDNORM);
    XX(EPOLLRDBAND);
    XX(EPOLLWRNORM);
    XX(EPOLLWRBAND);
    XX(EPOLLMSG);
    XX(EPOLLERR);
    XX(EPOLLHUP);
    XX(EPOLLRDHUP);
    XX(EPOLLONESHOT);
    XX(EPOLLET);
#undef XX
    return os;
}

IOManager::FdContext::EventContext& IOManager::FdContext::getContext(IOManager::Event event) {
    switch(event) {
        case IOManager::READ:
            return read;
        case IOManager::WRITE:
            return write;
        default:
            SYLAR_ASSERT2(false, "getContext");
    }
    throw std::invalid_argument("getContext invalid event");
}

void IOManager::FdContext::resetContext(EventContext& ctx) {
    ctx.scheduler = nullptr;
    ctx.fiber.reset();
    ctx.cb = nullptr;
}

void IOManager::FdContext::triggerEvent(IOManager::Event event) {
    SYLAR_ASSERT(events & event);
    // 触发该事件就将该事件从注册事件中删掉
    events = (Event)(events & ~event);
    EventContext& ctx = getContext(event);
    if(ctx.cb) {
        ctx.scheduler->schedule(&ctx.cb);
    } 
    else {
        ctx.scheduler->schedule(&ctx.fiber);
    }
    ctx.scheduler = nullptr;
    return;
}

IOManager::IOManager(size_t threads, bool use_caller, const std::string& name)
    :Scheduler(threads, use_caller, name) {     //先执行Scheduler的构造函数

    // 创建内核事件表
    m_epfd = epoll_create(5000);
    SYLAR_ASSERT(m_epfd > 0);

    // 创建通信管道
    int rt = pipe(m_tickleFds);
    SYLAR_ASSERT(!rt);

    epoll_event event;
    memset(&event, 0, sizeof(epoll_event));
    
    // 注册读事件，设置边缘触发模式，即立即处理，并且一次处理完成
    event.events = EPOLLIN | EPOLLET;

    // fd关联pipe的读端    
    event.data.fd = m_tickleFds[0];

    // 将读端设置为非阻塞模式
    rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK);
    SYLAR_ASSERT(!rt);

    // 在内核事件表中添加监听事件
    // 将event注册到epoll的内核事件表中，现在epoll会关注event.data.fd上的可读事件了
    rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
    SYLAR_ASSERT(!rt);

    contextResize(32);
    SYLAR_LOG_DEBUG(g_logger)<<"IOManager() end";
    start();
}

IOManager::~IOManager() {
    stop();
    close(m_epfd);
    close(m_tickleFds[0]);
    close(m_tickleFds[1]);

    for(size_t i = 0; i < m_fdContexts.size(); ++i) {
        if(m_fdContexts[i]) {
            delete m_fdContexts[i];
        }
    }
}

void IOManager::contextResize(size_t size) {
    m_fdContexts.resize(size);

    for(size_t i = 0; i < m_fdContexts.size(); ++i) {
        if(!m_fdContexts[i]) {
            m_fdContexts[i] = new FdContext;
            m_fdContexts[i]->fd = i;
        }
    }
}

int IOManager::addEvent(int fd, Event event, std::function<void()> cb) {
    FdContext* fd_ctx = nullptr;
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() > fd) {
        fd_ctx = m_fdContexts[fd];
        lock.unlock();
    } 
    else {
        lock.unlock();
        RWMutexType::WriteLock lock2(m_mutex);
        contextResize(fd * 1.5);
        fd_ctx = m_fdContexts[fd];
    }

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);

    // 在添加 event 事件之前检查event事件是否在 fd_ctx->events 中已经存在。防止重复添加相同事件。
    // 同一个fd不允许重复添加相同的事件
    if(SYLAR_UNLIKELY(fd_ctx->events & event)) {
        SYLAR_LOG_ERROR(g_logger) << "addEvent assert fd=" << fd << " event=" << (EPOLL_EVENTS)event << " fd_ctx.event=" << (EPOLL_EVENTS)fd_ctx->events;
        SYLAR_ASSERT(!(fd_ctx->events & event));
    }

    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event epevent;

    // 设置边缘触发模式，添加原有的事件以及要注册的事件
    epevent.events = EPOLLET | fd_ctx->events | event;

    // 将fd_ctx存到data的指针中
    epevent.data.ptr = fd_ctx;

    // 注册事件，接下来，会在内核事件表m_epfd中监听fd上的所有事件
    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt) {
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", " << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ") fd_ctx->events=" << (EPOLL_EVENTS)fd_ctx->events;
        return -1;
    }

    ++m_pendingEventCount;

    // 将 fd_ctx 上注册的事件更新
    fd_ctx->events = (Event)(fd_ctx->events | event);
    
    // 获得event事件的事件上下文
    FdContext::EventContext& event_ctx = fd_ctx->getContext(event);

    SYLAR_ASSERT(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb);

    // 设置协程调度器
    event_ctx.scheduler = Scheduler::GetThis();

    // 为该事件设置回调函数或者回调协程，如果回调函数为空，则把当前协程当成回调执行体
    if(cb) {
        event_ctx.cb.swap(cb);
    } 
    else {
        event_ctx.fiber = Fiber::GetThis();
        SYLAR_ASSERT2(event_ctx.fiber->getState() == Fiber::EXEC ,"state=" << event_ctx.fiber->getState());
    }
    return 0;
}

bool IOManager::delEvent(int fd, Event event) {
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);

    // 不能重复删除
    if(SYLAR_UNLIKELY(!(fd_ctx->events & event))) {
        return false;
    }

    // 清除指定的事件，表示不关心这个事件了，如果清除之后结果为0，则从epoll_wait中删除该文件描述符
    Event new_events = (Event)(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;

    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt) {
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", " << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    --m_pendingEventCount;
    fd_ctx->events = new_events;

    // 重置该fd对应的event事件上下文
    FdContext::EventContext& event_ctx = fd_ctx->getContext(event);
    fd_ctx->resetContext(event_ctx);
    return true;
}

bool IOManager::cancelEvent(int fd, Event event) {
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);

    // 不能重复取消
    if(SYLAR_UNLIKELY(!(fd_ctx->events & event))) {
        return false;
    }

    // 从注册的事件集中删除event
    Event new_events = (Event)(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt) {
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
            << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    // 取消之后，触发一次事件
    fd_ctx->triggerEvent(event);
    --m_pendingEventCount;
    return true;
}

bool IOManager::cancelAll(int fd) {
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size() <= fd) {
        return false;
    }
    FdContext* fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if(!fd_ctx->events) {
        return false;
    }

    int op = EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = 0;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt) {
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
            << (EpollCtlOp)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
            << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    if(fd_ctx->events & READ) {
        fd_ctx->triggerEvent(READ);
        --m_pendingEventCount;
    }
    if(fd_ctx->events & WRITE) {
        fd_ctx->triggerEvent(WRITE);
        --m_pendingEventCount;
    }

    SYLAR_ASSERT(fd_ctx->events == 0);
    return true;
}

IOManager* IOManager::GetThis() {
    return dynamic_cast<IOManager*>(Scheduler::GetThis());
}

void IOManager::tickle() {
    // 如果没有空闲的线程来执行协程任务，就无须通知
    if(!hasIdleThreads()) {
        return;
    }
    SYLAR_LOG_DEBUG(g_logger) << "io_tickle";
    // 往管道中写一个字符，用来通知IO调度器
    int rt = write(m_tickleFds[1], "T", 1);
    SYLAR_ASSERT(rt == 1);
}

bool IOManager::stopping(uint64_t& timeout) {
    timeout = getNextTimer();
    // 定时器为空 && 等待执行的事件数量为0 && scheduler可以stop
    return timeout == ~0ull && m_pendingEventCount == 0 && Scheduler::stopping();

}

bool IOManager::stopping() {
    uint64_t timeout = 0;
    return stopping(timeout);
}

/**
 * @brief idle协程
 * @details 对于IO协程调度来说，应阻塞在等待IO事件上，idle退出的时机是epoll_wait返回，对应的操作是tickle或注册的IO事件就绪
 * 调度器无调度任务时会阻塞idle协程上，对IO调度器而言，idle状态应该关注两件事，一是有没有新的调度任务，对应Schduler::schedule()，
 * 如果有新的调度任务，那应该立即退出idle状态，并执行对应的任务；二是关注当前注册的所有IO事件有没有触发，如果有触发，那么应该执行
 * IO事件对应的回调函数
 */
void IOManager::idle() {
    SYLAR_LOG_DEBUG(g_logger) << "idle";

    // 一次epoll_wait最多检测256个就绪事件，如果就绪事件超过了这个数，那么会在下轮epoll_wati继续处理
    const uint64_t MAX_EVNETS = 256;

    // 存贮就绪的事件
    epoll_event* events = new epoll_event[MAX_EVNETS]();

    // 使用智能指针托管events， 离开idle自动释放
    std::shared_ptr<epoll_event> shared_events(events, [](epoll_event* ptr){
        delete[] ptr;
    });


    while(true) {
        // 下一个任务要执行的时间
        uint64_t next_timeout = 0;

        // 获得下一个执行任务的时间，并且判断是否达到停止条件，next_timeout存储下一个定时器还有多久执行
        if(SYLAR_UNLIKELY(stopping(next_timeout))) {
            SYLAR_LOG_INFO(g_logger) << "name =" << getName() << " idle stopping exit";
            break;
        }

        int rt = 0;
        do {
            // 最大超时时间
            static const int MAX_TIMEOUT = 3000;

            // 如果有定时器任务
            if(next_timeout != ~0ull) {
                next_timeout = (int)next_timeout > MAX_TIMEOUT ? MAX_TIMEOUT : next_timeout;
            } 
            // 没定时器任务就睡眠MAX_TIMEOUT
            else {
                next_timeout = MAX_TIMEOUT;
            }

            /*  
             * 阻塞在这里，但有3中情况能够唤醒epoll_wait
             * 1. 超时时间到了  
             * 2. 关注的 socket 有事件发生
             * 3. 通过 tickle 往 pipe 里发数据
             */
            rt = epoll_wait(m_epfd, events, MAX_EVNETS, (int)next_timeout);
            if(rt < 0 && errno == EINTR) {

            } 
            else {
                break;
            }
        } while(true);

        std::vector<std::function<void()> > cbs;

        // 获取所有已经超时的任务，将其存放在cbs中
        listExpiredCb(cbs);
        if(!cbs.empty()) {
            schedule(cbs.begin(), cbs.end());
            cbs.clear();
        }

        // 遍历所有发生的事件，根据epoll_event的私有指针找到对应的FdContext，进行事件处理
        for(int i = 0; i < rt; ++i) {
            epoll_event& event = events[i];

            // ticklefd[0]用于通知协程调度，这时只需要把管道里的内容读完即可，本轮idle结束之后，Scheduler::run会重新执行协程调度
            // 如果只有tickle()发生，那么读取完管道中的数据之后会退出idle，重新开始run
            if(event.data.fd == m_tickleFds[0]) {
                uint8_t dummy[256];
                while(read(m_tickleFds[0], dummy, sizeof(dummy)) > 0);
                continue;
            }

            // 获取事件的上下文
            FdContext* fd_ctx = (FdContext*)event.data.ptr;
            FdContext::MutexType::Lock lock(fd_ctx->mutex);
            /**
             * EPOLLERR: 出错，比如写读端已经关闭的pipe
             * EPOLLHUP: 套接字对端关闭
             * 出现这两种事件，应该同时触发fd的读和写事件，否则有可能出现注册的事件永远执行不到的情况
             * 在源码中，注册事件时内核会自动关注POLLERR和POLLHUP 
             */
            if(event.events & (EPOLLERR | EPOLLHUP)) {
                // 添加读事件、写事件、原来的事件
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
            }
            int real_events = NONE;
            if(event.events & EPOLLIN) {
                real_events |= READ;
            }
            if(event.events & EPOLLOUT) {
                real_events |= WRITE;
            }

            // 没有事件发生
            if((fd_ctx->events & real_events) == NONE) {
                continue;
            }

            // 剔除已经发生的事件
            int left_events = (fd_ctx->events & ~real_events);
            int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events = EPOLLET | left_events;
            int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);

            if(rt2) {
                SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", " << (EpollCtlOp)op << ", " << fd_ctx->fd << ", " << (EPOLL_EVENTS)event.events << "):"
                    << rt2 << " (" << errno << ") (" << strerror(errno) << ")";
                continue;
            }

            // 触发读事件
            if(real_events & READ) {
                fd_ctx->triggerEvent(READ);
                SYLAR_LOG_DEBUG(g_logger)<<"READ";
                --m_pendingEventCount;
            }

            // 触发一次写事件
            if(real_events & WRITE) {
                fd_ctx->triggerEvent(WRITE);
                SYLAR_LOG_DEBUG(g_logger)<<"WRITE";
                --m_pendingEventCount;
            }
        }

        Fiber::ptr cur = Fiber::GetThis();
        auto raw_ptr = cur.get();
        cur.reset();

        // idle协程退出
        raw_ptr->swapOut();
    }
}

void IOManager::onTimerInsertedAtFront() {
    tickle();
}

}
