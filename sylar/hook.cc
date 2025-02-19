#include "hook.h"
#include <dlfcn.h>

#include "config.h"
#include "log.h"
#include "fiber.h"
#include "iomanager.h"
#include "fd_manager.h"
#include "macro.h"

sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");
namespace sylar {

static sylar::ConfigVar<int>::ptr g_tcp_connect_timeout =
    sylar::Config::Lookup("tcp.connect.timeout", 5000, "tcp connect timeout");

//线程局部变量，表示当前线程是否启用了钩子机制。
static thread_local bool t_hook_enable = false;

// 通过 dlsym 动态链接来替换一些系统调用（如 socket, connect, read, write 等）为自定义的版本，
// 从而实现对这些系统调用的增强功能，通常是在协程或异步 I/O 模型下的支持。

#define HOOK_FUN(XX) \
    XX(sleep) \
    XX(usleep) \
    XX(nanosleep) \
    XX(socket) \
    XX(connect) \
    XX(accept) \
    XX(read) \
    XX(readv) \
    XX(recv) \
    XX(recvfrom) \
    XX(recvmsg) \
    XX(write) \
    XX(writev) \
    XX(send) \
    XX(sendto) \
    XX(sendmsg) \
    XX(close) \
    XX(fcntl) \
    XX(ioctl) \
    XX(getsockopt) \
    XX(setsockopt)
void hook_init() {
    //这确保了钩子初始化代码只执行一次。
    static bool is_inited = false;
    if(is_inited) {
        return;
    }
    
    //dlsym(RTLD_NEXT, #name)：该函数通过 dlsym 动态加载下一个共享库中的符号（即系统默认的库），在这里会加载原始的系统调用实现。
    //name ## _f = (name ## _fun)dlsym(RTLD_NEXT, #name)：将原始的系统调用地址赋值给相应的钩子函数指针（如 sleep_f, socket_f 等）。
#define XX(name) name ## _f = (name ## _fun)dlsym(RTLD_NEXT, #name);
    
    // HOOK_FUN(XX) 会展开成一系列钩子函数的初始化操作。例如，XX(sleep) 会变成 sleep_f = (sleep_fun)dlsym(RTLD_NEXT, "sleep");，
    // 这样就把 sleep 函数的地址存储到 sleep_f 中。
    HOOK_FUN(XX);
#undef XX
}

//TCP连接超时配置，初始值为 -1，表示没有超时限制。
static uint64_t s_connect_timeout = -1;


struct _HookIniter {
    _HookIniter() {
        hook_init();
        //监听一个配置变量 tcp.connect.timeout，这个变量控制 TCP 连接的超时时间，并在其值变化时更新 s_connect_timeout。
        s_connect_timeout = g_tcp_connect_timeout->getValue();
        g_tcp_connect_timeout->addListener([](const int& old_value, const int& new_value){
                SYLAR_LOG_INFO(g_logger) << "tcp connect timeout changed from "
                                         << old_value << " to " << new_value;
                s_connect_timeout = new_value;
        });
    }
};

//静态变量 s_hook_initer 会在程序启动时自动构造，从而初始化钩子机制和超时配置。这意味着在 main() 函数执行之前，钩子和配置已经完成初始化。
static _HookIniter s_hook_initer;

bool is_hook_enable() {
    return t_hook_enable;
}

void set_hook_enable(bool flag) {
    t_hook_enable = flag;
}

}

//用来标记定时器是否被取消。
struct timer_info {
    int cancelled = 0;
};



/**
 * @brief do_io 函数（一个模板函数）：自定义的 I/O 操作
 * @param[in] OriginFun 表示原始的 I/O 函数类型。例如，这可以是 read、write 或其他类似的系统调用函数指针类型。
 * @param[in] Args... 这个是可变参数模板，用来传递原始函数所需的参数（如 fd，buf，len 等）。
 * @param[in] fd I/O 操作的文件描述符，通常是一个套接字或文件句柄。
 * @param[in] fun 原始的 I/O 操作函数，如 read、write 等
 * @param[in] hook_fun_name 当前函数的名称，用于日志记录和调试。
 * @param[in] event 要监听的 I/O 事件，可能是读取事件（READ）或写入事件（WRITE）。
 * @param[in] timeout_so 控制超时的 socket 选项。
 * @attention 旨在为 I/O 操作（如读写、发送接收等）提供钩子（hook）支持，并结合异步 I/O 和定时器机制来处理阻塞和超时。
 * 它的目的是支持非阻塞的异步 I/O 操作，确保在执行 I/O 时能够正确处理超时、信号中断和阻塞情况。
 * 
 * do_io 函数通过钩子机制将标准的 I/O 操作（如 read、write）转化为异步操作。它处理以下情况：
 *  1. 在 I/O 操作无法立即完成时（如套接字非阻塞），会注册超时定时器并将操作挂起。
 *  2. 使用定时器机制实现超时控制，在超时情况下取消事件和操作。
 *  3. 支持重试逻辑，以应对系统调用中断（EINTR）的情况（retry）。
 */
