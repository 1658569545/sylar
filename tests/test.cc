#include <iostream>
#include "sylar/log.h"
#include "sylar/util.h"

int main(int argc, char** argv) {
    //这里创建了一个 sylar::Logger 类的智能指针（std::shared_ptr），并通过 new 动态分配了一个 sylar::Logger 对象
    sylar::Logger::ptr logger(new sylar::Logger);
    
    logger->addAppender(sylar::LogAppender::ptr(new sylar::StdoutLogAppender));

    sylar::FileLogAppender::ptr file_appender(new sylar::FileLogAppender("log.txt"));
    sylar::LogFormatter::ptr fmt(new sylar::LogFormatter("%d%T%p%T%m%n"));
    file_appender->setFormatter(fmt);
    //将ERROR级别的输入到文件中
    file_appender->setLevel(sylar::LogLevel::ERROR);

    logger->addAppender(file_appender);

    //sylar::LogEvent::ptr event(new sylar::LogEvent(__FILE__, __LINE__, 0, sylar::GetThreadId(), sylar::GetFiberId(), time(0)));
    //event->getSS() << "hello sylar log";
    //logger->log(sylar::LogLevel::DEBUG, event);
    std::cout << "hello sylar log" << std::endl;

    SYLAR_LOG_INFO(logger) << "test macro";
    SYLAR_LOG_ERROR(logger) << "test macro error";

    SYLAR_LOG_FMT_ERROR(logger, "test macro fmt error %s", "aa");

    auto l = sylar::LoggerMgr::GetInstance()->getLogger("xx");
    SYLAR_LOG_INFO(l) << "xxx";
    SYLAR_LOG_FMT_ERROR(l, "test macro fmt error %s","aa");

    sylar::Logger::ptr logger2 = SYLAR_LOG_ROOT();
    SYLAR_LOG_INFO(logger2) << "This is an info log message";
    return 0;
}
