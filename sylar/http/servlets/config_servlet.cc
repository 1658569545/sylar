#include "config_servlet.h"
#include "sylar/config.h"

namespace sylar {
namespace http {


ConfigServlet::ConfigServlet()
    :Servlet("ConfigServlet") {
}
/*
用于提供系统配置的 HTTP 接口，允许通过 HTTP 请求获取配置信息。
客户端可以通过 type 参数选择返回的格式，可以是 JSON 或 YAML 格式。
*/
int32_t ConfigServlet::handle(sylar::http::HttpRequest::ptr request
                              ,sylar::http::HttpResponse::ptr response
                              ,sylar::http::HttpSession::ptr session) {
    std::string type = request->getParam("type");
    if(type == "json") {
        response->setHeader("Content-Type", "text/json charset=utf-8");
    } else {
        response->setHeader("Content-Type", "text/yaml charset=utf-8");
    }
    YAML::Node node;
    sylar::Config::Visit([&node](ConfigVarBase::ptr base) {
        YAML::Node n;
        try {
            n = YAML::Load(base->toString());
        } catch(...) {
            return;
        }
        node[base->getName()] = n;
        node[base->getName() + "$description"] = base->getDescription();
    });
    if(type == "json") {
        Json::Value jvalue;
        if(YamlToJson(node, jvalue)) {
            response->setBody(JsonUtil::ToString(jvalue));
            return 0;
        }
    }
    std::stringstream ss;
    ss << node;
    response->setBody(ss.str());
    return 0;
}

}
}
