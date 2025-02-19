#include "thread.h"
#include "log.h"
#include "util.h"

namespace sylar {

/// 线程局部变量，指向当前线程
static thread_local Thread* t_thread = nullptr;        

/// 记录当前线程的名称
static thread_local std::string t_thread_name = "UNKNOW";   

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

Thread* Thread::GetThis() {
    return t_thread;
}

const std::string& Thread::GetName() {
    return t_thread_name;
}

void Thread::SetName(const std::string& name) {
    if(name.empty()) {
        return;
    }
    if(t_thread) {
        t_thread->m_name = name;
    }
    t_thread_name = name;
}

Thread::Thread(std::function<void()> cb, const std::string& name)
    :m_cb(cb)
    ,m_name(name) {
    if(name.empty()) {
        m_name = "UNKNOW";
    }
    //线程的入口函数，即执行函数，指定为run函数
    //m_thread用于存储线程创建后返回的线程 ID
    //第二个参数用来指定线程的属性，如堆栈大小、调度策略等，传递nullptr表示使用默认属性
    //this是传递给线程函数的参数
    int rt = pthread_create(&m_thread, nullptr, &Thread::run, this);
    if(rt) {
        SYLAR_LOG_ERROR(g_logger) << "pthread_create thread fail, rt=" << rt << " name=" << name;
        throw std::logic_error("pthread_create error");
    }

    //等待子线程初始化
    m_semaphore.wait();
}

Thread::~Thread() {
    if(m_thread) {
        pthread_detach(m_thread);
    }
}

//线程回收
void Thread::join() {
    //判断当前线程是否有效
    if(m_thread) {
        //m_thread是要等待的线程
        //第二个参数用来接受线程的退出返回值，传入nullptr代表我们不关心返回值。
        //pthread_join 会阻塞调用pthread_join的线程，在这里是m_thread所代表的线程，直到 m_thread 表示的线程执行完成。
        int rt = pthread_join(m_thread, nullptr);
        if(rt) {
            SYLAR_LOG_ERROR(g_logger) << "pthread_join thread fail, rt=" << rt << " name=" << m_name;
            throw std::logic_error("pthread_join error");
        }
        m_thread = 0;
    }
}

void* Thread::run(void* arg) {
    SYLAR_LOG_DEBUG(g_logger)<<"Thread run";
    Thread* thread = (Thread*)arg;
    
    //记录当前线程的相关信息
    t_thread = thread;
    t_thread_name = thread->m_name;

    thread->m_id = sylar::GetThreadId();

    //设置当前线程的名称
    //pthread_self()：获取当前线程的 pthread_t 标识。
    pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());

    std::function<void()> cb;
    cb.swap(thread->m_cb);

    //释放信号量，通知主线程该线程已经完成初始化。
    thread->m_semaphore.notify();

    //执行线程执行函数
    cb();
    return 0;
}

}
