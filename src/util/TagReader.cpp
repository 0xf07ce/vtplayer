// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "TagReader.h"

#include "../plugin/DecoderRegistry.h"
#include "UnicodeNormalize.h"

#include "vtplayer/plugin.h"

// Bare header names — see audio/ReplayGain.cpp for rationale.
#include <audioproperties.h>
#include <fileref.h>
#include <tfile.h>
#include <tpropertymap.h>
#include <tstring.h>

#include <cctype>
#include <string>

namespace vtplayer
{

namespace
{

std::string lowerExt(std::filesystem::path const & p)
{
    std::string ext = p.extension().string();
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
    for (auto & c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

std::string firstValue(TagLib::PropertyMap const & props, char const * key)
{
    TagLib::String const wanted = TagLib::String(key).upper();
    for (auto const & entry : props)
    {
        if (entry.first.upper() == wanted && !entry.second.isEmpty())
            return entry.second.front().to8Bit(/*unicode=*/true);
    }
    return {};
}

int parseLeadingInt(std::string const & v)
{
    try
    {
        if (v.empty()) return 0;
        return std::stoi(v);
    }
    catch (...)
    {
        return 0;
    }
}

} // namespace

TrackInfo readTrackInfo(std::filesystem::path const & path,
                        bool filenameTitleFallback)
{
    TrackInfo info;
    info.path = path;
    info.format = TrackInfo::formatFromPath(path);
    if (filenameTitleFallback)
        info.title = toNfc(path.stem().string());

    if (VtpInputPlugin const * plug = DecoderRegistry::instance().find(lowerExt(path)))
    {
        info.format = AudioFormat::Plugin;
        if (plug->read_tags)
        {
            VtpTagOut t{};
            t.struct_size = sizeof(t);
            if (plug->read_tags(path.string().c_str(), &t) == 0)
            {
                if (t.title[0])        info.title       = toNfc(t.title);
                if (t.artist[0])       info.artist      = toNfc(t.artist);
                if (t.album[0])        info.album       = toNfc(t.album);
                if (t.album_artist[0]) info.albumArtist = toNfc(t.album_artist);
                if (t.grouping[0])     info.grouping    = toNfc(t.grouping);
                info.trackNumber = t.track_number;
                info.year        = t.year;
                if (t.duration > 0.0)
                    info.duration = static_cast<float>(t.duration);
            }
        }
        return info;
    }

    TagLib::FileRef ref(path.c_str());
    if (ref.isNull() || !ref.file())
        return info;

    TagLib::PropertyMap props = ref.file()->properties();

    auto title = firstValue(props, "TITLE");
    if (!title.empty()) info.title = toNfc(title);

    info.artist      = toNfc(firstValue(props, "ARTIST"));
    info.album       = toNfc(firstValue(props, "ALBUM"));
    info.albumArtist = toNfc(firstValue(props, "ALBUMARTIST"));
    info.genre       = toNfc(firstValue(props, "GENRE"));
    info.grouping    = toNfc(firstValue(props, "GROUPING"));
    info.trackNumber = parseLeadingInt(firstValue(props, "TRACKNUMBER"));
    info.discNumber  = parseLeadingInt(firstValue(props, "DISCNUMBER"));
    info.year        = parseLeadingInt(firstValue(props, "DATE"));

    if (auto * ap = ref.audioProperties())
        info.duration = static_cast<float>(ap->lengthInSeconds());

    return info;
}

} // namespace vtplayer
