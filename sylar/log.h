/**
 * @file log.h
 * @brief 日志模块封装
 * 用于格式化输出程序日志，方便从日志中定位程序运行过程中出现的问题。
 * @author zq
 */
#ifndef __SYLAR_LOG_H__
#define __SYLAR_LOG_H__

#include <string>
#include <stdint.h>
#include <memory>
#include <list>
#include <sstream>
#include <fstream>
#include <vector>
#include <stdarg.h>
#include <map>
#include "util.h"
#include "singleton.h"
#include "thread.h"

/*
    日志模块主要功能：
    LogFormatter: 日志格式器，与log4cpp的PatternLayout对应，用于格式化一个日志事件。该类构建时可以指定pattern，表示如何进行格式化。提供format方法，用于将日志事件格式化成字符串。

    LogAppender: 日志输出器，用于将一个日志事件输出到对应的输出地。该类内部包含一个LogFormatter成员和一个log方法，日志事件先经过LogFormatter格式化后再输出到对应的输出地。
从这个类可以派生出不同的Appender类型，比如StdoutLogAppender和FileLogAppender，分别表示输出到终端和文件。

    Logger: 日志器，负责进行日志输出。一个Logger包含多个LogAppender和一个日志级别，提供log方法，传入日志事件，判断该日志事件的级别高于日志器本身的级别之后调用LogAppender将日志进行输出，
否则该日志被抛弃。

    LogEvent: 日志事件，用于记录日志现场，比如该日志的级别，文件名/行号，日志消息，线程/协程号，所属日志器名称等。

    LogEventWrap: 日志事件包装类，其实就是将日志事件和日志器包装到一起，因为一条日志只会在一个日志器上进行输出。
将日志事件和日志器包装到一起后，方便通过宏定义来简化日志模块的使用。另外，LogEventWrap还负责在构建时指定日志事件和日志器，在析构时调用日志器的log方法将日志事件进行输出。

    LogManager: 日志器管理类，单例模式，用于统一管理所有的日志器，提供日志器的创建与获取方法。LogManager自带一个root Logger，用于为日志模块提供一个初始可用的日志器。
 */

/**
 * 流式方式调用逻辑：
 * 我们使用是这样使用,首先定义logger： logger=SYLAR_LOG_ROOT()
 * 然后就可以调用对应的宏，SYLAR_LOG_INFO(logger)<<"test";
 * 接下来，对该宏进行一个展开
 * 1. 首先，new一个LogEvent对象，并且一个智能指针指向它，然后将这个LogEvent对象传入LogEventWrap的构造函数来构造一个LogEventWrap的临时对象
 * （为什么要这么做，具体见LogEventWrap的定义处）。
 * 2. 然后该临时对象会调用getSS()。在getSS()中，临时对象的LogEvent成员又会调用对应的getSS(),而在LogEvent的getSS()中，会获取已经写好的日志内容字符串流。
 * 3. 而该字符串流是什么时候写好的呢，是在临时对象析构时，即离开if语句之后会自动进行析构，并且调用Logger的log函数来进行写入。
 *    但是Logger的log函数实际上是调用StdoutLogAppender和FileLogAppender的log函数来进行写入。
 * 4. 对于是先获取字符串流还是先写入，由于获取的是引用，因此无需多纠结。
 */

//使用流式方式将日志级别level的日志写入到logger
#define SYLAR_LOG_LEVEL(logger, level) \
    if(logger->getLevel() <= level) \
        sylar::LogEventWrap(sylar::LogEvent::ptr(new sylar::LogEvent(logger, level, \
                        __FILE__, __LINE__, 0, sylar::GetThreadId(),\
                sylar::GetFiberId(), time(0), sylar::Thread::GetName()))).getSS()

//使用流式方式将日志写入到logger
#define SYLAR_LOG_DEBUG(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::DEBUG)
#define SYLAR_LOG_INFO(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::INFO)
#define SYLAR_LOG_WARN(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::WARN)
#define SYLAR_LOG_ERROR(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::ERROR)
#define SYLAR_LOG_FATAL(logger) SYLAR_LOG_LEVEL(logger, sylar::LogLevel::FATAL)

