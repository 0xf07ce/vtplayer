// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PlaylistView.h"

#include <ventty/art/AsciiArt.h>

#include <algorithm>
#include <cmath>

namespace vtplayer
{

using Key = ventty::KeyEvent::Key;

void PlaylistView::addTrack(TrackInfo const & track)
{
    _tracks.push_back(track);
}

void PlaylistView::removeSelected()
{
    if (_selectedIndex < 0 || _selectedIndex >= static_cast<int>(_tracks.size()))
    {
        return;
    }

    _tracks.erase(_tracks.begin() + _selectedIndex);

    if (_playingIndex == _selectedIndex)
    {
        _playingIndex = -1;
    }
    else if (_playingIndex > _selectedIndex)
    {
        _playingIndex--;
    }

    if (_selectedIndex >= static_cast<int>(_tracks.size()) && !_tracks.empty())
    {
        _selectedIndex = static_cast<int>(_tracks.size()) - 1;
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
    _tracks.clear();
    _selectedIndex = 0;
    _scrollOffset = 0;
    _playingIndex = -1;
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
    if (!_tracks.empty())
    {
        header += " (" + std::to_string(_tracks.size()) + ")";
    }
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
        bool selected = (idx == _selectedIndex) && isFocused();
        bool playing = (idx == _playingIndex);

        Color fg = _theme.playlistFg;
        Color bg = selected ? _theme.playlistSelBg : _theme.playlistBg;
        if (selected)
        {
            fg = _theme.playlistSelFg;
        }
        if (playing && !selected)
        {
            fg = _theme.playlistPlayingFg;
        }

        ventty::Style style{fg, bg};
        window.fill(r.x, y, r.width - 1, 1, U' ', style);

        // Index number
        char numBuf[8];
        std::snprintf(numBuf, sizeof(numBuf), "%2d.", idx + 1);
        ventty::Style indexStyle{selected ? fg : _theme.playlistIndexFg, bg};
        window.drawText(r.x + 1, y, numBuf, indexStyle);

        // Playing indicator
        if (playing)
        {
            window.drawText(r.x, y, "\xE2\x96\xB6", // ▶
                            ventty::Style{_theme.playlistPlayingFg, bg});
        }

        // Track name
        std::string name = track.title;
        int durLen = 6; // " MM:SS"
        int maxNameW = contentW - 5 - durLen; // 4 for index, 1 padding
        if (static_cast<int>(name.size()) > maxNameW)
        {
            name = name.substr(0, maxNameW - 2) + "..";
        }
        window.drawText(r.x + 5, y, name, style);

        // Duration (right-aligned)
        std::string dur = formatDuration(track.duration);
        int durX = r.x + r.width - 2 - static_cast<int>(dur.size());
        ventty::Style durStyle{selected ? fg : _theme.playlistDurationFg, bg};
        window.drawText(durX, y, dur, durStyle);
    }
}

bool PlaylistView::handleKey(ventty::KeyEvent const & event)
{
    if (event.key == Key::Up)
    {
        if (_selectedIndex > 0)
        {
            _selectedIndex--;
            scrollToSelected();
        }
        return true;
    }

    if (event.key == Key::Down)
    {
        if (_selectedIndex < static_cast<int>(_tracks.size()) - 1)
        {
            _selectedIndex++;
            scrollToSelected();
        }
        return true;
    }

    if (event.key == Key::PageUp)
    {
        int listH = rect().height - 2;
        _selectedIndex = std::max(0, _selectedIndex - listH);
        scrollToSelected();
        return true;
    }

    if (event.key == Key::PageDown)
    {
        int listH = rect().height - 2;
        _selectedIndex = std::min(static_cast<int>(_tracks.size()) - 1, _selectedIndex + listH);
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
