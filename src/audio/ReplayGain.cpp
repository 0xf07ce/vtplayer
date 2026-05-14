// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "ReplayGain.h"

// TagLib v2 (consumed via FetchContent / add_subdirectory): headers live in
// a non-flat source tree, so we use bare header names (the deps target adds
// `taglib/` and `taglib/toolkit/` to the include path).
#include <fileref.h>
#include <tfile.h>
#include <tpropertymap.h>
#include <tstring.h>

#include <cstddef>
#include <iostream>
#include <string>

namespace vtplayer
{

namespace
{

bool parseFloat(TagLib::String const & s, float & out)
{
    std::string str = s.to8Bit(true);
    if (str.empty()) return false;
    try
    {
        std::size_t idx = 0;
        // ReplayGain values look like "-6.54 dB" — stof reads the number prefix.
        out = std::stof(str, &idx);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace

ReplayGainInfo readReplayGain(std::filesystem::path const & path)
{
    ReplayGainInfo info;

    TagLib::FileRef ref(path.c_str());
    if (ref.isNull() || !ref.file()) return info;

    TagLib::PropertyMap props = ref.file()->properties();

    // ReplayGain spec mandates uppercase keys, but ID3v2 TXXX frame
    // descriptions preserve the case the tagger wrote, and PropertyMap
    // doesn't normalize. Search case-insensitively so we tolerate
    // legacy/non-conforming taggers.
    auto pickFirst = [&](char const * key, float & out) -> bool {
        TagLib::String const wantedUpper = TagLib::String(key).upper();
        for (auto const & entry : props)
        {
            if (entry.first.upper() == wantedUpper && !entry.second.isEmpty())
            {
                return parseFloat(entry.second.front(), out);
            }
        }
        return false;
    };

    if (pickFirst("REPLAYGAIN_TRACK_GAIN", info.trackGainDb))
    {
        info.hasTrackGain = true;
    }
    pickFirst("REPLAYGAIN_TRACK_PEAK", info.trackPeak);

    return info;
}

bool dumpTags(std::filesystem::path const & path)
{
    TagLib::FileRef ref(path.c_str());
    if (ref.isNull() || !ref.file())
    {
        std::cerr << "Cannot open: " << path.string() << "\n";
        return false;
    }

    std::cout << "File: " << path.string() << "\n";
    std::cout << "Properties (" << ref.file()->properties().size() << "):\n";

    TagLib::PropertyMap props = ref.file()->properties();
    for (auto const & entry : props)
    {
        std::cout << "  " << entry.first.to8Bit(true) << " =";
        for (auto const & v : entry.second)
        {
            std::cout << " [" << v.to8Bit(true) << "]";
        }
        std::cout << "\n";
    }

    if (auto * ap = ref.audioProperties())
    {
        std::cout << "Audio: " << ap->lengthInSeconds() << "s, "
                  << ap->bitrate() << "kbps, "
                  << ap->sampleRate() << "Hz, "
                  << ap->channels() << "ch\n";
    }
    return true;
}

} // namespace vtplayer