template<typename OriginFun, typename... Args>
static ssize_t do_io(int fd, OriginFun fun, const char* hook_fun_name,
        uint32_t event, int timeout_so, Args&&... args) {
    
    // 是否启用钩子函数：
    if(!sylar::t_hook_enable) {
        // 如果钩子没有启用，直接调用原始的 I/O 函数（如 read 或 write）。
        return fun(fd, std::forward<Args>(args)...);
    }
    
    // 获取文件描述符上下文：
    sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
    if(!ctx) {
        //如果获取失败，说明 fd 不存在上下文，直接调用原始的 I/O 函数。
        return fun(fd, std::forward<Args>(args)...);
    }

    //如果文件描述符已经关闭，返回 -1，并设置 errno = EBADF（表示文件描述符无效）
    if(ctx->isClose()) {
        errno = EBADF;
        return -1;
    }

    //如果 fd 不是一个套接字或者该套接字已被标记为非阻塞，则直接调用原始的 I/O 函数。
    if(!ctx->isSocket() || ctx->getUserNonblock()) {
        return fun(fd, std::forward<Args>(args)...);
    }


    // ------------------------------------- hook要做了 -------------------------------异步IO
    
    //通过 getTimeout(timeout_so) 获取超时值，timeout_so 表示 socket 的超时配置。
    uint64_t to = ctx->getTimeout(timeout_so);

    //创建一个 timer_info 结构体的 shared_ptr，用来跟踪定时器的状态，特别是超时信息。
    std::shared_ptr<timer_info> tinfo(new timer_info);

retry: 
    //尝试执行原始 I/O 操作。如果返回 -1 且 errno 为 EINTR（表示被中断的系统调用），则重试该操作。
    ssize_t n = fun(fd, std::forward<Args>(args)...);
    while(n == -1 && errno == EINTR) {
        n = fun(fd, std::forward<Args>(args)...);
    }

    //如果 n == -1 且 errno == EAGAIN，表示当前操作不能立即完成，通常是因为套接字处于非阻塞状态，I/O 操作暂时无法完成。
    // 因此进行协程调度，采用和sleep等函数同样的调用逻辑。
    if(n == -1 && errno == EAGAIN) {
        //创建一个 IOManager 对象和一个 timer_info 的弱引用 winfo，用来在超时发生时取消 I/O 操作。
        sylar::IOManager* iom = sylar::IOManager::GetThis();
        sylar::Timer::ptr timer;
        std::weak_ptr<timer_info> winfo(tinfo);

        //添加定时器和事件
        //如果超时值 to 不为 -1，则会添加一个定时器来处理超时。定时器回调中会检查 timer_info，如果超时会标记为 ETIMEDOUT 并取消该事件并执行一次回调。
        if(to != (uint64_t)-1) {
            timer = iom->addConditionTimer(to, [winfo, fd, iom, event]() {
                auto t = winfo.lock();
                if(!t || t->cancelled) {
                    return;
                }
                t->cancelled = ETIMEDOUT;
                // 如果已经超时，则取消事件强制唤醒
                iom->cancelEvent(fd, (sylar::IOManager::Event)(event));
            }, winfo);
        }

        //将 fd 和 event 添加到 IOManager 事件列表中。如果添加事件失败，取消定时器并返回错误。
        int rt = iom->addEvent(fd, (sylar::IOManager::Event)(event));
        if(SYLAR_UNLIKELY(rt)) {
            SYLAR_LOG_ERROR(g_logger) << hook_fun_name << " addEvent(" << fd << ", " << event << ")";
            if(timer) {
                timer->cancel();
            }
            return -1;
        } 

        else {
             /*	addEvent成功，将当前执行的协程挂起，等待事件完成。
             *	只有两种情况会从这回来：
             * 	1) 超时了， timer cancelEvent triggerEvent会唤醒回来，即上面的条件定时器部分，且需要自己设置了定时器（即to==-1）
             * 	2) addEvent数据回来了会唤醒回来 
             */
            sylar::Fiber::YieldToHold();
            // 回来了还有定时器就取消
            if(timer) {
                timer->cancel();
            }
            if(tinfo->cancelled) {
                //如果 timer_info 的 cancelled 被设置为超时错误（ETIMEDOUT），则返回 -1 并设置 errno 为超时错误。
                errno = tinfo->cancelled;
                return -1;
            }
            goto retry;
        }
    }
    
    return n;
}


