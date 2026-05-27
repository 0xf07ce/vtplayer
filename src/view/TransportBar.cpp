// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "TransportBar.h"

#include "../util/UnicodeNormalize.h"

#include <ventty/art/AsciiArt.h>
#include <ventty/core/Utf8.h>

#include <algorithm>
#include <cstdio>

namespace vtplayer
{

std::string TransportBar::formatTime(float seconds)
{
    if (seconds < 0.0f) seconds = 0.0f;
    int total = static_cast<int>(seconds);
    int m = total / 60;
    int s = total % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return buf;
}

void TransportBar::draw(ventty::Window & window)
{
    auto const & r = rect();
    if (r.height < 1) return;

    // Single row: bottom border of the surrounding box, with transport
    // controls (state, track name, progress, time, AG) overlaid on top.
    int y1 = r.y;
    ventty::Style baseBorder{_theme.border, _theme.transportBg};

    // Bottom border line
    window.putChar(r.x, y1, ventty::DOUBLE_BOX.bl, baseBorder);
    for (int x = r.x + 1; x < r.x + r.width - 1; ++x)
    {
        window.putChar(x, y1, ventty::DOUBLE_BOX.h, baseBorder);
    }
    window.putChar(r.x + r.width - 1, y1, ventty::DOUBLE_BOX.br, baseBorder);

    // Embed transport controls into the border line. The two indicator
    // slots are reserved unconditionally so toggling either mode doesn't
    // shift the track name to the right — when an indicator is off, the
    // existing double-line border glyph already drawn on that column shows
    // through.
    int cx = r.x + 2;
    ventty::Style indicatorStyle{_theme.transportStateFg, _theme.transportBg};

    // Shuffle slot (always reserved). Sits immediately to the left of the
    // repeat slot so the two render as a single label ("sR" / "sr" / "s")
    // when both modes are on.
    if (_shuffleMode)
    {
        window.drawText(cx, y1, "s", indicatorStyle);
    }
    cx++;

    // Repeat slot (always reserved). The play/pause/stop state is conveyed
    // by the play-time on the right, so this slot is reused for the repeat
    // mode: R = repeat-all, r = repeat-1.
    if (_repeatMode != RepeatMode::None)
    {
        char repeatGlyph = (_repeatMode == RepeatMode::One) ? 'r' : 'R';
        window.drawText(cx, y1, std::string(1, repeatGlyph), indicatorStyle);
    }
    cx += 2; // repeat slot + 1 col padding before the track name

    // Track name (display-width truncation; advance cx by display width, not byte size)
    if (!_trackName.empty())
    {
        int const maxNameW = r.width / 3;
        std::string name = " " + _trackName + " ";
        name = truncateToWidth(name, maxNameW, ".. ");
        window.drawText(cx, y1, name,
                        ventty::Style{_theme.transportTimeFg, _theme.transportBg});
        cx += ventty::stringWidth(name);
    }

    // Progress bar
    int timeLen = 13; // " MM:SS/MM:SS "
    int gainLen = _gainNormEnabled ? 11 : 0; // " RG:+99.9 " / " AG:+99.9 "
    int progressW = r.width - cx - timeLen - gainLen - 2;
    if (progressW > 4)
    {
        _progressX = cx;
        _progressW = _live ? 0 : progressW; // no seeking on a live stream

        if (_live)
        {
            // Centre the LIVE / BUFFERING label on the box's bottom border
            // (═) — no progress rail drawn, so the border shows through on
            // both sides of the label. Seeking stays disabled (_progressW = 0).
            std::string lbl = _buffering ? "\xE2\x97\x8B BUFFERING" // ○ BUFFERING
                                         : "\xE2\x97\x89 LIVE";    // ◉ LIVE
            int const lblW = ventty::stringWidth(lbl);
            int const lblX = cx + std::max(0, (progressW - lblW) / 2);
            window.drawText(lblX, y1, lbl,
                            ventty::Style{_theme.transportStateFg,
                                          _theme.transportBg, ventty::Attr::Bold});
        }
        else
        {
            float ratio = (_duration > 0.0f) ? (_position / _duration) : 0.0f;
            std::string bar = ventty::progressBar(progressW, ratio, ventty::PROGRESS_SMOOTH);
            window.drawText(cx, y1, bar,
                            ventty::Style{_theme.transportProgressFg, _theme.transportBg});
        }
        cx += progressW;
    }

    // Time display: elapsed only for a live stream (total is unknown).
    std::string timeStr = _live
        ? (" " + formatTime(_position) + " ")
        : (" " + formatTime(_position) + "/" + formatTime(_duration) + " ");
    window.drawText(cx, y1, timeStr,
                    ventty::Style{_theme.transportTimeFg, _theme.transportBg});
    cx += static_cast<int>(timeStr.size());

    // Gain-normalization indicator (only when enabled).
    // Label shows which source is driving the current gain: RG (ReplayGain
    // tag) or AG (runtime auto-gain). Source is set by AudioEngine on load().
    if (_gainNormEnabled)
    {
        char const * label =
            (_gainSource == GainSource::ReplayGain) ? "RG" : "AG";
        char buf[16];
        std::snprintf(buf, sizeof(buf), " %s:%+5.1f ", label, _gainDb);
        window.drawText(cx, y1, buf,
                        ventty::Style{_theme.transportStateFg, _theme.transportBg});
    }
}

float TransportBar::handleMouse(ventty::MouseEvent const & event)
{
    auto const & r = rect();
    if (!r.contains(event.x, event.y))
    {
        return -1.0f;
    }

    using Button = ventty::MouseEvent::Button;
    using Action = ventty::MouseEvent::Action;

    // Click on progress bar row to seek
    if (event.button == Button::Left && event.action == Action::Press
        && event.y == r.y && _progressW > 0 && _duration > 0.0f)
    {
        if (event.x >= _progressX && event.x < _progressX + _progressW)
        {
            float ratio = static_cast<float>(event.x - _progressX)
                        / static_cast<float>(_progressW);
            return ratio;
        }
    }

    return -1.0f;
}

} // namespace vtplayer
