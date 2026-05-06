// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PlaylistView.h"

#include "../util/UnicodeNormalize.h"

#include <ventty/art/AsciiArt.h>

#include <algorithm>
#include <cmath>
#include <random>

namespace vtplayer
{

using Key = ventty::KeyEvent::Key;

void PlaylistView::addTrack(TrackInfo const & track)
{
    _tracks.push_back(track);
}

void PlaylistView::insertTrack(int idx, TrackInfo const & track)
{
    if (idx < 0) idx = 0;
    if (idx > static_cast<int>(_tracks.size())) idx = static_cast<int>(_tracks.size());
    _tracks.insert(_tracks.begin() + idx, track);
    if (_playingIndex >= idx) _playingIndex++;
    if (_selectedIndex >= idx) _selectedIndex++;
    clearMultiSelection();
}

void PlaylistView::removeSelected()
{
    if (_tracks.empty()) return;

    std::set<int> targets = _multiSelected;
    if (_selectedIndex >= 0 && _selectedIndex < static_cast<int>(_tracks.size()))
    {
        targets.insert(_selectedIndex);
    }
    if (targets.empty()) return;

    bool const removingPlaying = (_playingIndex >= 0) && targets.count(_playingIndex) > 0;
    int const firstRemoved = *targets.begin();

    // Erase from highest to lowest so earlier indices stay valid.
    for (auto it = targets.rbegin(); it != targets.rend(); ++it)
    {
        int const idx = *it;
        if (idx < 0 || idx >= static_cast<int>(_tracks.size())) continue;
        _tracks.erase(_tracks.begin() + idx);
        if (!removingPlaying && _playingIndex > idx) _playingIndex--;
    }

    if (removingPlaying) _playingIndex = -1;

    if (_tracks.empty())
    {
        _selectedIndex = 0;
    }
    else
    {
        _selectedIndex = std::min(firstRemoved, static_cast<int>(_tracks.size()) - 1);
    }

    clearMultiSelection();
    scrollToSelected();

    if (removingPlaying && _onPlayingRemoved)
    {
        _onPlayingRemoved();
    }
}

void PlaylistView::moveSelectedUp()
{
    if (_selectedIndex <= 0 || _selectedIndex >= static_cast<int>(_tracks.size()))
    {
        return;
    }

    std::swap(_tracks[_selectedIndex], _tracks[_selectedIndex - 1]);

    if (_playingIndex == _selectedIndex) _playingIndex--;
    else if (_playingIndex == _selectedIndex - 1) _playingIndex++;

    _selectedIndex--;
    scrollToSelected();
}

void PlaylistView::moveSelectedDown()
{
    if (_selectedIndex < 0 || _selectedIndex >= static_cast<int>(_tracks.size()) - 1)
    {
        return;
    }

    std::swap(_tracks[_selectedIndex], _tracks[_selectedIndex + 1]);

    if (_playingIndex == _selectedIndex) _playingIndex++;
    else if (_playingIndex == _selectedIndex + 1) _playingIndex--;

    _selectedIndex++;
    scrollToSelected();
}

void PlaylistView::clear()
{
    bool const hadPlaying = (_playingIndex >= 0);
    _tracks.clear();
    _selectedIndex = 0;
    _scrollOffset = 0;
    _playingIndex = -1;
    clearMultiSelection();
    if (hadPlaying && _onPlayingRemoved)
    {
        _onPlayingRemoved();
    }
}

void PlaylistView::shuffle()
{
    if (_tracks.size() < 2) return;

    std::filesystem::path playingPath;
    if (_playingIndex >= 0 && _playingIndex < static_cast<int>(_tracks.size()))
    {
        playingPath = _tracks[_playingIndex].path;
    }

    static std::mt19937 rng{std::random_device{}()};
    std::shuffle(_tracks.begin(), _tracks.end(), rng);

    if (!playingPath.empty())
    {
        for (int i = 0; i < static_cast<int>(_tracks.size()); ++i)
        {
            if (_tracks[i].path == playingPath)
            {
                _playingIndex = i;
                break;
            }
        }
    }

    _selectedIndex = 0;
    _scrollOffset = 0;
    clearMultiSelection();
    scrollToSelected();
}

void PlaylistView::setTracks(std::vector<TrackInfo> tracks)
{
    bool const hadPlaying = (_playingIndex >= 0);
    _tracks = std::move(tracks);
    _selectedIndex = 0;
    _scrollOffset = 0;
    _playingIndex = -1;
    clearMultiSelection();
    if (hadPlaying && _onPlayingRemoved)
    {
        _onPlayingRemoved();
    }
}

void PlaylistView::clearMultiSelection()
{
    _multiSelected.clear();
    _selectionAnchor = -1;
}

void PlaylistView::selectAll()
{
    _multiSelected.clear();
    for (int i = 0; i < static_cast<int>(_tracks.size()); ++i)
    {
        _multiSelected.insert(i);
    }
    _selectionAnchor = _selectedIndex;
}

void PlaylistView::extendSelectionTo(int newIndex)
{
    if (_selectionAnchor < 0) _selectionAnchor = _selectedIndex;
    int const lo = std::min(_selectionAnchor, newIndex);
    int const hi = std::max(_selectionAnchor, newIndex);
    _multiSelected.clear();
    for (int i = lo; i <= hi; ++i) _multiSelected.insert(i);
}

void PlaylistView::onFocusChanged()
{
    if (!isFocused())
    {
        clearMultiSelection();
    }
}

void PlaylistView::setSelectedIndex(int idx)
{
    _selectedIndex = std::clamp(idx, 0, std::max(0, static_cast<int>(_tracks.size()) - 1));
    scrollToSelected();
}

TrackInfo const * PlaylistView::selectedTrack() const
{
    if (_selectedIndex >= 0 && _selectedIndex < static_cast<int>(_tracks.size()))
    {
        return &_tracks[_selectedIndex];
    }
    return nullptr;
}

TrackInfo const * PlaylistView::track(int idx) const
{
    if (idx >= 0 && idx < static_cast<int>(_tracks.size()))
    {
        return &_tracks[idx];
    }
    return nullptr;
}

static std::string formatDuration(float seconds)
{
    if (seconds <= 0.0f)
    {
        return "--:--";
    }
    int total = static_cast<int>(seconds);
    int m = total / 60;
    int s = total % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return buf;
}

void PlaylistView::draw(ventty::Window & window)
{
    auto const & r = rect();

    // Right border
    for (int y = 0; y < r.height; ++y)
    {
        window.putChar(r.x + r.width - 1, r.y + y, ventty::DOUBLE_BOX.v,
                       ventty::Style{_theme.border, _theme.playlistBg});
    }

    // Header
    ventty::Style headerStyle{_theme.playlistHeaderFg, _theme.playlistBg, ventty::Attr::Bold};
    window.fill(r.x, r.y, r.width - 1, 1, U' ', headerStyle);

    std::string header = " Playlist";
    if (!_playlistName.empty())
    {
        header += ": " + _playlistName;
    }
    if (!_tracks.empty())
    {
        header += " (" + std::to_string(_tracks.size()) + ")";
    }
    header = truncateToWidth(header, r.width - 1, "...");
    window.drawText(r.x, r.y, header, headerStyle);

    // Separator
    ventty::Style sepStyle{_theme.border, _theme.playlistBg};
    for (int x = r.x; x < r.x + r.width - 1; ++x)
    {
        window.putChar(x, r.y + 1, ventty::HR_THIN, sepStyle);
    }

    // Track list
    int listH = r.height - 2;
    int contentW = r.width - 2; // right border + padding

    for (int i = 0; i < listH; ++i)
    {
        int idx = _scrollOffset + i;
        int y = r.y + 2 + i;

        if (idx >= static_cast<int>(_tracks.size()))
        {
            window.fill(r.x, y, r.width - 1, 1, U' ',
                        ventty::Style{_theme.playlistFg, _theme.playlistBg});
            continue;
        }

        auto const & track = _tracks[idx];
        bool const cursor = (idx == _selectedIndex) && isFocused();
        bool const multi  = isFocused() && _multiSelected.count(idx) > 0;
        bool const playing = (idx == _playingIndex);

        Color fg;
        Color bg = (cursor || multi) ? _theme.playlistSelBg : _theme.playlistBg;
        if (cursor)
        {
            fg = _theme.playlistSelFg;
        }
        else if (multi)
        {
            fg = _theme.playlistFg;
        }
        else if (playing)
        {
            fg = _theme.playlistPlayingFg;
        }
        else
        {
            fg = _theme.playlistFg;
        }

        ventty::Style style{fg, bg};
        window.fill(r.x, y, r.width - 1, 1, U' ', style);

        // Prefix: index number, or ▶ for the currently-playing row.
        // Drawn at r.x + 1 (the panel separator at r.x would otherwise
        // overwrite anything drawn at column r.x). 3 cells wide to align
        // with the "%2d." format used for non-playing rows.
        if (playing)
        {
            Color arrowFg = cursor ? _theme.playlistSelFg : _theme.playlistPlayingFg;
            window.drawText(r.x + 1, y, " \xE2\x96\xB6 ", // " ▶ "
                            ventty::Style{arrowFg, bg});
        }
        else
        {
            char numBuf[8];
            std::snprintf(numBuf, sizeof(numBuf), "%2d.", idx + 1);
            ventty::Style indexStyle{cursor ? fg : _theme.playlistIndexFg, bg};
            window.drawText(r.x + 1, y, numBuf, indexStyle);
        }

        // Track name (display-width truncation; CJK = 2 cells per codepoint)
        int const durLen = 6; // " MM:SS"
        int const maxNameW = contentW - 5 - durLen; // 4 for index, 1 padding
        std::string name = truncateToWidth(track.title, maxNameW);
        window.drawText(r.x + 5, y, name, style);

        // Duration (right-aligned)
        std::string dur = formatDuration(track.duration);
        int durX = r.x + r.width - 2 - static_cast<int>(dur.size());
        ventty::Style durStyle{cursor ? fg : _theme.playlistDurationFg, bg};
        window.drawText(durX, y, dur, durStyle);
    }
}

bool PlaylistView::handleKey(ventty::KeyEvent const & event)
{
    // Ctrl+A: select all tracks.
    if (event.key == Key::Char && event.ctrl &&
        (event.ch == 'a' || event.ch == 'A' || event.ch == 1))
    {
        if (!_tracks.empty()) selectAll();
        return true;
    }

    if (event.key == Key::Up)
    {
        if (_selectedIndex > 0)
        {
            int const target = _selectedIndex - 1;
            if (event.shift)
            {
                if (_selectionAnchor < 0) _selectionAnchor = _selectedIndex;
                _selectedIndex = target;
                extendSelectionTo(target);
            }
            else
            {
                clearMultiSelection();
                _selectedIndex = target;
            }
            scrollToSelected();
        }
        else if (!event.shift)
        {
            clearMultiSelection();
        }
        return true;
    }

    if (event.key == Key::Down)
    {
        if (_selectedIndex < static_cast<int>(_tracks.size()) - 1)
        {
            int const target = _selectedIndex + 1;
            if (event.shift)
            {
                if (_selectionAnchor < 0) _selectionAnchor = _selectedIndex;
                _selectedIndex = target;
                extendSelectionTo(target);
            }
            else
            {
                clearMultiSelection();
                _selectedIndex = target;
            }
            scrollToSelected();
        }
        else if (!event.shift)
        {
            clearMultiSelection();
        }
        return true;
    }

    if (event.key == Key::PageUp)
    {
        clearMultiSelection();
        int listH = rect().height - 2;
        _selectedIndex = std::max(0, _selectedIndex - listH);
        scrollToSelected();
        return true;
    }

    if (event.key == Key::PageDown)
    {
        clearMultiSelection();
        int listH = rect().height - 2;
        _selectedIndex = std::min(static_cast<int>(_tracks.size()) - 1, _selectedIndex + listH);
        scrollToSelected();
        return true;
    }

    if (event.key == Key::Home || event.key == Key::Left)
    {
        clearMultiSelection();
        _selectedIndex = 0;
        scrollToSelected();
        return true;
    }

    if (event.key == Key::End || event.key == Key::Right)
    {
        clearMultiSelection();
        if (!_tracks.empty())
        {
            _selectedIndex = static_cast<int>(_tracks.size()) - 1;
        }
        scrollToSelected();
        return true;
    }

    if (event.key == Key::Enter)
    {
        if (_onPlay && !_tracks.empty())
        {
            _onPlay(_selectedIndex);
        }
        return true;
    }

    if (event.key == Key::Delete ||
        event.key == Key::Backspace ||
        (event.key == Key::Char && (event.ch == 'd' || event.ch == 'D')))
    {
        removeSelected();
        return true;
    }

    return false;
}

bool PlaylistView::handleMouse(ventty::MouseEvent const & event)
{
    auto const & r = rect();
    if (!r.contains(event.x, event.y))
    {
        return false;
    }

    using Button = ventty::MouseEvent::Button;
    using Action = ventty::MouseEvent::Action;

    // Scroll wheel
    if (event.button == Button::ScrollUp)
    {
        if (_selectedIndex > 0)
        {
            _selectedIndex--;
            scrollToSelected();
        }
        return true;
    }
    if (event.button == Button::ScrollDown)
    {
        if (_selectedIndex < static_cast<int>(_tracks.size()) - 1)
        {
            _selectedIndex++;
            scrollToSelected();
        }
        return true;
    }

    // Left click on list area
    if (event.button == Button::Left && event.action == Action::Press)
    {
        int listY = r.y + 2; // list starts after header + separator
        if (event.y >= listY)
        {
            int clickedRow = event.y - listY;
            int clickedIdx = _scrollOffset + clickedRow;
            if (clickedIdx >= 0 && clickedIdx < static_cast<int>(_tracks.size()))
            {
                if (_selectedIndex == clickedIdx && _onPlay)
                {
                    // Second click on same item: play
                    _onPlay(clickedIdx);
                }
                else
                {
                    clearMultiSelection();
                    _selectedIndex = clickedIdx;
                }
            }
        }
        return true;
    }

    return false;
}

void PlaylistView::scrollToSelected()
{
    int listH = rect().height - 2;
    if (listH <= 0) return;
    if (_selectedIndex < _scrollOffset)
    {
        _scrollOffset = _selectedIndex;
    }
    if (_selectedIndex >= _scrollOffset + listH)
    {
        _scrollOffset = _selectedIndex - listH + 1;
    }
}

} // namespace vtplayer
