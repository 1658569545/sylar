/**
 * @file tcp_server.h
 * @brief TCP服务器的封装
 * @author zq
 */
#ifndef __SYLAR_TCP_SERVER_H__
#define __SYLAR_TCP_SERVER_H__

#include <memory>
#include <functional>
#include "address.h"
#include "iomanager.h"
#include "socket.h"
#include "noncopyable.h"
#include "config.h"

namespace sylar {


/**
 * @brief 配置 TCP 服务器的结构体
 * @details 
 */
struct TcpServerConf {
    typedef std::shared_ptr<TcpServerConf> ptr;

    std::vector<std::string> address;   //地址
    int keepalive = 0;                  //keepalive标志
    int timeout = 1000 * 2 * 60;        //超时时间
    int ssl = 0;                        //是否启用sll
    std::string id;      

    // 服务器类型，http, ws, rock  
    std::string type = "http"; 

    // 名称，类似的还有nginx
    std::string name;     

    // SSL 证书和私钥文件路径
    std::string cert_file;              
    std::string key_file;   
    
    // 调度器名称
    std::string accept_worker;
    std::string io_worker;
    std::string process_worker;

    // 附加参数
    std::map<std::string, std::string> args;    

    // 是否有地址
    bool isValid() const {
        return !address.empty();
    }

    bool operator==(const TcpServerConf& oth) const {
        return address == oth.address
            && keepalive == oth.keepalive
            && timeout == oth.timeout
            && name == oth.name
            && ssl == oth.ssl
            && cert_file == oth.cert_file
            && key_file == oth.key_file
            && accept_worker == oth.accept_worker
            && io_worker == oth.io_worker
            && process_worker == oth.process_worker
            && args == oth.args
            && id == oth.id
            && type == oth.type;
    }
};


// 从配置文件加载相关配置   YAML String 转换成 TcpServerConf
template<>
class LexicalCast<std::string, TcpServerConf> {
public:
    TcpServerConf operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        TcpServerConf conf;

        // 将配置文件中的字段逐个映射到 TcpServerConf 对象中

        // .as<std::string>(conf.id)：强制转换为string类型，同时，如果没有id，会使用 conf.id 作为默认值
        conf.id = node["id"].as<std::string>(conf.id);
        conf.type = node["type"].as<std::string>(conf.type);
        conf.keepalive = node["keepalive"].as<int>(conf.keepalive);
        conf.timeout = node["timeout"].as<int>(conf.timeout);
        conf.name = node["name"].as<std::string>(conf.name);
        conf.ssl = node["ssl"].as<int>(conf.ssl);
        conf.cert_file = node["cert_file"].as<std::string>(conf.cert_file);
        conf.key_file = node["key_file"].as<std::string>(conf.key_file);
        conf.accept_worker = node["accept_worker"].as<std::string>();
        conf.io_worker = node["io_worker"].as<std::string>();
        conf.process_worker = node["process_worker"].as<std::string>();
        conf.args = LexicalCast<std::string,std::map<std::string, std::string> >()(node["args"].as<std::string>(""));
        if(node["address"].IsDefined()) {
            for(size_t i = 0; i < node["address"].size(); ++i) {
                conf.address.push_back(node["address"][i].as<std::string>());
            }
        }
        return conf;
    }
};

