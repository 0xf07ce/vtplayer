// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Visualizer.h"

namespace vtplayer
{

    /// Slot-5 visualizer: a spinning LP record / CD.
    /// A fully filled disc (center label + spindle hole) where grooves are
    /// faint radial color bands and rotation is conveyed purely by a
    /// rotating angular color sheen — no overlaid marker glyph.
    /// Rotation is frame-driven (one update() == one frame, ~60fps main
    /// loop). Audio reactivity (spectrum / debug-bar data) is planned but
    /// not yet wired.
    class VinylVis : public Visualizer
    {
    public:
        void update(AudioEngine const &engine) override;
        void draw(ventty::Window &window, int x, int y, int w, int h) override;
        void setTheme(Theme const &theme) override { _theme = theme; }

    private:
        Theme _theme;
        double _angle = 0.0;  ///< current disc rotation, radians
    };

} // namespace vtplayer
