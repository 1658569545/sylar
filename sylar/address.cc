#include "address.h"
#include "log.h"
#include <sstream>
#include <netdb.h>
#include <ifaddrs.h>
#include <stddef.h>

#include "endian.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

/**
 * @brief 生成子网掩码
 * @param[in] bits: 指定掩码的有效位数（为 1 的位数）
 */
template<class T>
static T CreateMask(uint32_t bits) {
    return (1 << (sizeof(T) * 8 - bits)) - 1;
}

/**
 * @brief 计算一个值 value 中二进制表示中 1 的数量
 */
template<class T>
static uint32_t CountBytes(T value) {
    uint32_t result = 0;
    for(; value; ++result) {
        value &= value - 1;
    }
    return result;
}

Address::ptr Address::LookupAny(const std::string& host,int family, int type, int protocol) {
    std::vector<Address::ptr> result;
    //直接调用Lookup即可
    if(Lookup(result, host, family, type, protocol)) {
        return result[0];
    }
    return nullptr;
}

IPAddress::ptr Address::LookupAnyIPAddress(const std::string& host, int family, int type, int protocol) {
    std::vector<Address::ptr> result;
    if(Lookup(result, host, family, type, protocol)) {
        for(auto& i : result) {
            //多态类之间的类型转换使用dynamic_pointer_cast进行类型转换。转换后必须使用一个指针来存储值
            IPAddress::ptr v = std::dynamic_pointer_cast<IPAddress>(i);
            if(v) {
                return v;
            }
        }
    }
    return nullptr;
}


bool Address::Lookup(std::vector<Address::ptr>& result, const std::string& host,int family, int type, int protocol) {
/*
struct addrinfo
{
     int ai_flags;                  自定义行为
     int ai_family;                 地址族
     int ai_socktype;               socket类型
     int ai_protocol;               协议
     socklen_t ai_addrlen;          地址的字节长度
     struct sockaddr *ai_addr;      地址
     char *ai_canonname;            主机规范名称
     struct addrinfo *ai_next;      指向下一个结点
};

void *memchr(const void *str, int c, size_t n)
    在参数 str 所指向的字符串的前 n 个字节中搜索第一次出现字符 c（一个无符号字符）的位置。返回一个指向匹配字节的指针。

c_str()
    将std::string对象转换为 C 风格的字符串，也就是以null（空字符'\0'）结尾的字符数组。

*/
    addrinfo hints, *results, *next;
    hints.ai_flags = 0;
    hints.ai_family = family;
    hints.ai_socktype = type;
    hints.ai_protocol = protocol;
    hints.ai_addrlen = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    std::string node;           //地址，一般为这种形式：www.sylar.top:80（端口号可选）
    const char* service = NULL; //端口号

    //  检查是否是ipv6地址  [address]:
    if(!host.empty() && host[0] == '[') {
        //memchr 查找第一个 ] 的位置，返回该指针
        const char* endipv6 = (const char*)memchr(host.c_str() + 1, ']', host.size() - 1);
        // 找到了 ]
        if(endipv6) {
            //如果存在端口号（: 后面的部分），则提取出端口号。
            if(*(endipv6 + 1) == ':') {
                //  service指向:后第一个字符处，即指向端口号的开始处
                service = endipv6 + 2;
            }
            //  地址为[]里的内容
            // endipv6 - host.c_str() 是 ] 字符的位置减去 host.c_str() 的起始位置，减去1是为了不包括 ] 字符。
            node = host.substr(1, endipv6 - host.c_str() - 1);
        }
    }
    
     //  检查是否是ipv4地址    address:port
    if(node.empty()) {
        // service将会指向第一个 ：
        service = (const char*)memchr(host.c_str(), ':', host.size());
        if(service) {
            //如果找到了 : 字符，并且没有其他 : 字符，则认为这是一个合法的ipv4地址。
            if(!memchr(service + 1, ':', host.c_str() + host.size() - service - 1)) {
                // node存储地址
                node = host.substr(0, service - host.c_str());
                //service指向端口号， ：后面就是端口号
                ++service;
            }
        }
    }

    // 如果没设置地址，就将host赋给他
    if(node.empty()) {
        node = host;
    }

    // 获得地址链表     hints用于指定查询的选项和限制条件，results存储addrinfo地址链表
    int error = getaddrinfo(node.c_str(), service, &hints, &results);
    if(error) {
        SYLAR_LOG_DEBUG(g_logger) << "Address::Lookup getaddress(" << host << ", "
            << family << ", " << type << ") err=" << error << " errstr="
            << gai_strerror(error);
        return false;
    }

    // results指向头节点，用next遍历
    next = results;
    while(next) {
        // 将得到的地址创建出来放到result容器中
        result.push_back(Create(next->ai_addr, (socklen_t)next->ai_addrlen));
        //SYLAR_LOG_INFO(g_logger) << ((sockaddr_in*)next->ai_addr)->sin_addr.s_addr;
        next = next->ai_next;
    }

    // 释放addrinfo指针
    freeaddrinfo(results);
    return !result.empty();
}

