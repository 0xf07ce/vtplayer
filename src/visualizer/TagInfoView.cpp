// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "TagInfoView.h"

#include "../util/UnicodeNormalize.h"

// Bare header names — see audio/ReplayGain.cpp for rationale.
#include <audioproperties.h>
#include <fileref.h>
#include <tfile.h>
#include <tpropertymap.h>
#include <tstring.h>

#include <ventty/core/Utf8.h>

#include <cstdint>
#include <cstdio>
#include <system_error>

namespace vtplayer
{

namespace
{

std::string toStdString(TagLib::String const & s)
{
    return s.to8Bit(/*unicode=*/true);
}

std::string firstValue(TagLib::PropertyMap const & props, char const * key)
{
    TagLib::String const wanted = TagLib::String(key).upper();
    for (auto const & entry : props)
    {
        if (entry.first.upper() == wanted && !entry.second.isEmpty())
        {
            return toStdString(entry.second.front());
        }
    }
    return {};
}

std::string joinValues(TagLib::PropertyMap const & props, char const * key, char const * sep = ", ")
{
    TagLib::String const wanted = TagLib::String(key).upper();
    std::string out;
    for (auto const & entry : props)
    {
        if (entry.first.upper() == wanted)
        {
            for (auto const & v : entry.second)
            {
                if (!out.empty()) out += sep;
                out += toStdString(v);
            }
            break;
        }
    }
    return out;
}

std::string formatSize(std::uintmax_t bytes)
{
    char buf[32];
    if (bytes >= 1024ULL * 1024 * 1024)
        std::snprintf(buf, sizeof(buf), "%.2f GiB", bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024ULL * 1024)
        std::snprintf(buf, sizeof(buf), "%.2f MiB", bytes / (1024.0 * 1024));
    else if (bytes >= 1024ULL)
        std::snprintf(buf, sizeof(buf), "%.2f KiB", bytes / 1024.0);
    else
        std::snprintf(buf, sizeof(buf), "%ju B", bytes);
    return buf;
}

std::string formatDuration(int seconds)
{
    if (seconds < 0) seconds = 0;
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;
    char buf[16];
    if (h > 0)
        std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    else
        std::snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return buf;
}

void addIfNonEmpty(std::vector<TagInfoView::Row> & rows,
                   char const * label,
                   std::string const & value)
{
    if (!value.empty())
    {
        rows.emplace_back(label, value);
    }
}

} // namespace

void TagInfoView::refresh(std::filesystem::path const & path)
{
    _sections.clear();
    _cachedPath = path;
    _hasFile = false;

    if (path.empty()) return;

    TagLib::FileRef ref(path.c_str());
    if (ref.isNull() || !ref.file()) return;

    _hasFile = true;
    TagLib::PropertyMap props = ref.file()->properties();

    // --- File ---
    {
        std::vector<Row> rows;
        rows.emplace_back("Path", path.string());
        std::error_code ec;
        std::uintmax_t const bytes = std::filesystem::file_size(path, ec);
        if (!ec)
        {
            rows.emplace_back("Size", formatSize(bytes));
        }
        _sections.emplace_back("File", std::move(rows));
    }

    // --- Metadata ---
    {
        std::vector<Row> rows;
        addIfNonEmpty(rows, "Title",        firstValue(props, "TITLE"));
        addIfNonEmpty(rows, "Artist",       firstValue(props, "ARTIST"));
        addIfNonEmpty(rows, "Album",        firstValue(props, "ALBUM"));
        addIfNonEmpty(rows, "Album Artist", firstValue(props, "ALBUMARTIST"));
        addIfNonEmpty(rows, "Composer",     firstValue(props, "COMPOSER"));
        addIfNonEmpty(rows, "Genre",        joinValues(props, "GENRE"));
        addIfNonEmpty(rows, "Date",         firstValue(props, "DATE"));
        // TRACKNUMBER may be "5" or "5/12"
        addIfNonEmpty(rows, "Track",        firstValue(props, "TRACKNUMBER"));
        addIfNonEmpty(rows, "Disc",         firstValue(props, "DISCNUMBER"));
        addIfNonEmpty(rows, "Comment",      firstValue(props, "COMMENT"));

        if (!rows.empty())
        {
            _sections.emplace_back("Metadata", std::move(rows));
        }
    }

    // --- Audio properties ---
    if (auto * ap = ref.audioProperties())
    {
        std::vector<Row> rows;
        rows.emplace_back("Duration",   formatDuration(ap->lengthInSeconds()));
        if (ap->bitrate() > 0)
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d kbps", ap->bitrate());
            rows.emplace_back("Bitrate", buf);
        }
        if (ap->sampleRate() > 0)
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d Hz", ap->sampleRate());
            rows.emplace_back("Sample rate", buf);
        }
        if (ap->channels() > 0)
        {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%d", ap->channels());
            rows.emplace_back("Channels", buf);
        }
        _sections.emplace_back("Audio", std::move(rows));
    }

    // --- ReplayGain ---
    {
        std::vector<Row> rows;
        addIfNonEmpty(rows, "Track gain",  firstValue(props, "REPLAYGAIN_TRACK_GAIN"));
        addIfNonEmpty(rows, "Track peak",  firstValue(props, "REPLAYGAIN_TRACK_PEAK"));
        addIfNonEmpty(rows, "Album gain",  firstValue(props, "REPLAYGAIN_ALBUM_GAIN"));
        addIfNonEmpty(rows, "Album peak",  firstValue(props, "REPLAYGAIN_ALBUM_PEAK"));
        addIfNonEmpty(rows, "Reference",   firstValue(props, "REPLAYGAIN_REFERENCE_LOUDNESS"));

        if (rows.empty())
        {
            rows.emplace_back("(none)", "no ReplayGain tags");
        }
        _sections.emplace_back("ReplayGain", std::move(rows));
    }
}

