/**
 * @file config.h
 * @brief 配置模块
 * 用于定义/声明配置项，并且从配置文件中加载用户配置。
 * @author zq
 */
#ifndef __SYLAR_CONFIG_H__
#define __SYLAR_CONFIG_H__

#include <memory>
#include <string>
#include <sstream>
#include <boost/lexical_cast.hpp>
#include <yaml-cpp/yaml.h>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <functional>

#include "thread.h"
#include "log.h"
#include "util.h"

/* 
    配置模块主要功能：
    ConfigVarBase: 配置项基类，虚基类，定义了配置项公有的成员和方法。sylar对每个配置项都包括名称和描述两项成员，以及toString/fromString两个纯虚函数方法。ConfigVarBase并不包含配置项类型和值，
这些由继承类实现，由继承类实现的还包括具体类型的toString/fromString方法，用于和YAML字符串进行相互转换。

    ConfigVar: 具体的配置参数类，继承自ConfigVarBase，并且是一个模板类，有3个模板参数。第一个模板参数是类型T，表示配置项的类型。另外两个模板参数是FromStr和ToStr，这两个参数是仿函数，
FromStr用于将YAML字符串转类型T，ToStr用于将T转YAML字符串。这两个模板参数具有默认值LexicalCast<std::string, T>和LexicalCast<T, std::string>，根据不同的类型T，FromStr和ToStr具有不同的偏特化实现。

    ConfigVar类在ConfigVarBase上基础上包含了一个T类型的成员和一个变更回调函数数组，此外，ConfigVar还提供了setValue/getValue方法用于获取/更新配置值（更新配置时会一并触发全部的配置变更回调函数），
以及addListener/delListener方法用于添加或删除配置变更回调函数。

    Config: ConfigVar的管理类，负责托管全部的ConfigVar对象，单例模式。提供Lookup方法，用于根据配置名称查询配置项。如果调用Lookup查询时同时提供了默认值和配置项的描述信息，那么在未找到对应的配置时，
会自动创建一个对应的配置项，这样就保证了配置模块定义即可用的特性。除此外，Config类还提供了LoadFromYaml和LoadFromConfDir两个方法，用于从YAML对象或从命令行-c选项指定的配置文件路径中加载配置。
Config的全部成员变量和方法都是static类型，保证了全局只有一个实例。
*/


