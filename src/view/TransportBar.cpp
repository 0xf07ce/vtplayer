// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "TransportBar.h"

#include <ventty/art/AsciiArt.h>
#include <ventty/core/Utf8.h>

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
    if (r.height < 2) return;

    // --- Row 1: Transport info + progress bar ---
    int y1 = r.y;
    ventty::Style baseBorder{_theme.border, _theme.transportBg};
    ventty::Style baseStyle{_theme.transportFg, _theme.transportBg};

    // Bottom border line
    window.putChar(r.x, y1, ventty::DOUBLE_BOX.bl, baseBorder);
    for (int x = r.x + 1; x < r.x + r.width - 1; ++x)
    {
        window.putChar(x, y1, ventty::DOUBLE_BOX.h, baseBorder);
    }
    window.putChar(r.x + r.width - 1, y1, ventty::DOUBLE_BOX.br, baseBorder);

    // Embed transport controls into the border line
    int cx = r.x + 2;

    // State icon
    std::string stateIcon;
    switch (_state)
    {
    case PlayState::Playing: stateIcon = "\xE2\x96\xB6"; break; // ▶
    case PlayState::Paused:  stateIcon = "\xE2\x8F\xB8"; break; // ⏸
    default:                 stateIcon = "\xE2\x8F\xB9"; break; // ⏹
    }
    window.drawText(cx, y1, stateIcon,
                    ventty::Style{_theme.transportStateFg, _theme.transportBg});
    cx += 2;

    // Track name (truncated)
    if (!_trackName.empty())
    {
        std::string name = " " + _trackName + " ";
        int maxNameLen = r.width / 3;
        if (static_cast<int>(name.size()) > maxNameLen)
        {
            name = name.substr(0, maxNameLen - 2) + ".. ";
        }
        window.drawText(cx, y1, name,
                        ventty::Style{_theme.transportTimeFg, _theme.transportBg});
        cx += static_cast<int>(name.size());
    }

    // Progress bar
    int timeLen = 13; // " MM:SS/MM:SS "
    int agLen = _autoGainEnabled ? 11 : 0; // " AG:+99.9 "
    int progressW = r.width - cx - timeLen - agLen - 2;
    if (progressW > 4)
    {
        _progressX = cx;
        _progressW = progressW;

        float ratio = (_duration > 0.0f) ? (_position / _duration) : 0.0f;
        std::string bar = ventty::progressBar(progressW, ratio, ventty::PROGRESS_SMOOTH);
        window.drawText(cx, y1, bar,
                        ventty::Style{_theme.transportProgressFg, _theme.transportBg});
        cx += progressW;
    }

    // Time display
    std::string timeStr = " " + formatTime(_position) + "/" + formatTime(_duration) + " ";
    window.drawText(cx, y1, timeStr,
                    ventty::Style{_theme.transportTimeFg, _theme.transportBg});
    cx += static_cast<int>(timeStr.size());

    // Auto-gain indicator (only when enabled)
    if (_autoGainEnabled)
    {
        char agBuf[16];
        std::snprintf(agBuf, sizeof(agBuf), " AG:%+5.1f ", _autoGainDb);
        window.drawText(cx, y1, agBuf,
                        ventty::Style{_theme.transportStateFg, _theme.transportBg});
    }

    // --- Row 2: Function key hints ---
    int y2 = r.y + 1;
    window.fill(r.x, y2, r.width, 1, U' ',
                ventty::Style{_theme.transportFnLabelFg, _theme.transportBg});

    struct FnHint
    {
        std::string key;
        std::string label;
    };

    FnHint hints[] = {
        {"Space", "Play/Pause"},
        {"S", "Stop"},
        {"N/P", "Next/Prev"},
        {"</>", "Seek"},
        {"G", "AutoGain"},
        {"V", "Visualizer"},
        {"Q", "Quit"},
    };

    int hx = r.x + 1;
    for (auto const & hint : hints)
    {
        if (hx + static_cast<int>(hint.key.size() + hint.label.size()) + 3 > r.x + r.width)
        {
            break;
        }
        window.drawText(hx, y2, hint.key,
                        ventty::Style{_theme.transportFnKeyFg, _theme.transportBg});
        hx += static_cast<int>(ventty::stringWidth(hint.key));

        std::string label = ":" + hint.label + " ";
        window.drawText(hx, y2, label,
                        ventty::Style{_theme.transportFnLabelFg, _theme.transportBg});
        hx += static_cast<int>(label.size());
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
