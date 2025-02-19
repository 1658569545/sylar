#include "mutex.h"
#include "macro.h"
#include "scheduler.h"

namespace sylar {

Semaphore::Semaphore(uint32_t count) {
    //初始化信号量
    //0代表在线程间共享，而非线程之间共享
    //count是信号量初始值
    if(sem_init(&m_semaphore, 0, count)) {
        throw std::logic_error("sem_init error");
    }
}

Semaphore::~Semaphore() {
    //销毁信号量
    sem_destroy(&m_semaphore);
}

void Semaphore::wait() {
    // 尝试获取信号量，如果信号量值大于 0，则减 1 并立即返回；否则阻塞当前线程，直到信号量值大于 0。
    if(sem_wait(&m_semaphore)) {
        throw std::logic_error("sem_wait error");
    }
}

void Semaphore::notify() {
    // 释放信号量，增加信号量值。如果有线程因 wait 阻塞，将唤醒其中一个线程。
    if(sem_post(&m_semaphore)) {
        throw std::logic_error("sem_post error");
    }
}

FiberSemaphore::FiberSemaphore(size_t initial_concurrency)
    :m_concurrency(initial_concurrency) {
}

FiberSemaphore::~FiberSemaphore() {
    SYLAR_ASSERT(m_waiters.empty());
}

bool FiberSemaphore::tryWait() {
    SYLAR_ASSERT(Scheduler::GetThis());
    {
        MutexType::Lock lock(m_mutex);
        if(m_concurrency > 0u) {
            --m_concurrency;
            return true;
        }
        return false;
    }
}

void FiberSemaphore::wait() {
    SYLAR_ASSERT(Scheduler::GetThis());
    {
        MutexType::Lock lock(m_mutex);
        if(m_concurrency > 0u) {
            --m_concurrency;
            return;
        }
        m_waiters.push_back(std::make_pair(Scheduler::GetThis(), Fiber::GetThis()));
    }
    Fiber::YieldToHold();
}

void FiberSemaphore::notify() {
    MutexType::Lock lock(m_mutex);
    if(!m_waiters.empty()) {
        auto next = m_waiters.front();
        m_waiters.pop_front();
        next.first->schedule(next.second);
    } else {
        ++m_concurrency;
    }
}

}
