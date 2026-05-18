// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "app/Application.h"
#include "audio/ReplayGain.h"
#include "util/TagLibSilencer.h"

#include <cxxopts.hpp>

#include <filesystem>
#include <iostream>

#ifndef VTPLAYER_VERSION
#define VTPLAYER_VERSION "unknown"
#endif

int main(int argc, char *argv[])
{
    // Mute TagLib's stderr warnings (broken UTF-16 BOM in legacy tags)
    // before any FileRef is opened — otherwise they corrupt the TUI.
    vtplayer::silenceTagLib();

    cxxopts::Options options("vtplayer", "Terminal-based music player for MP3, OGG, and FLAC");
    options.add_options()
        ("h,help",      "Show this help message")
        ("v,version",   "Show version and exit")
        ("dump-tags",   "Print every TagLib property of FILE and exit (diagnostic)")
        ("path",        "Audio file or directory to open", cxxopts::value<std::string>());
    options.parse_positional({"path"});
    options.positional_help("[FILE|DIR]");

    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
        std::cout << options.help() << std::endl;
        return 0;
    }

    if (result.count("version"))
    {
        std::cout << "vtplayer " << VTPLAYER_VERSION << std::endl;
        return 0;
    }

    if (result.count("dump-tags"))
    {
        if (!result.count("path"))
        {
            std::cerr << "vtplayer: --dump-tags requires a FILE argument" << std::endl;
            return 1;
        }
        auto path = std::filesystem::absolute(result["path"].as<std::string>());
        return vtplayer::dumpTags(path) ? 0 : 1;
    }

    vtplayer::Application app;

    if (result.count("path"))
    {
        auto path = std::filesystem::absolute(result["path"].as<std::string>());
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec))
        {
            // Directory argument: open the FileBrowser there (4).
            app.setInitialDirectory(std::move(path));
        }
        else if (std::filesystem::exists(path))
        {
            // File argument: open its directory and play just this file.
            app.setInitialFile(std::move(path));
        }
        else
        {
            std::cerr << "vtplayer: path not found: " << path.string() << std::endl;
            return 1;
        }
    }

    return app.run();
}