extern "C" {
#define XX(name) name ## _fun name ## _f = nullptr;
    HOOK_FUN(XX);
#undef XX

/*
当 sylar::t_hook_enable 为 true 时，sleep 函数会利用协程和定时器机制替代传统的 sleep 系统调用。
当前协程（fiber）会被挂起，等待指定的秒数（seconds）。而非传统的阻塞制定秒数
在 seconds 秒后，IOManager 会通过定时器触发，恢复协程的执行。
*/

unsigned int sleep(unsigned int seconds) {
    if(!sylar::t_hook_enable) {
        // 如果没有启用 hook，则直接调用原生的 sleep 函数
        return sleep_f(seconds);
    }

    // 获取当前协程实例
    sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();

    // 获取 IOManager 实例（负责调度 IO 事件的管理）
    sylar::IOManager* iom = sylar::IOManager::GetThis();

    // 添加一个定时器到 IOManager，定时器将在 seconds * 1000 毫秒后触发，执行调度任务
    // 使用了 std::bind 来绑定一个函数指针 &sylar::IOManager::schedule，它会在定时器触发时调度当前协程（即 fiber）。
    iom->addTimer(seconds * 1000, std::bind((void(sylar::Scheduler::*)
            (sylar::Fiber::ptr, int thread))&sylar::IOManager::schedule
            ,iom, fiber, -1));

    // 当前协程将主动挂起并让出控制权（相当于 "yield"）
    sylar::Fiber::YieldToHold();
    return 0;
}

//与sleep同理
int usleep(useconds_t usec) {
    if(!sylar::t_hook_enable) {
        return usleep_f(usec);
    }
    sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
    sylar::IOManager* iom = sylar::IOManager::GetThis();
    iom->addTimer(usec / 1000, std::bind((void(sylar::Scheduler::*)
            (sylar::Fiber::ptr, int thread))&sylar::IOManager::schedule
            ,iom, fiber, -1));
    sylar::Fiber::YieldToHold();
    return 0;
}

//同理
int nanosleep(const struct timespec *req, struct timespec *rem) {
    if(!sylar::t_hook_enable) {
        return nanosleep_f(req, rem);
    }

    int timeout_ms = req->tv_sec * 1000 + req->tv_nsec / 1000 /1000;
    sylar::Fiber::ptr fiber = sylar::Fiber::GetThis();
    sylar::IOManager* iom = sylar::IOManager::GetThis();
    iom->addTimer(timeout_ms, std::bind((void(sylar::Scheduler::*)
            (sylar::Fiber::ptr, int thread))&sylar::IOManager::schedule
            ,iom, fiber, -1));
    sylar::Fiber::YieldToHold();
    return 0;
}

//socket和connect_with_timeout都是为了支持异步操作，因此在钩子机制下，均先调用原生的函数，如果调用成功，则会直接调用原生函数。

int socket(int domain, int type, int protocol) {
    if(!sylar::t_hook_enable) {
        //如果钩子机制未启用（sylar::t_hook_enable 为 false），则直接调用原生的 socket_f，即系统的 socket 函数创建一个普通的套接字，并返回。
        return socket_f(domain, type, protocol);
    }

    //如果启用了钩子机制，则首先调用原生 socket_f 函数来创建套接字。
    int fd = socket_f(domain, type, protocol);
    if(fd == -1) {
        return fd;
    }

    //用于将新创建的套接字文件描述符 fd 注册到 FdMgr 中。
    sylar::FdMgr::GetInstance()->get(fd, true);
    return fd;
}

/*
实现了一个自定义的、支持超时和异步操作的 connect 函数。其工作流程如下：
 1. 非钩子模式：直接调用原生的 connect。
 2. 钩子模式：首先检查文件描述符的状态，如果是有效的 socket 且不需要立即返回，则：
        如果设置了超时，添加一个定时器。
        向 IOManager 添加写事件并挂起当前协程。
        如果事件触发或者超时，恢复协程执行，检查连接状态。
 3. 超时处理：如果在指定超时时间内未完成连接，则取消写事件并返回超时错误。
*/
int connect_with_timeout(int fd, const struct sockaddr* addr, socklen_t addrlen, uint64_t timeout_ms) {
    SYLAR_LOG_DEBUG(g_logger)<<"connect_with_timeout start";
    if(!sylar::t_hook_enable) {
        // 如果没有启动hook机制，使用原生的connect，将进行阻塞的连接操作。
        return connect_f(fd, addr, addrlen);
    }

    // 获取文件描述符 fd 的上下文（FdCtx）。文件描述符上下文中包含了关于文件描述符的各种状态信息。
    sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
    if(!ctx || ctx->isClose()) {
        errno = EBADF;
        return -1;
    }

    // 如果文件描述符不是一个套接字（!ctx->isSocket()），则调用原生的 connect_f（即系统的 connect），进行标准的连接操作。
    if(!ctx->isSocket()) {
        return connect_f(fd, addr, addrlen);
    }

    // 如果文件描述符的上下文中标记为非阻塞（通过 ctx->getUserNonblock() 获取），
    // 则直接调用原生 connect_f，不进行异步操作，依然是一个标准的连接调用。
    if(ctx->getUserNonblock()) {
        return connect_f(fd, addr, addrlen);
    }

    // 调用原生的 connect_f，尝试进行连接。如果连接成功（n == 0），则立即返回 0。
    // 如果连接失败（n != -1）且错误码不是 EINPROGRESS，表示连接失败或其他错误，直接返回 n（即 -1 或其他错误码）。
    // EINPROGRESS代表连接还在进行中
    int n = connect_f(fd, addr, addrlen);
    if(n == 0) {
        return 0;
    } 
    else if(n != -1 || errno != EINPROGRESS) {
        return n;
    }

    // 获取 IOManager 实例，用于管理和调度异步操作。
    sylar::IOManager* iom = sylar::IOManager::GetThis();
    sylar::Timer::ptr timer;

    // 存储定时器的信息
    std::shared_ptr<timer_info> tinfo(new timer_info);
    std::weak_ptr<timer_info> winfo(tinfo);

    // 如果 timeout_ms 不等于 -1，即设置了超时值，程序会向 IOManager 添加一个定时器（addConditionTimer）。
    // 定时器将在指定的超时时间后触发，回调函数会检查定时器是否已被取消，如果没有则标记为超时（ETIMEDOUT）
    // 并取消与 fd 相关的 WRITE 事件。
    if(timeout_ms != (uint64_t)-1) {
        timer = iom->addConditionTimer(timeout_ms, [winfo, fd, iom]() {
                auto t = winfo.lock();
                if(!t || t->cancelled) {
                    return;
                }
                t->cancelled = ETIMEDOUT;
                // 超时之后，会取消事件并且强制唤醒一次
                iom->cancelEvent(fd, sylar::IOManager::WRITE);
        }, winfo);
    }
    
    //将文件描述符 fd 的写事件添加到事件队列。
    int rt = iom->addEvent(fd, sylar::IOManager::WRITE);

    if(rt == 0) {
        /*	addEvent成功之后，会将当前执行的协程挂起，等待事件完成。
         *	只有两种情况会从这回来：
         * 	1) 超时了， timer cancelEvent triggerEvent会唤醒回来，即上面的条件定时器部分，且需要自己设置了定时器（即to==-1）
         * 	2) addEvent数据回来了会唤醒回来 
         */
        sylar::Fiber::YieldToHold();

        //如果定时器存在，则在挂起并回来之后取消定时器。
        if(timer) {
            timer->cancel();
        }
        //如果定时器被触发并标记为超时（tinfo->cancelled），则设置 errno 为超时错误并返回 -1。
        if(tinfo->cancelled) {
            errno = tinfo->cancelled;
            return -1;
        }
    } 
    //如果事件添加失败，则取消定时器并记录错误日志。
    else {
        if(timer) {
            timer->cancel();
        }
        SYLAR_LOG_ERROR(g_logger) << "connect addEvent(" << fd << ", WRITE) error";
    }

    //检查连接错误

    int error = 0;
    socklen_t len = sizeof(int);

    //调用 getsockopt(fd, SOL_SOCKET, SO_ERROR) 获取套接字连接的错误状态。
    //如果 error 非零，则表示连接失败，将 errno 设置为该错误码并返回 -1。
    if(-1 == getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len)) {
        return -1;
    }

    //如果 error 为 0，则连接成功，返回 0。
    if(!error) {
        return 0;
    } 
    else {
        errno = error;
        return -1;
    }
}

