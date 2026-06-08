// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PlayQueueView.h"

#include "../util/UnicodeNormalize.h"

#include <ventty/art/AsciiArt.h>
#include <ventty/core/Utf8.h>

#include <algorithm>
#include <cmath>

namespace vtplayer
{

using Key = ventty::KeyEvent::Key;

void PlayQueueView::addTrack(TrackInfo const & track)
{
    _queue.addTrack(track);
    notifyContentsChanged();
}

void PlayQueueView::insertTrack(int idx, TrackInfo const & track)
{
    int const sz = _queue.size();
    if (idx < 0) idx = 0;
    if (idx > sz) idx = sz;
    _queue.insertTrack(idx, track);
    if (_playingIndex >= idx) _playingIndex++;
    if (_selectedIndex >= idx) _selectedIndex++;
    clearMultiSelection();
    notifyContentsChanged();
}

void PlayQueueView::removeSelected()
{
    if (_queue.empty()) return;

    std::set<int> targets = _multiSelected;
    if (_selectedIndex >= 0 && _selectedIndex < _queue.size())
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
        if (idx < 0 || idx >= _queue.size()) continue;
        _queue.removeAt(idx);
        if (!removingPlaying && _playingIndex > idx) _playingIndex--;
    }

    if (removingPlaying) _playingIndex = -1;

    if (_queue.empty())
    {
        _selectedIndex = 0;
    }
    else
    {
        _selectedIndex = std::min(firstRemoved, _queue.size() - 1);
    }

    clearMultiSelection();
    scrollToSelected();
    notifyContentsChanged();

    if (removingPlaying && _onPlayingRemoved)
    {
        _onPlayingRemoved();
    }
}

void PlayQueueView::moveSelectionUp()
{
    if (_selectedIndex < 0 || _selectedIndex >= _queue.size()) return;

    // Contiguous block = multi-selection span unioned with the cursor.
    int lo = _selectedIndex;
    int hi = _selectedIndex;
    if (!_multiSelected.empty())
    {
        lo = std::min(lo, *_multiSelected.begin());
        hi = std::max(hi, *_multiSelected.rbegin());
    }
    if (lo <= 0) return;

    // Slide the row above the block down to the block's tail.
    for (int i = lo - 1; i < hi; ++i) _queue.swap(i, i + 1);

    if (_playingIndex == lo - 1) _playingIndex = hi;
    else if (_playingIndex >= lo && _playingIndex <= hi) _playingIndex--;

    shiftSelection(-1);
    scrollToSelected();
    notifyContentsChanged();
}

void PlayQueueView::moveSelectionDown()
{
    if (_selectedIndex < 0 || _selectedIndex >= _queue.size()) return;

    int lo = _selectedIndex;
    int hi = _selectedIndex;
    if (!_multiSelected.empty())
    {
        lo = std::min(lo, *_multiSelected.begin());
        hi = std::max(hi, *_multiSelected.rbegin());
    }
    if (hi >= _queue.size() - 1) return;

    // Slide the row below the block up to the block's head.
    for (int i = hi + 1; i > lo; --i) _queue.swap(i, i - 1);

    if (_playingIndex == hi + 1) _playingIndex = lo;
    else if (_playingIndex >= lo && _playingIndex <= hi) _playingIndex++;

    shiftSelection(+1);
    scrollToSelected();
    notifyContentsChanged();
}

void PlayQueueView::shiftSelection(int delta)
{
    _selectedIndex += delta;
    if (!_multiSelected.empty())
    {
        std::set<int> moved;
        for (int idx : _multiSelected) moved.insert(idx + delta);
        _multiSelected = std::move(moved);
    }
    if (_selectionAnchor >= 0) _selectionAnchor += delta;
}

void PlayQueueView::clear()
{
    bool const hadPlaying = (_playingIndex >= 0);
    _queue.clear();
    _selectedIndex = 0;
    _scrollOffset = 0;
    _playingIndex = -1;
    clearMultiSelection();
    notifyContentsChanged();
    if (hadPlaying && _onPlayingRemoved)
    {
        _onPlayingRemoved();
    }
}

void PlayQueueView::setTracks(std::vector<TrackInfo> tracks)
{
    bool const hadPlaying = (_playingIndex >= 0);
    _queue.setTracks(std::move(tracks));
    _selectedIndex = 0;
    _scrollOffset = 0;
    _playingIndex = -1;
    clearMultiSelection();
    notifyContentsChanged();
    if (hadPlaying && _onPlayingRemoved)
    {
        _onPlayingRemoved();
    }
}

