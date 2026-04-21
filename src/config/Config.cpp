// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "Config.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace vtplayer
{

std::filesystem::path Config::defaultPath()
{
    char const * home = std::getenv("HOME");
    if (!home) return {};

    std::filesystem::path configDir = std::filesystem::path(home) / ".config" / "ventty-player";
    return configDir / "config.ini";
}

void Config::load()
{
    auto path = defaultPath();
    if (!path.empty() && std::filesystem::exists(path))
    {
        loadFrom(path);
    }

    // Set default start directory if not specified
    if (startDirectory.empty())
    {
        char const * home = std::getenv("HOME");
        if (home)
        {
            auto musicDir = std::filesystem::path(home) / "Music";
            if (std::filesystem::exists(musicDir))
            {
                startDirectory = musicDir;
            }
            else
            {
                startDirectory = home;
            }
        }
        else
        {
            startDirectory = "/";
        }
    }
}

void Config::loadFrom(std::filesystem::path const & path)
{
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::ostringstream ss;
    ss << file.rdbuf();
    parseIni(ss.str());
}

void Config::parseIni(std::string const & content)
{
    std::unordered_map<std::string, std::string> values;
    std::string currentSection;

    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line))
    {
        // Trim whitespace
        auto start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        // Skip comments
        if (line[0] == '#' || line[0] == ';') continue;

        // Section header
        if (line[0] == '[')
        {
            auto end = line.find(']');
            if (end != std::string::npos)
            {
                currentSection = line.substr(1, end - 1);
            }
            continue;
        }

        // Key = value
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        // Trim
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        auto vs = value.find_first_not_of(" \t");
        if (vs != std::string::npos) value = value.substr(vs);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
            value.pop_back();

        std::string fullKey = currentSection.empty() ? key : currentSection + "." + key;
        values[fullKey] = value;
    }

    applyValues(values);
}

void Config::applyValues(std::unordered_map<std::string, std::string> const & values)
{
    auto get = [&](std::string const & key) -> std::string const *
    {
        auto it = values.find(key);
        return (it != values.end()) ? &it->second : nullptr;
    };

    if (auto * v = get("audio.volume"))
    {
        try { volume = std::stof(*v) / 100.0f; } catch (...) {}
    }
    if (auto * v = get("audio.auto_gain"))
    {
        autoGain = (*v == "true" || *v == "1" || *v == "yes" || *v == "on");
    }
    if (auto * v = get("ui.start_directory"))
    {
        std::string dir = *v;
        // Expand ~
        if (!dir.empty() && dir[0] == '~')
        {
            char const * home = std::getenv("HOME");
            if (home)
            {
                dir = std::string(home) + dir.substr(1);
            }
        }
        startDirectory = dir;
    }
    if (auto * v = get("ui.show_hidden"))
    {
        showHidden = (*v == "true" || *v == "1" || *v == "yes");
    }
    if (auto * v = get("visualizer.bar_count"))
    {
        try { barCount = std::stoi(*v); } catch (...) {}
    }
    if (auto * v = get("formats.extensions"))
    {
        extensions = *v;
    }

    // Collect all theme.* keys
    for (auto const & [key, value] : values)
    {
        if (key.starts_with("theme."))
        {
            themeColors[key.substr(6)] = value;
        }
    }
}

} // namespace vtplayer
