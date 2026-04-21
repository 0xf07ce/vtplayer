// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "app/Application.h"

#include <cxxopts.hpp>

#include <filesystem>
#include <iostream>

int main(int argc, char *argv[])
{
    cxxopts::Options options("vtplayer", "Terminal-based music player for MP3, OGG, and FLAC");
    options.add_options()
        ("h,help", "Show this help message")
        ("file", "Audio file to play", cxxopts::value<std::string>());
    options.parse_positional({"file"});
    options.positional_help("[FILE]");

    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
        std::cout << options.help() << std::endl;
        return 0;
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
