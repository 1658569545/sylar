#include "http_server.h"
#include "sylar/log.h"
#include "sylar/http/servlets/config_servlet.h"
#include "sylar/http/servlets/status_servlet.h"

namespace sylar {
namespace http {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

HttpServer::HttpServer(bool keepalive
               ,sylar::IOManager* worker
               ,sylar::IOManager* io_worker
               ,sylar::IOManager* accept_worker)
    :TcpServer(worker, io_worker, accept_worker)
    ,m_isKeepalive(keepalive) {
    m_dispatch.reset(new ServletDispatch);

    m_type = "http";
    m_dispatch->addServlet("/_/status", Servlet::ptr(new StatusServlet));
    m_dispatch->addServlet("/_/config", Servlet::ptr(new ConfigServlet));
}

void HttpServer::setName(const std::string& v) {
    TcpServer::setName(v);
    // 设置默认servlet名称
    m_dispatch->setDefault(std::make_shared<NotFoundServlet>(v));
}

void HttpServer::handleClient(Socket::ptr client) {
    SYLAR_LOG_DEBUG(g_logger) << "handleClient " << *client;
    //创建一个会话
    HttpSession::ptr session(new HttpSession(client));
    do {
        // 接收请求报文
        auto req = session->recvRequest();
        if(!req) {
            SYLAR_LOG_DEBUG(g_logger) << "recv http request fail, errno=" << errno << " errstr=" << strerror(errno) << " cliet:" << *client << " keep_alive=" << m_isKeepalive;
            break;
        }
        // 创建响应报文
        HttpResponse::ptr rsp(new HttpResponse(req->getVersion(),req->isClose() || !m_isKeepalive));
        // 设置Server名Head
        rsp->setHeader("Server", getName());
        // 执行操作
        m_dispatch->handle(req, rsp, session);
        // 发送响应报文
        session->sendResponse(rsp);

        if(!m_isKeepalive || req->isClose()) {
            break;
        }
    } while(true);
    session->close();
}

}
}
