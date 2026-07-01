// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <ventty/core/Color.h>

#include <string>
#include <string_view>

namespace vtplayer
{
    using namespace ventty;

    struct Theme
    {
        bool isLight = false;

        // Global
        Color background{0x12, 0x10, 0x1A};
        Color foreground{0xB0, 0xA8, 0xC0};
        Color border{0x80, 0x70, 0x90};
        Color borderDim{0x3A, 0x32, 0x45};

        // Header bar
        Color headerBg{0x16, 0x13, 0x1F};
        Color headerFg{0x80, 0x70, 0x90};
        Color headerTitleFg{0xC8, 0xA2, 0xD0};
        Color headerTrackFg{0xE0, 0xD8, 0xEC};

        // File browser
        Color browserBg{0x12, 0x10, 0x1A};
        Color browserFg{0xB0, 0xA8, 0xC0};
        Color browserDirFg{0x9A, 0xAF, 0xC8};
        Color browserAudioFg{0x8B, 0xAA, 0x8B};
        Color browserSelBg{0x2A, 0x22, 0x35};
        Color browserSelFg{0xE0, 0xD8, 0xEC};
        Color browserHeaderFg{0x8B, 0x5C, 0x8B};

        // Library tree — four muted tones so the four-level hierarchy
        // (Grouping → Artist → Album → Track) reads at a glance while
        // staying in vtplayer's tone: warm cream groupings → amber artists
        // → muted periwinkle albums → sage tracks. Two extra tints set
        // the synthetic group labels apart: `(null)` (greyed lilac, "tag
        // is missing") and `(stream)` (faint cyan, "this isn't a tagged
        // file"), applied wherever they appear (depth 0 grouping, depth 1
        // artist).
        Color libraryGroupingFg{0xE6, 0xC8, 0x82};
        Color libraryArtistFg{0xC9, 0xA0, 0x62};
        Color libraryAlbumFg{0x8C, 0x93, 0xC2};
        Color libraryTrackFg{0x8E, 0x9A, 0x77};
        Color libraryNullFg{0x80, 0x76, 0x8C};
        Color libraryStreamFg{0x6A, 0xA8, 0xC9};

        // Play queue
        Color playQueueBg{0x12, 0x10, 0x1A};
        Color playQueueFg{0xB0, 0xA8, 0xC0};
        Color playQueueSelBg{0x2A, 0x22, 0x35};
        Color playQueueSelFg{0xE0, 0xD8, 0xEC};
        Color playQueuePlayingFg{0xA0, 0x88, 0xB0};
        Color playQueueIndexFg{0x3A, 0x32, 0x45};
        Color playQueueDurationFg{0x3A, 0x32, 0x45};
        Color playQueueArtistFg{0x60, 0x55, 0x70};
        Color playQueueHeaderFg{0x8B, 0x5C, 0x8B};

        // Transport bar
        Color transportBg{0x12, 0x10, 0x1A};
        Color transportFg{0x80, 0x70, 0x90};
        Color transportProgressFg{0x9A, 0x6E, 0xAA};
        Color transportTimeFg{0xE0, 0xD8, 0xEC};
        Color transportStateFg{0x8B, 0xAA, 0x8B};
        Color transportFnKeyFg{0xC8, 0xA2, 0xD0};
        Color transportFnLabelFg{0x66, 0x5C, 0x70};

        // Visualizer — spectrum gradient runs vertically along each bar:
        //   bottom row = visBarLow (deep purple) → top row = visBarHigh (light purple).
        // visBarMid is the midpoint stop and is also reused by the oscilloscope trace.
        // visTrailFg is the brightest shade of the fade-to-black trail.
        Color visBarLow{0x4A, 0x2A, 0x88};
        Color visBarMid{0x8A, 0x6E, 0xC0};
        Color visBarHigh{0xE0, 0xC8, 0xF5};
        Color visTrailFg{0x4D, 0x4D, 0x4D};
        Color visLabelFg{0x3A, 0x32, 0x45};

        // Matrix rain — head/body/tail green gradient.
        Color matrixHead{0xB8, 0xFF, 0xB8};
        Color matrixBody{0x00, 0xCC, 0x00};
        Color matrixTail{0x0A, 0x3A, 0x0A};