/**
 * 格式化方式调用逻辑
 * 1. 与流式差不多，但是最后LogEventWrap临时对象没有调用getSS()而是调用getEvent(),获取LogEvent，
 * 2. 然后调用format将内容写入字符串流
 */

#define SYLAR_LOG_FMT_LEVEL(logger, level, fmt, ...) \
    if(logger->getLevel() <= level) \
        sylar::LogEventWrap(sylar::LogEvent::ptr(new sylar::LogEvent(logger, level, \
                        __FILE__, __LINE__, 0, sylar::GetThreadId(),\
                sylar::GetFiberId(), time(0), sylar::Thread::GetName()))).getEvent()->format(fmt, __VA_ARGS__)

//使用格式化方式将日志写入到logger
#define SYLAR_LOG_FMT_DEBUG(logger, fmt, ...) SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::DEBUG, fmt, __VA_ARGS__)
#define SYLAR_LOG_FMT_INFO(logger, fmt, ...)  SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::INFO, fmt, __VA_ARGS__)
#define SYLAR_LOG_FMT_WARN(logger, fmt, ...)  SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::WARN, fmt, __VA_ARGS__)
#define SYLAR_LOG_FMT_ERROR(logger, fmt, ...) SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::ERROR, fmt, __VA_ARGS__)
#define SYLAR_LOG_FMT_FATAL(logger, fmt, ...) SYLAR_LOG_FMT_LEVEL(logger, sylar::LogLevel::FATAL, fmt, __VA_ARGS__)


//获取主日志器
#define SYLAR_LOG_ROOT() sylar::LoggerMgr::GetInstance()->getRoot()

//获取一个名为name的日志器
#define SYLAR_LOG_NAME(name) sylar::LoggerMgr::GetInstance()->getLogger(name)


namespace sylar {

class Logger;
class LoggerManager;

//日志级别
class LogLevel {
public:
    enum Level {
        UNKNOW = 0,
        DEBUG = 1,
        INFO = 2,
        WARN = 3,
        ERROR = 4,
        FATAL = 5
    };

    //将日志级别转成字符串文本输出
    static const char* ToString(LogLevel::Level level);
    //将字符串文本转换成日志级别
    static LogLevel::Level FromString(const std::string& str);
    
    //static关键字表示该方法属于类本身，而非类的实例。可以通过类名直接调用，而无须new一个具体的对象再来进行调用。例如，LogLevel::ToString(LogLevel::DEBUG)获取DEBUG字符串
};

/**
 * @brief 日志事件，用于记录日志现场信息，比如该日志的级别，文件名/行号，日志消息，线程/协程号，所属日志器名称等。
 */
class LogEvent {
public:
    //用智能指针来管理实例化对象，实现自动回收内存，防止内存泄漏。
    typedef std::shared_ptr<LogEvent> ptr;

    /**
     * @brief 构造函数
     * @param[in] logger 日志器
     * @param[in] level 日志级别
     * @param[in] file 文件名
     * @param[in] line 文件行号
     * @param[in] elapse 程序启动依赖的耗时(毫秒)
     * @param[in] thread_id 线程id
     * @param[in] fiber_id 协程id
     * @param[in] time 日志事件(秒)
     * @param[in] thread_name 线程名称
     */
    LogEvent(std::shared_ptr<Logger> logger, LogLevel::Level level ,const char* file, int32_t line, uint32_t elapse, uint32_t thread_id, uint32_t fiber_id, uint64_t time, const std::string& thread_name);

    /**
     * @brief 返回文件名
     */
    const char* getFile() const { return m_file;}

    /**
     * @brief 返回行号
     */
    int32_t getLine() const { return m_line;}

    /**
     * @brief 返回耗时
     */
    uint32_t getElapse() const { return m_elapse;}

