#include "config_store.h"
#include "config_paths.h"
#include "settings.h"
#include "config_value_manager.h"
#include "optimization.h"
#include "qol.h"
#include "logger.h"
#include "utils.h"
#include <toml++/toml.hpp>
#include <filesystem>

namespace fs = std::filesystem;

static constexpr int CONFIG_VERSION = 1;

ConfigStore& ConfigStore::Get() {
    static ConfigStore instance;
    return instance;
}

bool ConfigStore::SaveAll(std::string* error) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        std::string configPath = ConfigPaths::GetConfigPath();

        if (!ConfigPaths::EnsureDirectoryExists()) {
            std::string msg = "Failed to create config directory";
            LOG_ERROR("[ConfigStore] " + msg);
            if (error) *error = msg;
            return false;
        }

        toml::table root;

        // Meta section
        toml::table meta;
        meta.insert("version", CONFIG_VERSION);
        root.insert("meta", std::move(meta));

        // Collect from all subsystems
        SettingsManager::Get().SaveToToml(root);
        ConfigValueManager::Get().SaveToToml(root);
        OptimizationManager::Get().SaveToToml(root);

        // QoL: build a shared qol table, each subsystem adds its own sub-table
        toml::table qolTable;
        UISettings::Get().SaveToToml(qolTable);
        MemoryMonitor::Get().SaveToToml(qolTable);
        BorderlessWindow::Get().SaveToToml(qolTable);
        if (!qolTable.empty()) { root.insert("qol", std::move(qolTable)); }

        if (!ConfigPaths::AtomicWriteToml(configPath, root, error)) { return false; }

        LOG_DEBUG("[ConfigStore] Saved config to " + configPath);
        return true;
    } catch (const std::exception& e) {
        std::string msg = std::string("Exception saving config: ") + e.what();
        LOG_ERROR("[ConfigStore] " + msg);
        if (error) *error = msg;
        return false;
    }
}

bool ConfigStore::LoadAll(std::string* error) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        std::string configPath = ConfigPaths::GetConfigPath();

        if (!fs::exists(Utils::ToPath(configPath))) {
            LOG_DEBUG("[ConfigStore] No config file found at " + configPath + " (fresh install)");
            return true; // Not an error, just no config yet
        }

        toml::table root = toml::parse_file(Utils::Utf8ToWide(configPath));

        // Distribute to all subsystems
        SettingsManager::Get().LoadFromToml(root);
        ConfigValueManager::Get().LoadFromToml(root);

        // QoL: extract the [qol] section and pass to each subsystem
        auto qolNode = root["qol"].as_table();
        if (qolNode) {
            UISettings::Get().LoadFromToml(*qolNode);
            MemoryMonitor::Get().LoadFromToml(*qolNode);
            BorderlessWindow::Get().LoadFromToml(*qolNode);
        }

        // Note: patches are loaded separately via LoadPatches() after D3D9 init because patches need the game process to be further along
        // Don't ask me why...
        LOG_INFO("[ConfigStore] Loaded config from " + configPath);
        return true;
    } catch (const toml::parse_error& e) {
        std::string msg = std::string("TOML parse error: ") + e.what();
        LOG_ERROR("[ConfigStore] " + msg);
        if (error) *error = msg;
        return false;
    } catch (const std::exception& e) {
        std::string msg = std::string("Exception loading config: ") + e.what();
        LOG_ERROR("[ConfigStore] " + msg);
        if (error) *error = msg;
        return false;
    }
}

bool ConfigStore::LoadPatches(std::string* error) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        std::string configPath = ConfigPaths::GetConfigPath();

        if (!fs::exists(Utils::ToPath(configPath))) {
            LOG_DEBUG("[ConfigStore] No config file found, skipping patch loading");
            return true;
        }

        toml::table root = toml::parse_file(Utils::Utf8ToWide(configPath));
        OptimizationManager::Get().LoadFromToml(root);

        LOG_INFO("[ConfigStore] Loaded patches from " + configPath);
        return true;
    } catch (const toml::parse_error& e) {
        std::string msg = std::string("TOML parse error loading patches: ") + e.what();
        LOG_ERROR("[ConfigStore] " + msg);
        if (error) *error = msg;
        return false;
    } catch (const std::exception& e) {
        std::string msg = std::string("Exception loading patches: ") + e.what();
        LOG_ERROR("[ConfigStore] " + msg);
        if (error) *error = msg;
        return false;
    }
}

bool ConfigStore::SaveDefaults(std::string* error) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        std::string defaultsPath = ConfigPaths::GetDefaultsPath();

        if (!ConfigPaths::EnsureDirectoryExists()) {
            std::string msg = "Failed to create config directory";
            LOG_ERROR("[ConfigStore] " + msg);
            if (error) *error = msg;
            return false;
        }

        toml::table root;

        toml::table meta;
        meta.insert("version", CONFIG_VERSION);
        root.insert("meta", std::move(meta));

        SettingsManager::Get().SaveDefaultsToToml(root);

        if (!ConfigPaths::AtomicWriteToml(defaultsPath, root, error)) { return false; }

        LOG_DEBUG("[ConfigStore] Saved defaults to " + defaultsPath);
        return true;
    } catch (const std::exception& e) {
        std::string msg = std::string("Exception saving defaults: ") + e.what();
        LOG_ERROR("[ConfigStore] " + msg);
        if (error) *error = msg;
        return false;
    }
}

bool ConfigStore::LoadDefaults(std::string* error) {
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        std::string defaultsPath = ConfigPaths::GetDefaultsPath();

        if (!fs::exists(Utils::ToPath(defaultsPath))) {
            LOG_DEBUG("[ConfigStore] No defaults file found at " + defaultsPath);
            return true;
        }

        toml::table root = toml::parse_file(Utils::Utf8ToWide(defaultsPath));
        SettingsManager::Get().LoadDefaultsFromToml(root);

        LOG_DEBUG("[ConfigStore] Loaded defaults from " + defaultsPath);
        return true;
    } catch (const toml::parse_error& e) {
        std::string msg = std::string("TOML parse error in defaults: ") + e.what();
        LOG_ERROR("[ConfigStore] " + msg);
        if (error) *error = msg;
        return false;
    } catch (const std::exception& e) {
        std::string msg = std::string("Exception loading defaults: ") + e.what();
        LOG_ERROR("[ConfigStore] " + msg);
        if (error) *error = msg;
        return false;
    }
}
