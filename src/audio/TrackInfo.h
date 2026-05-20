// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace vtplayer
{

enum class AudioFormat
{
    Unknown,
    Mp3,
    Wav,
    Ogg,
    Flac,
    M4a,
    Aac,
    Opus,
    Wma,
    Webm,
};

struct TrackInfo
{
    std::filesystem::path path;
    std::string title;
    std::string artist;
    std::string album;
    std::string albumArtist;
    std::string genre;
    int   trackNumber = 0;
    int   discNumber  = 0;
    int   year        = 0;
    float duration    = 0.0f;   ///< seconds
    AudioFormat format = AudioFormat::Unknown;

    /// File modification time (unix seconds) and size in bytes — used by the
    /// MediaLibrary scanner for incremental updates. Zero when unknown.
    std::int64_t mtime = 0;
    std::int64_t size  = 0;

    /// Derive format from file extension
    static AudioFormat formatFromPath(std::filesystem::path const & p)
    {
        auto ext = p.extension().string();
        for (auto & c : ext)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (ext == ".mp3") return AudioFormat::Mp3;
        if (ext == ".wav") return AudioFormat::Wav;
        if (ext == ".ogg") return AudioFormat::Ogg;
        if (ext == ".flac") return AudioFormat::Flac;
        if (ext == ".m4a") return AudioFormat::M4a;
        if (ext == ".aac") return AudioFormat::Aac;
        if (ext == ".opus") return AudioFormat::Opus;
        if (ext == ".wma") return AudioFormat::Wma;
        if (ext == ".webm") return AudioFormat::Webm;
        return AudioFormat::Unknown;
    }
};

} // namespace vtplayer
