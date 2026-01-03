#include "Logger.h"
#include <iostream>
#include <filesystem>
#include <sstream>
#include <ctime>
#include <iomanip>

namespace {

    // Generate a unique backup filename based on timestamp and optional numeric suffix.
    static std::string makeBackupName(const std::string& filename) {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
    #ifdef _WIN32
        localtime_s(&tm, &t);
    #else
        localtime_r(&t, &tm);
    #endif
        std::ostringstream ss;
        ss << std::put_time(&tm, "%Y%m%d_%H%M%S");
        std::string base = filename + "." + ss.str() + ".old";
        std::string candidate = base;
        int i = 0;
        while (std::filesystem::exists(candidate)) {
            ++i;
            candidate = base + "." + std::to_string(i);
        }
        return candidate;
    }

} // anonymous namespace

namespace TilelandWorld {

    Logger& Logger::getInstance() {
        // C++11 保证静态局部变量的初始化是线程安全的
        static Logger instance;
        return instance;
    }

    bool Logger::initialize(const std::string& filename, size_t maxFileSize) {
        std::lock_guard<std::mutex> lock(logMutex);
        currentFilename = filename;
        maxFileSizeLimit = maxFileSize;

        if (initialized) {
            // 允许重新初始化，先关闭旧的
            if (logFile.is_open()) {
                logFile.close();
            }
        }

        // 检查是否需要轮转
        if (std::filesystem::exists(filename)) {
            if (std::filesystem::file_size(filename) >= maxFileSizeLimit) {
                std::string backup = makeBackupName(filename);
                std::error_code ec;
                std::filesystem::rename(filename, backup, ec);
                if (ec) {
                    std::cerr << "Warning: Failed to rotate log '" << filename << "' to '" << backup << "': " << ec.message() << std::endl;
                }
            }
        }

        logFile.open(filename, std::ios::app);
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

    void Logger::checkRotation() {
        // 注意：此函数应在持有 logMutex 时调用
        if (!initialized || !logFile.is_open()) return;

        if (std::filesystem::file_size(currentFilename) >= maxFileSizeLimit) {
            logFile.close();
            std::string backup = makeBackupName(currentFilename);
            std::error_code ec;
            std::filesystem::rename(currentFilename, backup, ec);
            if (ec) {
                std::cerr << "Warning: Failed to rotate log '" << currentFilename << "' to '" << backup << "': " << ec.message() << std::endl;
            }
            logFile.open(currentFilename, std::ios::app);
        }
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

    void Logger::setLogLevel(LogLevel level) {
        LogLevel old = minLevel;
        minLevel = level;
        logRaw("--- Log level changed from " + levelToString(old) + " to " + levelToString(level) + " ---");
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
            case LogLevel::LOG_DEBUG:   return "DEBUG";
            case LogLevel::LOG_INFO:    return "INFO";
            case LogLevel::LOG_WARNING: return "WARN";
            case LogLevel::LOG_ERROR:   return "ERROR";
            case LogLevel::LOG_NONE:    return "NONE";
            default:                    return "?????";
        }
    }

    void Logger::log(LogLevel level, const std::string& message) {
        if (level < minLevel) return;

        std::lock_guard<std::mutex> lock(logMutex);
        if (!initialized || !logFile.is_open()) {
            // 如果未初始化或文件未打开，尝试输出到 cerr
            std::cerr << "[" << getCurrentTimestamp() << "] [" << levelToString(level) << "] " << message << " (Logger not initialized!)" << std::endl;
            return;
        }
        
        checkRotation();
        logFile << "[" << getCurrentTimestamp() << "] [" << levelToString(level) << "] " << message << std::endl;
    }

    void Logger::logRaw(const std::string& message) {
        std::lock_guard<std::mutex> lock(logMutex);
        if (!initialized || !logFile.is_open()) {
            std::cerr << message << " (Logger not initialized!)" << std::endl;
            return;
        }
        checkRotation();
        logFile << message << std::endl;
    }

    void Logger::logDebug(const std::string& message) {
        log(LogLevel::LOG_DEBUG, message);
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
