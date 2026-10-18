#pragma once

#include <mutex>
#include <sstream>
#include <string_view>

namespace betacraft::core {

enum class LogLevel {
    Debug = 0,
    Info,
    Warn,
    Error
};

class Logger {
public:
    static void setMinLevel(LogLevel level) noexcept;
    static LogLevel getMinLevel() noexcept;

    static void log(LogLevel level, std::string_view subsystem, std::string_view message);

    template <typename... Args>
    static void debug(std::string_view subsystem, Args&&... args) {
        logFormatted(LogLevel::Debug, subsystem, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void info(std::string_view subsystem, Args&&... args) {
        logFormatted(LogLevel::Info, subsystem, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void warn(std::string_view subsystem, Args&&... args) {
        logFormatted(LogLevel::Warn, subsystem, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void error(std::string_view subsystem, Args&&... args) {
        logFormatted(LogLevel::Error, subsystem, std::forward<Args>(args)...);
    }

private:
    template <typename... Args>
    static void logFormatted(LogLevel level, std::string_view subsystem, Args&&... args) {
        if (level < getMinLevel()) {
            return;
        }
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        log(level, subsystem, oss.str());
    }

    static LogLevel s_minLevel;
    static std::mutex s_mutex;
};

} // namespace betacraft::core
