// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Theme.h"

#include <ventty/widget/Widget.h>

#include <string>

namespace vtamp
{

class HeaderBar : public ventty::Widget
{
public:
    void setTheme(Theme const & theme) { _theme = theme; }
    void setTrackName(std::string const & name) { _trackName = name; }
    void setTrackTime(std::string const & time) { _trackTime = time; }
    void setPlaying(bool playing) { _playing = playing; }

    void draw(ventty::Window & window) override;

private:
    Theme _theme;
    std::string _trackName;
    std::string _trackTime;
    bool _playing = false;
};

} // namespace vtamp