//调用connect_with_timeout
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    return connect_with_timeout(sockfd, addr, addrlen, sylar::s_connect_timeout);
}


/**
 * 以下函数均是对原始的系统调用进行包装，以实现异步I/O操作。具体参数可以查看手册和参考do_io函数。
 * 如果 do_io 发现操作不能立即完成，它会将当前操作添加到 IOManager 中，并等待事件触发。
 * 这意味着当前线程或协程会被挂起，直到操作可以完成或者超时发生。
 */
int accept(int s, struct sockaddr *addr, socklen_t *addrlen) {
    int fd = do_io(s, accept_f, "accept", sylar::IOManager::READ, SO_RCVTIMEO, addr, addrlen);
    if(fd >= 0) {
        sylar::FdMgr::GetInstance()->get(fd, true);
    }
    return fd;
}

ssize_t read(int fd, void *buf, size_t count) {
    return do_io(fd, read_f, "read", sylar::IOManager::READ, SO_RCVTIMEO, buf, count);
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
    return do_io(fd, readv_f, "readv", sylar::IOManager::READ, SO_RCVTIMEO, iov, iovcnt);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    return do_io(sockfd, recv_f, "recv", sylar::IOManager::READ, SO_RCVTIMEO, buf, len, flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen) {
    return do_io(sockfd, recvfrom_f, "recvfrom", sylar::IOManager::READ, SO_RCVTIMEO, buf, len, flags, src_addr, addrlen);
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
    return do_io(sockfd, recvmsg_f, "recvmsg", sylar::IOManager::READ, SO_RCVTIMEO, msg, flags);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return do_io(fd, write_f, "write", sylar::IOManager::WRITE, SO_SNDTIMEO, buf, count);
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
    return do_io(fd, writev_f, "writev", sylar::IOManager::WRITE, SO_SNDTIMEO, iov, iovcnt);
}

ssize_t send(int s, const void *msg, size_t len, int flags) {
    return do_io(s, send_f, "send", sylar::IOManager::WRITE, SO_SNDTIMEO, msg, len, flags);
}

ssize_t sendto(int s, const void *msg, size_t len, int flags, const struct sockaddr *to, socklen_t tolen) {
    return do_io(s, sendto_f, "sendto", sylar::IOManager::WRITE, SO_SNDTIMEO, msg, len, flags, to, tolen);
}

ssize_t sendmsg(int s, const struct msghdr *msg, int flags) {
    return do_io(s, sendmsg_f, "sendmsg", sylar::IOManager::WRITE, SO_SNDTIMEO, msg, flags);
}

// 对close进行包装
int close(int fd) {
    //钩子启用判断
    if(!sylar::t_hook_enable) {
        return close_f(fd);
    }

    //获取文件描述符上下文
    sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);

    //如果文件描述符上下文 ctx 存在，表示该文件描述符正在使用中（例如用于异步 I/O 操作）。
    //获取IOManager实例 --> 取消与fd相关的异步I/O操作 --> 从文件描述符管理器中删除fd
    if(ctx) {
        auto iom = sylar::IOManager::GetThis();
        if(iom) {
            iom->cancelAll(fd);
        }
        sylar::FdMgr::GetInstance()->del(fd);
    }
    return close_f(fd);
}

