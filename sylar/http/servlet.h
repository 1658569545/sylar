/**
 * @file servlet.h
 * @brief Servlet封装
 * @details Servlet 是一个运行在服务器端的组件，用于处理客户端（通常是浏览器）发送的请求，并生成动态响应。
 * @author zq
 */
#ifndef __SYLAR_HTTP_SERVLET_H__
#define __SYLAR_HTTP_SERVLET_H__

#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include "http.h"
#include "http_session.h"
#include "sylar/thread.h"
#include "sylar/util.h"

namespace sylar {
namespace http {

/**
 * @brief Servlet封装
 */
class Servlet {
public:
    /// 智能指针类型定义
    typedef std::shared_ptr<Servlet> ptr;

    /**
     * @brief 构造函数
     * @param[in] name 名称
     */
    Servlet(const std::string& name)
        :m_name(name) {}

    /**
     * @brief 析构函数
     */
    virtual ~Servlet() {}

    /**
     * @brief 处理请求
     * @param[in] request HTTP请求
     * @param[in] response HTTP响应
     * @param[in] session HTTP连接
     * @details 当访问到该servlet时，执行handle方法
     * @return 是否处理成功
     */
    virtual int32_t handle(sylar::http::HttpRequest::ptr request
                   , sylar::http::HttpResponse::ptr response
                   , sylar::http::HttpSession::ptr session) = 0;
                   
    /**
     * @brief 返回Servlet名称
     */
    const std::string& getName() const { return m_name;}
protected:
    /// 名称
    std::string m_name;
};

/**
 * @brief 函数式Servlet
 * @details 通过一个回调函数来处理 HTTP 请求，并生成响应。
 */
class FunctionServlet : public Servlet {
public:
    /// 智能指针类型定义
    typedef std::shared_ptr<FunctionServlet> ptr;
    /*
    函数回调类型定义
        回调函数的参数：
        HttpRequest::ptr request：封装了 HTTP 请求对象，包含了客户端的请求信息。
        HttpResponse::ptr response：用于构造和发送 HTTP 响应。
        HttpSession::ptr session：封装了客户端和服务器之间的会话信息。
    */
    typedef std::function<int32_t (sylar::http::HttpRequest::ptr request
                   , sylar::http::HttpResponse::ptr response
                   , sylar::http::HttpSession::ptr session)> callback;


    /**
     * @brief 构造函数
     * @param[in] cb 回调函数
     * @details 业务逻辑被封装在回调函数中，可以在实例化时动态传入。因此也更加灵活
     */
    FunctionServlet(callback cb);
    virtual int32_t handle(sylar::http::HttpRequest::ptr request
                   , sylar::http::HttpResponse::ptr response
                   , sylar::http::HttpSession::ptr session) override;
private:
    /// 回调函数
    callback m_cb;
};

/**
 * @brief 定义了一个接口，用于抽象 Servlet 的创建
 */
class IServletCreator {
public:
    typedef std::shared_ptr<IServletCreator> ptr;
    virtual ~IServletCreator() {}
    virtual Servlet::ptr get() const = 0;
    virtual std::string getName() const = 0;
};

/**
 * @brief 持有一个现成的 Servlet 实例，并通过接口方法返回这个实例。
 */
class HoldServletCreator : public IServletCreator {
public:
    typedef std::shared_ptr<HoldServletCreator> ptr;
    HoldServletCreator(Servlet::ptr slt)
        :m_servlet(slt) {
    }

    Servlet::ptr get() const override {
        return m_servlet;
    }

    std::string getName() const override {
        return m_servlet->getName();
    }
private:
    // 保存一个servlet
    Servlet::ptr m_servlet;
};

/**
 * @brief 动态创建不同类型的 Servlet 对象。
 */
template<class T>
class ServletCreator : public IServletCreator {
public:
    typedef std::shared_ptr<ServletCreator> ptr;

    ServletCreator() {
    }
    /*
        动态创建一个 T 类型的 Servlet 对象，并返回其智能指针。使用 new T 直接创建 Servlet 实例，
        要求 T 必须是 Servlet 的派生类，并且有默认构造函数。
    */
    Servlet::ptr get() const override {
        return Servlet::ptr(new T);
    }

    std::string getName() const override {
        return TypeToName<T>();
    }
};

/**
 * @brief Servlet分发器
 * @details 通过 URI 来匹配和分发不同的 Servlet，并能处理精确匹配和模糊匹配请求
 */
class ServletDispatch : public Servlet {
public:
    /// 智能指针类型定义
    typedef std::shared_ptr<ServletDispatch> ptr;
    /// 读写锁类型定义
    typedef RWMutex RWMutexType;

