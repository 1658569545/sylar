/**
 * @file hook.h
 * @brief hook函数封装
 * @details hook 操作是指通过替换或拦截系统调用
 * @attention hook模块封装了一些C标准库提供的API，socket IO相关的API。能够使同步API实现异步的性能。
 * @attention 只对socket fd进行了hook，如果操作的不是socket fd，那会直接调用系统原本的API
 * @author zq
 */

#ifndef __SYLAR_HOOK_H__
#define __SYLAR_HOOK_H__

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

/*
参考https://www.midlane.top/wiki/pages/viewpage.action?pageId=16417219
如果不hook，在单线程的情况下，一旦一个协程阻塞，就只能等待阻塞完成，才能执行下一个协程，
hook了之后，当前协程阻塞之后，便可以直接切换到其余协程，而原来的协程，会在定时器超时或者事件发生之后被重新唤醒
*/
namespace sylar {
    //当前线程是否hook
    bool is_hook_enable();

    //设置当前线程的hook状态
    void set_hook_enable(bool flag);
}

extern "C" {

//对以下函数进行hook，并且只对socket fd进行了hook，如果操作的不是socket fd，
//那会直接调用系统原本的API，而不是hook之后的API。


//extern 用于声明变量或函数是在其他文件中定义的
//sleep
typedef unsigned int (*sleep_fun)(unsigned int seconds);
extern sleep_fun sleep_f;

typedef int (*usleep_fun)(useconds_t usec);
extern usleep_fun usleep_f;

typedef int (*nanosleep_fun)(const struct timespec *req, struct timespec *rem);
extern nanosleep_fun nanosleep_f;

//socket
typedef int (*socket_fun)(int domain, int type, int protocol);
extern socket_fun socket_f;

typedef int (*connect_fun)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
extern connect_fun connect_f;

typedef int (*accept_fun)(int s, struct sockaddr *addr, socklen_t *addrlen);
extern accept_fun accept_f;

//read
/**
 * typedef ssize_t (*read_fun)(int fd, void *buf, size_t count);
 * 定义了一个函数指针类型 read_fun
 * 定义这个类型是为了声明与 read 系统调用格式完全相同的函数指针，以便在 hook 操作中用自定义函数替换系统调用。
 */
typedef ssize_t (*read_fun)(int fd, void *buf, size_t count);
extern read_fun read_f;

typedef ssize_t (*readv_fun)(int fd, const struct iovec *iov, int iovcnt);
extern readv_fun readv_f;

typedef ssize_t (*recv_fun)(int sockfd, void *buf, size_t len, int flags);
extern recv_fun recv_f;

typedef ssize_t (*recvfrom_fun)(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
extern recvfrom_fun recvfrom_f;

typedef ssize_t (*recvmsg_fun)(int sockfd, struct msghdr *msg, int flags);
extern recvmsg_fun recvmsg_f;

//write
typedef ssize_t (*write_fun)(int fd, const void *buf, size_t count);
extern write_fun write_f;

typedef ssize_t (*writev_fun)(int fd, const struct iovec *iov, int iovcnt);
extern writev_fun writev_f;

typedef ssize_t (*send_fun)(int s, const void *msg, size_t len, int flags);
extern send_fun send_f;

typedef ssize_t (*sendto_fun)(int s, const void *msg, size_t len, int flags, const struct sockaddr *to, socklen_t tolen);
extern sendto_fun sendto_f;

typedef ssize_t (*sendmsg_fun)(int s, const struct msghdr *msg, int flags);
extern sendmsg_fun sendmsg_f;

typedef int (*close_fun)(int fd);
extern close_fun close_f;

//
typedef int (*fcntl_fun)(int fd, int cmd, ... /* arg */ );
extern fcntl_fun fcntl_f;

typedef int (*ioctl_fun)(int d, unsigned long int request, ...);
extern ioctl_fun ioctl_f;

typedef int (*getsockopt_fun)(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
extern getsockopt_fun getsockopt_f;

typedef int (*setsockopt_fun)(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
extern setsockopt_fun setsockopt_f;

extern int connect_with_timeout(int fd, const struct sockaddr* addr, socklen_t addrlen, uint64_t timeout_ms);

}

#endif
