#include "timer.h"
#include "util.h"
#include"log.h"
namespace sylar {

bool Timer::Comparator::operator()(const Timer::ptr& lhs, const Timer::ptr& rhs) const {
    if(!lhs && !rhs) {
        return false;
    }
    if(!lhs) {
        return true;
    }
    if(!rhs) {
        return false;
    }
    if(lhs->m_next < rhs->m_next) {
        return true;
    }
    if(rhs->m_next < lhs->m_next) {
        return false;
    }
    return lhs.get() < rhs.get();
}


Timer::Timer(uint64_t ms, std::function<void()> cb, bool recurring, TimerManager* manager)
    :m_recurring(recurring)
    ,m_ms(ms)
    ,m_cb(cb)
    ,m_manager(manager) {
    
    // 执行时间为当前时间+执行周期
    m_next = sylar::GetCurrentMS() + m_ms;
}

Timer::Timer(uint64_t next)
    :m_next(next) {
}


bool Timer::cancel() {
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if(m_cb) {
        m_cb = nullptr;
        // 在set中找到自身定时器并进行删除
        auto it = m_manager->m_timers.find(shared_from_this());
        m_manager->m_timers.erase(it);
        return true;
    }
    return false;
}

bool Timer::refresh() {
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if(!m_cb) {
        return false;
    }
    auto it = m_manager->m_timers.find(shared_from_this());
    if(it == m_manager->m_timers.end()) {
        return false;
    }
    m_manager->m_timers.erase(it);
    // 更新执行时间
    m_next = sylar::GetCurrentMS() + m_ms;
    // 将自己重新插入，这样能按最新的时间排序
    m_manager->m_timers.insert(shared_from_this());
    return true;
}

bool Timer::reset(uint64_t ms, bool from_now) {
    // 若周期相同，且不按当前时间计算
    if(ms == m_ms && !from_now) {
        return true;
    }
    TimerManager::RWMutexType::WriteLock lock(m_manager->m_mutex);
    if(!m_cb) {
        return false;
    }

    // 在set中找到自身定时器
    auto it = m_manager->m_timers.find(shared_from_this());
    if(it == m_manager->m_timers.end()) {
        return false;
    }

    // 删除定时器
    m_manager->m_timers.erase(it);
    // 起始时间
    uint64_t start = 0;
    if(from_now) {
        // 更新起始时间
        start = sylar::GetCurrentMS();
    } 
    else {
        // 起始时间为当时创建时的起始时间
        start = m_next - m_ms;
    }
    m_ms = ms;
    m_next = start + m_ms;
    // 重新加入set中
    m_manager->addTimer(shared_from_this(), lock);
    return true;

}

TimerManager::TimerManager() {
    m_previouseTime = sylar::GetCurrentMS();
}

TimerManager::~TimerManager() {
}

Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb,bool recurring) {
    // 创建定时器
    Timer::ptr timer(new Timer(ms, cb, recurring, this));
    RWMutexType::WriteLock lock(m_mutex);
    addTimer(timer, lock);
    return timer;
}

static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb) {
    // 它首先使用weak_cond的lock函数获取一个shared_ptr指针tmp
    std::shared_ptr<void> tmp = weak_cond.lock();
    // 如果tmp不为空，则调用回调函数cb。
    if(tmp) {
        cb();
    }
}

Timer::ptr TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb,std::weak_ptr<void> weak_cond ,bool recurring) {                                
    // 在定时器触发时会调用 OnTimer 函数，并在OnTimer函数中判断条件对象是否存在，如果存在则调用回调函数cb。
    return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring);
}

uint64_t TimerManager::getNextTimer() {
    RWMutexType::ReadLock lock(m_mutex);
    
    // 不触发 onTimerInsertedAtFront
    m_tickled = false;

    // 如果没有定时器，返回一个最大值
    if(m_timers.empty()) {
        return ~0ull;
    }

    // 拿到第一个定时器
    const Timer::ptr& next = *m_timers.begin();

    // 现在的时间
    uint64_t now_ms = sylar::GetCurrentMS();
    // 如果当前时间 >= 该定时器的执行时间，说明该定时器已经超时了，该执行了
    if(now_ms >= next->m_next) {
        return 0;
    } 
    else {
        // 还没超时，返回还要多久执行
        return next->m_next - now_ms;
    }
}

void TimerManager::listExpiredCb(std::vector<std::function<void()> >& cbs) {
    // 获得当前时间
    uint64_t now_ms = sylar::GetCurrentMS();
    std::vector<Timer::ptr> expired;
    {
        RWMutexType::ReadLock lock(m_mutex);
        // 没有定时器
        if(m_timers.empty()) {
            return;
        }
    }
    RWMutexType::WriteLock lock(m_mutex);
    if(m_timers.empty()) {
        return;
    }

    // 判断服务器时间是否调后了
    bool rollover = detectClockRollover(now_ms);

    // 如果服务器时间没问题，并且第一个定时器都没有到执行时间，就说明没有任务需要执行
    if(!rollover && ((*m_timers.begin())->m_next > now_ms)) {
        return;
    }

    // 定义一个当前时间的定时器
    Timer::ptr now_timer(new Timer(now_ms));

    /* 若系统时间改动则将m_timers的所有Timer视为过期的
     * 否则返回第一个 >= now_ms的迭代器，在此迭代器之前的定时器全都已经超时 
     * lower_bound用于在已排序的序列（比如数组、vector等容器）中查找第一个大于或等于给定值的元素的位置。*/
    auto it = rollover ? m_timers.end() : m_timers.lower_bound(now_timer);

    // 筛选出当前时间等于next时间执行的Timer
    while(it != m_timers.end() && (*it)->m_next == now_ms) {
        ++it;
    }

    // 小于当前时间的Timer都是已经过了时间的Timer，it是当前时间要执行的Timer
    // 将这些Timer都加入到expired中
    expired.insert(expired.begin(), m_timers.begin(), it);
    // 将已经放入expired的定时器删掉 
    m_timers.erase(m_timers.begin(), it);

    cbs.reserve(expired.size());

    // 将expired的timer的回调函数放入到cbs中
    for(auto& timer : expired) {
        cbs.push_back(timer->m_cb);
        if(timer->m_recurring) {
            timer->m_next = now_ms + timer->m_ms;
            m_timers.insert(timer);
        } else {
            timer->m_cb = nullptr;
        }
    }
}

void TimerManager::addTimer(Timer::ptr val, RWMutexType::WriteLock& lock) {
    // 添加到set中并且拿到迭代器
    auto it = m_timers.insert(val).first;

    // 如果该定时器是超时时间最短 并且没有设置触发onTimerInsertedAtFront
    bool at_front = (it == m_timers.begin()) && !m_tickled;

    // 设置触发onTimerInsertedAtFront
    if(at_front) {
        m_tickled = true;
    }
    lock.unlock();

    // 触发onTimerInsertedAtFront()
	// onTimerInsertedAtFront()在IOManager中就是做了一次tickle()的操作 
    if(at_front) {
        onTimerInsertedAtFront();
    }
}

bool TimerManager::detectClockRollover(uint64_t now_ms) {
    bool rollover = false;
    // 如果当前时间比上次执行时间还小 并且 小于一个小时的时间，相当于时间倒流了
    if(now_ms < m_previouseTime && now_ms < (m_previouseTime - 60 * 60 * 1000)) {
        rollover = true;
    }
    m_previouseTime = now_ms;
    return rollover;
}

bool TimerManager::hasTimer() {
    RWMutexType::ReadLock lock(m_mutex);
    return !m_timers.empty();
}

}