    /**
     * @brief 构造函数
     */
    ServletDispatch();

    /**
     * @brief 处理HTTP请求
     * @details 用于处理 HTTP 请求。根据请求的 URI，匹配并分发到相应的 Servlet。如果没有找到匹配的 Servlet，将使用默认 Servlet。
     */
    virtual int32_t handle(sylar::http::HttpRequest::ptr request
                   , sylar::http::HttpResponse::ptr response
                   , sylar::http::HttpSession::ptr session) override;

    /**
     * @brief 添加servlet
     * @param[in] uri uri
     * @param[in] slt serlvet
     * @details 将指定 URI 路径与 Servlet 对象关联
     */
    void addServlet(const std::string& uri, Servlet::ptr slt);

    /**
     * @brief 添加servlet
     * @param[in] uri uri
     * @param[in] cb FunctionServlet回调函数
     * @details 将指定 URI 路径与一个回调函数 FunctionServlet 关联。
     */
    void addServlet(const std::string& uri, FunctionServlet::callback cb);

    /**
     * @brief 添加模糊匹配servlet
     * @param[in] uri uri 模糊匹配 /sylar_*
     * @param[in] slt servlet
     */
    void addGlobServlet(const std::string& uri, Servlet::ptr slt);

    /**
     * @brief 添加模糊匹配servlet
     * @param[in] uri uri 模糊匹配 /sylar_*
     * @param[in] cb FunctionServlet回调函数
     */
    void addGlobServlet(const std::string& uri, FunctionServlet::callback cb);

    // 使用 IServletCreator 创建器，将 URI 路径与 Servlet 创建器关联。
    void addServletCreator(const std::string& uri, IServletCreator::ptr creator);

    // 使用 IServletCreator 创建器，将 URI 路径与模糊匹配的 Servlet 创建器关联。
    void addGlobServletCreator(const std::string& uri, IServletCreator::ptr creator);

    //模板方法添加 Servlet 创建器
    template<class T>
    void addServletCreator(const std::string& uri) {
        addServletCreator(uri, std::make_shared<ServletCreator<T> >());
    }

    template<class T>
    void addGlobServletCreator(const std::string& uri) {
        addGlobServletCreator(uri, std::make_shared<ServletCreator<T> >());
    }

    /**
     * @brief 删除servlet
     * @param[in] uri uri
     */
    void delServlet(const std::string& uri);

    /**
     * @brief 删除模糊匹配servlet
     * @param[in] uri uri
     */
    void delGlobServlet(const std::string& uri);

    /**
     * @brief 返回默认servlet
     */
    Servlet::ptr getDefault() const { return m_default;}

    /**
     * @brief 设置默认servlet
     * @param[in] v servlet
     */
    void setDefault(Servlet::ptr v) { m_default = v;}


    /**
     * @brief 通过uri获取servlet
     * @param[in] uri uri
     * @return 返回对应的servlet
     */
    Servlet::ptr getServlet(const std::string& uri);

    /**
     * @brief 通过uri获取模糊匹配servlet
     * @param[in] uri uri
     * @return 返回对应的servlet
     */
    Servlet::ptr getGlobServlet(const std::string& uri);

    /**
     * @brief 通过uri获取servlet
     * @param[in] uri uri
     * @return 优先精准匹配,其次模糊匹配,最后返回默认
     */
    Servlet::ptr getMatchedServlet(const std::string& uri);

    void listAllServletCreator(std::map<std::string, IServletCreator::ptr>& infos);
    void listAllGlobServletCreator(std::map<std::string, IServletCreator::ptr>& infos);
private:
    /// 读写互斥量
    RWMutexType m_mutex;
    /// 精准匹配servlet MAP
    /// uri(/sylar/xxx) -> servlet
    std::unordered_map<std::string, IServletCreator::ptr> m_datas;
    /// 模糊匹配servlet 数组
    /// uri(/sylar/*) -> servlet
    std::vector<std::pair<std::string, IServletCreator::ptr> > m_globs;
    /// 默认servlet，所有路径都没匹配到时使用
    Servlet::ptr m_default;
};

/**
 * @brief NotFoundServlet(默认返回404)
 */
class NotFoundServlet : public Servlet {
public:
    /// 智能指针类型定义
    typedef std::shared_ptr<NotFoundServlet> ptr;
    /**
     * @brief 构造函数
     */
    NotFoundServlet(const std::string& name);
    virtual int32_t handle(sylar::http::HttpRequest::ptr request
                   , sylar::http::HttpResponse::ptr response
                   , sylar::http::HttpSession::ptr session) override;

private:
    // 服务器版本名称
    std::string m_name;
    // body
    std::string m_content;
};

}
}

#endif
