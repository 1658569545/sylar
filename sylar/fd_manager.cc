#include "fd_manager.h"
#include "hook.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

namespace sylar {

FdCtx::FdCtx(int fd)
    :m_isInit(false)
    ,m_isSocket(false)
    ,m_sysNonblock(false)
    ,m_userNonblock(false)
    ,m_isClosed(false)
    ,m_fd(fd)
    ,m_recvTimeout(-1)
    ,m_sendTimeout(-1) {
    init();
}

FdCtx::~FdCtx() {
}

bool FdCtx::init() {
    if(m_isInit) {
        return true;
    }
    //初始化接收和发送超时，默认设置为 -1，表示没有超时限制。
    m_recvTimeout = -1;
    m_sendTimeout = -1;

    //获取文件描述符的状态
    struct stat fd_stat;

    //如果 fstat() 调用失败（返回 -1），则设置 m_isInit 为 false，并认为该文件描述符不是套接字（m_isSocket = false）
    if(-1 == fstat(m_fd, &fd_stat)) {
        m_isInit = false;
        m_isSocket = false;
    } 
    //调用成功则继续判断是否为socket
    else {
        m_isInit = true;
        // 判断文件是否为socket
        m_isSocket = S_ISSOCK(fd_stat.st_mode);
    }

    // 如果是socket
    if(m_isSocket) {
        //获取socket的文件状态标志
        int flags = fcntl_f(m_fd, F_GETFL, 0);
        //如果不是非阻塞，则设置为非阻塞
        if(!(flags & O_NONBLOCK)) {
            fcntl_f(m_fd, F_SETFL, flags | O_NONBLOCK);
        }
        m_sysNonblock = true;
    } 
    //如果不是
    else {
        m_sysNonblock = false;
    }

    //用户非阻塞标志和关闭状态
    m_userNonblock = false;
    m_isClosed = false;


    return m_isInit;
}

void FdCtx::setTimeout(int type, uint64_t v) {
    if(type == SO_RCVTIMEO) {
        m_recvTimeout = v;
    } 
    else {
        m_sendTimeout = v;
    }
}

uint64_t FdCtx::getTimeout(int type) {
    if(type == SO_RCVTIMEO) {
        return m_recvTimeout;
    } 
    else {
        return m_sendTimeout;
    }
}

FdManager::FdManager() {
    m_datas.resize(64);
}

FdCtx::ptr FdManager::get(int fd, bool auto_create) {
    //检查 fd 是否有效
    if(fd == -1) {
        return nullptr;
    }
    RWMutexType::ReadLock lock(m_mutex);

    if((int)m_datas.size() <= fd) {
        if(auto_create == false) {
            return nullptr;
        }
    } 
    else {
        //如果该文件描述符已经存在（即 m_datas[fd] 非空），或者 auto_create 为 false，则直接返回对应的 FdCtx。
        if(m_datas[fd] || !auto_create) {
            return m_datas[fd];
        }
    }
    lock.unlock();

    RWMutexType::WriteLock lock2(m_mutex);
    //创建新上下文并添加到 m_datas
    FdCtx::ptr ctx(new FdCtx(fd));
    if(fd >= (int)m_datas.size()) {
        m_datas.resize(fd * 1.5);
    }
    m_datas[fd] = ctx;
    return ctx;
}

void FdManager::del(int fd) {
    RWMutexType::WriteLock lock(m_mutex);
    if((int)m_datas.size() <= fd) {
        return;
    }
    m_datas[fd].reset();
}

}
