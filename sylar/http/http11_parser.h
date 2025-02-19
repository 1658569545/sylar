/**
 * @file http11_parser.h
 * @brief http解析器
 * @author zq
 */
#ifndef http11_parser_h
#define http11_parser_h

#include "http11_common.h"

// http解析器
typedef struct http_parser { 
  ///  用于存储解析器的当前状态。通常在状态机中用于跟踪当前解析步骤。
  int cs;         
  ///  表示请求或响应正文的起始位置（通常是在解析头部之后）。
  size_t body_start;
  ///  表示内容的长度，通常是 HTTP 消息体的长度。
  int content_len;
  ///  表示已读取的数据的长度。
  size_t nread;
  ///  标记位置，通常用于记录需要回退或重置的地方。
  size_t mark;
  ///  记录 HTTP 头字段的起始位置。
  size_t field_start;
  ///  记录 HTTP 头字段的长度。
  size_t field_len;
  ///  记录查询字符串的起始位置。
  size_t query_start;
  ///  标记 XML 数据是否已被处理过。
  int xml_sent;
  ///  标记 XML 数据是否已被处理过。
  int json_sent;
  ///  可以是用户自定义的指针，通常用于存储额外的状态或数据。
  void *data;
  ///  表示 URI 是否松散解析。
  int uri_relaxed;
  ///  指向一个回调函数，处理 HTTP 头字段。
  field_cb http_field;

  /// 回调，处理http请求的方法部分
  element_cb request_method;
  /// 回调，处理http请求的uri部分
  element_cb request_uri;
  /// 回调，处理http请求的fragment
  element_cb fragment;
  /// 回调，处理http请求的路径部分
  element_cb request_path;
  /// 回调，处理http请求的查询部分
  element_cb query_string;
  /// 回调，处理http请求的http版本部分
  element_cb http_version;
  /// 回调，处理http请求的头部
  element_cb header_done;
  
} http_parser;

//  初始化 HTTP 解析器。它将解析器的状态设置为初始值。
int http_parser_init(http_parser *parser);
//  是否解析完成。
int http_parser_finish(http_parser *parser);

/*
执行解析操作。它将数据传递给解析器，解析器会处理这部分数据并更新其内部状态。
  parser：传递的 HTTP 解析器实例。
  data：要解析的数据。
  len：数据的长度。
  off：数据偏移量，指示数据从哪个位置开始解析。
*/
size_t http_parser_execute(http_parser *parser, const char *data, size_t len, size_t off);

//检查解析器是否遇到错误。
int http_parser_has_error(http_parser *parser);
//检查解析器是否已完成 HTTP 消息的解析。
int http_parser_is_finished(http_parser *parser);

// 该宏用于获取解析器已读取的数据长度。它等价于 parser->nread，用于获取解析器读取的字节数。
#define http_parser_nread(parser) (parser)->nread 

#endif
