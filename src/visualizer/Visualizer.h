// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "../audio/AudioEngine.h"
#include "../view/Theme.h"

#include <ventty/widget/Widget.h>

namespace vtplayer
{

    /// Abstract interface for a visualizer that renders audio data
    /// into a rectangular region of a terminal window.
    class Visualizer
    {
    public:
        /// Sentinel for preferredFps(): this visualizer is static and needs
        /// no periodic wake — the run loop pages it like the Browser screen
        /// (repaint only on input / playback ticks), so idle CPU drops to ~0.
        static constexpr int kStaticFps = -1;

        virtual ~Visualizer() = default;

        /// Feed new audio data from the engine.
        virtual void update(AudioEngine const &engine) = 0;

        /// Draw into the given window region.
        virtual void draw(ventty::Window &window, int x, int y, int w, int h) = 0;

        /// Set the color theme.
        virtual void setTheme(Theme const &theme) = 0;

        /// Optional vertical scroll input from arrow keys / mouse wheel.
        /// Default is a no-op for audio-reactive visualizers; static views
        /// (e.g. TagInfoView) override. Returns true if the input was used.
        virtual bool scrollBy(int /*delta*/) { return false; }

        /// Preferred run-loop wake cadence while this visualizer is on screen:
        ///   0           → use the global [visualizer] fps setting (default,
        ///                  for audio-reactive visualizers that stay smooth).
        ///   >0          → cap the wake rate at this many FPS regardless of
        ///                  the global setting (animations fine at lower rates).
        ///   kStaticFps  → static view, no periodic wake (see kStaticFps).
        /// The global setting always acts as a ceiling for the >0 case.
        virtual int preferredFps() const { return 0; }
    };

} // namespace vtplayer