        // Separator
        Color separatorFg{0x80, 0x70, 0x90};

        static Theme retro()
        {
            return Theme{};
        }

        static Theme dark()
        {
            return Theme{};
        }

        static Theme light()
        {
            Theme theme;
            theme.isLight = true;

            // Global
            theme.background = Color{0xF4, 0xF3, 0xF6};
            theme.foreground = Color{0x3E, 0x3A, 0x44};
            theme.border = Color{0xB9, 0xB2, 0xC1};
            theme.borderDim = Color{0xD9, 0xD5, 0xDE};

            // Header bar
            theme.headerBg = Color{0xEA, 0xE7, 0xEE};
            theme.headerFg = Color{0x7A, 0x72, 0x83};
            theme.headerTitleFg = Color{0x9A, 0x4F, 0x96};
            theme.headerTrackFg = Color{0x2F, 0x2B, 0x35};

            // File browser
            theme.browserBg = Color{0xF4, 0xF3, 0xF6};
            theme.browserFg = Color{0x3E, 0x3A, 0x44};
            theme.browserDirFg = Color{0x4C, 0x6E, 0x91};
            theme.browserAudioFg = Color{0x4B, 0x7A, 0x5B};
            theme.browserSelBg = Color{0xDF, 0xD7, 0xE8};
            theme.browserSelFg = Color{0x24, 0x20, 0x29};
            theme.browserHeaderFg = Color{0xA8, 0x4A, 0x92};

            // Library tree
            theme.libraryGroupingFg = Color{0x9A, 0x65, 0x19};
            theme.libraryArtistFg = Color{0x8E, 0x5A, 0x34};
            theme.libraryAlbumFg = Color{0x62, 0x5C, 0x9A};
            theme.libraryTrackFg = Color{0x5C, 0x6F, 0x45};
            theme.libraryNullFg = Color{0x8E, 0x86, 0x95};
            theme.libraryStreamFg = Color{0x3C, 0x7D, 0x9B};

            // Play queue
            theme.playQueueBg = Color{0xF1, 0xEF, 0xF4};
            theme.playQueueFg = Color{0x3E, 0x3A, 0x44};
            theme.playQueueSelBg = Color{0xDD, 0xD3, 0xE7};
            theme.playQueueSelFg = Color{0x24, 0x20, 0x29};
            theme.playQueuePlayingFg = Color{0x8D, 0x55, 0xA3};
            theme.playQueueIndexFg = Color{0xAA, 0xA3, 0xB2};
            theme.playQueueDurationFg = Color{0x8F, 0x88, 0x98};
            theme.playQueueArtistFg = Color{0x72, 0x6B, 0x7C};
            theme.playQueueHeaderFg = Color{0xA8, 0x4A, 0x92};

            // Transport bar
            theme.transportBg = Color{0xEA, 0xE7, 0xEE};
            theme.transportFg = Color{0x72, 0x6B, 0x7C};
            theme.transportProgressFg = Color{0xA7, 0x55, 0xB4};
            theme.transportTimeFg = Color{0x2F, 0x2B, 0x35};
            theme.transportStateFg = Color{0x4B, 0x7A, 0x5B};
            theme.transportFnKeyFg = Color{0x9A, 0x4F, 0x96};
            theme.transportFnLabelFg = Color{0x8F, 0x88, 0x98};

            // Visualizer
            theme.visBarLow = Color{0x75, 0x4C, 0xB3};
            theme.visBarMid = Color{0xB0, 0x6F, 0xC4};
            theme.visBarHigh = Color{0xE6, 0x9C, 0xC7};
            theme.visTrailFg = Color{0xB0, 0xAA, 0xB8};
            theme.visLabelFg = Color{0x9B, 0x93, 0xA4};

            // Matrix rain
            theme.matrixHead = Color{0x2F, 0x8F, 0x51};
            theme.matrixBody = Color{0x4B, 0xA8, 0x63};
            theme.matrixTail = Color{0x9D, 0xC8, 0xA9};

            // Separator
            theme.separatorFg = Color{0xA6, 0x9F, 0xAE};

            return theme;
        }

        static Theme fromName(std::string_view name)
        {
            if (name == "light")
                return light();
            return dark();
        }

    };

} // namespace vtplayer
