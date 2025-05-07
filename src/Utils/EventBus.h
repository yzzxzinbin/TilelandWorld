#pragma once
#ifndef TILELANDWORLD_EVENTBUS_H
#define TILELANDWORLD_EVENTBUS_H

#include <unordered_map>
#include <functional>
#include <typeindex>
#include <vector>
#include <memory>
#include <mutex>
#include <algorithm>
#include <any>
#include <string>
#include <future>
#include <thread>
#include <queue>
#include <condition_variable>
#include <atomic>
#include "../Utils/Logger.h"

namespace TilelandWorld {

// 事件处理器基类
class EventHandlerBase {
public:
    virtual ~EventHandlerBase() = default;
    virtual std::string getEventName() const = 0;
};

// 具体事件处理器模板
template<typename EventType>
class EventHandler : public EventHandlerBase {
public:
    using HandlerFunction = std::function<void(const EventType&)>;  // 这是关键定义

    EventHandler(HandlerFunction handler, int priority = 0, std::string handlerName = "")
        : handler(std::move(handler)), priority(priority), handlerName(std::move(handlerName)) {}

    void handle(const EventType& event) const {
        handler(event);
    }

    int getPriority() const { return priority; }
    const std::string& getHandlerName() const { return handlerName; }
    
    std::string getEventName() const override {
        return typeid(EventType).name();
    }

private:
    HandlerFunction handler;  // 存储std::function对象
    int priority;
    std::string handlerName; // 用于调试和取消订阅
};

// 订阅令牌 - 用于跟踪和取消订阅
class SubscriptionToken {
public:
    SubscriptionToken() : eventType(typeid(void)), valid(false) {}
    SubscriptionToken(std::type_index eventType, size_t handlerIndex) 
        : eventType(eventType), handlerIndex(handlerIndex), valid(true) {}
    
    // 允许检查令牌是否有效
    bool isValid() const { return valid; }
    
    // 取消令牌有效性
    void invalidate() { valid = false; }
    
    // 获取事件类型和处理器索引
    std::type_index getEventType() const { return eventType; }
    size_t getHandlerIndex() const { return handlerIndex; }

private:
    std::type_index eventType;
    size_t handlerIndex;
    bool valid;
};

// 线程池类
class ThreadPool {
public:
    ThreadPool(size_t numThreads = std::thread::hardware_concurrency()) : stop(false) {
        for(size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this] {
                while(true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queueMutex);
                        condition.wait(lock, [this] { 
                            return stop || !tasks.empty(); 
                        });
                        
                        if(stop && tasks.empty()) {
                            return;
                        }
                        
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    
                    try {
                        task();
                    } catch(const std::exception& e) {
                        LOG_ERROR("异步任务执行异常: " + std::string(e.what()));
                    } catch(...) {
                        LOG_ERROR("异步任务发生未知异常");
                    }
                }
            });
        }
        LOG_INFO("线程池已初始化，线程数: " + std::to_string(numThreads));
    }
    
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        using ReturnType = decltype(f(args...));
        
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<ReturnType> result = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            if(stop) {
                throw std::runtime_error("不能在停止的线程池中添加任务");
            }
            
            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return result;
    }
    
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stop = true;
        }
        condition.notify_all();
        for(auto &worker : workers) {
            if(worker.joinable()) {
                worker.join();
            }
        }
        LOG_INFO("线程池已关闭");
    }
    
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> stop;
};

// 事件总线
class EventBus {
public:
    // 获取单例实例
    static EventBus& getInstance() {
        static EventBus instance;
        return instance;
    }

    // 订阅事件
    template<typename EventType>
    SubscriptionToken subscribe(
            std::function<void(const EventType&)> handler, 
            int priority = 0,
            const std::string& handlerName = "") {
        
        auto eventType = std::type_index(typeid(EventType));
        std::lock_guard<std::mutex> lock(mutex);
        
        auto& handlers = eventHandlers[eventType];
        
        auto eventHandler = std::make_shared<EventHandler<EventType>>(
            std::move(handler), priority, handlerName);
            
        size_t handlerIndex = handlers.size();
        handlers.push_back(std::static_pointer_cast<EventHandlerBase>(eventHandler));
        
        // 按优先级排序（高优先级先处理）
        std::sort(handlers.begin(), handlers.end(), 
            [](const std::shared_ptr<EventHandlerBase>& a, 
               const std::shared_ptr<EventHandlerBase>& b) {
                auto ah = std::dynamic_pointer_cast<EventHandler<EventType>>(a);
                auto bh = std::dynamic_pointer_cast<EventHandler<EventType>>(b);
                
                if (ah && bh) {
                    return ah->getPriority() > bh->getPriority();
                }
                return false;
            });
            
        // 更新索引以反映排序后的位置
        for (size_t i = 0; i < handlers.size(); ++i) {
            auto handler = std::dynamic_pointer_cast<EventHandler<EventType>>(handlers[i]);
            if (handler && handler->getHandlerName() == handlerName) {
                handlerIndex = i;
                break;
            }
        }
        
        LOG_INFO("Subscribed to event " + std::string(typeid(EventType).name()) + 
                 " with handler " + handlerName);
        
        return SubscriptionToken(eventType, handlerIndex);
    }

