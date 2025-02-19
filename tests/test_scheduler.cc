#include "sylar/sylar.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

void test_fiber() {
    static int s_count = 1000;
    SYLAR_LOG_INFO(g_logger) << "test in fiber s_count=" << s_count;

    //sleep(1);     //手动阻塞,阻塞会发生段错误,为什么会这样

    // 循环s_count次将test_fiber加入到任务队列中
    if(--s_count >= 0) {
        //指定第一个拿到该任务的线程一直执行
        //sylar::Scheduler::GetThis()->schedule(&test_fiber, sylar::GetThreadId());

        //不指定
        sylar::Scheduler::GetThis()->schedule(&test_fiber);
    }
}

int main(int argc, char** argv) {
    SYLAR_LOG_INFO(g_logger) << "main";

    using namespace std::chrono;
    // 记录开始时间
    auto start_time = high_resolution_clock::now();

    // 创建协程调度器
    sylar::Scheduler sc(100, true, "work");

    //启动
    sc.start();
    SYLAR_LOG_INFO(g_logger) << "schedule";

    //加入任务队列
    sc.schedule(&test_fiber);
    SYLAR_LOG_INFO(g_logger) << "test_fiber over";

    //结束
    sc.stop();
    SYLAR_LOG_INFO(g_logger) << "over";

    // 记录结束时间
    auto end_time = high_resolution_clock::now();
    // 计算并输出总耗时（毫秒）
    auto duration = duration_cast<milliseconds>(end_time - start_time).count();
    std::cout << "Execution time: " << duration << " ms" << std::endl;

    
    return 0;
}
