#pragma once
#include <mutex>
#include <string>

// Central coordinator for all config I/O.
// Builds a single TOML table from all subsystems, writes/reads atomically.
class ConfigStore {
  public:
    static ConfigStore& Get();

    // Primary save/load (coordinates all subsystems)
    bool SaveAll(std::string* error = nullptr);
    bool LoadAll(std::string* error = nullptr);
    bool LoadPatches(std::string* error = nullptr);

    // Default values
    bool SaveDefaults(std::string* error = nullptr);
    bool LoadDefaults(std::string* error = nullptr);

  private:
    ConfigStore() = default;
    std::mutex m_mutex;
};
