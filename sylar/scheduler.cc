#include "scheduler.h"
#include "log.h"
#include "macro.h"
#include "hook.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

/// 当前线程的调度器，同一个调度器下的所有线程共享同一个实例，整个系统中，只会有一个协程调度器。
static thread_local Scheduler* t_scheduler = nullptr;

/// 当前线程的调度协程，每个线程都独有一份
static thread_local Fiber* t_scheduler_fiber = nullptr;

Scheduler::Scheduler(size_t threads, bool use_caller, const std::string& name)
    :m_name(name) {
    SYLAR_ASSERT(threads > 0);

    // 调度协程在main函数所在的线程中
    if(use_caller) {
        // 会创建一个主协程，用来执行main函数中的相关代码
        sylar::Fiber::GetThis();

        // 由于main函数线程也将作为一个工作线程，（即该线程也会来执行任务队列中的协程任务），因此要--
        --threads;

        SYLAR_ASSERT(GetThis() == nullptr);
        t_scheduler = this;

        // 创建一个子协程，作为调度协程
        m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, true));
        sylar::Thread::SetName(m_name);

        // 设置主线程（main函数坐在线程）的调度协程，该调度协程是一个子协程
        t_scheduler_fiber = m_rootFiber.get();
         
        //将该线程 ID 存入线程 ID 列表 m_threadIds，供调度管理和状态输出使用。
        m_rootThread = sylar::GetThreadId();
        m_threadIds.push_back(m_rootThread);
    } 
    else {
        m_rootThread = -1;
    }
    m_threadCount = threads;
}

Scheduler::~Scheduler() {
    SYLAR_ASSERT(m_stopping);
    if(GetThis() == this) {
        t_scheduler = nullptr;
    }
}

Scheduler* Scheduler::GetThis() {
    return t_scheduler;
}

Fiber* Scheduler::GetMainFiber() {
    return t_scheduler_fiber;
}

void Scheduler::start() {
    MutexType::Lock lock(m_mutex);
    if(!m_stopping) {
        return;
    }
    m_stopping = false;
    SYLAR_ASSERT(m_threads.empty());

    m_threads.resize(m_threadCount);
    for(size_t i = 0; i < m_threadCount; ++i) {
        // 每个线程都会绑定run函数，则代表每个线程都会有一个调度协程
        m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this)
                            , m_name + "_" + std::to_string(i)));
        m_threadIds.push_back(m_threads[i]->getId());
    }
    lock.unlock();
    SYLAR_LOG_DEBUG(g_logger)<<"start() end";
}

void Scheduler::stop() {
    m_autoStop = true;
    if(m_rootFiber && m_threadCount == 0 && (m_rootFiber->getState() == Fiber::TERM||m_rootFiber->getState() == Fiber::INIT)) {
        SYLAR_LOG_INFO(g_logger) << this << " stopped";
        m_stopping = true;
        if(stopping()) {
            return;
        }
    }

    if(m_rootThread != -1) {
        SYLAR_ASSERT(GetThis() == this);
    } 
    else {
        SYLAR_ASSERT(GetThis() != this);
    }

    m_stopping = true;
    for(size_t i = 0; i < m_threadCount; ++i) {
        tickle();
    }

    // 由于main线程参与执行协程任务，因此要多tickle一下
    if(m_rootFiber) {
        tickle();
    }

    // main函数线程通过使用主协程（协程id=0）来执行完main函数中相关代码之后（在test_secheduler.cc文件中为启动调度器、加入任务队列、停止等一系列代码），会切换到调度协程
    if(m_rootFiber) {
        if(!stopping()) {
            // 切换到m_rootFiber代表的调度协程
            m_rootFiber->call();
        }
    }

    // 在调度完成之后会回收线程
    std::vector<Thread::ptr> thrs;
    {
        MutexType::Lock lock(m_mutex);
        thrs.swap(m_threads);
    }
    for(auto& i : thrs) {
        i->join();
    }
}

void Scheduler::setThis() {
    t_scheduler = this;
}

