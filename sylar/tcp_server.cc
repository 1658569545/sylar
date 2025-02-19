#include "tcp_server.h"
#include "config.h"
#include "log.h"

namespace sylar {

static sylar::ConfigVar<uint64_t>::ptr g_tcp_server_read_timeout =
    sylar::Config::Lookup("tcp_server.read_timeout", (uint64_t)(60 * 1000 * 2),"tcp server read timeout");

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

TcpServer::TcpServer(sylar::IOManager* worker,sylar::IOManager* io_worker,sylar::IOManager* accept_worker)
    :m_worker(worker)
    ,m_ioWorker(io_worker)
    ,m_acceptWorker(accept_worker)
    ,m_recvTimeout(g_tcp_server_read_timeout->getValue())
    ,m_name("sylar/1.0.0")
    ,m_isStop(true) {
}

TcpServer::~TcpServer() {
    for(auto& i : m_socks) {
        i->close();
    }
    m_socks.clear();
}

void TcpServer::setConf(const TcpServerConf& v) {
    m_conf.reset(new TcpServerConf(v));
}

// 绑定单个地址
bool TcpServer::bind(sylar::Address::ptr addr, bool ssl) {
    std::vector<Address::ptr> addrs;
    std::vector<Address::ptr> fails;
    addrs.push_back(addr);
    return bind(addrs, fails, ssl);
}

// 绑定多个地址
bool TcpServer::bind(const std::vector<Address::ptr>& addrs,std::vector<Address::ptr>& fails,bool ssl) {
    m_ssl = ssl;
    for(auto& addr : addrs) {
        // 根据 ssl 参数决定是否创建 SSL 套接字或普通 TCP 套接字。
        Socket::ptr sock = ssl ? SSLSocket::CreateTCP(addr) : Socket::CreateTCP(addr);
        //绑定
        if(!sock->bind(addr)) {
            // 绑定失败
            SYLAR_LOG_ERROR(g_logger) << "bind fail errno="<< errno << " errstr=" << strerror(errno)<< " addr=[" << addr->toString() << "]";
            fails.push_back(addr);
            continue;
        }
        //绑定之后开始listen
        if(!sock->listen()) {
            // 监听失败
            SYLAR_LOG_ERROR(g_logger) << "listen fail errno=" << errno << " errstr=" << strerror(errno) << " addr=[" << addr->toString() << "]";
            fails.push_back(addr);
            continue;
        }
        m_socks.push_back(sock);
    }

    if(!fails.empty()) {
        // 只有有1个失败，就设置为整体任务失败
        m_socks.clear();
        return false;
    }

    // 绑定成功
    for(auto& i : m_socks) {
        SYLAR_LOG_INFO(g_logger) << "type=" << m_type << " name=" << m_name << " ssl=" << m_ssl << " server bind success: " << *i;
    }
    return true;
}


void TcpServer::startAccept(Socket::ptr sock) {
    //如果服务没有停止
    while(!m_isStop) {
        // accept，client为连接套接字
        Socket::ptr client = sock->accept();
        // 如果连接成功
        if(client) {
            // 设置接收超时时间
            client->setRecvTimeout(m_recvTimeout);

            // 加入任务队列
            /**    
             * fc对应的是std::bind(&TcpServer::handleClient, shared_from_this(), client)
             * std::bind 会将 shared_from_this() 和 client 作为参数，绑定到 TcpServer::handleClient 方法。
             * shared_from_this(): 用于保证 TcpServer 对象在异步操作期间不会被销毁。
             * 实际上起作用的是client，即传给 handleClient 的参数为client
             */
            m_ioWorker->schedule(std::bind(&TcpServer::handleClient,shared_from_this(), client));
        } else {
            SYLAR_LOG_ERROR(g_logger) << "accept errno=" << errno << " errstr=" << strerror(errno);
        }
    }
}

bool TcpServer::start() {
    if(!m_isStop) {
        return true;
    }
    m_isStop = false;
        
    // 每个socket接收连接任务放入任务队列中
    for(auto& sock : m_socks) {
        m_acceptWorker->schedule(std::bind(&TcpServer::startAccept,shared_from_this(), sock));
    }
    return true;
}

void TcpServer::stop() {
    m_isStop = true;
    // 获取一个指向当前 TcpServer 对象的 shared_ptr，并将其存储在 self 变量中。
    // 这样做的目的是为了确保在后续的异步操作中，TcpServer 对象在 m_acceptWorker 执行任务时不会被销毁。
    auto self = shared_from_this();
/*
this 和 self 都被捕获进 lambda 表达式：
    this 用于访问当前 TcpServer 对象的成员。
    如果在 lambda 表达式内部需要访问外部的成员（如本例的 m_socks），需要显式地捕获 this。  
    
    self 用于确保 TcpServer 对象的生命周期在任务执行时不会被销毁（类似 shared_from_this() 的效果）。
*/
    m_acceptWorker->schedule([this, self]() {
        for(auto& sock : m_socks) {
            sock->cancelAll();
            sock->close();
        }
        m_socks.clear();
    });
}

void TcpServer::handleClient(Socket::ptr client) {
    SYLAR_LOG_INFO(g_logger) << "handleClient: " << *client;
}

bool TcpServer::loadCertificates(const std::string& cert_file, const std::string& key_file) {
    for(auto& i : m_socks) {
        auto ssl_socket = std::dynamic_pointer_cast<SSLSocket>(i);
        if(ssl_socket) {
            //加载 SSL 证书和私钥文件路径
            if(!ssl_socket->loadCertificates(cert_file, key_file)) {
                return false;
            }
        }
    }
    return true;
}

std::string TcpServer::toString(const std::string& prefix) {
    std::stringstream ss;
    ss << prefix << "[type=" << m_type
       << " name=" << m_name << " ssl=" << m_ssl
       << " worker=" << (m_worker ? m_worker->getName() : "")
       << " accept=" << (m_acceptWorker ? m_acceptWorker->getName() : "")
       << " recv_timeout=" << m_recvTimeout << "]" << std::endl;
    std::string pfx = prefix.empty() ? "    " : prefix;
    for(auto& i : m_socks) {
        ss << pfx << pfx << *i << std::endl;
    }
    return ss.str();
}

}
