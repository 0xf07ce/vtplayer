// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "FileBrowser.h"
#include "../audio/TrackInfo.h"

#include <ventty/art/AsciiArt.h>
#include <ventty/core/Utf8.h>

#include <algorithm>

namespace vtplayer
{

using Key = ventty::KeyEvent::Key;

FileBrowser::FileBrowser()
{
    char const * home = std::getenv("HOME");
    if (home)
    {
        setDirectory(home);
    }
    else
    {
        setDirectory("/");
    }
}

void FileBrowser::setDirectory(std::filesystem::path const & dir)
{
    _currentDir = dir;
    _selectedIndex = 0;
    _scrollOffset = 0;
    refresh();
}

void FileBrowser::refresh()
{
    _entries.clear();

    // Parent directory entry
    if (_currentDir.has_parent_path() && _currentDir != _currentDir.root_path())
    {
        FileEntry parent;
        parent.name = "..";
        parent.path = _currentDir.parent_path();
        parent.isDirectory = true;
        parent.isParent = true;
        _entries.push_back(std::move(parent));
    }

    std::vector<FileEntry> dirs;
    std::vector<FileEntry> files;

    std::error_code ec;
    for (auto const & entry : std::filesystem::directory_iterator(_currentDir, ec))
    {
        auto name = entry.path().filename().string();
        if (name.empty() || name[0] == '.')
        {
            continue; // skip hidden files
        }

        FileEntry fe;
        fe.name = name;
        fe.path = entry.path();

        if (entry.is_directory(ec))
        {
            fe.isDirectory = true;
            dirs.push_back(std::move(fe));
        }
        else if (entry.is_regular_file(ec))
        {
            auto ext = entry.path().extension().string();
            if (TrackInfo::isSupportedExtension(ext))
            {
                fe.isAudio = true;
                files.push_back(std::move(fe));
            }
        }
    }

    // Sort alphabetically
    auto cmp = [](FileEntry const & a, FileEntry const & b)
    {
        return a.name < b.name;
    };
    std::sort(dirs.begin(), dirs.end(), cmp);
    std::sort(files.begin(), files.end(), cmp);

    _entries.insert(_entries.end(), dirs.begin(), dirs.end());
    _entries.insert(_entries.end(), files.begin(), files.end());

    if (_selectedIndex >= static_cast<int>(_entries.size()))
    {
        _selectedIndex = std::max(0, static_cast<int>(_entries.size()) - 1);
    }
}

FileEntry const * FileBrowser::selectedEntry() const
{
    if (_selectedIndex >= 0 && _selectedIndex < static_cast<int>(_entries.size()))
    {
        return &_entries[_selectedIndex];
    }
    return nullptr;
}

void FileBrowser::draw(ventty::Window & window)
{
    auto const & r = rect();

    // Left border
    for (int y = 0; y < r.height; ++y)
    {
        window.putChar(r.x, r.y + y, ventty::DOUBLE_BOX.v,
                       ventty::Style{_theme.border, _theme.browserBg});
    }

    // Header
    ventty::Style headerStyle{_theme.browserHeaderFg, _theme.browserBg, ventty::Attr::Bold};
    window.fill(r.x + 1, r.y, r.width - 1, 1, U' ', headerStyle);

    std::string dirName = _currentDir.filename().string();
    if (dirName.empty())
    {
        dirName = _currentDir.string();
    }
    std::string header = " [" + dirName + "]";
    if (static_cast<int>(header.size()) > r.width - 2)
    {
        header = header.substr(0, r.width - 5) + "...";
    }
    window.drawText(r.x + 1, r.y, header, headerStyle);

    // Separator line
    ventty::Style sepStyle{_theme.border, _theme.browserBg};
    for (int x = r.x + 1; x < r.x + r.width; ++x)
    {
        window.putChar(x, r.y + 1, ventty::HR_THIN, sepStyle);
    }

    // File list
    int listH = r.height - 2;
    int contentW = r.width - 2; // left border + 1 padding

    for (int i = 0; i < listH; ++i)
    {
        int idx = _scrollOffset + i;
        int y = r.y + 2 + i;

        if (idx >= static_cast<int>(_entries.size()))
        {
            window.fill(r.x + 1, y, r.width - 1, 1, U' ',
                        ventty::Style{_theme.browserFg, _theme.browserBg});
            continue;
        }

        auto const & entry = _entries[idx];
        bool selected = (idx == _selectedIndex) && isFocused();

        Color fg = _theme.browserFg;
        Color bg = selected ? _theme.browserSelBg : _theme.browserBg;
        if (selected)
        {
            fg = _theme.browserSelFg;
        }
        else if (entry.isDirectory)
        {
            fg = _theme.browserDirFg;
        }
        else if (entry.isAudio)
        {
            fg = _theme.browserAudioFg;
        }

        ventty::Style style{fg, bg};
        window.fill(r.x + 1, y, r.width - 1, 1, U' ', style);

        // Icon + name
        std::string icon;
        if (entry.isParent)
        {
            icon = " \xE2\x86\x90 "; // ←
        }
        else if (entry.isDirectory)
        {
            icon = " \xE2\x96\xB8 "; // ▸
        }
        else
        {
            icon = " \xE2\x99\xAA "; // ♪
        }

        window.drawText(r.x + 1, y, icon, style);
        int textX = r.x + 1 + static_cast<int>(ventty::stringWidth(icon));

        std::string name = entry.name;
        int maxNameW = contentW - 4;
        if (static_cast<int>(name.size()) > maxNameW)
        {
            name = name.substr(0, maxNameW - 2) + "..";
        }
        window.drawText(textX, y, name, style);
    }
}

bool FileBrowser::handleKey(ventty::KeyEvent const & event)
{
    if (_entries.empty())
    {
        return false;
    }

    if (event.key == Key::Up)
    {
        if (_selectedIndex > 0)
        {
            _selectedIndex--;
            scrollToSelected();
        }
        return true;
    }

    if (event.key == Key::Down)
    {
        if (_selectedIndex < static_cast<int>(_entries.size()) - 1)
        {
            _selectedIndex++;
            scrollToSelected();
        }
        return true;
    }

    if (event.key == Key::PageUp)
    {
        int listH = rect().height - 2;
        _selectedIndex = std::max(0, _selectedIndex - listH);
        scrollToSelected();
        return true;
    }

    if (event.key == Key::PageDown)
    {
        int listH = rect().height - 2;
        _selectedIndex = std::min(static_cast<int>(_entries.size()) - 1, _selectedIndex + listH);
        scrollToSelected();
        return true;
    }

    if (event.key == Key::Home)
    {
        _selectedIndex = 0;
        scrollToSelected();
        return true;
    }

    if (event.key == Key::End)
    {
        _selectedIndex = static_cast<int>(_entries.size()) - 1;
        scrollToSelected();
        return true;
    }

    if (event.key == Key::Enter)
    {
        auto const * entry = selectedEntry();
        if (!entry)
        {
            return false;
        }
        if (entry->isDirectory)
        {
            setDirectory(entry->path);
            return true;
        }
        if (entry->isAudio && _onAdd)
        {
            _onAdd(entry->path);
            return true;
        }
        return true;
    }

    if (event.key == Key::Backspace)
    {
        if (_currentDir.has_parent_path() && _currentDir != _currentDir.root_path())
        {
            setDirectory(_currentDir.parent_path());
        }
        return true;
    }

    return false;
}

bool FileBrowser::handleMouse(ventty::MouseEvent const & event)
{
    auto const & r = rect();
    if (!r.contains(event.x, event.y))
    {
        return false;
    }

    using Button = ventty::MouseEvent::Button;
    using Action = ventty::MouseEvent::Action;

    // Scroll wheel
    if (event.button == Button::ScrollUp)
    {
        if (_selectedIndex > 0)
        {
            _selectedIndex--;
            scrollToSelected();
        }
        return true;
    }
    if (event.button == Button::ScrollDown)
    {
        if (_selectedIndex < static_cast<int>(_entries.size()) - 1)
        {
            _selectedIndex++;
            scrollToSelected();
        }
        return true;
    }

    // Left click on list area
    if (event.button == Button::Left && event.action == Action::Press)
    {
        int listY = r.y + 2; // list starts after header + separator
        if (event.y >= listY)
        {
            int clickedRow = event.y - listY;
            int clickedIdx = _scrollOffset + clickedRow;
            if (clickedIdx >= 0 && clickedIdx < static_cast<int>(_entries.size()))
            {
                if (_selectedIndex == clickedIdx)
                {
                    // Double-click effect: activate on second click of same item
                    auto const * entry = selectedEntry();
                    if (entry && entry->isDirectory)
                    {
                        setDirectory(entry->path);
                    }
                    else if (entry && entry->isAudio && _onAdd)
                    {
                        _onAdd(entry->path);
                    }
                }
                else
                {
                    _selectedIndex = clickedIdx;
                }
            }
        }
        return true;
    }

    return false;
}

void FileBrowser::scrollToSelected()
{
    int listH = rect().height - 2;
    if (listH <= 0)
    {
        return;
    }
    if (_selectedIndex < _scrollOffset)
    {
        _scrollOffset = _selectedIndex;
    }
    if (_selectedIndex >= _scrollOffset + listH)
    {
        _scrollOffset = _selectedIndex - listH + 1;
    }
}

} // namespace vtplayer
