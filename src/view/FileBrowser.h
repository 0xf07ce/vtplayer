// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Theme.h"

#include <ventty/widget/Widget.h>

#include <filesystem>
#include <functional>
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
    bool isParent = false;
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

    using OnAddCallback = std::function<void(std::filesystem::path const &)>;
    void setOnAdd(OnAddCallback cb) { _onAdd = std::move(cb); }

    void draw(ventty::Window & window) override;
    bool handleKey(ventty::KeyEvent const & event) override;
    bool handleMouse(ventty::MouseEvent const & event);

private:
    void scrollToSelected();

    Theme _theme;
    std::filesystem::path _currentDir;
    std::vector<FileEntry> _entries;
    int _selectedIndex = 0;
    int _scrollOffset = 0;
    bool _showHidden = false;
    std::vector<std::string> _allowedExts{".mp3", ".ogg", ".flac"};
    OnAddCallback _onAdd;
};

} // namespace vtplayer