/*
对 fcntl 系统调用的钩子实现，用于拦截文件描述符的操作，特别是对于控制文件描述符的状态，如设置非阻塞模式等。
fcntl 函数用于修改或获取文件描述符的属性，操作包括设置文件描述符标志、获取文件描述符标志、文件锁、文件拥有者等信息。
*/
int fcntl(int fd, int cmd, ... /* arg */ ) {
    va_list va;
    va_start(va, cmd);
    switch(cmd) {
        case F_SETFL:
            {
                int arg = va_arg(va, int);
                va_end(va);
                sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
                if(!ctx || ctx->isClose() || !ctx->isSocket()) {
                    return fcntl_f(fd, cmd, arg);
                }
                ctx->setUserNonblock(arg & O_NONBLOCK);
                if(ctx->getSysNonblock()) {
                    arg |= O_NONBLOCK;
                } else {
                    arg &= ~O_NONBLOCK;
                }
                return fcntl_f(fd, cmd, arg);
            }
            break;
        case F_GETFL:
            {
                va_end(va);
                int arg = fcntl_f(fd, cmd);
                sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(fd);
                if(!ctx || ctx->isClose() || !ctx->isSocket()) {
                    return arg;
                }
                if(ctx->getUserNonblock()) {
                    return arg | O_NONBLOCK;
                } else {
                    return arg & ~O_NONBLOCK;
                }
            }
            break;
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
        case F_SETFD:
        case F_SETOWN:
        case F_SETSIG:
        case F_SETLEASE:
        case F_NOTIFY:
#ifdef F_SETPIPE_SZ
        case F_SETPIPE_SZ:
#endif
            {
                int arg = va_arg(va, int);
                va_end(va);
                return fcntl_f(fd, cmd, arg); 
            }
            break;
        case F_GETFD:
        case F_GETOWN:
        case F_GETSIG:
        case F_GETLEASE:
#ifdef F_GETPIPE_SZ
        case F_GETPIPE_SZ:
#endif
            {
                va_end(va);
                return fcntl_f(fd, cmd);
            }
            break;
        case F_SETLK:
        case F_SETLKW:
        case F_GETLK:
            {
                struct flock* arg = va_arg(va, struct flock*);
                va_end(va);
                return fcntl_f(fd, cmd, arg);
            }
            break;
        case F_GETOWN_EX:
        case F_SETOWN_EX:
            {
                struct f_owner_exlock* arg = va_arg(va, struct f_owner_exlock*);
                va_end(va);
                return fcntl_f(fd, cmd, arg);
            }
            break;
        default:
            va_end(va);
            return fcntl_f(fd, cmd);
    }
}