    /**
     * @brief 返回线程ID
     */
    uint32_t getThreadId() const { return m_threadId;}

    /**
     * @brief 返回协程ID
     */
    uint32_t getFiberId() const { return m_fiberId;}

    /**
     * @brief 返回时间
     */
    uint64_t getTime() const { return m_time;}

    /**
     * @brief 返回线程名称
     */
    const std::string& getThreadName() const { return m_threadName;}

    /**
     * @brief 返回日志内容
     */
    std::string getContent() const { return m_ss.str();}

    /**
     * @brief 返回日志器
     */
    std::shared_ptr<Logger> getLogger() const { return m_logger;}

    /**
     * @brief 返回日志级别
     */
    LogLevel::Level getLevel() const { return m_level;}

    /**
     * @brief 返回日志内容字符串流
     */
    std::stringstream& getSS() { return m_ss;}

    /**
     * @brief 格式化写入日志内容
     */
    void format(const char* fmt, ...);

    /**
     * @brief 格式化写入日志内容
     */
    void format(const char* fmt, va_list al);
private:
    /// 文件名
    const char* m_file = nullptr;
    /// 行号
    int32_t m_line = 0;
    /// 程序启动开始到现在的毫秒数
    uint32_t m_elapse = 0;
    /// 线程ID
    uint32_t m_threadId = 0;
    /// 协程ID
    uint32_t m_fiberId = 0;
    /// 时间戳
    uint64_t m_time = 0;
    /// 线程名称
    std::string m_threadName;
    /// 日志内容流
    std::stringstream m_ss;
    /// 日志器
    std::shared_ptr<Logger> m_logger;
    /// 日志等级
    LogLevel::Level m_level;
};

/**
 * @brief 日志事件包装器
 * @attention LogEventWrap 是对 LogEvent 的包装器类，其主要目的是确保日志事件在使用完毕后自动释放资源或触发一些后续的处理。使用日志事件包装不仅仅可以自动回收LogEvent(借助智能指针)，
 * 还能够在析构函数中自动进行log写入,从而无需显示创建LogEvent对象、手动调用log函数，从而减少用户对底层的依赖。
 */
class LogEventWrap {
public:
    /**
     * @brief 构造函数
     * @param[in] e 日志事件
     */
    LogEventWrap(LogEvent::ptr e);

    /**
     * @brief 析构函数
     * @attention 调用Logger的log函数，实现自动写入
     */
    ~LogEventWrap();

    /**
     * @brief 获取日志事件
     */
    LogEvent::ptr getEvent() const { return m_event;}

    /**
     * @brief 获取日志内容流
     * @attention 通过调用LogEvent的getSS()来将日志消息写入到字符串流中
     */
    std::stringstream& getSS();

private:
    /// 日志事件
    LogEvent::ptr m_event;      
};

//日志格式化
class LogFormatter {
public:
    typedef std::shared_ptr<LogFormatter> ptr;
    /**
     * @brief 构造函数
     * @param[in] pattern 格式模板
     * @details 
     *  %m 消息
     *  %p 日志级别
     *  %r 累计毫秒数
     *  %c 日志名称
     *  %t 线程id
     *  %n 换行
     *  %d 时间
     *  %f 文件名
     *  %l 行号
     *  %T 制表符
     *  %F 协程id
     *  %N 线程名称
     *
     *  默认格式 "%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"
     *  年-月-日 时：分：秒     线程名称    协程id      [日志级别]      [日志名称]      文件名：行号        消息
     * @attention 调用init()对格式模板pattern进行解析
     */
    LogFormatter(const std::string& pattern);

    /**
     * @brief 返回格式化日志文本
     * @param[in] logger 日志器
     * @param[in] level 日志级别
     * @param[in] event 日志事件
     */
    std::string format(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event);

