// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "Config.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace vtplayer
{

    std::filesystem::path Config::defaultPath()
    {
        char const *home = std::getenv("HOME");
        if (!home)
            return {};

        std::filesystem::path configDir = std::filesystem::path(home) / ".config" / "vtplayer";
        return configDir / "config.ini";
    }

    void Config::load()
    {
        // One-time migration: drop the legacy `ventty-player` config directory.
        if (char const *home = std::getenv("HOME"))
        {
            std::error_code ec;
            std::filesystem::remove_all(
                std::filesystem::path(home) / ".config" / "ventty-player", ec);
        }

        auto path = defaultPath();
        bool const existed = !path.empty() && std::filesystem::exists(path);
        if (existed)
        {
            loadFrom(path);
        }

        // First run: materialize defaults so the user can discover/edit them.
        if (!existed && !path.empty())
        {
            save();
        }
    }

    void Config::loadFrom(std::filesystem::path const &path)
    {
        std::ifstream file(path);
        if (!file.is_open())
            return;

        std::ostringstream ss;
        ss << file.rdbuf();
        parseIni(ss.str());
    }

    void Config::parseIni(std::string const &content)
    {
        std::unordered_map<std::string, std::string> values;
        std::string currentSection;

        std::istringstream stream(content);
        std::string line;

        while (std::getline(stream, line))
        {
            // Trim whitespace
            auto start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos)
                continue;
            line = line.substr(start);

            // Skip comments
            if (line[0] == '#' || line[0] == ';')
                continue;

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
            if (eq == std::string::npos)
                continue;

            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);

            // Trim
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
                key.pop_back();
            auto vs = value.find_first_not_of(" \t");
            if (vs != std::string::npos)
                value = value.substr(vs);
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
                value.pop_back();

            std::string fullKey = currentSection.empty() ? key : currentSection + "." + key;
            values[fullKey] = value;
        }

        applyValues(values);
    }

    void Config::applyValues(std::unordered_map<std::string, std::string> const &values)
    {
        auto get = [&](std::string const &key) -> std::string const *
        {
            auto it = values.find(key);
            return (it != values.end()) ? &it->second : nullptr;
        };

        if (auto *v = get("audio.gain_norm"))
        {
            gainNorm = (*v == "true" || *v == "1" || *v == "yes" || *v == "on");
        }
        if (auto *v = get("audio.stream_buffer_seconds"))
        {
            try
            {
                // Clamp to a sane window: a stray/negative value must not
                // size the radio ring buffer to zero or to gigabytes.
                streamBufferSeconds = std::clamp(std::stof(*v), 1.0f, 600.0f);
            }
            catch (...)
            {
            }
        }
        if (auto *v = get("audio.stream_prebuffer_seconds"))
        {
            try
            {
                // Absolute clamp only; the prebuffer is additionally pinned
                // below the buffer depth at runtime (AudioEngine::setBuffer).
                streamPrebufferSeconds = std::clamp(std::stof(*v), 0.5f, 600.0f);
            }
            catch (...)
            {
            }
        }
        if (auto *v = get("ui.show_hidden"))
        {
            showHidden = (*v == "true" || *v == "1" || *v == "yes");
        }
        if (auto *v = get("visualizer.bar_count"))
        {
            try
            {
                // Documented range is 4–256; clamp so an absurd value can't
                // blow up FFT binning / render work.
                barCount = std::clamp(std::stoi(*v), 4, 256);
            }
            catch (...)
            {
            }
        }
        if (auto *v = get("visualizer.index"))
        {
            try
            {
                visualizerIndex = std::stoi(*v);
            }
            catch (...)
            {
            }
        }
        if (auto *v = get("visualizer.fps"))
        {
            try
            {
                int fps = std::stoi(*v);
                // Snap to the nearest supported tier so a stray value
                // doesn't bake an arbitrary refresh rate into the loop.
                if (fps <= 22) visualizerFps = 15;
                else if (fps <= 45) visualizerFps = 30;
                else visualizerFps = 60;
            }
            catch (...)
            {
            }
        }
        if (auto *v = get("library.root"))
        {
            std::string dir = *v;
            // Expand ~
            if (!dir.empty() && dir[0] == '~')
            {
                char const *home = std::getenv("HOME");
                if (home)
                {
                    dir = std::string(home) + dir.substr(1);
                }
            }
            libraryRoot = dir;
        }
        if (auto *v = get("library.left_mode"))
        {
            // "radio" (v0.9.x and earlier) is intentionally rejected here so
            // the default "album" takes over after the upgrade. Application
            // also maps it to Album defensively in leftModeFromConfig.
            if (*v == "artist" || *v == "album" || *v == "directory"
                || *v == "playlists")
            {
                leftMode = *v;
            }
        }
        if (auto *v = get("library.focus_path"))
        {
            libraryFocus = *v;
        }
        if (auto *v = get("library.scan_sig"))
        {
            scanSig = *v;
        }
        // Collect all theme.* keys
        for (auto const &[key, value] : values)
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

    bool Config::saveTo(std::filesystem::path const &path) const
    {
        if (path.empty())
            return false;

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
            return false;

        std::ofstream file(path, std::ios::trunc);
        if (!file.is_open())
            return false;

        file << serializeIni();
        return static_cast<bool>(file);
    }

    std::string Config::serializeIni() const
    {
        std::ostringstream out;

        out << "# VT-PLAYER configuration\n";
        out << "# Auto-generated on first run; rewritten on exit.\n\n";

        out << "[audio]\n";
        out << "gain_norm = " << (gainNorm ? "true" : "false") << "\n";
        out << "stream_buffer_seconds = " << streamBufferSeconds << "\n";
        out << "stream_prebuffer_seconds = " << streamPrebufferSeconds << "\n\n";

        out << "[ui]\n";
        out << "show_hidden = " << (showHidden ? "true" : "false") << "\n\n";

        out << "[visualizer]\n";
        out << "bar_count = " << barCount << "\n";
        out << "index = " << visualizerIndex << "\n";
        out << "fps = " << visualizerFps << "\n\n";

        out << "[library]\n";
        out << "root = " << libraryRoot.string() << "\n";
        out << "left_mode = " << leftMode << "\n";
        out << "focus_path = " << libraryFocus.string() << "\n";
        out << "scan_sig = " << scanSig << "\n";

        if (!themeColors.empty())
        {
            out << "\n[theme]\n";
            for (auto const &[key, value] : themeColors)
            {
                out << key << " = " << value << "\n";
            }
        }

        return out.str();
    }

} // namespace vtplayer
