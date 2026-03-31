// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Theme.h"

#include <ventty/widget/Widget.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace vtamp
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
    void refresh();

    int selectedIndex() const { return _selectedIndex; }
    FileEntry const * selectedEntry() const;

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
    OnAddCallback _onAdd;
};

} // namespace vtamp
