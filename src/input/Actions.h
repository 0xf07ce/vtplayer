// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <string>
#include <vector>

namespace vtplayer
{
    /// The vocabulary of things a keybinding can trigger. The input engine
    /// (ventty) resolves a key sequence to one of these as an opaque token
    /// string; Application::dispatch() gives each one its meaning.
    ///
    /// Actions split into two kinds:
    ///   - global: mode/playback/screen commands handled directly.
    ///   - focused-view: cursor/selection commands forwarded to whichever list
    ///     widget has focus (by synthesizing the equivalent KeyEvent, so the
    ///     views' existing navigation — including Shift-extend for Visual mode —
    ///     is reused rather than duplicated).
    enum class Action
    {
        None = 0,

        // -- global --
        Quit,
        ToggleVisualizer,
        ToggleHelp,
        ToggleLeftPanel,
        ToggleRightPanel,
        PlayPause,
        Stop,
        NextTrack,
        PrevTrack,
        SeekBack,
        SeekForward,
        CycleRepeat,
        ToggleShuffle,
        ToggleGain,
        FocusNext,
        FocusLeft,
        FocusRight,
        LeftModeAlbum,
        LeftModeArtist,
        LeftModeDirectory,
        LeftModeFiles,
        LeftModePlaylists,
        LeftModeStreaming,
        Append,
        AddToPlaylist,
        TagEdit,
        Search,
        SearchNext,
        SearchPrev,

        // -- focused-view --
        CursorUp,
        CursorDown,
        CursorPageUp,
        CursorPageDown,
        CursorHome,
        CursorEnd,
        Expand,
        Collapse,
        Activate,
        Remove,
        MoveUp,
        MoveDown,
        ExtendSelectionUp,
        ExtendSelectionDown,
        SelectAll,
        Refresh,
        GoBack,
        PlaylistEdit,
        PlaylistSave,
        EnterVisual,
        ExitVisual,
    };

    /// Map a binding token (e.g. "cursor-down") to an Action. Returns
    /// Action::None for an unknown token.
    Action actionFromToken(std::string const & token);

    /// True if `token` names a known action — used as the keymap parser's
    /// validator so a typo in a preset file produces a warning.
    bool isKnownAction(std::string const & token);

    /// Category grouping for the help screen.
    enum class ActionCategory
    {
        Playback,
        Panels,
        Commands,
        Navigation,
    };

    char const * categoryTitle(ActionCategory category);

    /// One help-screen entry: an action, its category, and a short description.
    struct HelpEntry
    {
        Action action;
        ActionCategory category;
        char const * description;
    };

    /// Actions to show on the help screen, grouped by category in display
    /// order. The help renderer pairs each with the key(s) actually bound to
    /// it in the active preset and skips any that are unbound. (EnterVisual and
    /// the focused-view Visual bindings are described separately by the
    /// renderer when the preset has a Visual mode.)
    std::vector<HelpEntry> const & actionHelpEntries();
} // namespace vtplayer