bool Address::GetInterfaceAddresses(std::multimap<std::string,std::pair<Address::ptr, uint32_t> >& result,int family) {
/*
struct ifaddrs   
{   
    struct ifaddrs  *ifa_next;      指向链表的下一个成员
    char            *ifa_name;      网卡名称，以0结尾的字符串，比如eth0
    unsigned int     ifa_flags;     接口的标识位
IFF_BROADCAST或IFF_POINTOPOINT设置到此标识位时，影响联合体变量ifu_broadaddr存储广播地址或ifu_dstaddr记录点对点地址

    struct sockaddr *ifa_addr;      存储该接口的地址
    struct sockaddr *ifa_netmask;   存储该接口的子网掩码
    union   
    {   
        struct sockaddr *ifu_broadaddr;     广播地址
        struct sockaddr *ifu_dstaddr;       点对点地址
    } ifa_ifu;   
    #define              ifa_broadaddr ifa_ifu.ifu_broadaddr   
    #define              ifa_dstaddr   ifa_ifu.ifu_dstaddr   
    void            *ifa_data;      存储了该接口协议族的特殊信息，它通常是NULL
};   
*/
    struct ifaddrs *next, *results;

    // getifaddrs(struct ifaddrs **ifap) 获取系统中所有网络接口的信息
    if(getifaddrs(&results) != 0) {
        SYLAR_LOG_DEBUG(g_logger) << "Address::GetInterfaceAddresses getifaddrs err= " << errno << " errstr=" << strerror(errno);
        return false;
    }

    try {
        // results指向头节点，用next遍历
        for(next = results; next; next = next->ifa_next) {
            Address::ptr addr;
            uint32_t prefix_len = ~0u;

             // 地址族确定 并且 该地址族与解析出来的不同
            if(family != AF_UNSPEC && family != next->ifa_addr->sa_family) {
                continue;
            }
            switch(next->ifa_addr->sa_family) {
                // IPv4
                case AF_INET:
                    {
                        // 创建ipv4地址
                        addr = Create(next->ifa_addr, sizeof(sockaddr_in));
                        // 掩码地址
                        uint32_t netmask = ((sockaddr_in*)next->ifa_netmask)->sin_addr.s_addr;
                        // 前缀长度，网络地址的长度
                        prefix_len = CountBytes(netmask);
                    }
                    break;
                // IPv6
                case AF_INET6:
                    {
                        // 创建ipv6地址
                        addr = Create(next->ifa_addr, sizeof(sockaddr_in6));
                        // 掩码地址
                        in6_addr& netmask = ((sockaddr_in6*)next->ifa_netmask)->sin6_addr;
                        prefix_len = 0;
                        // 前缀长度，16字节挨个算
                        for(int i = 0; i < 16; ++i) {
                            prefix_len += CountBytes(netmask.s6_addr[i]);
                        }
                    }
                    break;
                default:
                    break;
            }   
            // 插入到容器中，保存了网卡名，地址和前缀长度
            if(addr) {
                result.insert(std::make_pair(next->ifa_name,std::make_pair(addr, prefix_len)));
            }
        }
    } catch (...) {
        SYLAR_LOG_ERROR(g_logger) << "Address::GetInterfaceAddresses exception";
        freeifaddrs(results);
        return false;
    }

    // 释放results
    freeifaddrs(results);
    return !result.empty();
}

