// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Theme.h"

#include <ventty/widget/Widget.h>

#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace vtplayer
{

struct FileEntry
{
    std::string name;
    std::filesystem::path path;
    bool isDirectory = false;
    bool isAudio = false;
    bool isPlaylist = false;  ///< .m3u / .m3u8 file
    bool isParent = false;

    /// True for entries that participate in multi-selection (audio + playlist files).
    bool isSelectable() const { return isAudio || isPlaylist; }
};

class FileBrowser : public ventty::Widget
{
public:
    FileBrowser();

    void setTheme(Theme const & theme) { _theme = theme; }
    void setDirectory(std::filesystem::path const & dir);
    void setShowHidden(bool show);
    void setAllowedExtensions(std::string const & csv);
    void refresh();

    int selectedIndex() const { return _selectedIndex; }
    FileEntry const * selectedEntry() const;
    std::filesystem::path const & currentDirectory() const { return _currentDir; }

    /// Non-recursively collect audio files from `dir`, applying the same
    /// extension and hidden-file filters as the browser listing. Results
    /// are sorted alphabetically by filename.
    std::vector<std::filesystem::path>
    collectAudioFiles(std::filesystem::path const & dir) const;

    /// Called when the user activates audio file(s) (Enter / double-click).
    /// `paths` contains every audio file to add: the multi-selection set
    /// when non-empty, otherwise just the cursor entry. `quietAppend` is
    /// true on Shift+Enter — append to the end of the play queue without
    /// disturbing playback. When false, append and start playing the first
    /// newly-added track.
    using OnActivateCallback =
        std::function<void(std::vector<std::filesystem::path> const &, bool quietAppend)>;
    void setOnActivate(OnActivateCallback cb) { _onActivate = std::move(cb); }

    using OnOpenPlaylistCallback = std::function<void(std::filesystem::path const &)>;
    void setOnOpenPlaylist(OnOpenPlaylistCallback cb) { _onOpenPlaylist = std::move(cb); }

    /// Multi-selection (files only — `..` and directories never join the set).
    bool isMultiSelected(int idx) const { return _multiSelected.count(idx) > 0; }
    void clearMultiSelection();
    void selectAllFiles();

    void draw(ventty::Window & window) override;
    bool handleKey(ventty::KeyEvent const & event) override;
    bool handleMouse(ventty::MouseEvent const & event);

protected:
    void onFocusChanged() override;

private:
    void scrollToSelected();
    void extendSelectionTo(int newIndex);

    Theme _theme;
    std::filesystem::path _currentDir;
    std::vector<FileEntry> _entries;
    int _selectedIndex = 0;
    int _scrollOffset = 0;
    bool _showHidden = false;
    std::vector<std::string> _allowedExts{".mp3", ".wav", ".ogg", ".flac"};
    std::set<int> _multiSelected;
    int _selectionAnchor = -1;
    OnActivateCallback _onActivate;
    OnOpenPlaylistCallback _onOpenPlaylist;
};

} // namespace vtplayer
