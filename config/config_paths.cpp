#include "config_paths.h"
#include "utils.h"
#include "logger.h"
#include <toml++/toml.hpp>
#include <fstream>
#include <windows.h>
#include <Shlobj.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace ConfigPaths {

const std::wstring& GetS3SSDirectory() {
    static std::wstring s3ssDir;
    if (s3ssDir.empty()) {
        wchar_t docPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docPath))) { s3ssDir = std::wstring(docPath) + L"\\Electronic Arts\\The Sims 3\\S3SS\\"; }
    }
    return s3ssDir;
}

std::string GetConfigPath() {
    return Utils::WideToUtf8(GetS3SSDirectory()) + "S3SS.toml";
}

std::string GetDefaultsPath() {
    return Utils::WideToUtf8(GetS3SSDirectory()) + "S3SS_defaults.toml";
}

std::string GetLogPath() {
    return Utils::WideToUtf8(GetS3SSDirectory()) + "S3SS_LOG.txt";
}

std::string GetLegacyINIPath() {
    return Utils::GetGameFilePath("S3SS.ini");
}

bool NeedsMigration() {
    return fs::exists(GetLegacyINIPath()) && !fs::exists(GetConfigPath());
}

bool EnsureDirectoryExists() {
    const std::wstring& dir = GetS3SSDirectory();
    if (dir.empty()) { return false; }

    std::error_code ec;
    fs::create_directories(dir, ec);
    return !ec && fs::exists(dir);
}

bool AtomicWriteToml(const std::string& destPath, const toml::table& root, std::string* error) {
    std::string tempPath = destPath + ".tmp";
    {
        std::ofstream out(tempPath, std::ios::binary);
        if (!out.is_open()) {
            std::string msg = "Failed to open temp file for writing: " + tempPath;
            LOG_ERROR("[ConfigPaths] " + msg);
            if (error) *error = msg;
            return false;
        }
        out << root;
    }

    std::wstring wideTempPath = Utils::Utf8ToWide(tempPath);
    std::wstring wideDestPath = Utils::Utf8ToWide(destPath);
    if (!MoveFileExW(wideTempPath.c_str(), wideDestPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ec;
        fs::rename(tempPath, destPath, ec);
        if (ec) {
            std::string msg = "Failed to rename temp file to " + destPath + ": " + ec.message();
            LOG_ERROR("[ConfigPaths] " + msg);
            if (error) *error = msg;
            return false;
        }
    }

    return true;
}

} // namespace ConfigPaths