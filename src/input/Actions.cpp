// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "Actions.h"

#include <unordered_map>

namespace vtplayer
{
    namespace
    {
        std::unordered_map<std::string, Action> const & tokenTable()
        {
            static std::unordered_map<std::string, Action> const table = {
                { "quit", Action::Quit },
                { "visualizer-toggle", Action::ToggleVisualizer },
                { "help-toggle", Action::ToggleHelp },
                { "panel-toggle", Action::ToggleLeftPanel },
                { "play-pause", Action::PlayPause },
                { "stop", Action::Stop },
                { "next-track", Action::NextTrack },
                { "prev-track", Action::PrevTrack },
                { "seek-back", Action::SeekBack },
                { "seek-fwd", Action::SeekForward },
                { "repeat-cycle", Action::CycleRepeat },
                { "shuffle-toggle", Action::ToggleShuffle },
                { "gain-toggle", Action::ToggleGain },
                { "focus-next", Action::FocusNext },
                { "focus-left", Action::FocusLeft },
                { "focus-right", Action::FocusRight },
                { "left-album", Action::LeftModeAlbum },
                { "left-artist", Action::LeftModeArtist },
                { "left-directory", Action::LeftModeDirectory },
                { "left-files", Action::LeftModeFiles },
                { "left-playlists", Action::LeftModePlaylists },
                { "append", Action::Append },
                { "add-playlist", Action::AddToPlaylist },
                { "tag-edit", Action::TagEdit },
                { "search", Action::Search },
                { "search-next", Action::SearchNext },
                { "search-prev", Action::SearchPrev },
                { "cursor-up", Action::CursorUp },
                { "cursor-down", Action::CursorDown },
                { "page-up", Action::CursorPageUp },
                { "page-down", Action::CursorPageDown },
                { "cursor-home", Action::CursorHome },
                { "cursor-end", Action::CursorEnd },
                { "expand", Action::Expand },
                { "collapse", Action::Collapse },
                { "activate", Action::Activate },
                { "remove", Action::Remove },
                { "move-up", Action::MoveUp },
                { "move-down", Action::MoveDown },
                { "select-all", Action::SelectAll },
                { "refresh", Action::Refresh },
                { "go-back", Action::GoBack },
                { "playlist-edit", Action::PlaylistEdit },
                { "playlist-save", Action::PlaylistSave },
                { "enter-visual", Action::EnterVisual },
                { "exit-visual", Action::ExitVisual },
            };
            return table;
        }
    } // namespace

    Action actionFromToken(std::string const & token)
    {
        auto const & table = tokenTable();
        auto const it = table.find(token);
        return it == table.end() ? Action::None : it->second;
    }

    bool isKnownAction(std::string const & token)
    {
        return tokenTable().count(token) != 0;
    }

    char const * categoryTitle(ActionCategory category)
    {
        switch (category)
        {
        case ActionCategory::Playback: return "Playback";
        case ActionCategory::Panels: return "Screens & panels";
        case ActionCategory::Commands: return "Library / queue";
        case ActionCategory::Navigation: return "List navigation";
        }
        return "";
    }

    std::vector<HelpEntry> const & actionHelpEntries()
    {
        using C = ActionCategory;
        static std::vector<HelpEntry> const entries = {
            { Action::PlayPause, C::Playback, "Play / pause" },
            { Action::Stop, C::Playback, "Stop" },
            { Action::PrevTrack, C::Playback, "Previous track" },
            { Action::NextTrack, C::Playback, "Next track" },
            { Action::SeekBack, C::Playback, "Seek -5s" },
            { Action::SeekForward, C::Playback, "Seek +5s" },
            { Action::CycleRepeat, C::Playback, "Cycle repeat (none / all / one)" },
            { Action::ToggleShuffle, C::Playback, "Toggle shuffle" },
            { Action::ToggleGain, C::Playback, "Toggle gain normalization" },

            { Action::ToggleVisualizer, C::Panels, "Visualizer screen" },
            { Action::ToggleHelp, C::Panels, "Help" },
            { Action::ToggleLeftPanel, C::Panels, "Toggle left panel" },
            { Action::FocusNext, C::Panels, "Switch focus (panel <-> queue)" },
            { Action::FocusLeft, C::Panels, "Focus left panel" },
            { Action::FocusRight, C::Panels, "Focus play queue" },
            { Action::LeftModeAlbum, C::Panels, "Left panel: Album" },
            { Action::LeftModeArtist, C::Panels, "Left panel: Artist" },
            { Action::LeftModeDirectory, C::Panels, "Left panel: Directory" },
            { Action::LeftModeFiles, C::Panels, "Left panel: Files" },
            { Action::LeftModePlaylists, C::Panels, "Left panel: Playlists" },

            { Action::Activate, C::Commands, "Play / open the selection" },
            { Action::Append, C::Commands, "Append selection to the queue" },
            { Action::AddToPlaylist, C::Commands, "Add selection to a saved playlist" },
            { Action::TagEdit, C::Commands, "Edit tags" },
            { Action::Remove, C::Commands, "Remove item(s) from the queue" },
            { Action::MoveUp, C::Commands, "Move selection up" },
            { Action::MoveDown, C::Commands, "Move selection down" },
            { Action::SelectAll, C::Commands, "Select all" },
            { Action::Search, C::Commands, "Search the library" },
            { Action::SearchNext, C::Commands, "Next search result" },
            { Action::SearchPrev, C::Commands, "Previous search result" },
            { Action::Refresh, C::Commands, "Refresh the file browser" },
            { Action::GoBack, C::Commands, "Parent directory / back" },
            { Action::PlaylistEdit, C::Commands, "Edit playlist" },
            { Action::PlaylistSave, C::Commands, "Save playlist edits" },

            { Action::CursorUp, C::Navigation, "Move up" },
            { Action::CursorDown, C::Navigation, "Move down" },
            { Action::CursorPageUp, C::Navigation, "Page up" },
            { Action::CursorPageDown, C::Navigation, "Page down" },
            { Action::CursorHome, C::Navigation, "First item" },
            { Action::CursorEnd, C::Navigation, "Last item" },
            { Action::Collapse, C::Navigation, "Collapse group / parent" },
            { Action::Expand, C::Navigation, "Expand group" },
        };
        return entries;
    }
} // namespace vtplayer
