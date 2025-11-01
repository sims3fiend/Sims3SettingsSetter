#pragma once
#include <string>
#include <windows.h>
#include <vector>
#include <sstream>
#include <fstream>
#include <mutex>
#include <ctime>
#include <iomanip>

extern HMODULE GetDllModuleHandle();

namespace Utils {
    inline std::wstring Utf8ToWide(const std::string& str) {
        if (str.empty()) return std::wstring();
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstrTo[0], size_needed);
        return wstrTo;
    }

    inline std::string WideToUtf8(const std::wstring& wstr) {
        if (wstr.empty()) return std::string();
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &strTo[0], size_needed, nullptr, nullptr);
        return strTo;
    }

    inline std::vector<std::string> SplitString(const std::string& str, char delim) {
        std::vector<std::string> result;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, delim)) {
            result.push_back(item);
        }
        return result;
    }
    
    // Simple logger class for writing to a file
    class Logger {
    private:
        static Logger* s_instance;
        std::ofstream m_logFile;
        std::mutex m_mutex;
        bool m_enabled;
        
        Logger() : m_enabled(false) {}
        
    public:
        static Logger& Get() {
            if (!s_instance) {
                s_instance = new Logger();
            }
            return *s_instance;
        }
        
        bool Initialize(const std::string& filename) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_logFile.open(filename, std::ios::out | std::ios::trunc);
            m_enabled = m_logFile.is_open();
            
            if (m_enabled) {
                // Write header with timestamp
                auto t = std::time(nullptr);
                auto tm = *std::localtime(&t);
                m_logFile << "S3SS Log - Started at " 
                          << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << std::endl;
                m_logFile << "----------------------------------------" << std::endl;
            }
            
            return m_enabled;
        }
        
        void Log(const std::string& message) {
            if (!m_enabled) return;
            
            std::lock_guard<std::mutex> lock(m_mutex);
            m_logFile << message << std::endl;
            m_logFile.flush();
            
            // Note: OutputDebugString is now handled by ErrorHandler to avoid duplication
        }
        
        void Close() {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_logFile.is_open()) {
                m_logFile.close();
            }
            m_enabled = false;
        }
        
        ~Logger() {
            Close();
        }
    };
} 