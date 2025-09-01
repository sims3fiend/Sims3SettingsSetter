#include "logger.h"

namespace Logger {
    bool Handler::s_debugMode = false;
    bool Handler::s_fileLoggingEnabled = true;
    std::ofstream Handler::s_logFile;
    std::mutex Handler::s_mutex;
    bool Handler::s_initialized = false;

    bool Handler::Initialize(const std::string& filename) {
        std::lock_guard<std::mutex> lock(s_mutex);
        
        if (s_initialized) {
            return true; // Already initialized
        }
        
        s_logFile.open(filename, std::ios::out | std::ios::trunc);
        if (!s_logFile.is_open()) {
            return false;
        }
        
        // Write header with timestamp
        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);
        s_logFile << "S3SS Log - Started at " 
                  << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << std::endl;
        s_logFile << "----------------------------------------" << std::endl;
        
        s_initialized = true;
        return true;
    }
    
    void Handler::Close() {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_logFile.is_open()) {
            s_logFile.close();
        }
        s_initialized = false;
    }

    void Handler::Log(Level level, const std::string& message, const std::source_location& loc) {
        // Format the message with context
        std::string levelStr;
        switch (level) {
            case Level::Debug:   levelStr = "DEBUG"; break;
            case Level::Info:    levelStr = "INFO"; break;
            case Level::Warning: levelStr = "WARN"; break;
            case Level::Error:   levelStr = "ERROR"; break;
            case Level::Critical: levelStr = "CRITICAL"; break;
        }

        // Build the log message
        std::string logMsg;
        
        // For debug level, include file and line info
        if (level == Level::Debug || level == Level::Error || level == Level::Critical) {
            // Extract just the filename from the path
            std::string filename(loc.file_name());
            size_t lastSlash = filename.find_last_of("/\\");
            if (lastSlash != std::string::npos) {
                filename = filename.substr(lastSlash + 1);
            }
            
            logMsg = std::format("[{}] {} ({}:{})", 
                levelStr, message, filename, loc.line());
        } else {
            logMsg = std::format("[{}] {}", levelStr, message);
        }

        // Log to file for Info and above (if enabled)
        if (s_fileLoggingEnabled && level >= Level::Info && s_initialized) {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (s_logFile.is_open()) {
                s_logFile << logMsg << std::endl;
                s_logFile.flush();
            }
        }
        
        // Always output to debug console for troubleshooting
        OutputDebugStringA((logMsg + "\n").c_str());
    }
}