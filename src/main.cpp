// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "app/Application.h"
#include "audio/ReplayGain.h"

#include <cxxopts.hpp>

#include <filesystem>
#include <iostream>

#ifndef VTPLAYER_VERSION
#define VTPLAYER_VERSION "unknown"
#endif

int main(int argc, char *argv[])
{
    cxxopts::Options options("vtplayer", "Terminal-based music player for MP3, OGG, and FLAC");
    options.add_options()
        ("h,help",      "Show this help message")
        ("v,version",   "Show version and exit")
        ("dump-tags",   "Print every TagLib property of FILE and exit (diagnostic)")
        ("file",        "Audio file to play", cxxopts::value<std::string>());
    options.parse_positional({"file"});
    options.positional_help("[FILE]");

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
        if (!result.count("file"))
        {
            std::cerr << "vtplayer: --dump-tags requires a FILE argument" << std::endl;
            return 1;
        }
        auto path = std::filesystem::absolute(result["file"].as<std::string>());
        return vtplayer::dumpTags(path) ? 0 : 1;
    }

    vtplayer::Application app;

    if (result.count("file"))
    {
        auto path = std::filesystem::absolute(result["file"].as<std::string>());
        if (!std::filesystem::exists(path))
        {
            std::cerr << "vtplayer: file not found: " << path.string() << std::endl;
            return 1;
        }
        app.setInitialFile(std::move(path));
    }

    return app.run();
}
