// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "TagWriter.h"

// Bare header names — see audio/ReplayGain.cpp for rationale.
#include <fileref.h>
#include <tfile.h>
#include <tpropertymap.h>
#include <tstring.h>
#include <tstringlist.h>

#include <string>

namespace vtplayer
{

namespace
{

/// Locate the existing case-variant of `keyUpper` in `props`, if any.
/// Returns the actual key string used in the file so we overwrite the same
/// entry instead of leaving a stale lower-cased duplicate next to a new
/// upper-cased one (different formats settle on different conventions).
TagLib::String existingKey(TagLib::PropertyMap const & props, char const * keyUpper)
{
    TagLib::String const wanted = TagLib::String(keyUpper).upper();
    for (auto const & entry : props)
    {
        if (entry.first.upper() == wanted)
        {
            return entry.first;
        }
    }
    return TagLib::String(keyUpper);
}

void setStringField(TagLib::PropertyMap & props, char const * key, std::string const & value)
{
    TagLib::String const actual = existingKey(props, key);
    props.erase(actual);
    if (!value.empty())
    {
        props.insert(actual, TagLib::StringList(TagLib::String(value, TagLib::String::UTF8)));
    }
}

void setIntField(TagLib::PropertyMap & props, char const * key, int value)
{
    TagLib::String const actual = existingKey(props, key);
    props.erase(actual);
    if (value > 0)
    {
        props.insert(actual, TagLib::StringList(TagLib::String(std::to_string(value))));
    }
}

} // namespace

bool applyTagUpdate(std::filesystem::path const & path, TagUpdate const & upd)
{
    if (upd.empty())
        return true;

    TagLib::FileRef ref(path.c_str());
    if (ref.isNull() || !ref.file())
        return false;

    TagLib::PropertyMap props = ref.file()->properties();

    if (upd.title)       setStringField(props, "TITLE",       *upd.title);
    if (upd.artist)      setStringField(props, "ARTIST",      *upd.artist);
    if (upd.album)       setStringField(props, "ALBUM",       *upd.album);
    if (upd.albumArtist) setStringField(props, "ALBUMARTIST", *upd.albumArtist);
    if (upd.genre)       setStringField(props, "GENRE",       *upd.genre);
    if (upd.trackNumber) setIntField(props, "TRACKNUMBER",    *upd.trackNumber);
    if (upd.discNumber)  setIntField(props, "DISCNUMBER",     *upd.discNumber);
    if (upd.year)        setIntField(props, "DATE",           *upd.year);

    ref.file()->setProperties(props);
    return ref.file()->save();
}

} // namespace vtplayer
