#pragma once

#include <iostream>
#include <sstream>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace OrchFaust {

class Logger {
public:
    enum class Level {
        Info,
        Warning,
        Error
    };

    static void log(Level level, const std::string& message) {
        std::lock_guard<std::mutex> lock(getMutex());
        
        std::string prefix;
        switch (level) {
            case Level::Info:    prefix = "[INFO]  "; break;
            case Level::Warning: prefix = "[WARN]  "; break;
            case Level::Error:   prefix = "[ERROR] "; break;
        }

        std::string formatted = "[OrchFaust] " + prefix + message + "\n";
        
        // Output to console/stdout
        std::cout << formatted;
        std::cout.flush();

#ifdef _WIN32
        // Output to debugger console in Visual Studio or Host debugger
        OutputDebugStringA(formatted.c_str());
#endif
    }

    template<typename... Args>
    static void logInfo(Args&&... args) {
        log(Level::Info, buildString(std::forward<Args>(args)...));
    }

    template<typename... Args>
    static void logWarning(Args&&... args) {
        log(Level::Warning, buildString(std::forward<Args>(args)...));
    }

    template<typename... Args>
    static void logError(Args&&... args) {
        log(Level::Error, buildString(std::forward<Args>(args)...));
    }

private:
    static std::mutex& getMutex() {
        static std::mutex mtx;
        return mtx;
    }

    template<typename T, typename... Args>
    static std::string buildString(T&& first, Args&&... rest) {
        std::ostringstream oss;
        oss << first;
        if constexpr (sizeof...(rest) > 0) {
            oss << buildString(std::forward<Args>(rest)...);
        }
        return oss.str();
    }

    static std::string buildString() {
        return "";
    }
};

} // namespace OrchFaust