void PlayQueueView::clearMultiSelection()
{
    _multiSelected.clear();
    _selectionAnchor = -1;
}

void PlayQueueView::selectAll()
{
    _multiSelected.clear();
    for (int i = 0; i < _queue.size(); ++i)
    {
        _multiSelected.insert(i);
    }
    _selectionAnchor = _selectedIndex;
}

void PlayQueueView::extendSelectionTo(int newIndex)
{
    if (_selectionAnchor < 0) _selectionAnchor = _selectedIndex;
    int const lo = std::min(_selectionAnchor, newIndex);
    int const hi = std::max(_selectionAnchor, newIndex);
    _multiSelected.clear();
    for (int i = lo; i <= hi; ++i) _multiSelected.insert(i);
}

void PlayQueueView::onFocusChanged()
{
    if (!isFocused())
    {
        clearMultiSelection();
    }
}

void PlayQueueView::setSelectedIndex(int idx)
{
    _selectedIndex = std::clamp(idx, 0, std::max(0, _queue.size() - 1));
    scrollToSelected();
}

TrackInfo const * PlayQueueView::selectedTrack() const
{
    return _queue.at(_selectedIndex);
}

TrackInfo const * PlayQueueView::track(int idx) const
{
    return _queue.at(idx);
}

std::vector<TrackInfo> PlayQueueView::selectedTracks() const
{
    std::set<int> idxs = _multiSelected;
    if (_selectedIndex >= 0 && _selectedIndex < _queue.size())
        idxs.insert(_selectedIndex);

    std::vector<TrackInfo> out;
    out.reserve(idxs.size());
    for (int i : idxs)
        if (auto const * t = _queue.at(i)) out.push_back(*t);
    return out;
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

void PlayQueueView::draw(ventty::Window & window)
{
    auto const & r = rect();
    auto const & tracks = _queue.tracks();

    // Right border
    for (int y = 0; y < r.height; ++y)
    {
        window.putChar(r.x + r.width - 1, r.y + y, ventty::DOUBLE_BOX.v,
                       ventty::Style{_theme.border, _theme.playQueueBg});
    }

    // Header
    ventty::Style headerStyle{_theme.playQueueHeaderFg, _theme.playQueueBg, ventty::Attr::Bold};
    window.fill(r.x, r.y, r.width - 1, 1, U' ', headerStyle);

    // Two leading spaces: the inter-panel separator overwrites this column's
    // first cell, so the second space is what actually shows before the text —
    // matching the " <text>" gap the left panel renders after its border.
    std::string header = "  " + _title;
    if (!tracks.empty())
    {
        header += " (" + std::to_string(tracks.size()) + ")";
    }
    header = truncateToWidth(header, r.width - 1, "...");
    window.drawText(r.x, r.y, header, headerStyle);

    // Separator
    ventty::Style sepStyle{_theme.border, _theme.playQueueBg};
    for (int x = r.x; x < r.x + r.width - 1; ++x)
    {
        window.putChar(x, r.y + 1, ventty::HR_THIN, sepStyle);
    }

    // Track list
    int listH = r.height - 2;
    int contentW = r.width - 2; // right border + padding

    // Layout anchors for the title/artist/duration band.
    int const titleStart = r.x + 5;            // border(1) + idx(4)
    int const durLen     = 5;                  // "MM:SS" / "--:--"
    int const durX       = r.x + r.width - 2 - durLen;
    int const usable     = std::max(0, durX - titleStart - 1);
    int const maxArtistW = std::max(0, usable - 8 - 1); // keep ≥8 cells for title

    for (int i = 0; i < listH; ++i)
    {
        int idx = _scrollOffset + i;
        int y = r.y + 2 + i;

        if (idx >= static_cast<int>(tracks.size()))
        {
            window.fill(r.x, y, r.width - 1, 1, U' ',
                        ventty::Style{_theme.playQueueFg, _theme.playQueueBg});
            continue;
        }

        auto const & track = tracks[idx];
        bool const cursor = (idx == _selectedIndex) && isFocused();
        bool const multi  = isFocused() && _multiSelected.count(idx) > 0;
        bool const playing = (idx == _playingIndex);

        Color fg;
        Color bg = (cursor || multi) ? _theme.playQueueSelBg : _theme.playQueueBg;
        if (cursor)
        {
            fg = _theme.playQueueSelFg;
        }
        else if (multi)
        {
            fg = _theme.playQueueFg;
        }
        else if (playing)
        {
            fg = _theme.playQueuePlayingFg;
        }
        else
        {
            fg = _theme.playQueueFg;
        }

        ventty::Style style{fg, bg};
        window.fill(r.x, y, r.width - 1, 1, U' ', style);

        // Prefix: queue index, or ▶ for the currently-playing row.
        // Drawn at r.x + 1; 3 cells wide to align with the "%2d." format.
        if (playing)
        {
            Color arrowFg = cursor ? _theme.playQueueSelFg : _theme.playQueuePlayingFg;
            window.drawText(r.x + 1, y, " \xE2\x96\xB6 ", // " ▶ "
                            ventty::Style{arrowFg, bg});
        }
        else
        {
            char numBuf[8];
            std::snprintf(numBuf, sizeof(numBuf), "%2d.", idx + 1);
            ventty::Style indexStyle{cursor ? fg : _theme.playQueueIndexFg, bg};
            window.drawText(r.x + 1, y, numBuf, indexStyle);
        }

        // Title + artist band (per-row right-aligned artist):
        //   - artist text ends 1 cell before duration (right-anchored);
        //   - title fills from titleStart up to artist start - 1;
        //   - if title would overflow that space, artist is omitted on this
        //     row and title takes over the full band.
        std::string const & artist = track.artist;
        int const titleW   = ventty::stringWidth(track.title);
        int       artistW  = std::min(ventty::stringWidth(artist), maxArtistW);
        int const titleMax = (artistW > 0) ? (usable - artistW - 1) : usable;

        if (artist.empty() || artistW == 0 || titleW > titleMax)
        {
            std::string name = truncateToWidth(track.title, usable);
            window.drawText(titleStart, y, name, style);
        }
        else
        {
            window.drawText(titleStart, y, track.title, style);
            std::string artistCut = truncateToWidth(artist, artistW);
            int const renderedW = ventty::stringWidth(artistCut);
            int const artistX   = durX - 1 - renderedW;
            ventty::Style artistStyle{cursor ? fg : _theme.playQueueArtistFg, bg};
            window.drawText(artistX, y, artistCut, artistStyle);
        }

        // Duration (right-aligned)
        std::string const dur = formatDuration(track.duration);
        ventty::Style durStyle{cursor ? fg : _theme.playQueueDurationFg, bg};
        window.drawText(durX, y, dur, durStyle);
    }
}

bool PlayQueueView::handleKey(ventty::KeyEvent const & event)
{
    // Ctrl+A: select all tracks.
    if (event.key == Key::Char && event.ctrl &&
        (event.ch == 'a' || event.ch == 'A' || event.ch == 1))
    {
        if (!_queue.empty()) selectAll();
        return true;
    }

    // Ctrl+Up / Ctrl+Down are reorder shortcuts handled by Application's global
    // key path; swallow them here so they don't also move the cursor.
    if (event.ctrl && (event.key == Key::Up || event.key == Key::Down))
        return true;

    // Shift+Left / Shift+Right reorder the selected block (Left=up, Right=down).
    if (event.key == Key::Left && event.shift)
    {
        moveSelectionUp();
        return true;
    }
    if (event.key == Key::Right && event.shift)
    {
        moveSelectionDown();
        return true;
    }

    // Left mirrors Up, Right mirrors Down for plain cursor movement.
    if (event.key == Key::Up || (event.key == Key::Left && !event.shift))
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

    if (event.key == Key::Down || (event.key == Key::Right && !event.shift))
    {
        if (_selectedIndex < _queue.size() - 1)
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
        _selectedIndex = std::min(_queue.size() - 1, _selectedIndex + listH);
        scrollToSelected();
        return true;
    }

    if (event.key == Key::Home)
    {
        clearMultiSelection();
        _selectedIndex = 0;
        scrollToSelected();
        return true;
    }

    if (event.key == Key::End)
    {
        clearMultiSelection();
        if (!_queue.empty())
        {
            _selectedIndex = _queue.size() - 1;
        }
        scrollToSelected();
        return true;
    }

    if (event.key == Key::Enter)
    {
        if (_onPlay && !_queue.empty())
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

bool PlayQueueView::handleMouse(ventty::MouseEvent const & event)
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
        if (_selectedIndex < _queue.size() - 1)
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
            if (clickedIdx >= 0 && clickedIdx < _queue.size())
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

void PlayQueueView::focusPlayingTrack()
{
    if (_playingIndex < 0 || _playingIndex >= _queue.size())
        return;
    _selectedIndex = _playingIndex;
    int const listH = rect().height - 2;
    int const maxOffset = std::max(0, _queue.size() - std::max(1, listH));
    _scrollOffset = std::min(_playingIndex, maxOffset);
}

void PlayQueueView::scrollToSelected()
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
