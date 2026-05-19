// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Theme.h"
#include "../audio/AudioEngine.h"

#include <ventty/widget/Widget.h>

#include <string>

namespace vtplayer
{

enum class RepeatMode
{
    None,
    All,
    One,
};

class TransportBar : public ventty::Widget
{
public:
    void setTheme(Theme const & theme) { _theme = theme; }
    void setState(PlayState state) { _state = state; }
    void setRepeatMode(RepeatMode mode) { _repeatMode = mode; }
    void setTrackName(std::string const & name) { _trackName = name; }
    void setPosition(float pos) { _position = pos; }
    void setDuration(float dur) { _duration = dur; }
    /// Live stream: show a LIVE indicator + elapsed time instead of a
    /// progress bar and total duration.
    void setLive(bool live) { _live = live; }
    void setVolume(float vol) { _volume = vol; }
    void setGainNorm(bool enabled, float gainDb, GainSource source)
    {
        _gainNormEnabled = enabled;
        _gainDb = gainDb;
        _gainSource = source;
    }

    void draw(ventty::Window & window) override;

    /// Returns seek position (0..1) if click is on progress bar, -1 otherwise
    float handleMouse(ventty::MouseEvent const & event);

private:
    static std::string formatTime(float seconds);

    Theme _theme;
    PlayState _state = PlayState::Stopped;
    RepeatMode _repeatMode = RepeatMode::None;
    std::string _trackName;
    float _position = 0.0f;
    float _duration = 0.0f;
    bool _live = false;
    float _volume = 1.0f;
    bool _gainNormEnabled = false;
    float _gainDb = 0.0f;
    GainSource _gainSource = GainSource::None;

    // Progress bar position (set during draw for mouse hit testing)
    int _progressX = 0;
    int _progressW = 0;
};

} // namespace vtplayer
