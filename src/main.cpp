// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "app/Application.h"
#include "audio/ReplayGain.h"
#include "util/TagLibSilencer.h"

#include <cxxopts/cxxopts.hpp>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/log.h>
}

#include <cstdio>
#include <exception>
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

    cxxopts::Options options("vtplayer", "Terminal-based music player (mp3/ogg/flac/m4a/mp4/aac/opus/wav/wma/webm + internet radio)");
    options.add_options()
        ("h,help",      "Show this help message")
        ("v,version",   "Show version and exit")
        ("dump-tags",   "Print every TagLib property of FILE and exit (diagnostic)")
        ("debug",       "Raise libav (ffmpeg) log verbosity (stream diagnostics)")
        ("path",        "Audio file or directory to open", cxxopts::value<std::string>());
    options.parse_positional({"path"});
    options.positional_help("[FILE|DIR]");

    cxxopts::ParseResult result;
    try
    {
        result = options.parse(argc, argv);
    }
    catch (std::exception const & e)
    {
        std::cerr << "vtplayer: " << e.what() << "\n\n"
                  << options.help() << std::endl;
        return 1;
    }

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

    // libav needs network init for HTTP/HTTPS sources. Default to ERROR-only
    // logging so transient diagnostics never leak into the TUI; --debug below
    // raises it. (StreamSource also raises it on start() — this is the safe
    // default for the rest of the process.)
    avformat_network_init();
    av_log_set_level(AV_LOG_ERROR);

    vtplayer::Application app;

    bool const debug = result.count("debug") > 0;
    if (debug)
    {
        app.setDebug(true);
        av_log_set_level(AV_LOG_VERBOSE);
    }

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

    if (!debug)
    {
        // Without --debug, redirect stderr to /dev/null for the lifetime of
        // the TUI. Keep CLI validation errors above visible; only silence
        // stderr once we are about to enter the terminal UI. libav writes
        // AV_LOG_ERROR-level diagnostics (e.g. radio stream disconnects)
        // straight to stderr, which corrupts the rendered screen.
        std::freopen("/dev/null", "w", stderr);
    }

    return app.run();
}
