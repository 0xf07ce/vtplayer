// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <filesystem>
#include <string>

namespace vtplayer
{

enum class AudioFormat
{
    Unknown,
    Mp3,
    Ogg,
    Flac,
    Gme,  ///< All game-music-emu formats: VGM, VGZ, NSF, SPC, GBS, etc.
};

struct TrackInfo
{
    std::filesystem::path path;
    std::string title;
    std::string artist;
    float duration = 0.0f;   ///< seconds
    AudioFormat format = AudioFormat::Unknown;

    /// Derive format from file extension
    static AudioFormat formatFromPath(std::filesystem::path const & p)
    {
        auto ext = p.extension().string();
        for (auto & c : ext)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (ext == ".mp3") return AudioFormat::Mp3;
        if (ext == ".ogg") return AudioFormat::Ogg;
        if (ext == ".flac") return AudioFormat::Flac;
        // GME-supported formats
        if (ext == ".vgm" || ext == ".vgz"
            || ext == ".nsf" || ext == ".nsfe"
            || ext == ".spc"
            || ext == ".gbs"
            || ext == ".gym"
            || ext == ".hes")
        {
            return AudioFormat::Gme;
        }
        return AudioFormat::Unknown;
    }

    static bool isSupportedExtension(std::string const & ext)
    {
        std::string lower = ext;
        for (auto & c : lower)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return lower == ".mp3" || lower == ".ogg" || lower == ".flac"
            || lower == ".vgm" || lower == ".vgz"
            || lower == ".nsf" || lower == ".nsfe"
            || lower == ".spc" || lower == ".gbs"
            || lower == ".gym" || lower == ".hes";
    }
};

} // namespace vtplayer
