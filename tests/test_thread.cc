/**
 * @file test_thread.cc
 * @brief 线程模块测试
 * @version 0.1
 */
#include "sylar/sylar.h"

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

int count = 0;
sylar::Mutex s_mutex;

void func1(void *arg) {
    /*SYLAR_LOG_INFO(g_logger) << "arg: " << *(int*)arg;
    SYLAR_LOG_INFO(g_logger) << "name: " << sylar::Thread::GetName()
                             << " this.name: " << sylar::Thread::GetThis()->getName()
                             << " id: " << sylar::GetThreadId()
                             << " this.id: " << sylar::Thread::GetThis()->getId();*/

    for(int i = 0; i < 100000; ++i) {
        //sylar::RWMutex::WriteLock lock(s_mutex);
        //sylar::Mutex::Lock lock(s_mutex);
        ++count;
        --count;
    }
}

void fun2() {
    while(true) {
        SYLAR_LOG_INFO(g_logger) << "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    }
}

void fun3() {
    while(true) {
        SYLAR_LOG_INFO(g_logger) << "========================================";
    }
}

int main(int argc, char *argv[]) {
    //sylar::EnvMgr::GetInstance()->init(argc, argv);
    //sylar::Config::LoadFromConfDir(sylar::EnvMgr::GetInstance()->getConfigPath());

    using namespace std::chrono;
    // 记录开始时间
    auto start_time = high_resolution_clock::now();

    // 创建线程池
    std::vector<sylar::Thread::ptr> thrs;
    int arg = 123456;
    for(int i = 0; i < 300; i++) {
        // 带参数的线程用std::bind进行参数绑定，即main函数的arg参数会传给run函数中的arg参数
        sylar::Thread::ptr thr(new sylar::Thread(std::bind(func1, &arg), "thread_" + std::to_string(i)));
        thrs.push_back(thr);
    }

    for(int i = 0; i < 300; i++) {
        thrs[i]->join();
    }
    
    SYLAR_LOG_INFO(g_logger) << "count = " << count;

    // 记录结束时间
    auto end_time = high_resolution_clock::now();
    // 计算并输出总耗时（毫秒）
    auto duration = duration_cast<milliseconds>(end_time - start_time).count();
    std::cout << "Execution time: " << duration << " ms" << std::endl;

    // 加锁：1780/1659/1461 ms  都没用O3优化
    // 不加：67/65/69 ms

    // 加锁：
    // 不加：
    
    return 0;
}

