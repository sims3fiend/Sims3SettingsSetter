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

// Resolve the localized game folder name (e.g. "Die Sims 3" tehehe)
// Scan the exe's string resources, we do this the same way the game does internally
// String table is organized in blocks of 100 per locale: base+0 = locale code (e.g. "fr-fr"),
// base+1 = display name with TM, base+2 = folder name
static std::wstring ResolveLocalizedGameFolder() {
    HMODULE exe = GetModuleHandle(NULL);
    wchar_t sysLocale[LOCALE_NAME_MAX_LENGTH];
    GetUserDefaultLocaleName(sysLocale, LOCALE_NAME_MAX_LENGTH);
    _wcslwr_s(sysLocale);

    // Extract language prefix (e.g. "en" from "en-au") for fallback matching, :)))))
    std::wstring sysLang(sysLocale);
    auto hyphen = sysLang.find(L'-');
    if (hyphen != std::wstring::npos) sysLang.resize(hyphen);

    wchar_t blockLocale[16];
    wchar_t gameName[MAX_PATH];
    UINT prefixMatch = 0;

    for (UINT base = 1000; base < 3600; base += 100) {
        if (LoadStringW(exe, base, blockLocale, 16) > 0) {
            _wcslwr_s(blockLocale);
            if (wcscmp(sysLocale, blockLocale) == 0 &&
                LoadStringW(exe, base + 2, gameName, MAX_PATH) > 0) {
                return gameName;
            }
            // Track first language-prefix match as fallback (e.g. "en-au" → "en-us")
            if (prefixMatch == 0 && wcsncmp(sysLang.c_str(), blockLocale, sysLang.size()) == 0) {
                prefixMatch = base;
            }
        }
    }
    if (prefixMatch != 0 && LoadStringW(exe, prefixMatch + 2, gameName, MAX_PATH) > 0) {
        return gameName;
    }
    //If this every happens I swear to GODDDDDDD
    return L"The Sims 3";
}

const std::wstring& GetS3SSDirectory() {
    static std::wstring s3ssDir;
    if (s3ssDir.empty()) {
        wchar_t docPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docPath))) {
            s3ssDir = std::wstring(docPath) + L"\\Electronic Arts\\"
                    + ResolveLocalizedGameFolder() + L"\\S3SS\\";
        }
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