    /**
     * @brief 返回格式化日志文本
     * @param[in] ofs 输出流
     * @param[in] logger 日志器
     * @param[in] level 日志级别
     * @param[in] event 日志事件
     * @attention 遍历每个日志项，依次调用各个日志项的format函数来进行输出
     */
    std::ostream& format(std::ostream& ofs, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event);
public:
    /**
     * @brief 日志内容项格式化
     * @attention 对日志消息中的每个内容都进行格式化输出，即消息、日志级别、文件名、时间这些东西
     */
    class FormatItem {
    public:
        typedef std::shared_ptr<FormatItem> ptr;
    
        /**
         * @brief 析构函数
         */
        virtual ~FormatItem() {}
        /**
         * @brief 格式化日志到流
         * @param[in, out] os 日志输出流
         * @param[in] logger 日志器
         * @param[in] level 日志等级
         * @param[in] event 日志事件
         * @attention 会在子类中进行重写，以完成各个日志内容项的输出
         */
        virtual void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) = 0;
    };

    /**
     * @brief 初始化,解析日志模板
     * @attention 在构造函数中自动被调用，并解析后的模板存放在m_items中
     */
    void init();

    /**
     * @brief 是否有错误
     */
    bool isError() const { return m_error;}

    /**
     * @brief 返回日志模板
     */
    const std::string getPattern() const { return m_pattern;}
private:
    /// 日志格式模板
    std::string m_pattern;
    /// 日志格式解析后格式
    std::vector<FormatItem::ptr> m_items;
    /// 是否有错误
    bool m_error = false;

};

/**
 * @brief 日志输出器
 * @attention 用于将一个日志事件输出到对应的输出地, 主要是先利用LogFormatter来格式化日志事件，然后再使用log写入
 */
class LogAppender {
friend class Logger;
public:
    typedef std::shared_ptr<LogAppender> ptr;
    typedef Spinlock MutexType;

    /**
     * @brief 析构函数
     * @details 虚析构函数，保证子类在析构时也能调用父类的析构函数
     */
    virtual ~LogAppender() {}

    /**
     * @brief 写入日志
     * @param[in] logger 日志器
     * @param[in] level 日志级别
     * @param[in] event 日志事件
     */
    virtual void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) = 0;

    /**
     * @brief 将日志输出目标的配置转成YAML String
     */
    virtual std::string toYamlString() = 0;

    /**
     * @brief 更改日志格式器
     */
    void setFormatter(LogFormatter::ptr val);

    /**
     * @brief 获取日志格式器
     */
    LogFormatter::ptr getFormatter();

    /**
     * @brief 获取日志级别
     */
    LogLevel::Level getLevel() const { return m_level;}

    /**
     * @brief 设置日志级别
     */
    void setLevel(LogLevel::Level val) { m_level = val;}
protected:
    /// 日志级别
    LogLevel::Level m_level = LogLevel::DEBUG;
    /// 是否有自己的日志格式器
    bool m_hasFormatter = false;
    /// Mutex
    MutexType m_mutex;
    /// 日志格式器
    LogFormatter::ptr m_formatter;
};

/**
 * @brief 日志器，负责管理日志的记录和输出
 * @details std::enable_shared_from_this 是一个模板类，它允许一个类的成员函数生成一个指向当前对象的shared_ptr实例
 * Logger类可以使用std::shared_ptr<Logger>来获取指向自己的shared_ptr
 */
class Logger : public std::enable_shared_from_this<Logger> {

//友元类可以访问该类别的private、protected
friend class LoggerManager;
public:
    typedef std::shared_ptr<Logger> ptr;
    typedef Spinlock MutexType;

    /**
     * @brief 构造函数
     * @param[in] name 日志器名称，默认为root
     * @attention 会默认构造一个日志输出模板
     */
    Logger(const std::string& name = "root");

    /**
     * @brief 写日志
     * @param[in] level 日志级别
     * @param[in] event 日志事件
     */
    void log(LogLevel::Level level, LogEvent::ptr event);

    /**
     * @brief 写debug级别日志
     * @param[in] event 日志事件
     */
    void debug(LogEvent::ptr event);