void TagInfoView::update(AudioEngine const & engine)
{
    auto const & path = engine.currentTrack().path;
    if (path != _cachedPath)
    {
        refresh(path);
        _scroll = 0;
    }
}

bool TagInfoView::scrollBy(int delta)
{
    int const maxScroll = std::max(0, _contentHeight - _viewportHeight);
    int const next = std::clamp(_scroll + delta, 0, maxScroll);
    bool changed = (next != _scroll);
    _scroll = next;
    return changed;
}

namespace
{

/// Single row to render at a fixed y position.
struct DrawRow
{
    enum class Kind { Heading, KeyValue };
    Kind kind;
    std::string a; ///< heading text, or label
    std::string b; ///< value (KeyValue only)
};

} // namespace

void TagInfoView::draw(ventty::Window & window, int x, int y, int w, int h)
{
    if (w < 10 || h < 3) return;

    ventty::Style const styHeader{_theme.headerTitleFg, _theme.background};
    ventty::Style const styLabel{_theme.transportFg, _theme.background};
    ventty::Style const styValue{_theme.transportTimeFg, _theme.background};
    ventty::Style const styDim{_theme.visLabelFg, _theme.background};
    ventty::Style const styScroll{_theme.visLabelFg, _theme.background};

    if (!_hasFile)
    {
        std::string msg = "(no track loaded)";
        int mx = x + (w - static_cast<int>(msg.size())) / 2;
        int my = y + h / 2;
        window.drawText(mx, my, msg, styDim);
        _contentHeight = 0;
        _viewportHeight = h;
        return;
    }

    // Flatten sections into a row list so scroll math is trivial.
    std::vector<DrawRow> rows;
    for (size_t i = 0; i < _sections.size(); ++i)
    {
        auto const & section = _sections[i];
        rows.push_back({DrawRow::Kind::Heading, section.first, {}});
        for (auto const & [label, value] : section.second)
        {
            rows.push_back({DrawRow::Kind::KeyValue, label, value});
        }
        if (i + 1 < _sections.size())
        {
            rows.push_back({DrawRow::Kind::KeyValue, {}, {}}); // blank spacer
        }
    }

    _contentHeight = static_cast<int>(rows.size());
    _viewportHeight = h;
    int const maxScroll = std::max(0, _contentHeight - _viewportHeight);
    if (_scroll > maxScroll) _scroll = maxScroll;
    if (_scroll < 0) _scroll = 0;

    constexpr int kLabelW = 14;
    int const valueX = x + kLabelW + 2;
    int const valueW = w - kLabelW - 2;

    int const end = std::min(_contentHeight, _scroll + h);
    for (int i = _scroll; i < end; ++i)
    {
        int const row = y + (i - _scroll);
        auto const & dr = rows[i];

        if (dr.kind == DrawRow::Kind::Heading)
        {
            std::string heading = "-- " + dr.a + " ";
            int dashes = w - static_cast<int>(heading.size());
            if (dashes > 0) heading.append(static_cast<size_t>(dashes), '-');
            window.drawText(x, row, heading, styHeader);
        }
        else if (!dr.a.empty())
        {
            window.drawText(x, row, dr.a, styLabel);
            if (valueW > 0)
            {
                std::string v = truncateToWidth(dr.b, valueW, "..");
                window.drawText(valueX, row, v, styValue);
            }
        }
        // else: blank spacer
    }

    // Scroll indicator on the right edge when content overflows.
    if (maxScroll > 0)
    {
        if (_scroll > 0)
        {
            window.drawText(x + w - 1, y, "^", styScroll);
        }
        if (_scroll < maxScroll)
        {
            window.drawText(x + w - 1, y + h - 1, "v", styScroll);
        }
    }
}

} // namespace vtplayer
