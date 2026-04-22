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
    bool const existed = !path.empty() && std::filesystem::exists(path);
    if (existed)
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

    // First run: materialize defaults so the user can discover/edit them.
    if (!existed && !path.empty())
    {
        save();
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

bool Config::save() const
{
    return saveTo(defaultPath());
}

bool Config::saveTo(std::filesystem::path const & path) const
{
    if (path.empty()) return false;

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) return false;

    file << serializeIni();
    return static_cast<bool>(file);
}

std::string Config::serializeIni() const
{
    std::ostringstream out;

    out << "# VTAMP (ventty-player) configuration\n";
    out << "# Auto-generated on first run; rewritten on exit.\n\n";

    out << "[audio]\n";
    out << "volume = " << static_cast<int>(volume * 100.0f + 0.5f) << "\n";
    out << "auto_gain = " << (autoGain ? "true" : "false") << "\n\n";

    out << "[ui]\n";
    out << "start_directory = " << startDirectory.string() << "\n";
    out << "show_hidden = " << (showHidden ? "true" : "false") << "\n\n";

    out << "[visualizer]\n";
    out << "bar_count = " << barCount << "\n\n";

    out << "[formats]\n";
    out << "extensions = " << extensions << "\n";

    if (!themeColors.empty())
    {
        out << "\n[theme]\n";
        for (auto const & [key, value] : themeColors)
        {
            out << key << " = " << value << "\n";
        }
    }

    return out.str();
}

} // namespace vtplayer
