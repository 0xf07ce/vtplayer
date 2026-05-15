// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Visualizer.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace vtplayer
{

/// Read-only tag inspector — renders the current track's TagLib properties
/// (metadata + audio properties + ReplayGain values) as a two-column list.
/// Not an audio-reactive visualizer; only re-reads the file when the playing
/// path changes.
class TagInfoView : public Visualizer
{
public:
    using Row = std::pair<std::string, std::string>; ///< (label, value)
    using Section = std::pair<std::string, std::vector<Row>>; ///< (heading, rows)

    void update(AudioEngine const & engine) override;
    void draw(ventty::Window & window, int x, int y, int w, int h) override;
    void setTheme(Theme const & theme) override { _theme = theme; }
    bool scrollBy(int delta) override;

private:
    void refresh(std::filesystem::path const & path);

    Theme _theme;
    std::filesystem::path _cachedPath;
    std::vector<Section> _sections;
    bool _hasFile = false;

    int _scroll = 0;          ///< first content row to render
    int _contentHeight = 0;   ///< total renderable rows (set during draw)
    int _viewportHeight = 0;  ///< visible rows (set during draw)
};

} // namespace vtplayer