    /**
     * @brief 写info级别日志
     * @param[in] event 日志事件
     */
    void info(LogEvent::ptr event);

    /**
     * @brief 写warn级别日志
     * @param[in] event 日志事件
     */
    void warn(LogEvent::ptr event);

    /**
     * @brief 写error级别日志
     * @param[in] event 日志事件
     */
    void error(LogEvent::ptr event);

    /**
     * @brief 写fatal级别日志
     * @param[in] event 日志事件
     */
    void fatal(LogEvent::ptr event);

    /**
     * @brief 添加日志目标
     * @param[in] appender 日志目标
     */
    void addAppender(LogAppender::ptr appender);

    /**
     * @brief 删除日志目标
     * @param[in] appender 日志目标
     */
    void delAppender(LogAppender::ptr appender);

    /**
     * @brief 清空日志目标
     */
    void clearAppenders();

    /**
     * @brief 返回日志级别
     */
    LogLevel::Level getLevel() const { return m_level;}

    /**
     * @brief 设置日志级别
     */
    void setLevel(LogLevel::Level val) { m_level = val;}

    /**
     * @brief 返回日志名称
     */
    const std::string& getName() const { return m_name;}

    /**
     * @brief 设置日志格式器
     */
    void setFormatter(LogFormatter::ptr val);

    /**
     * @brief 设置日志格式模板
     */
    void setFormatter(const std::string& val);

    /**
     * @brief 获取日志格式器
     */
    LogFormatter::ptr getFormatter();

    /**
     * @brief 将日志器的配置转成YAML String
     */
    std::string toYamlString();
private:
    /// 日志名称（例如log、systeem、root）
    std::string m_name;
    /// 日志级别
    LogLevel::Level m_level;
    /// Mutex
    MutexType m_mutex;
    /// 日志目标集合
    std::list<LogAppender::ptr> m_appenders;
    /// 日志格式器
    LogFormatter::ptr m_formatter;
    /// 主日志器
    Logger::ptr m_root;
};

/**
 * @brief 输出到控制台的Appender
 */
class StdoutLogAppender : public LogAppender {
public:
    typedef std::shared_ptr<StdoutLogAppender> ptr;

    /**
     * @brief 调用LogFormatter的format函数来实现格式化输出
     */
    void log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override;
    std::string toYamlString() override;
};

/**
 * @brief 输出到文件的Appender
 */
class FileLogAppender : public LogAppender {
public:
    typedef std::shared_ptr<FileLogAppender> ptr;
    FileLogAppender(const std::string& filename);

    /**
     * @brief 调用LogFormatter的format函数来实现格式化输出
     */
    void log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) override;

    std::string toYamlString() override;

    /**
     * @brief 重新打开日志文件
     * @return 成功返回true
     */
    bool reopen();
private:
    /// 文件路径
    std::string m_filename;
    /// 文件流
    std::ofstream m_filestream;
    /// 上次重新打开时间
    uint64_t m_lastTime = 0;
};

/**
 * @brief 日志器管理类
 */
class LoggerManager {
public:
    typedef Spinlock MutexType;
    /**
     * @brief 构造函数
     * @attention 会自动添加一个StdoutLogAppender
     */
    LoggerManager();

    /**
     * @brief 获取日志器
     * @param[in] name 日志器名称
     */
    Logger::ptr getLogger(const std::string& name);

    /**
     * @brief 初始化
     */
    void init();

    /**
     * @brief 返回主日志器
     */
    Logger::ptr getRoot() const { return m_root;}

    /**
     * @brief 将所有的日志器配置转成YAML String
     */
    std::string toYamlString();
private:
    /// Mutex
    MutexType m_mutex;
    /// 日志器容器
    std::map<std::string, Logger::ptr> m_loggers;
    /// 主日志器
    Logger::ptr m_root;
};

/// 日志器管理类单例模式，LoggerMgr是定义的实例名称
typedef sylar::Singleton<LoggerManager> LoggerMgr;

}



#endif
