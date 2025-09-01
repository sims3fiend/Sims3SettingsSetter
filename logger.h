#pragma once
#include <string>
#include <windows.h>
#include <format>
#include <source_location>
#include <fstream>
#include <mutex>
#include <ctime>
#include <iomanip>

namespace Logger {
    enum class Level {
        Debug,      // Diagnostic info - outputs to OutputDebugStringA so we can tell people to use that without cluttering the file
        Info,       // Normal operation info - logged to file
        Warning,    // Recoverable issues - logged to file
        Error,      // Errors that need attention - logged to file + debug output
        Critical    // Critical errors - always logged + debug output
    };

    class Handler {
    private:
        static bool s_debugMode;
        static bool s_fileLoggingEnabled;
        static std::ofstream s_logFile;
        static std::mutex s_mutex;
        static bool s_initialized;
        
    public:
        // Initialize the logger with a file
        static bool Initialize(const std::string& filename);
        
        // Close the log file
        static void Close();
        
        static void SetDebugMode(bool enable) { s_debugMode = enable; }
        static void SetFileLogging(bool enable) { s_fileLoggingEnabled = enable; }
        
        // Main logging function with source location
        static void Log(Level level, const std::string& message, 
                       const std::source_location& loc = std::source_location::current());
        
        // Convenience functions
        static void Debug(const std::string& msg, 
                         const std::source_location& loc = std::source_location::current()) {
            Log(Level::Debug, msg, loc);
        }
        
        static void Info(const std::string& msg,
                        const std::source_location& loc = std::source_location::current()) {
            Log(Level::Info, msg, loc);
        }
        
        static void Warning(const std::string& msg,
                           const std::source_location& loc = std::source_location::current()) {
            Log(Level::Warning, msg, loc);
        }
        
        static void Error(const std::string& msg,
                         const std::source_location& loc = std::source_location::current()) {
            Log(Level::Error, msg, loc);
        }
        
        static void Critical(const std::string& msg,
                            const std::source_location& loc = std::source_location::current()) {
            Log(Level::Critical, msg, loc);
        }
        
        // SEH safe memory check
        template<typename T>
        static bool IsSafeToRead(void* ptr, size_t size = sizeof(T)) {
            if (!ptr) return false;
            __try {
                volatile char dummy;
                for (size_t i = 0; i < size; i++) {
                    dummy = static_cast<char*>(ptr)[i];
                }
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }
        
        // Safe exception wrapper for operations
        template<typename Func>
        static bool SafeExecute(Func func, const std::string& operation) {
            try {
                func();
                return true;
            }
            catch (const std::exception& e) {
                Error(std::format("{} failed: {}", operation, e.what()));
                return false;
            }
            catch (...) {
                Error(std::format("{} failed: unknown exception", operation));
                return false;
            }
        }
    };

    // Macros for convenient logging with automatic location
    #ifdef _DEBUG
        #define LOG_DEBUG(msg) Logger::Handler::Debug(msg)
    #else
        #define LOG_DEBUG(msg) Logger::Handler::Debug(msg)  // Keep in release for troubleshooting
    #endif

    #define LOG_INFO(msg) Logger::Handler::Info(msg)
    #define LOG_WARNING(msg) Logger::Handler::Warning(msg)
    #define LOG_ERROR(msg) Logger::Handler::Error(msg)
    #define LOG_CRITICAL(msg) Logger::Handler::Critical(msg)

    // Safe execution macro
    #define SAFE_EXECUTE(func, op_name) Logger::Handler::SafeExecute([&](){ func; }, op_name)
}