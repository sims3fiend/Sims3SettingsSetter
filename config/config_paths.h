#pragma once
#include <string>

namespace toml {
inline namespace v3 {
class table;
}
} // namespace toml

namespace ConfigPaths {
// Get the S3SS config directory: Documents\Electronic Arts\The Sims 3\S3SS\
    // Result is cached after first call.
const std::wstring& GetS3SSDirectory();

// File paths in the new Documents location
std::string GetConfigPath();   // S3SS.toml
std::string GetDefaultsPath(); // S3SS_defaults.toml
std::string GetLogPath();      // S3SS_LOG.txt

// Game's Bin directory (for migration detection)
std::string GetLegacyINIPath();

// Check if legacy INI exists and new TOML does not (migration needed)
bool NeedsMigration();

// Create the S3SS directory chain if it doesn't exist, returns true if the directory exists (or was created).
bool EnsureDirectoryExists();

// Atomically write a TOML table to destPath (temp file + rename), returns true on success; on failure, sets *error if non-null.
bool AtomicWriteToml(const std::string& destPath, const toml::table& root, std::string* error = nullptr);
} // namespace ConfigPaths