bool Address::GetInterfaceAddresses(std::vector<std::pair<Address::ptr, uint32_t> >&result,const std::string& iface, int family) {
    if(iface.empty() || iface == "*") {
        if(family == AF_INET || family == AF_UNSPEC) {
            // 如果 family 是 AF_INET 或 AF_UNSPEC（表示查询所有地址族），则将一个 IPv4Address 的空实例添加到 
            // result 中，0u 表示附加信息（如网络掩码）为 0。
            result.push_back(std::make_pair(Address::ptr(new IPv4Address()), 0u));
        }
        if(family == AF_INET6 || family == AF_UNSPEC) {
            // 同理
            result.push_back(std::make_pair(Address::ptr(new IPv6Address()), 0u));
        }
        return true;
    }

    std::multimap<std::string,std::pair<Address::ptr, uint32_t> > results;

    // 调用重载的
    if(!GetInterfaceAddresses(results, family)) {
        return false;
    }

    // 使用 multimap 的 equal_range 方法，查找所有与指定接口名 iface 相关的地址信息。
    // equal_range 返回一个返回pair，first为第一个等于的迭代器，second为第一个大于的迭代器
    // euqal_range(itace)会在results中查找所有key为iface的key-value，并且返回一个pair，这个pair的first指向第一个等于iface，second指向第一个大于的
    auto its = results.equal_range(iface);
    for(; its.first != its.second; ++its.first) {
        result.push_back(its.first->second);
    }
    return !result.empty();
}

int Address::getFamily() const {
    return getAddr()->sa_family;
}

std::string Address::toString() const {
    std::stringstream ss;
    insert(ss);
    return ss.str();
}

Address::ptr Address::Create(const sockaddr* addr, socklen_t addrlen) {
    if(addr == nullptr) {
        return nullptr;
    }

    Address::ptr result;
    //根据sockaddr* addr中的sa_family来判断是什么类型的地址
    switch(addr->sa_family) {
        case AF_INET:
            result.reset(new IPv4Address(*(const sockaddr_in*)addr));
            break;
        case AF_INET6:
            result.reset(new IPv6Address(*(const sockaddr_in6*)addr));
            break;
        default:
            result.reset(new UnknownAddress(*addr));
            break;
    }
    return result;
}

bool Address::operator<(const Address& rhs) const {
/*
int memcmp(const void *s1, const void *s2, size_t n);
    s1 和 s2 是指向要比较的内存区域的指针。
    n 是要比较的字节数。

如果返回值 < 0，则表示 str1 小于 str2。
如果返回值 > 0，则表示 str1 大于 str2。
如果返回值 = 0，则表示 str1 等于 str2。
*/
    socklen_t minlen = std::min(getAddrLen(), rhs.getAddrLen());
    int result = memcmp(getAddr(), rhs.getAddr(), minlen);
    if(result < 0) {
        return true;
    } 
    else if(result > 0) {
        return false;
    } 
    else if(getAddrLen() < rhs.getAddrLen()) {
        return true;
    }
    return false;
}

bool Address::operator==(const Address& rhs) const {
    return getAddrLen() == rhs.getAddrLen()
        && memcmp(getAddr(), rhs.getAddr(), getAddrLen()) == 0;
}

bool Address::operator!=(const Address& rhs) const {
    return !(*this == rhs);
}

IPAddress::ptr IPAddress::Create(const char* address, uint16_t port) {
    addrinfo hints, *results;
    memset(&hints, 0, sizeof(addrinfo));

    hints.ai_flags = AI_NUMERICHOST;    // 设置 getaddrinfo函数只接受 IP 地址
    hints.ai_family = AF_UNSPEC;        // 协议无关。

    //根据域名获取IP地址信息并存放至results中
    //或者直接通过IP地址获取更为完善的IP地址信息（包括端口号）
    int error = getaddrinfo(address, NULL, &hints, &results);
    if(error) {
        SYLAR_LOG_DEBUG(g_logger) << "IPAddress::Create(" << address
            << ", " << port << ") error=" << error
            << " errno=" << errno << " errstr=" << strerror(errno);
        return nullptr;
    }

    try {
        //利用Address的Create函数创建一个IPAddress对象
        IPAddress::ptr result = std::dynamic_pointer_cast<IPAddress>(Address::Create(results->ai_addr, (socklen_t)results->ai_addrlen));
        if(result) {
            result->setPort(port);
        }
        freeaddrinfo(results);
        return result;
    } catch (...) {
        freeaddrinfo(results);
        return nullptr;
    }
}