void Scheduler::run() {
    SYLAR_LOG_DEBUG(g_logger) << m_name << "run";
    // 设置为true，支持异步操作
    set_hook_enable(true);
    setThis();

    // 如果当前线程不是主线程，那么代表执行该函数的协程不在main函数线程中，因此要进行设置，而主线程（main函数所在的线程）已经在构造函数中进行设置过了
    if(sylar::GetThreadId() != m_rootThread) {
        // 此时对于不是主线程的线程，设置主协程为调度协程
        t_scheduler_fiber = Fiber::GetThis().get();
    }

    // 设置idle协程，如果没有任务，则会执行idle协程
    Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));

    //回调协程，用来存储任务队列中的任务
    Fiber::ptr cb_fiber;

    FiberAndThread ft;
    while(true) {
        ft.reset();
        bool tickle_me = false;
        bool is_active = false;
        {
            MutexType::Lock lock(m_mutex);
            // 遍历协程队列，取出任务
            auto it = m_fibers.begin();
            while(it != m_fibers.end()) {
                // 如果该任务没有指定在本线程上执行，则跳过
                if(it->thread != -1 && it->thread != sylar::GetThreadId()) {
                    ++it;
                    tickle_me = true;
                    continue;
                }

                SYLAR_ASSERT(it->fiber || it->cb);

                //该任务已经在执行中，跳过
                if(it->fiber && it->fiber->getState() == Fiber::EXEC) {
                    ++it;
                    continue;
                }

                ft = *it;

                //在任务队列中，删除该任务
                m_fibers.erase(it++);
                ++m_activeThreadCount;
                is_active = true;
                break;
            }
            // 判断 it 是否到达了 m_fibers 的末尾。如果没有到达末尾，说明查找成功，结果为 true；否则为 false。
            tickle_me |= it != m_fibers.end();
        }

        if(tickle_me) {
            // 通知有任务来了
            tickle();
        }

        //该任务为协程形式
        if(ft.fiber && (ft.fiber->getState() != Fiber::TERM && ft.fiber->getState() != Fiber::EXCEPT)) {
            
            // 开始执行该协程，执行完成之后，会回到这里
            ft.fiber->swapIn();
            --m_activeThreadCount;

            // 如果执行完成之后，协程的状态被置为了READY,则将fiber重新加入到任务队列中
            if(ft.fiber->getState() == Fiber::READY) {
                schedule(ft.fiber);
            } 
            // 如果为INIT或HOLD状态
            else if(ft.fiber->getState() != Fiber::TERM
                    && ft.fiber->getState() != Fiber::EXCEPT) {
                ft.fiber->m_state = Fiber::HOLD;
            }
            // 执行完毕重置数据ft
            ft.reset();
        } 

        // 如果是回调函数形式
        else if(ft.cb) {
            // 包装成协程，且与Mainfunc绑定（自动返回调度协程）
            if(cb_fiber) {
                cb_fiber->reset(ft.cb);
            } 
            else {
                cb_fiber.reset(new Fiber(ft.cb));
            }

            ft.reset();
            cb_fiber->swapIn();
            --m_activeThreadCount;
            if(cb_fiber->getState() == Fiber::READY) {
                schedule(cb_fiber);
                cb_fiber.reset();
            } 
            else if(cb_fiber->getState() == Fiber::EXCEPT || cb_fiber->getState() == Fiber::TERM) {
                cb_fiber->reset(nullptr);
            } 
            else {
                cb_fiber->m_state = Fiber::HOLD;
                cb_fiber.reset();
            }
        } 
        // 啥也不是，即代表没有了任务了，运行idle协程，在执行完idle函数之后，判断idle协程是否处于TERM状态，
        // 如果idle处于TERM状态，则break出while循环，run函数也运行完成，则代表调度协程的任务也完成了，自此退出调度协程，返回到主线程（main函数在的线程）
        else {
            if(is_active) {
                --m_activeThreadCount;
                continue;
            }
            if(idle_fiber->getState() == Fiber::TERM) {
                SYLAR_LOG_INFO(g_logger) << "idle fiber term";
                break;
            }

            ++m_idleThreadCount;
            idle_fiber->swapIn();
            --m_idleThreadCount;
            if(idle_fiber->getState() != Fiber::TERM && idle_fiber->getState() != Fiber::EXCEPT) {
                idle_fiber->m_state = Fiber::HOLD;
            }
        }
    }
}

void Scheduler::tickle() {
    SYLAR_LOG_INFO(g_logger) << "tickle";
}

bool Scheduler::stopping() {
    MutexType::Lock lock(m_mutex);
    return m_autoStop && m_stopping && m_fibers.empty() && m_activeThreadCount == 0;
}

void Scheduler::idle() {
    SYLAR_LOG_INFO(g_logger) << "idle";
    while(!stopping()) {
        sylar::Fiber::YieldToHold();
    }
}

void Scheduler::switchTo(int thread) {
    SYLAR_ASSERT(Scheduler::GetThis() != nullptr);
    if(Scheduler::GetThis() == this) {
        if(thread == -1 || thread == sylar::GetThreadId()) {
            return;
        }
    }
    schedule(Fiber::GetThis(), thread);
    Fiber::YieldToHold();
}

std::ostream& Scheduler::dump(std::ostream& os) {
    os << "[Scheduler name=" << m_name
       << " size=" << m_threadCount
       << " active_count=" << m_activeThreadCount
       << " idle_count=" << m_idleThreadCount
       << " stopping=" << m_stopping
       << " ]" << std::endl << "    ";
    for(size_t i = 0; i < m_threadIds.size(); ++i) {
        if(i) {
            os << ", ";
        }
        os << m_threadIds[i];
    }
    return os;
}

SchedulerSwitcher::SchedulerSwitcher(Scheduler* target) {
    m_caller = Scheduler::GetThis();
    if(target) {
        target->switchTo();
    }
}

SchedulerSwitcher::~SchedulerSwitcher() {
    if(m_caller) {
        m_caller->switchTo();
    }
}

}
