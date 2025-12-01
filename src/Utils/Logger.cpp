#include "Logger.h"
#include <iostream> // For potential errors during init/shutdown

namespace TilelandWorld {

    Logger& Logger::getInstance() {
        // C++11 保证静态局部变量的初始化是线程安全的
        static Logger instance;
        return instance;
    }

    bool Logger::initialize(const std::string& filename) {
        std::lock_guard<std::mutex> lock(logMutex);
        if (initialized) {
            // 允许重新初始化，先关闭旧的
            if (logFile.is_open()) {
                logFile.close();
            }
        }
        logFile.open(filename, std::ios::app); // 追加模式打开
        if (!logFile.is_open()) {
            std::cerr << "Error: Failed to open log file: " << filename << std::endl;
            initialized = false;
            return false;
        }
        initialized = true;
        // 写入一条初始化消息，但不通过 log() 函数以避免死锁
        logFile << "[" << getCurrentTimestamp() << "] [" << levelToString(LogLevel::LOG_INFO) << "] " << "Logger initialized. Log file: " + filename << std::endl;
        return true;
    }

    void Logger::shutdown() {
        std::lock_guard<std::mutex> lock(logMutex);
        if (initialized && logFile.is_open()) {
            // 写入一条关闭消息，但不通过 log() 函数以避免死锁
            logFile << "[" << getCurrentTimestamp() << "] [" << levelToString(LogLevel::LOG_INFO) << "] " << "Logger shutting down." << std::endl;
            logFile.close();
            initialized = false;
        }
    }

    std::string Logger::getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        // 使用 std::put_time (需要 <iomanip>)
        ss << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    std::string Logger::levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::LOG_INFO:    return "INFO";
            case LogLevel::LOG_WARNING: return "WARN"; // 使用 WARN 缩写
            case LogLevel::LOG_ERROR:   return "ERROR";
            default:                return "?????";
        }
    }

    void Logger::log(LogLevel level, const std::string& message) {
        std::lock_guard<std::mutex> lock(logMutex);
        if (!initialized || !logFile.is_open()) {
            // 如果未初始化或文件未打开，尝试输出到 cerr
            std::cerr << "[" << getCurrentTimestamp() << "] [" << levelToString(level) << "] " << message << " (Logger not initialized!)" << std::endl;
            return;
        }
        logFile << "[" << getCurrentTimestamp() << "] [" << levelToString(level) << "] " << message << std::endl;
    }

    void Logger::logInfo(const std::string& message) {
        log(LogLevel::LOG_INFO, message);
    }

    void Logger::logWarning(const std::string& message) {
        log(LogLevel::LOG_WARNING, message);
    }

    void Logger::logError(const std::string& message) {
        log(LogLevel::LOG_ERROR, message);
    }

} // namespace TilelandWorld
