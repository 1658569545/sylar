#include "sylar/sylar.h"
#include "sylar/iomanager.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <sys/epoll.h>

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

int sock = 0;

void test_fiber_1() {
    SYLAR_LOG_INFO(g_logger) << "test_fiber sock=" << sock;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(sock, F_SETFL, O_NONBLOCK);

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    //115.239.210.27
    // 相当于自己和自己通讯
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr.s_addr);
    if(!connect(sock, (const sockaddr*)&addr, sizeof(addr))) {
    } 
    // EINPROGRESS（意味着操作正在进行中）
    else if(errno == EINPROGRESS) {
        SYLAR_LOG_INFO(g_logger) << "add event errno=" << errno << " " << strerror(errno);
        sylar::IOManager::GetThis()->addEvent(sock, sylar::IOManager::READ, [](){
            // 回调函数
            SYLAR_LOG_INFO(g_logger) << "read callback";
        });
        sylar::IOManager::GetThis()->addEvent(sock, sylar::IOManager::WRITE, [](){
            // 回调函数
            SYLAR_LOG_INFO(g_logger) << "write callback";
            // 对端触发WRITE事件，然后才能触发READ事件，由于是用的本地环回地址，因此是自己与自己通信，因此才会是先触发WRITE，再触发READ
            //sylar::IOManager::GetThis()->cancelEvent(sock, sylar::IOManager::READ);
            close(sock);
            SYLAR_LOG_DEBUG(g_logger)<<"close sock";
        });
    }
    else {
        SYLAR_LOG_INFO(g_logger) << "else " << errno << " " << strerror(errno);
    }
    SYLAR_LOG_DEBUG(g_logger)<<"test_fiber_1() end";
}

void test_1() {
    std::cout << "EPOLLIN=" << EPOLLIN << " EPOLLOUT=" << EPOLLOUT << std::endl;
    sylar::IOManager iom(1, false);
    SYLAR_LOG_DEBUG(g_logger)<<"主线程开始添加任务";
    iom.schedule(&test_fiber_1);
}

void test_fiber_2() {
    SYLAR_LOG_INFO(g_logger) << "test_fiber start";

    sock = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(sock, F_SETFL, O_NONBLOCK);

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "112.80.248.75", &addr.sin_addr.s_addr);

    if (!connect(sock, (const sockaddr*)&addr, sizeof(addr))) {
    } else if(errno == EINPROGRESS) {
        SYLAR_LOG_INFO(g_logger) << "add event errno=" << errno << " " << strerror(errno);

        sylar::IOManager::GetThis()->addEvent(sock, sylar::IOManager::READ, [](){
            SYLAR_LOG_INFO(g_logger) << "read callback";
            char temp[1000];
            int rt = read(sock, temp, 1000);
            if (rt >= 0) {
                std::string ans(temp, rt);
                SYLAR_LOG_INFO(g_logger) << "read:"<<std::endl<<"["<< ans << "]";
            } else {
                SYLAR_LOG_INFO(g_logger) << "read rt = " << rt;
            }
            });
        sylar::IOManager::GetThis()->addEvent(sock, sylar::IOManager::WRITE, [](){
            SYLAR_LOG_INFO(g_logger) << "write callback";
            int rt = write(sock, "GET / HTTP/1.1\r\ncontent-length: 0\r\n\r\n",38);
            SYLAR_LOG_INFO(g_logger) << "write rt = " << rt;
            });
    } 
    else {
        SYLAR_LOG_INFO(g_logger) << "else " << errno << " " << strerror(errno);
    }
    SYLAR_LOG_INFO(g_logger)<<"test_fiber2 end";
}

void test02() {
    sylar::IOManager iom(1, true, "IOM ");
    iom.schedule(test_fiber_2);
}

sylar::Timer::ptr s_timer;
void test_timer() {
    sylar::IOManager iom(1,true);
    
    s_timer = iom.addTimer(100, [](){
        static int i = 0;
        SYLAR_LOG_INFO(g_logger) << "hello timer i=" << i;
        if(++i == 3) {
            // reset会不断重置定时器，即不断重新开始计时并将其加入set中
            s_timer->reset(2000, true);
            //s_timer->cancel();
        }
        if(i==10){
            s_timer->cancel();
        }
    }, true);
}

int main(int argc, char** argv) {
    //g_logger->setLevel(sylar::LogLevel::INFO);
    //test_1();
    //test_fiber();
    //test02();
    
    test_timer();
    return 0;
}