// 将TcpServerConf对象的数据映射到 YAML::String 中    TcpServerConf ---> YAML::String
template<>
class LexicalCast<TcpServerConf, std::string> {
public:
    std::string operator()(const TcpServerConf& conf) {
        YAML::Node node;
        node["id"] = conf.id;
        node["type"] = conf.type;
        node["name"] = conf.name;
        node["keepalive"] = conf.keepalive;
        node["timeout"] = conf.timeout;
        node["ssl"] = conf.ssl;
        node["cert_file"] = conf.cert_file;
        node["key_file"] = conf.key_file;
        node["accept_worker"] = conf.accept_worker;
        node["io_worker"] = conf.io_worker;
        node["process_worker"] = conf.process_worker;
        node["args"] = YAML::Load(LexicalCast<std::map<std::string, std::string>, std::string>()(conf.args));
        for(auto& i : conf.address) {
            node["address"].push_back(i);
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

/**
 * @brief TCP服务器封装
 */
class TcpServer : public std::enable_shared_from_this<TcpServer>, Noncopyable {
public:
    typedef std::shared_ptr<TcpServer> ptr;
    /**
     * @brief 构造函数
     * @param[in] worker socket客户端工作的协程调度器
     * @param[in] accept_worker 服务器socket执行接收socket连接的协程调度器
     */
    TcpServer(sylar::IOManager* worker = sylar::IOManager::GetThis()
              ,sylar::IOManager* io_woker = sylar::IOManager::GetThis()
              ,sylar::IOManager* accept_worker = sylar::IOManager::GetThis());

    /**
     * @brief 析构函数
     */
    virtual ~TcpServer();

    /**
     * @brief 绑定地址
     * @attention 同时会开始listen
     * @return 返回是否绑定成功
     */
    virtual bool bind(sylar::Address::ptr addr, bool ssl = false);

    /**
     * @brief 绑定地址数组
     * @param[in] addrs 需要绑定的地址数组
     * @param[out] fails 绑定失败的地址
     * @attention 同时会开始listen
     * @return 是否绑定成功
     */
    virtual bool bind(const std::vector<Address::ptr>& addrs
                        ,std::vector<Address::ptr>& fails
                        ,bool ssl = false);

    /**
     * @brief 加载 SSL 证书和私钥
     */
    bool loadCertificates(const std::string& cert_file, const std::string& key_file);

    /**
     * @brief 启动服务
     * @pre 需要bind成功后执行
     * @attention 将监听的sock传入m_acceptWorker调度器中，负责监听这些sock上的连接请求
     */
    virtual bool start();

    /**
     * @brief 停止服务
     * @attention 在m_acceptWorker中，为每个监听的sock来处理
     */
    virtual void stop();

    /**
     * @brief 返回读取超时时间(毫秒)
     */
    uint64_t getRecvTimeout() const { return m_recvTimeout;}

    /**
     * @brief 设置读取超时时间(毫秒)
     */
    void setRecvTimeout(uint64_t v) { m_recvTimeout = v;}

    /**
     * @brief 设置服务器名称
     */
    virtual void setName(const std::string& v) { m_name = v;}
    
    /**
     * @brief 返回服务器名称
     */
    std::string getName() const { return m_name;}

    /**
     * @brief 是否停止
     */
    bool isStop() const { return m_isStop;}

    /**
     * @brief 获取TcpServerConf
     */
    TcpServerConf::ptr getConf() const { return m_conf;}

    /**
     * @brief 设置TcpServerConf
     */
    void setConf(TcpServerConf::ptr v) { m_conf = v;}

    /**
     * @brief 通过reset设置TcpServerConf
     */
    void setConf(const TcpServerConf& v);

    /**
     * @brief 转换为字符串流
     */
    virtual std::string toString(const std::string& prefix = "");

    /**
     * @brief 获取监听Socket数组
     */
    std::vector<Socket::ptr> getSocks() const { return m_socks;}
protected:

    /**
     * @brief 处理新连接的Socket类
     */
    virtual void handleClient(Socket::ptr client);

    /**
     * @brief 开始接受连接
     * @attention 循环监听传入的客户端连接，并将成功的连接分配给 I/O 工作线程池（m_ioWorker）进行处理。
     */
    virtual void startAccept(Socket::ptr sock);
protected:

    /// 监听Socket数组
    std::vector<Socket::ptr> m_socks;
    /// 新连接的Socket工作的调度器
    IOManager* m_worker;
    /// 新连接的Socket工作的调度器，负责已经连接成功的，即负责连接上的任务处理
    IOManager* m_ioWorker;

    /// 服务器Socket接收连接的调度器，专门负责处理连接请求
    IOManager* m_acceptWorker;
    /// 接收超时时间(毫秒)
    uint64_t m_recvTimeout;
    /// 服务器名称
    std::string m_name;
    /// 服务器类型
    std::string m_type = "tcp";
    /// 服务是否停止
    bool m_isStop;
    //是否启用ssl
    bool m_ssl = false;

    TcpServerConf::ptr m_conf;
};

}

#endif
