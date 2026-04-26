#pragma once
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <set>
#include <mutex>
#include <random>
#include <HWID.h>

class ServerBanlist
{
public:
    struct BanEntry
    {
        std::string Id;
        std::vector<std::string> Names;
        std::string MachineId;
        std::string BanReason;
        std::set<std::string> Components;
    };

    void LoadFromFile(const char* filename)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_filename = filename;

        std::ifstream input(filename);
        if (!input.is_open()) return;

        nlohmann::json json;
        input >> json;

        for (const auto& entry : json)
        {
            BanEntry ban;
            ban.Id = entry.value("Id", GenerateId());
            ban.MachineId = entry.value("MachineId", "");
            ban.BanReason = entry.value("BanReason", "");

            // support old format (single "Name" string) and new format ("Names" array)
            if (entry.contains("Names") && entry["Names"].is_array())
            {
                for (const auto& n : entry["Names"])
                    if (n.is_string())
                        ban.Names.push_back(n.get<std::string>());
            }
            else if (entry.contains("Name") && entry["Name"].is_string())
            {
                std::string name = entry["Name"].get<std::string>();
                if (!name.empty())
                    ban.Names.push_back(name);
            }

            if (entry.contains("Components") && entry["Components"].is_array())
            {
                for (const auto& c : entry["Components"])
                    if (c.is_string())
                        ban.Components.insert(c.get<std::string>());
            }

            m_entries.push_back(ban);

            for (const auto& c : ban.Components)
                m_bannedComponents.insert(c);
        }
    }

    void SaveToFile()
    {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& ban : m_entries)
        {
            nlohmann::json entry;
            entry["Id"] = ban.Id;
            entry["Names"] = ban.Names;
            entry["MachineId"] = ban.MachineId;
            entry["BanReason"] = ban.BanReason;
            entry["Components"] = nlohmann::json(std::vector<std::string>(ban.Components.begin(), ban.Components.end()));
            j.push_back(entry);
        }

        std::ofstream file(m_filename ? m_filename : "bans.json");
        if (file.is_open())
            file << j.dump(4);
    }

    // check if banned by machine id, hw components, or name (name only used as fallback for hw-less pre-bans)
    bool IsBanned(const char* name, const char* machineId = nullptr, const Cypress::HardwareFingerprint* fp = nullptr)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        for (const auto& ban : m_entries)
        {
            if (machineId && !ban.MachineId.empty() && strcmp(ban.MachineId.c_str(), machineId) == 0)
                return true;
        }

        if (fp)
        {
            for (const auto& component : fp->components)
            {
                if (m_bannedComponents.count(component))
                    return true;
            }
        }

        // fallback: name match only for pre-bans with no hw info yet
        if (name)
        {
            for (const auto& ban : m_entries)
            {
                if (ban.MachineId.empty() && ban.Components.empty())
                {
                    for (const auto& n : ban.Names)
                    {
                        if (strcmp(n.c_str(), name) == 0)
                            return true;
                    }
                }
            }
        }

        return false;
    }

    // viral spread - if any component matches a ban, absorb all their other components too
    // also tracks the name as a known alias
    void SpreadComponents(const Cypress::HardwareFingerprint& fp, const char* name = nullptr)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        bool matched = false;
        for (const auto& component : fp.components)
        {
            if (m_bannedComponents.count(component))
            {
                matched = true;
                break;
            }
        }

        if (!matched) return;

        bool changed = false;
        for (auto& ban : m_entries)
        {
            bool entryMatches = false;
            for (const auto& component : fp.components)
            {
                if (ban.Components.count(component))
                {
                    entryMatches = true;
                    break;
                }
            }

            if (entryMatches)
            {
                for (const auto& component : fp.components)
                {
                    if (ban.Components.insert(component).second)
                    {
                        m_bannedComponents.insert(component);
                        changed = true;
                    }
                }

                // track name as alias
                if (name)
                {
                    bool hasName = false;
                    for (const auto& n : ban.Names)
                        if (strcmp(n.c_str(), name) == 0) { hasName = true; break; }
                    if (!hasName)
                    {
                        ban.Names.push_back(name);
                        changed = true;
                    }
                }
            }
        }

        if (changed)
            SaveToFile();
    }

    void AddToList(const char* name, const char* machineId, const char* reasonText, const Cypress::HardwareFingerprint* fp = nullptr)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // check if any existing profile matches by component or name
        BanEntry* existing = nullptr;
        if (fp)
        {
            for (auto& ban : m_entries)
            {
                for (const auto& c : fp->components)
                {
                    if (ban.Components.count(c))
                    {
                        existing = &ban;
                        break;
                    }
                }
                if (existing) break;
            }
        }
        if (!existing)
        {
            for (auto& ban : m_entries)
            {
                for (const auto& n : ban.Names)
                {
                    if (strcmp(n.c_str(), name) == 0)
                    {
                        existing = &ban;
                        break;
                    }
                }
                if (existing) break;
            }
        }

        if (existing)
        {
            // merge into existing profile
            bool hasName = false;
            for (const auto& n : existing->Names)
                if (strcmp(n.c_str(), name) == 0) { hasName = true; break; }
            if (!hasName)
                existing->Names.push_back(name);

            if (machineId && existing->MachineId.empty())
                existing->MachineId = machineId;

            if (fp)
            {
                for (const auto& c : fp->components)
                {
                    existing->Components.insert(c);
                    m_bannedComponents.insert(c);
                }
            }
        }
        else
        {
            // new profile
            BanEntry ban;
            ban.Id = GenerateId();
            ban.Names.push_back(name);
            ban.MachineId = machineId ? machineId : "";
            ban.BanReason = reasonText ? reasonText : "";

            if (fp)
            {
                for (const auto& c : fp->components)
                {
                    ban.Components.insert(c);
                    m_bannedComponents.insert(c);
                }
            }

            m_entries.push_back(ban);
        }

        SaveToFile();
    }

    void RemoveFromList(const char* name)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
        {
            bool found = false;
            for (const auto& n : it->Names)
                if (strcmp(n.c_str(), name) == 0) { found = true; break; }

            if (found)
            {
                m_entries.erase(it);
                break;
            }
        }

        // rebuild index
        m_bannedComponents.clear();
        for (const auto& ban : m_entries)
            for (const auto& c : ban.Components)
                m_bannedComponents.insert(c);

        SaveToFile();
    }

    const BanEntry* GetBanEntry(const char* name) const
    {
        for (const auto& ban : m_entries)
            for (const auto& n : ban.Names)
                if (strcmp(n.c_str(), name) == 0)
                    return &ban;
        return nullptr;
    }

    const std::vector<BanEntry>& GetBannedPlayers() const { return m_entries; }

private:
    static std::string GenerateId()
    {
        static std::mt19937 rng(std::random_device{}());
        static const char hex[] = "0123456789abcdef";
        std::string id;
        id.reserve(16);
        for (int i = 0; i < 16; ++i)
            id += hex[rng() % 16];
        return id;
    }

    const char* m_filename = nullptr;
    std::vector<BanEntry> m_entries;
    std::set<std::string> m_bannedComponents;
    std::mutex m_mutex;
};
