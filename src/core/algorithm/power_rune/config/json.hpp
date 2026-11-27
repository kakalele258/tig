#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>

#include "json/ReJson.hpp"

inline std::string power_rune_config_path()
{
    const char *override_path = std::getenv("RP_POWER_RUNE_CONFIG");
    if (override_path != nullptr && override_path[0] != '\0')
        return std::string(override_path);
    return (std::filesystem::canonical("/proc/self/exe").parent_path().parent_path() / "config" /
            "power_rune.json")
        .string();
}

inline ReJson J_POWER_RUNE(power_rune_config_path());