//对ioctl进行包装
int ioctl(int d, unsigned long int request, ...) {
    //这一部分是标准的变参处理代码。ioctl 系统调用通常具有变长参数，因此需要使用 va_list 来获取传入的可变参数。
    va_list va;
    va_start(va, request);          // 初始化了 va，使其指向 request 参数之后的第一个参数。

    void* arg = va_arg(va, void*);  // 获取了 request 参数之后的第一个参数，并将其存储在 arg 中。
    va_end(va);                     

    //检查是否是 FIONBIO 请求, FIONBIO 是一个常见的 ioctl 请求，用于设置文件描述符的非阻塞模式。
    if(FIONBIO == request) {
        // arg指向一个int值，表示是否设置为非阻塞。0表示设置为非阻塞，使用 !!*(int*)arg 将 arg 转换为布尔值 user_nonblock。   
        bool user_nonblock = !!*(int*)arg;

        //获取文件描述符上下文
        sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(d);

        //如果获取不到文件描述符上下文 (!ctx)，或者文件描述符已经关闭 (ctx->isClose())，
        //或者该文件描述符不是一个套接字 (!ctx->isSocket())，就直接调用原始的 ioctl_f(d, request, arg) 来执行操作。
        //这样可以确保在不需要特殊处理的情况下，保持原始行为。
        if(!ctx || ctx->isClose() || !ctx->isSocket()) {
            return ioctl_f(d, request, arg);
        }
        ctx->setUserNonblock(user_nonblock);
    }
    return ioctl_f(d, request, arg);
}

//会直接调用原始的系统调用
int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
    return getsockopt_f(sockfd, level, optname, optval, optlen);
}

// 包装
// 主要针对 SO_RCVTIMEO（接收超时）和 SO_SNDTIMEO（发送超时）进行特殊处理。它将 timeval 类型的超时值转换成毫秒，
// 并将超时值存储到 FdCtx（文件描述符上下文）中，这样可以在后续的 I/O 操作中使用这些超时值。
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
    // 判断是否启用钩子
    if(!sylar::t_hook_enable) {
        return setsockopt_f(sockfd, level, optname, optval, optlen);
    }
    if(level == SOL_SOCKET) {
        //处理 SO_RCVTIMEO（接受超时） 和 SO_SNDTIMEO（发送超时） 超时设置
        if(optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) {
            sylar::FdCtx::ptr ctx = sylar::FdMgr::GetInstance()->get(sockfd);
            if(ctx) {
                const timeval* v = (const timeval*)optval;
                //设置超时
                ctx->setTimeout(optname, v->tv_sec * 1000 + v->tv_usec / 1000);
            }
        }
    }
    return setsockopt_f(sockfd, level, optname, optval, optlen);
}

}
