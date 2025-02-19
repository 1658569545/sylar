#include "sylar/config.h"
#include "sylar/env.h"
#include "sylar/util.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");


/*
从配置系统的数据存储中找到与指定名称（name）对应的配置变量，
并返回其智能指针（ConfigVarBase::ptr），如果没有找到，则返回 nullptr。
*/
ConfigVarBase::ptr Config::LookupBase(const std::string& name) {
    RWMutexType::ReadLock lock(GetMutex());
    auto it = GetDatas().find(name);
    return it == GetDatas().end() ? nullptr : it->second;
}

//"A.B", 10
//A:
//  B: 10
//  C: str

//在类的非成员函数中：将该函数限制为只能在当前源文件中使用（即内部链接）。
//它不能在其他源文件中被调用，避免了外部对该函数的访问。
//函数遍历一个 YAML 节点，检查每个键是否符合有效的命名规则，然后将该键值对的名称和对应的节点内容存入输出列表 output。
static void ListAllMember(const std::string& prefix,
                          const YAML::Node& node,
                          std::list<std::pair<std::string, const YAML::Node> >& output) {
    //如果 prefix 中包含了不合法的字符，函数会记录一个错误日志，并返回，跳过这个无效的配置项。
    if(prefix.find_first_not_of("abcdefghikjlmnopqrstuvwxyz._012345678")
            != std::string::npos) {
        SYLAR_LOG_ERROR(g_logger) << "Config invalid name: " << prefix << " : " << node;
        return;
    }
    output.push_back(std::make_pair(prefix, node));
    if(node.IsMap()) {
        for(auto it = node.begin(); it != node.end(); ++it) {
            ListAllMember(prefix.empty() ? it->first.Scalar() : prefix + "." + it->first.Scalar(), it->second, output);
        }
    }
}

//将一个 YAML 配置文件的数据加载到配置系统中
void Config::LoadFromYaml(const YAML::Node& root) {
    std::list<std::pair<std::string, const YAML::Node> > all_nodes;
    ListAllMember("", root, all_nodes);

    for(auto& i : all_nodes) {
        std::string key = i.first;
        if(key.empty()) {
            continue;
        }

        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        ConfigVarBase::ptr var = LookupBase(key);

        if(var) {
            if(i.second.IsScalar()) {
                //fromString 会调用setValue,将配置系统的数据设置为文件中的数据
                var->fromString(i.second.Scalar());
            } else {
                std::stringstream ss;
                ss << i.second;
                var->fromString(ss.str());
            }
        }
    }
}

static std::map<std::string, uint64_t> s_file2modifytime;
static sylar::Mutex s_mutex;

//用于从指定目录加载配置文件并更新配置。
void Config::LoadFromConfDir(const std::string& path, bool force) {
    std::string absoulte_path = sylar::EnvMgr::GetInstance()->getAbsolutePath(path);
    std::vector<std::string> files;

    //递归地查找目录 absoulte_path 下的所有 .yml 文件，并将这些文件的路径存储到 files 向量中
    FSUtil::ListAllFile(files, absoulte_path, ".yml");
    
    //遍历所有找到的配置文件路径，逐一进行处理
    for(auto& i : files) {
        {
            struct stat st;
            lstat(i.c_str(), &st);      // 获取文件的元信息（包括修改时间 st_mtime）
            sylar::Mutex::Lock lock(s_mutex);
            if(!force && s_file2modifytime[i] == (uint64_t)st.st_mtime) {
                continue;   //如果文件未改变且未强制加载，则跳过
            }
            s_file2modifytime[i] = st.st_mtime;
        }
        try {
            YAML::Node root = YAML::LoadFile(i);
            LoadFromYaml(root);
            SYLAR_LOG_INFO(g_logger) << "LoadConfFile file=" << i << " ok";
        } catch (...) {
            SYLAR_LOG_ERROR(g_logger) << "LoadConfFile file="
                << i << " failed";
        }
    }
}

//对当前所有配置项执行给定的回调函数 cb
void Config::Visit(std::function<void(ConfigVarBase::ptr)> cb) {
    RWMutexType::ReadLock lock(GetMutex());
    ConfigVarMap& m = GetDatas();
    for(auto it = m.begin(); it != m.end(); ++it) {
        cb(it->second);
    }

}

}