IPv4Address::ptr IPv4Address::Create(const char* address, uint16_t port) {
    IPv4Address::ptr rt(new IPv4Address);
    rt->m_addr.sin_port = byteswapOnLittleEndian(port);
    //即点分十进制转二进制整数
    int result = inet_pton(AF_INET, address, &rt->m_addr.sin_addr);
    if(result <= 0) {
        SYLAR_LOG_DEBUG(g_logger) << "IPv4Address::Create(" << address << ", "
                << port << ") rt=" << result << " errno=" << errno
                << " errstr=" << strerror(errno);
        return nullptr;
    }
    return rt;
}

IPv4Address::IPv4Address(const sockaddr_in& address) {
    m_addr = address;
}

IPv4Address::IPv4Address(uint32_t address, uint16_t port) {
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sin_family = AF_INET;
    m_addr.sin_port = byteswapOnLittleEndian(port);
    m_addr.sin_addr.s_addr = byteswapOnLittleEndian(address);
}

sockaddr* IPv4Address::getAddr() {
    return (sockaddr*)&m_addr;
}

const sockaddr* IPv4Address::getAddr() const {
    return (sockaddr*)&m_addr;
}

socklen_t IPv4Address::getAddrLen() const {
    return sizeof(m_addr);
}

// 将 IPv4Address 对象的内容输出到流 (std::ostream) 中，通常用于实现类似 std::cout 或者日志记录等输出功能。
// 它会将 IPv4 地址（包括地址和端口）格式化成易于阅读的字符串形式，例如 192.168.1.1:8080。
std::ostream& IPv4Address::insert(std::ostream& os) const {
    uint32_t addr = byteswapOnLittleEndian(m_addr.sin_addr.s_addr);
    //IP地址二进制整数转为常见的192.168.1.1:8080
    os << ((addr >> 24) & 0xff) << "."
       << ((addr >> 16) & 0xff) << "."
       << ((addr >> 8) & 0xff) << "."
       << (addr & 0xff);
    os << ":" << byteswapOnLittleEndian(m_addr.sin_port);
    return os;
}

// 获取该地址的广播地址
IPAddress::ptr IPv4Address::broadcastAddress(uint32_t prefix_len) {
    //如果prefix_len大于 32，则表示无效的子网前缀长度
    if(prefix_len > 32) {
        return nullptr;
    }

    sockaddr_in baddr(m_addr);

    // baddr.sin_addr.s_addr |= ...：这是按位或操作，它将原始地址的主机部分（后面的位）与掩码的主机部分（全 1）进行合并。
    // 实际上，这是将原始地址的主机部分设为 1，从而生成广播地址。
    baddr.sin_addr.s_addr |= byteswapOnLittleEndian(CreateMask<uint32_t>(prefix_len));
    return IPv4Address::ptr(new IPv4Address(baddr));
}

//根据给定的前缀长度（prefix_len），计算出与该地址对应的网络地址（IPv4）。
IPAddress::ptr IPv4Address::networdAddress(uint32_t prefix_len) {
    if(prefix_len > 32) {
        return nullptr;
    }

    sockaddr_in baddr(m_addr);
    baddr.sin_addr.s_addr &= byteswapOnLittleEndian(CreateMask<uint32_t>(prefix_len));
    return IPv4Address::ptr(new IPv4Address(baddr));
}

//根据给定的前缀长度（prefix_len），计算并返回相应的子网掩码(IPv4)。
IPAddress::ptr IPv4Address::subnetMask(uint32_t prefix_len) {
    sockaddr_in subnet;
    memset(&subnet, 0, sizeof(subnet));
    subnet.sin_family = AF_INET;

    //获取被反转后的掩码。
    subnet.sin_addr.s_addr = ~byteswapOnLittleEndian(CreateMask<uint32_t>(prefix_len));
    return IPv4Address::ptr(new IPv4Address(subnet));
}

//获取端口号
uint32_t IPv4Address::getPort() const {
    return byteswapOnLittleEndian(m_addr.sin_port);
}

