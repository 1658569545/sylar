#include "http_session.h"
#include "http_parser.h"

namespace sylar {
namespace http {

HttpSession::HttpSession(Socket::ptr sock, bool owner)
    :SocketStream(sock, owner) {
}

HttpRequest::ptr HttpSession::recvRequest() {
    // 创建HttpRequestParser
    HttpRequestParser::ptr parser(new HttpRequestParser);
    // 获取缓冲区大小
    uint64_t buff_size = HttpRequestParser::GetHttpRequestBufferSize();
    // 交给智能指针托管
    std::shared_ptr<char> buffer(
            new char[buff_size], [](char* ptr){
                delete[] ptr;
            });
    char* data = buffer.get();
    int offset = 0;
    do {
        // 在offset后面接着读数据
        int len = read(data + offset, buff_size - offset);
        if(len <= 0) {
            close();
            return nullptr;
        }
        // 当前已经读取的数据长度
        len += offset;
        // 解析缓冲区data中的数据
        // execute会将data向前移动nparse个字节，nparse为已经成功解析的字节数
        size_t nparse = parser->execute(data, len);
        if(parser->hasError()) {
            close();
            return nullptr;
        }
        // 此时data还剩下已经读到的数据 - 解析过的数据
        offset = len - nparse;
        // 缓冲区满了还没解析完
        if(offset == (int)buff_size) {
            close();
            return nullptr;
        }
        // 解析结束
        if(parser->isFinished()) {
            break;
        }
    } while(true);
    // 获得body(消息体)的长度
    int64_t length = parser->getContentLength();
    if(length > 0) {
        std::string body;
        body.resize(length);

        int len = 0;
        // 如果body长度比缓冲区剩余的还大，将缓冲区全部加进来
        if(length >= offset) {
            memcpy(&body[0], data, offset);
            len = offset;
        } 
        // 否则将取length
        else {
            memcpy(&body[0], data, length);
            len = length;
        }
        length -= offset;
        // 缓冲区里的数据也不够，继续读取直到满足length
        if(length > 0) {
            if(readFixSize(&body[len], length) <= 0) {
                close();
                return nullptr;
            }
        }
        // 设置body
        parser->getData()->setBody(body);
    }

    parser->getData()->init();
    //返回解析完的HttpRequest
    return parser->getData();
}

int HttpSession::sendResponse(HttpResponse::ptr rsp) {
    std::stringstream ss;
    ss << *rsp;
    std::string data = ss.str();
    return writeFixSize(data.c_str(), data.size());
}

}
}
