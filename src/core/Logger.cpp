#include "core/Logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>

namespace betacraft::core {

LogLevel Logger::s_minLevel = LogLevel::Info;
std::mutex Logger::s_mutex;

void Logger::setMinLevel(LogLevel level) noexcept {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_minLevel = level;
}

LogLevel Logger::getMinLevel() noexcept {
    return s_minLevel;
}

void Logger::log(LogLevel level, std::string_view subsystem, std::string_view message) {
    if (level < s_minLevel) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const auto timeT = std::chrono::system_clock::to_time_t(now);
    std::tm localTm{};
#if defined(_WIN32)
    localtime_s(&localTm, &timeT);
#else
    localtime_r(&timeT, &localTm);
#endif

    const char* levelStr = "INFO ";
    switch (level) {
        case LogLevel::Debug: levelStr = "DEBUG"; break;
        case LogLevel::Info:  levelStr = "INFO "; break;
        case LogLevel::Warn:  levelStr = "WARN "; break;
        case LogLevel::Error: levelStr = "ERROR"; break;
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    std::ostream& out = (level == LogLevel::Error) ? std::cerr : std::cout;
    out << '[' << std::put_time(&localTm, "%H:%M:%S") << "] "
        << '[' << levelStr << "] "
        << '[' << subsystem << "] "
        << message << '\n';
}

} // namespace betacraft::core