namespace sylar {


//类型转换模板类(F 源类型, T 目标类型)
template<class F, class T>
class LexicalCast {
public:
    /**
     * @brief 类型转换
     * @param[in] v 源类型值
     * @return 返回v转换后的目标类型
     * @exception 当类型不可转换时抛出异常
     */
    T operator()(const F& v) {
        return boost::lexical_cast<T>(v);
    }
};

//类型转换模板类偏特化(YAML String 转换成 std::vector<T>)
template<class T>
class LexicalCast<std::string, std::vector<T> > {
public:
    std::vector<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        //强调 std::vector<T>是一个类型
        typename std::vector<T> vec;
        std::stringstream ss;
        for(size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            /**
             * 将ss中的元素强制转换为T类型
             * 先寻找string->T的偏特化版本，再寻找泛化
             * LexicalCast<std::string, T>()：构造一个临时的<std::string, T>对象
             * operator()(i)：调用重载的函数调用运算符，将 string 类型的对象 ss.str() 转换为 T 类型。
             */
            vec.push_back(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

//类型转换模板类偏特化(std::vector<T> 转换成 YAML String)
template<class T>
class LexicalCast<std::vector<T>, std::string> {
public:
    std::string operator()(const std::vector<T>& v) {
        //YAML::NodeType::Sequence 明确指定节点类型为序列。
        YAML::Node node(YAML::NodeType::Sequence);
        for(auto& i : v) {
            //将i转换为string类型
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

//类型转换模板类偏特化(YAML String 转换成 std::list<T>)
template<class T>
class LexicalCast<std::string, std::list<T> > {
public:
    std::list<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::list<T> vec;
        std::stringstream ss;
        for(size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.push_back(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

//类型转换模板类偏特化(std::list<T> 转换成 YAML String)
template<class T>
class LexicalCast<std::list<T>, std::string> {
public:
    std::string operator()(const std::list<T>& v) {
        YAML::Node node(YAML::NodeType::Sequence);
        for(auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

//类型转换模板类偏特化(YAML String 转换成 std::set<T>)
template<class T>
class LexicalCast<std::string, std::set<T> > {
public:
    std::set<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::set<T> vec;
        std::stringstream ss;
        for(size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.insert(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

//类型转换模板类偏特化(std::set<T> 转换成 YAML String)
template<class T>
class LexicalCast<std::set<T>, std::string> {
public:
    std::string operator()(const std::set<T>& v) {
        YAML::Node node(YAML::NodeType::Sequence);
        for(auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

//类型转换模板类偏特化(YAML String 转换成 std::unordered_set<T>)
template<class T>
class LexicalCast<std::string, std::unordered_set<T> > {
public:
    std::unordered_set<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::unordered_set<T> vec;
        std::stringstream ss;
        for(size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.insert(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

//类型转换模板类偏特化(std::unordered_set<T> 转换成 YAML String)
template<class T>
class LexicalCast<std::unordered_set<T>, std::string> {
public:
    std::string operator()(const std::unordered_set<T>& v) {
        YAML::Node node(YAML::NodeType::Sequence);
        for(auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

//类型转换模板类偏特化(YAML String 转换成 std::map<std::string, T>)
template<class T>
class LexicalCast<std::string, std::map<std::string, T> > {
public:
    std::map<std::string, T> operator()(const std::string& v) {
        //Node节点的类型通常会根据加载的数据自动推断,因此node是一个map类型
        YAML::Node node = YAML::Load(v);
        typename std::map<std::string, T> vec;
        std::stringstream ss;
        for(auto it = node.begin();
                it != node.end(); ++it) {
            ss.str("");
            ss << it->second;
            //it->first.Scalar() 是用于获取 YAML 节点键的字符串值 的方法
            vec.insert(std::make_pair(it->first.Scalar(),
                        LexicalCast<std::string, T>()(ss.str())));
        }
        return vec;
    }
};

//类型转换模板类偏特化(std::map<std::string, T> 转换成 YAML String)
template<class T>
class LexicalCast<std::map<std::string, T>, std::string> {
public:
    std::string operator()(const std::map<std::string, T>& v) {
        YAML::Node node(YAML::NodeType::Map);
        for(auto& i : v) {
            node[i.first] = YAML::Load(LexicalCast<T, std::string>()(i.second));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

//类型转换模板类偏特化(YAML String 转换成 std::unordered_map<std::string, T>)
template<class T>
class LexicalCast<std::string, std::unordered_map<std::string, T> > {
public:
    std::unordered_map<std::string, T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::unordered_map<std::string, T> vec;
        std::stringstream ss;
        for(auto it = node.begin();
                it != node.end(); ++it) {
            ss.str("");
            ss << it->second;
            vec.insert(std::make_pair(it->first.Scalar(),
                        LexicalCast<std::string, T>()(ss.str())));
        }
        return vec;
    }
};

//类型转换模板类偏特化(std::unordered_map<std::string, T> 转换成 YAML String)
template<class T>
class LexicalCast<std::unordered_map<std::string, T>, std::string> {
public:
    std::string operator()(const std::unordered_map<std::string, T>& v) {
        YAML::Node node(YAML::NodeType::Map);
        for(auto& i : v) {
            node[i.first] = YAML::Load(LexicalCast<T, std::string>()(i.second));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};


/**
 * @brief 配置变量的基类
 */
class ConfigVarBase {
public:
    typedef std::shared_ptr<ConfigVarBase> ptr;

    /**
     * @brief 构造函数
     * @param[in] name 配置参数名称[0-9a-z_.]
     * @param[in] description 配置参数描述
     */
    ConfigVarBase(const std::string& name, const std::string& description = "")
        :m_name(name)
        ,m_description(description) {

        //将所有名称转换为小写，第三个参数是输出的起始迭代器
        std::transform(m_name.begin(), m_name.end(), m_name.begin(), ::tolower);
    }

    /**
     * @brief 析构函数
     * @details 虚析构函数保证子类在析构的时候，也能调用父类的析构函数来析构掉父类的相关资源
     */
    virtual ~ConfigVarBase() {}

    /**
     * @brief 返回配置参数名称
     */
    const std::string& getName() const { return m_name;}

    /**
     * @brief 返回配置参数的描述
     */
    const std::string& getDescription() const { return m_description;}

    /**
     * @brief 转成字符串
     */
    virtual std::string toString() = 0;

    /**
     * @brief 从字符串初始化值
     */
    virtual bool fromString(const std::string& val) = 0;

    /**
     * @brief 返回配置参数值的类型名称
     */
    virtual std::string getTypeName() const = 0;

protected:
    /// 配置参数的名称
    std::string m_name;
    /// 配置参数的描述
    std::string m_description;
};



/*
    ConfigVar: 具体的配置参数类，继承自ConfigVarBase，并且是一个模板类，有3个模板参数。第一个模板参数是类型T，
    表示配置项的类型。另外两个模板参数是FromStr和ToStr，这两个参数是仿函数，FromStr用于将YAML字符串转类型T，
    ToStr用于将T转YAML字符串。这两个模板参数具有默认值LexicalCast<std::string, T>和LexicalCast<T, std::string>，
    根据不同的类型T，FromStr和ToStr具有不同的偏特化实现。

    ConfigVar类在ConfigVarBase上基础上包含了一个T类型的成员和一个变更回调函数数组，此外，ConfigVar还提供了
    setValue/getValue方法用于获取/更新配置值（更新配置时会一并触发全部的配置变更回调函数），以及addListener/delListener
    方法用于添加或删除配置变更回调函数。
*/

/**
 * @brief 配置参数模板子类,保存对应类型的参数值
 * 使用模板支持对不同类型的配置参数进行管理。
 * 允许通过 YAML 字符串初始化配置值，或者将配置值输出为 YAML 格式字符串。
 * 支持配置值更新时通知回调。
 * @details T 参数的具体类型
 *          FromStr 从std::string转换成T类型的仿函数
 *          ToStr 从T转换成std::string的仿函数
 *          std::string 为YAML格式的字符串
 */
template<class T, class FromStr = LexicalCast<std::string, T>
                ,class ToStr = LexicalCast<T, std::string> >
class ConfigVar : public ConfigVarBase {
public:
    typedef RWMutex RWMutexType; 
    typedef std::shared_ptr<ConfigVar> ptr;

    //封装一个回调函数
    typedef std::function<void (const T& old_value, const T& new_value)> on_change_cb;

    /**
     * @brief 通过参数名,参数值,描述构造ConfigVar
     * @param[in] name 参数名称有效字符为[0-9a-z_.]
     * @param[in] default_value 参数的默认值
     * @param[in] description 参数的描述
     */
    ConfigVar(const std::string& name, const T& default_value, const std::string& description = "")
        :ConfigVarBase(name, description) 
        ,m_val(default_value) {
    }

    /**
     * @brief 将参数值转换成YAML String
     * @exception 当转换失败抛出异常
     */
    std::string toString() override {
        try {
            //return boost::lexical_cast<std::string>(m_val);
            RWMutexType::ReadLock lock(m_mutex);

            //调用LexicalCast<T, std::string>的仿函数，然后找不到偏特化模板时，会从我们定义的偏特化模板中找最匹配的。
            return ToStr()(m_val);
        } catch (std::exception& e) {
            SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "ConfigVar::toString exception "
                << e.what() << " convert: " << TypeToName<T>() << " to string"
                << " name=" << m_name;
        }
        return "";
    }

    /**
     * @brief 从YAML String 转成参数的值
     * @exception 当转换失败抛出异常
     */
    bool fromString(const std::string& val) override {
        try {
            //与ToStr同理
            setValue(FromStr()(val));
        } catch (std::exception& e) {
            SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "ConfigVar::fromString exception "
                << e.what() << " convert: string to " << TypeToName<T>()
                << " name=" << m_name
                << " - " << val;
        }
        return false;
    }

    //获取当前参数的值
    const T getValue() {
        RWMutexType::ReadLock lock(m_mutex);
        return m_val;
    }

    //设置当前参数的值     如果参数的值有发生变化,则通知对应的注册回调函数
    void setValue(const T& v) {
        {
            RWMutexType::ReadLock lock(m_mutex);
            if(v == m_val) {
                return;
            }
            for(auto& i : m_cbs) {
                i.second(m_val, v);
            }
        }
        RWMutexType::WriteLock lock(m_mutex);
        m_val = v;
    }

    // 返回参数值的类型名称(typeinfo)
    std::string getTypeName() const override { return TypeToName<T>();}

    /**
     * @brief 添加变化回调函数
     * @return 返回该回调函数对应的唯一id,用于删除回调
     */
    uint64_t addListener(on_change_cb cb) {
        static uint64_t s_fun_id = 0;
        RWMutexType::WriteLock lock(m_mutex);
        ++s_fun_id;
        m_cbs[s_fun_id] = cb;
        return s_fun_id;
    }

    /**
     * @brief 删除回调函数
     * @param[in] key 回调函数的唯一id
     */
    void delListener(uint64_t key) {
        RWMutexType::WriteLock lock(m_mutex);
        m_cbs.erase(key);
    }

    /**
     * @brief 获取回调函数
     * @param[in] key 回调函数的唯一id
     * @return 如果存在返回对应的回调函数,否则返回nullptr
     */
    on_change_cb getListener(uint64_t key) {
        RWMutexType::ReadLock lock(m_mutex);
        auto it = m_cbs.find(key);
        return it == m_cbs.end() ? nullptr : it->second;
    }

    /**
     * @brief 清理所有的回调函数
     */
    void clearListener() {
        RWMutexType::WriteLock lock(m_mutex);
        m_cbs.clear();
    }
private:
    RWMutexType m_mutex;
    T m_val;        //参数的值
    std::map<uint64_t, on_change_cb> m_cbs;     //变更回调函数组, uint64_t key,要求唯一，一般可以用hash
};

/*
    Config: ConfigVar的管理类，负责托管全部的ConfigVar对象，单例模式。提供Lookup方法，用于根据配置名称查询配置项。
如果调用Lookup查询时同时提供了默认值和配置项的描述信息，那么在未找到对应的配置时，会自动创建一个对应的配置项，这样就保证
了配置模块定义即可用的特性。除此外，Config类还提供了LoadFromYaml和LoadFromConfDir两个方法，用于从YAML对象或从
命令行-c选项指定的配置文件路径中加载配置。
    Config的全部成员变量和方法都是static类型，保证了全局只有一个实例。
*/

/**
 * @brief ConfigVar的管理类
 * 提供全局的配置管理功能，支持动态获取和创建配置项。
 * @details 提供便捷的方法创建/访问ConfigVar
 */
class Config {
public:
    typedef std::unordered_map<std::string, ConfigVarBase::ptr> ConfigVarMap;
    typedef RWMutex RWMutexType;

    /**
     * @brief 获取/创建对应参数名的配置参数
     * @param[in] name 配置参数名称
     * @param[in] default_value 参数默认值
     * @param[in] description 参数描述
     * @details 获取参数名为name的配置参数,如果存在直接返回
     *          如果不存在,创建参数配置并用default_value赋值
     * @return 返回对应的配置参数,如果参数名存在但是类型不匹配则返回nullptr
     * @exception 如果参数名包含非法字符[^0-9a-z_.] 抛出异常 std::invalid_argument
     */
    template<class T>
    static typename ConfigVar<T>::ptr Lookup(const std::string& name, const T& default_value, const std::string& description = "") {
        RWMutexType::WriteLock lock(GetMutex());
        auto it = GetDatas().find(name);
        // 找到了名为name的配置参数
        if(it != GetDatas().end()) {
            auto tmp = std::dynamic_pointer_cast<ConfigVar<T> >(it->second);
            if(tmp) {
                SYLAR_LOG_INFO(SYLAR_LOG_ROOT()) << "Lookup name=" << name << " exists";
                return tmp;
            } 
            else {
                SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "Lookup name=" << name << " exists but type not "
                        << TypeToName<T>() << " real_type=" << it->second->getTypeName()
                        << " " << it->second->toString();
                return nullptr;
            }
        }
        //find_first_not_of用于在字符串中查找第一个与指定字符集合中任何字符都不匹配的字符位置
        if(name.find_first_not_of("abcdefghikjlmnopqrstuvwxyz._012345678")!= std::string::npos) {
            SYLAR_LOG_ERROR(SYLAR_LOG_ROOT()) << "Lookup name invalid " << name;
            throw std::invalid_argument(name);
        }

        // typename 用于强调ConfigVar<T>::ptr是一个类型名称
        typename ConfigVar<T>::ptr v(new ConfigVar<T>(name, default_value, description));
        GetDatas()[name] = v;
        return v;
    }

    /**
     * @brief 查找配置参数
     * @param[in] name 配置参数名称
     * @return 返回配置参数名为name的配置参数
     */
    template<class T>
    static typename ConfigVar<T>::ptr Lookup(const std::string& name) {
        RWMutexType::ReadLock lock(GetMutex());
        auto it = GetDatas().find(name);
        if(it == GetDatas().end()) {
            return nullptr;
        }
        return std::dynamic_pointer_cast<ConfigVar<T> >(it->second);
    }

    //使用YAML::Node初始化配置模块
    static void LoadFromYaml(const YAML::Node& root);

    //加载path文件夹里面的配置文件
    static void LoadFromConfDir(const std::string& path, bool force = false);

    /**
     * @brief 查找配置参数,返回配置参数的基类
     * @param[in] name 配置参数名称
     */
    static ConfigVarBase::ptr LookupBase(const std::string& name);

    /**
     * @brief 遍历配置模块里面所有配置项
     * @param[in] cb 配置项回调函数
     */
    static void Visit(std::function<void(ConfigVarBase::ptr)> cb);
private:

    //返回所有的配置项
    static ConfigVarMap& GetDatas() {
        static ConfigVarMap s_datas;
        return s_datas;
    }

    //配置项的RWMutex
    static RWMutexType& GetMutex() {
        static RWMutexType s_mutex;
        return s_mutex;
    }
};

}

#endif
