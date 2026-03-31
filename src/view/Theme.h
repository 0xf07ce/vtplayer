// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <ventty/core/Color.h>

#include <string>
#include <string_view>
#include <unordered_map>

namespace vtamp
{
    using namespace ventty;

    struct Theme
    {
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

        // Playlist
        Color playlistBg{0x12, 0x10, 0x1A};
        Color playlistFg{0xB0, 0xA8, 0xC0};
        Color playlistSelBg{0x2A, 0x22, 0x35};
        Color playlistSelFg{0xE0, 0xD8, 0xEC};
        Color playlistPlayingFg{0xA0, 0x88, 0xB0};
        Color playlistIndexFg{0x3A, 0x32, 0x45};
        Color playlistDurationFg{0x3A, 0x32, 0x45};
        Color playlistHeaderFg{0x8B, 0x5C, 0x8B};

        // Transport bar
        Color transportBg{0x12, 0x10, 0x1A};
        Color transportFg{0x80, 0x70, 0x90};
        Color transportProgressFg{0x9A, 0x6E, 0xAA};
        Color transportTimeFg{0xE0, 0xD8, 0xEC};
        Color transportStateFg{0x8B, 0xAA, 0x8B};
        Color transportFnKeyFg{0xC8, 0xA2, 0xD0};
        Color transportFnLabelFg{0x66, 0x5C, 0x70};

        // Visualizer
        Color visBarLow{0x6A, 0x8A, 0x6A};
        Color visBarMid{0xB0, 0x98, 0xC0};
        Color visBarHigh{0xC0, 0x70, 0x88};
        Color visLabelFg{0x3A, 0x32, 0x45};

        // Separator
        Color separatorFg{0x80, 0x70, 0x90};

        static Theme retro()
        {
            return Theme{};
        }

        /// Apply color overrides from key-value map.
        /// Keys match INI field names under [theme], values are "#RRGGBB" hex strings.
        void applyColors(std::unordered_map<std::string, std::string> const &colors)
        {
            auto set = [&](std::string const &key, Color &target)
            {
                auto it = colors.find(key);
                if (it != colors.end() && !it->second.empty())
                {
                    target = Color::fromHex(it->second);
                }
            };

            // Global
            set("background", background);
            set("foreground", foreground);
            set("border", border);
            set("border_dim", borderDim);

            // Header
            set("header_bg", headerBg);
            set("header_fg", headerFg);
            set("header_title_fg", headerTitleFg);
            set("header_track_fg", headerTrackFg);

            // File browser
            set("browser_bg", browserBg);
            set("browser_fg", browserFg);
            set("browser_dir_fg", browserDirFg);
            set("browser_audio_fg", browserAudioFg);
            set("browser_sel_bg", browserSelBg);
            set("browser_sel_fg", browserSelFg);
            set("browser_header_fg", browserHeaderFg);

            // Playlist
            set("playlist_bg", playlistBg);
            set("playlist_fg", playlistFg);
            set("playlist_sel_bg", playlistSelBg);
            set("playlist_sel_fg", playlistSelFg);
            set("playlist_playing_fg", playlistPlayingFg);
            set("playlist_index_fg", playlistIndexFg);
            set("playlist_duration_fg", playlistDurationFg);
            set("playlist_header_fg", playlistHeaderFg);

            // Transport
            set("transport_bg", transportBg);
            set("transport_fg", transportFg);
            set("transport_progress_fg", transportProgressFg);
            set("transport_time_fg", transportTimeFg);
            set("transport_state_fg", transportStateFg);
            set("transport_fn_key_fg", transportFnKeyFg);
            set("transport_fn_label_fg", transportFnLabelFg);

            // Visualizer
            set("vis_bar_low", visBarLow);
            set("vis_bar_mid", visBarMid);
            set("vis_bar_high", visBarHigh);
            set("vis_label_fg", visLabelFg);

            // Separator
            set("separator_fg", separatorFg);
        }
    };

} // namespace vtamp