    // 取消订阅
    bool unsubscribe(SubscriptionToken& token) {
        if (!token.isValid()) {
            return false;
        }
        
        std::lock_guard<std::mutex> lock(mutex);
        
        auto it = eventHandlers.find(token.getEventType());
        if (it == eventHandlers.end()) {
            return false;
        }
        
        auto& handlers = it->second;
        size_t index = token.getHandlerIndex();
        
        if (index >= handlers.size()) {
            return false;
        }
        
        // 获取处理器名称用于日志
        std::string eventName = handlers[index]->getEventName();
        
        handlers.erase(handlers.begin() + index);
        token.invalidate();
        
        LOG_INFO("Unsubscribed from event " + eventName);
        
        return true;
    }

    // 同步发布事件
    template<typename EventType>
    void publish(const EventType& event) {
        publishInternal(event, false);
    }
    
    // 异步发布事件
    template<typename EventType>
    std::vector<std::future<void>> publishAsync(const EventType& event) {
        return publishInternal(event, true);
    }

    // 清除所有事件处理器
    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        eventHandlers.clear();
        LOG_INFO("All event handlers cleared");
    }
    
    // 配置线程池大小
    void configureThreadPool(size_t numThreads) {
        std::lock_guard<std::mutex> lock(mutex);
        threadPool = std::make_unique<ThreadPool>(numThreads);
        LOG_INFO("事件总线线程池已重新配置，线程数: " + std::to_string(numThreads));
    }
    
    // 显式清理资源（用于程序结束前主动清理）
    void cleanup() {
        std::lock_guard<std::mutex> lock(mutex);
        eventHandlers.clear();
        if (threadPool) {
            threadPool.reset(); // 显式销毁线程池
        }
        LOG_INFO("EventBus资源已显式清理");
    }

private:
    EventBus() : threadPool(std::make_unique<ThreadPool>()) {}
    ~EventBus() = default;
    
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    std::unordered_map<std::type_index, std::vector<std::shared_ptr<EventHandlerBase>>> eventHandlers;
    std::mutex mutex;
    std::unique_ptr<ThreadPool> threadPool;
    
    // 内部发布事件方法，支持同步和异步
    template<typename EventType>
    std::vector<std::future<void>> publishInternal(const EventType& event, bool async) {
        std::vector<std::shared_ptr<EventHandlerBase>> handlersCopy;
        std::vector<std::future<void>> futures;
        
        {
            std::lock_guard<std::mutex> lock(mutex); // 线程安全的关键
            auto it = eventHandlers.find(std::type_index(typeid(EventType)));
            if (it == eventHandlers.end()) {
                return futures; // 没有处理器，返回空的future集合
            }
            
            // 创建处理器的副本以避免在调用处理函数时持有锁
            handlersCopy = it->second;
        }
        
        for (auto& baseHandler : handlersCopy) {
            auto handler = std::dynamic_pointer_cast<EventHandler<EventType>>(baseHandler);
            if (!handler) continue;
            
            if (async) {
                // 异步调度处理器
                auto future = threadPool->enqueue([handler, event]() {
                    try {
                        handler->handle(event);
                    }
                    catch (const std::exception& e) {
                        LOG_ERROR("异步事件处理器异常: " + std::string(e.what()));
                    }
                    catch (...) {
                        LOG_ERROR("异步事件处理器发生未知异常");
                    }
                });
                futures.push_back(std::move(future));
            } else {
                // 同步调用处理器
                try {
                    handler->handle(event);
                }
                catch (const std::exception& e) {
                    LOG_ERROR("事件处理器异常: " + std::string(e.what()));
                }
                catch (...) {
                    LOG_ERROR("事件处理器发生未知异常");
                }
            }
        }
        
        return futures;
    }
};

// 便捷宏
#define EVENT_BUS TilelandWorld::EventBus::getInstance()

} // namespace TilelandWorld

#endif // TILELANDWORLD_EVENTBUS_H