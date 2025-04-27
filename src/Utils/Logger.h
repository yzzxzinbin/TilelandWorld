#pragma once
#ifndef TILELANDWORLD_LOGGER_H
#define TILELANDWORLD_LOGGER_H

#include <fstream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip> // For std::put_time

namespace TilelandWorld {

    enum class LogLevel {
        INFO,
        WARNING,
        ERROR
    };

    class Logger {
    public:
        // 获取 Logger 单例实例
        static Logger& getInstance();

        // 初始化日志文件 (应在程序开始时调用)
        bool initialize(const std::string& filename = "tileland.log");

        // 关闭日志文件 (应在程序结束时调用)
        void shutdown();

        // 记录日志消息
        void log(LogLevel level, const std::string& message);

        // 便捷方法
        void logInfo(const std::string& message);
        void logWarning(const std::string& message);
        void logError(const std::string& message);

        // 禁用拷贝和赋值
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

    private:
        Logger() = default; // 私有构造函数
        ~Logger() = default; // 私有析构函数 (通过 shutdown 管理)

        std::ofstream logFile;
        std::mutex logMutex;
        bool initialized = false;

        // 获取当前时间戳字符串
        std::string getCurrentTimestamp();
        // 获取日志级别字符串
        std::string levelToString(LogLevel level);
    };

    // 全局便捷访问宏 (可选，但方便)
    #define LOG_INFO(msg)    TilelandWorld::Logger::getInstance().logInfo(msg)
    #define LOG_WARNING(msg) TilelandWorld::Logger::getInstance().logWarning(msg)
    #define LOG_ERROR(msg)   TilelandWorld::Logger::getInstance().logError(msg)

} // namespace TilelandWorld

#endif // TILELANDWORLD_LOGGER_H
