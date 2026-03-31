// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "../audio/AudioEngine.h"
#include "../view/Theme.h"

#include <ventty/widget/Widget.h>

namespace vtamp
{

    /// Abstract interface for a visualizer that renders audio data
    /// into a rectangular region of a terminal window.
    class Visualizer
    {
    public:
        virtual ~Visualizer() = default;

        /// Feed new audio data from the engine.
        virtual void update(AudioEngine const &engine) = 0;

        /// Draw into the given window region.
        virtual void draw(ventty::Window &window, int x, int y, int w, int h) = 0;

        /// Set the color theme.
        virtual void setTheme(Theme const &theme) = 0;
    };

} // namespace vtamp