//设置端口号
void IPv4Address::setPort(uint16_t v) {
    m_addr.sin_port = byteswapOnLittleEndian(v);
}

IPv6Address::ptr IPv6Address::Create(const char* address, uint16_t port) {
    IPv6Address::ptr rt(new IPv6Address);
    rt->m_addr.sin6_port = byteswapOnLittleEndian(port);
    int result = inet_pton(AF_INET6, address, &rt->m_addr.sin6_addr);
    if(result <= 0) {
        SYLAR_LOG_DEBUG(g_logger) << "IPv6Address::Create(" << address << ", "
                << port << ") rt=" << result << " errno=" << errno
                << " errstr=" << strerror(errno);
        return nullptr;
    }
    return rt;
}

IPv6Address::IPv6Address() {
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sin6_family = AF_INET6;
}

IPv6Address::IPv6Address(const sockaddr_in6& address) {
    m_addr = address;
}

IPv6Address::IPv6Address(const uint8_t address[16], uint16_t port) {
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sin6_family = AF_INET6;
    m_addr.sin6_port = byteswapOnLittleEndian(port);
    memcpy(&m_addr.sin6_addr.s6_addr, address, 16);
}

//获取IPv6地址
sockaddr* IPv6Address::getAddr() {
    return (sockaddr*)&m_addr;
}

//获取IPv6地址（只读）
const sockaddr* IPv6Address::getAddr() const {
    return (sockaddr*)&m_addr;
}

//获取地址长度
socklen_t IPv6Address::getAddrLen() const {
    return sizeof(m_addr);
}

// 将 IPv6Address 对象的内容输出到流 (std::ostream) 中，通常用于实现类似 std::cout 或者日志记录等输出功能。
// 首先，IPv6 地址通常由 8 个 16 位的段组成，每个段由 4 个十六进制数字表示。
// 在 IPv6 中，如果地址中有连续的 0 段，可以使用 :: 来压缩表示，且只可以出现一次。
// 例如：2001:0db8:0000:0042:0000:8a2e:0370:7334 -> 2001:db8::42:0:8a2e:370:7334
std::ostream& IPv6Address::insert(std::ostream& os) const {
    os << "[";

    // m_addr.sin6_addr.s6_addr 是一个包含 128 位 IPv6 地址的字节数组。
    // 我们将它转换成 uint16_t*，因为 IPv6 地址的每段是 16 位的。
    uint16_t* addr = (uint16_t*)m_addr.sin6_addr.s6_addr;

    // 标记是否使用过 "::" 来压缩连续的零段
    bool used_zeros = false;

    // 遍历 IPv6 地址的 8 个段
    for(size_t i = 0; i < 8; ++i) {
        // 如果该段为 0 且未使用 "::",则跳过该段，等待后续可能压缩为 "::"
        if(addr[i] == 0 && !used_zeros) {
            continue;
        }

        // 如果遇到连续的零段，并且还未压缩过零段，则多输出个:
        // i也加入判断是为了判断是否是第一个段
        if(i && addr[i - 1] == 0 && !used_zeros) {
            os << ":";
            used_zeros = true;
        }
        // 如果不是第一个段，前面加上冒号
        if(i) {
            os << ":";
        }
        // 按十六进制输出
        os << std::hex << (int)byteswapOnLittleEndian(addr[i]) << std::dec;
    }
    // 如果没有压缩零段，并且最后一个段是零，则多输出 ：：
    if(!used_zeros && addr[7] == 0) {
        os << "::";
    }

    os << "]:" << byteswapOnLittleEndian(m_addr.sin6_port);
    return os;
}

// 获取该地址的广播地址
IPAddress::ptr IPv6Address::broadcastAddress(uint32_t prefix_len) {
    sockaddr_in6 baddr(m_addr);

    baddr.sin6_addr.s6_addr[prefix_len / 8] |= CreateMask<uint8_t>(prefix_len % 8);

    for(int i = prefix_len / 8 + 1; i < 16; ++i) {
        baddr.sin6_addr.s6_addr[i] = 0xff;
    }
    return IPv6Address::ptr(new IPv6Address(baddr));
}

