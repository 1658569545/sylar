/**
 * @file http11_common.h
 * @brief 回调函数
 * @author zq
 */
#ifndef _http11_common_h
#define _http11_common_h

#include <sys/types.h>
/**
 * field_cb 和 element_cb 是函数指针类型，分别用于回调处理 HTTP 头字段和其他 HTTP 元素（如方法、URI 等）。
 * 这些回调在解析到相应部分时被调用，并将数据传递给它们进行处理。
 */
typedef void (*element_cb)(void *data, const char *at, size_t length);
typedef void (*field_cb)(void *data, const char *field, size_t flen, const char *value, size_t vlen);

#endif
