#include "env.h"
#include "sylar/log.h"
#include <string.h>
#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <stdlib.h>
#include "config.h"

namespace sylar {

//和静态非成员函数一样，只在本文件可见
static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

//对环境相关的信息进行初始化，具体包括获取可执行文件的路径、所在的当前工作目录、程序名以及解析命令行参数，
//并将解析后的参数信息存储到合适的地方（通过调用add函数）最后返回初始化是否成功的布尔值。
bool Env::init(int argc, char** argv) {
    char link[1024] = {0};
    char path[1024] = {0};
    sprintf(link, "/proc/%d/exe", getpid());
    readlink(link, path, sizeof(path));
    // /path/xxx/exe
    m_exe = path;

    auto pos = m_exe.find_last_of("/");
    m_cwd = m_exe.substr(0, pos) + "/";

    m_program = argv[0];
    // -config /path/to/config -file xxxx -d
    const char* now_key = nullptr;
    for(int i = 1; i < argc; ++i) {
        if(argv[i][0] == '-') {
            if(strlen(argv[i]) > 1) {
                if(now_key) {
                    add(now_key, "");
                }
                now_key = argv[i] + 1;
            } else {
                SYLAR_LOG_ERROR(g_logger) << "invalid arg idx=" << i
                    << " val=" << argv[i];
                return false;
            }
        } else {
            if(now_key) {
                add(now_key, argv[i]);
                now_key = nullptr;
            } else {
                SYLAR_LOG_ERROR(g_logger) << "invalid arg idx=" << i
                    << " val=" << argv[i];
                return false;
            }
        }
    }
    if(now_key) {
        add(now_key, "");
    }
    return true;
}

void Env::add(const std::string& key, const std::string& val) {
    RWMutexType::WriteLock lock(m_mutex);
    m_args[key] = val;
}

bool Env::has(const std::string& key) {
    RWMutexType::ReadLock lock(m_mutex);
    auto it = m_args.find(key);
    return it != m_args.end();
}

void Env::del(const std::string& key) {
    RWMutexType::WriteLock lock(m_mutex);
    m_args.erase(key);
}

std::string Env::get(const std::string& key, const std::string& default_value) {
    RWMutexType::ReadLock lock(m_mutex);
    auto it = m_args.find(key);
    return it != m_args.end() ? it->second : default_value;
}

void Env::addHelp(const std::string& key, const std::string& desc) {
    removeHelp(key);
    RWMutexType::WriteLock lock(m_mutex);
    m_helps.push_back(std::make_pair(key, desc));
}

void Env::removeHelp(const std::string& key) {
    RWMutexType::WriteLock lock(m_mutex);
    for(auto it = m_helps.begin();
            it != m_helps.end();) {
        if(it->first == key) {
            it = m_helps.erase(it);
        } else {
            ++it;
        }
    }
}

void Env::printHelp() {
    RWMutexType::ReadLock lock(m_mutex);
    std::cout << "Usage: " << m_program << " [options]" << std::endl;
    for(auto& i : m_helps) {
        std::cout << std::setw(5) << "-" << i.first << " : " << i.second << std::endl;
    }
}

bool Env::setEnv(const std::string& key, const std::string& val) {
    return !setenv(key.c_str(), val.c_str(), 1);
}

std::string Env::getEnv(const std::string& key, const std::string& default_value) {
    const char* v = getenv(key.c_str());
    if(v == nullptr) {
        return default_value;
    }
    return v;
}

std::string Env::getAbsolutePath(const std::string& path) const {
    if(path.empty()) {
        return "/";
    }
    if(path[0] == '/') {
        return path;
    }
    return m_cwd + path;
}

std::string Env::getAbsoluteWorkPath(const std::string& path) const {
    if(path.empty()) {
        return "/";
    }
    if(path[0] == '/') {
        return path;
    }
    static sylar::ConfigVar<std::string>::ptr g_server_work_path =
        sylar::Config::Lookup<std::string>("server.work_path");
    return g_server_work_path->getValue() + "/" + path;
}

std::string Env::getConfigPath() {
    return getAbsolutePath(get("c", "conf"));
}

}