//根据给定的前缀长度（prefix_len），计算出与该地址对应的网络地址（IPv6）。
IPAddress::ptr IPv6Address::networdAddress(uint32_t prefix_len) {
    sockaddr_in6 baddr(m_addr);

    baddr.sin6_addr.s6_addr[prefix_len / 8] &= CreateMask<uint8_t>(prefix_len % 8);

    for(int i = prefix_len / 8 + 1; i < 16; ++i) {
        baddr.sin6_addr.s6_addr[i] = 0x00;
    }
    return IPv6Address::ptr(new IPv6Address(baddr));
}

//根据给定的前缀长度（prefix_len），计算并返回相应的子网掩码(IPv6)。
IPAddress::ptr IPv6Address::subnetMask(uint32_t prefix_len) {
    sockaddr_in6 subnet;
    memset(&subnet, 0, sizeof(subnet));
    subnet.sin6_family = AF_INET6;
    subnet.sin6_addr.s6_addr[prefix_len /8] = ~CreateMask<uint8_t>(prefix_len % 8);

    for(uint32_t i = 0; i < prefix_len / 8; ++i) {
        subnet.sin6_addr.s6_addr[i] = 0xff;
    }
    return IPv6Address::ptr(new IPv6Address(subnet));
}

//获取端口号（IPv6）
uint32_t IPv6Address::getPort() const {
    return byteswapOnLittleEndian(m_addr.sin6_port);
}

//设置端口号（IPv6）
void IPv6Address::setPort(uint16_t v) {
    m_addr.sin6_port = byteswapOnLittleEndian(v);
}

static const size_t MAX_PATH_LEN = sizeof(((sockaddr_un*)0)->sun_path) - 1;

UnixAddress::UnixAddress() {
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sun_family = AF_UNIX;
    m_length = offsetof(sockaddr_un, sun_path) + MAX_PATH_LEN;
}

UnixAddress::UnixAddress(const std::string& path) {
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sun_family = AF_UNIX;
    m_length = path.size() + 1;

    if(!path.empty() && path[0] == '\0') {
        --m_length;
    }

    if(m_length > sizeof(m_addr.sun_path)) {
        throw std::logic_error("path too long");
    }
    memcpy(m_addr.sun_path, path.c_str(), m_length);
    m_length += offsetof(sockaddr_un, sun_path);
}

//设置长度
void UnixAddress::setAddrLen(uint32_t v) {
    m_length = v;
}

//获取地址
sockaddr* UnixAddress::getAddr() {
    return (sockaddr*)&m_addr;
}

//获取地址（只读）
const sockaddr* UnixAddress::getAddr() const {
    return (sockaddr*)&m_addr;
}

//获取地址长度
socklen_t UnixAddress::getAddrLen() const {
    return m_length;
}

//获取路径
std::string UnixAddress::getPath() const {
    std::stringstream ss;
    if(m_length > offsetof(sockaddr_un, sun_path)
            && m_addr.sun_path[0] == '\0') {
        ss << "\\0" << std::string(m_addr.sun_path + 1,
                m_length - offsetof(sockaddr_un, sun_path) - 1);
    } else {
        ss << m_addr.sun_path;
    }
    return ss.str();
}

//将UnixAddress地址输入到输出流中
std::ostream& UnixAddress::insert(std::ostream& os) const {
    if(m_length > offsetof(sockaddr_un, sun_path)
            && m_addr.sun_path[0] == '\0') {
        return os << "\\0" << std::string(m_addr.sun_path + 1,
                m_length - offsetof(sockaddr_un, sun_path) - 1);
    }
    return os << m_addr.sun_path;
}

UnknownAddress::UnknownAddress(int family) {
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sa_family = family;
}

UnknownAddress::UnknownAddress(const sockaddr& addr) {
    m_addr = addr;
}

sockaddr* UnknownAddress::getAddr() {
    return (sockaddr*)&m_addr;
}

const sockaddr* UnknownAddress::getAddr() const {
    return &m_addr;
}

socklen_t UnknownAddress::getAddrLen() const {
    return sizeof(m_addr);
}

std::ostream& UnknownAddress::insert(std::ostream& os) const {
    os << "[UnknownAddress family=" << m_addr.sa_family << "]";
    return os;
}

//
std::ostream& operator<<(std::ostream& os, const Address& addr) {
    return addr.insert(os);
}

}
