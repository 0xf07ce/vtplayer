// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "FileBrowser.h"

#include "../util/UnicodeNormalize.h"

#include <ventty/art/AsciiArt.h>
#include <ventty/core/Utf8.h>

#include <algorithm>
#include <cctype>

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

void FileBrowser::setShowHidden(bool show)
{
    if (_showHidden == show) return;
    _showHidden = show;
    refresh();
}

void FileBrowser::setAllowedExtensions(std::string const & csv)
{
    std::vector<std::string> exts;
    std::string cur;
    auto flush = [&]()
    {
        if (cur.empty()) return;
        std::string ext;
        ext.reserve(cur.size() + 1);
        if (cur.front() != '.') ext.push_back('.');
        for (char ch : cur)
        {
            ext.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        exts.push_back(std::move(ext));
        cur.clear();
    };
    for (char c : csv)
    {
        if (c == ',' || c == ' ' || c == '\t')
        {
            flush();
        }
        else
        {
            cur.push_back(c);
        }
    }
    flush();

    if (exts.empty()) return; // keep previous default rather than blanking the browser
    _allowedExts = std::move(exts);
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
        if (name.empty()) continue;
        if (!_showHidden && name[0] == '.') continue;

        FileEntry fe;
        fe.name = toNfc(name);
        fe.path = entry.path();

        if (entry.is_directory(ec))
        {
            fe.isDirectory = true;
            dirs.push_back(std::move(fe));
        }
        else if (entry.is_regular_file(ec))
        {
            auto ext = entry.path().extension().string();
            for (auto & c : ext)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (ext == ".m3u" || ext == ".m3u8")
            {
                fe.isPlaylist = true;
                files.push_back(std::move(fe));
            }
            else if (ext == ".stream")
            {
                fe.isStream = true;
                files.push_back(std::move(fe));
            }
            else if (std::find(_allowedExts.begin(), _allowedExts.end(), ext) != _allowedExts.end())
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

    clearMultiSelection();
}

std::vector<std::filesystem::path>
FileBrowser::collectAudioFiles(std::filesystem::path const & dir) const
{
    std::vector<std::filesystem::path> paths;
    std::error_code ec;
    for (auto const & entry : std::filesystem::directory_iterator(dir, ec))
    {
        auto name = entry.path().filename().string();
        if (name.empty()) continue;
        if (!_showHidden && name[0] == '.') continue;
        if (!entry.is_regular_file(ec)) continue;

        auto ext = entry.path().extension().string();
        for (auto & c : ext)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        bool const allowed =
            std::find(_allowedExts.begin(), _allowedExts.end(), ext) != _allowedExts.end()
            || ext == ".stream";
        if (allowed)
        {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end(),
              [](std::filesystem::path const & a, std::filesystem::path const & b)
              {
                  return a.filename().string() < b.filename().string();
              });
    return paths;
}

void FileBrowser::clearMultiSelection()
{
    _multiSelected.clear();
    _selectionAnchor = -1;
}

void FileBrowser::selectAllFiles()
{
    _multiSelected.clear();
    for (int i = 0; i < static_cast<int>(_entries.size()); ++i)
    {
        if (_entries[i].isSelectable()) _multiSelected.insert(i);
    }
    _selectionAnchor = _selectedIndex;
}

void FileBrowser::extendSelectionTo(int newIndex)
{
    if (_selectionAnchor < 0) _selectionAnchor = _selectedIndex;
    int const lo = std::min(_selectionAnchor, newIndex);
    int const hi = std::max(_selectionAnchor, newIndex);
    _multiSelected.clear();
    for (int i = lo; i <= hi; ++i)
    {
        if (i >= 0 && i < static_cast<int>(_entries.size()) && _entries[i].isSelectable())
        {
            _multiSelected.insert(i);
        }
    }
}

void FileBrowser::onFocusChanged()
{
    if (!isFocused())
    {
        clearMultiSelection();
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
    dirName = toNfc(dirName);
    std::string header = " " + dirName;
    header = truncateToWidth(header, r.width - 2, "...");
    // The header names the current directory, so tint it with the same blue
    // used for directory entries in the list below (keeps the filled
    // header background + bold weight).
    ventty::Style headerTextStyle{_theme.browserDirFg, _theme.browserBg, ventty::Attr::Bold};
    window.drawText(r.x + 1, r.y, header, headerTextStyle);

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
        bool const cursor = (idx == _selectedIndex) && isFocused();
        bool const multi  = isFocused() && _multiSelected.count(idx) > 0;

        Color fg;
        Color bg = (cursor || multi) ? _theme.browserSelBg : _theme.browserBg;
        if (cursor)
        {
            fg = _theme.browserSelFg;
        }
        else if (multi)
        {
            fg = _theme.browserFg;
        }
        else if (entry.isDirectory)
        {
            fg = _theme.browserDirFg;
        }
        else if (entry.isAudio || entry.isStream)
        {
            fg = _theme.browserAudioFg;
        }
        else
        {
            fg = _theme.browserFg;
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
            icon = " / ";
        }
        else if (entry.isPlaylist)
        {
            icon = " \xE2\x89\xA1 "; // ≡
        }
        else
        {
            icon = " \xE2\x99\xAA "; // ♪
        }

        window.drawText(r.x + 1, y, icon, style);
        int textX = r.x + 1 + static_cast<int>(ventty::stringWidth(icon));

        int const maxNameW = contentW - 4;
        std::string name = truncateToWidth(entry.name, maxNameW);
        window.drawText(textX, y, name, style);
    }
}

bool FileBrowser::handleKey(ventty::KeyEvent const & event)
{
    if (_entries.empty())
    {
        return false;
    }

    // Ctrl+A: select all selectable files (skips `..` and directories).
    if (event.key == Key::Char && event.ctrl &&
        (event.ch == 'a' || event.ch == 'A' || event.ch == 1))
    {
        selectAllFiles();
        return true;
    }

    if (event.key == Key::Up)
    {
        if (_selectedIndex > 0)
        {
            int const target = _selectedIndex - 1;
            if (event.shift)
            {
                if (_selectionAnchor < 0) _selectionAnchor = _selectedIndex;
                _selectedIndex = target;
                extendSelectionTo(target);
            }
            else
            {
                clearMultiSelection();
                _selectedIndex = target;
            }
            scrollToSelected();
        }
        else if (!event.shift)
        {
            clearMultiSelection();
        }
        return true;
    }

    if (event.key == Key::Down)
    {
        if (_selectedIndex < static_cast<int>(_entries.size()) - 1)
        {
            int const target = _selectedIndex + 1;
            if (event.shift)
            {
                if (_selectionAnchor < 0) _selectionAnchor = _selectedIndex;
                _selectedIndex = target;
                extendSelectionTo(target);
            }
            else
            {
                clearMultiSelection();
                _selectedIndex = target;
            }
            scrollToSelected();
        }
        else if (!event.shift)
        {
            clearMultiSelection();
        }
        return true;
    }

    if (event.key == Key::PageUp)
    {
        clearMultiSelection();
        int listH = rect().height - 2;
        _selectedIndex = std::max(0, _selectedIndex - listH);
        scrollToSelected();
        return true;
    }

    if (event.key == Key::PageDown)
    {
        clearMultiSelection();
        int listH = rect().height - 2;
        _selectedIndex = std::min(static_cast<int>(_entries.size()) - 1, _selectedIndex + listH);
        scrollToSelected();
        return true;
    }

    if (event.key == Key::Home || event.key == Key::Left)
    {
        clearMultiSelection();
        _selectedIndex = 0;
        scrollToSelected();
        return true;
    }

    if (event.key == Key::End || event.key == Key::Right)
    {
        clearMultiSelection();
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
        if (entry->isPlaylist && _onOpenPlaylist)
        {
            _onOpenPlaylist(entry->path);
            return true;
        }
        if ((entry->isAudio || entry->isStream) && _onActivate)
        {
            // Multi-selected files take priority over the cursor entry.
            // _multiSelected (std::set<int>) iterates in ascending index
            // order, so paths come out in display order. Stream descriptors
            // (.stream) ride the same path — Application::activateFromBrowser
            // recognises the extension and resolves the URL.
            std::vector<std::filesystem::path> paths;
            for (int sel : _multiSelected)
            {
                if (sel >= 0 && sel < static_cast<int>(_entries.size())
                    && (_entries[sel].isAudio || _entries[sel].isStream))
                {
                    paths.push_back(_entries[sel].path);
                }
            }
            if (paths.empty())
            {
                paths.push_back(entry->path);
            }
            _onActivate(paths);
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
                    else if (entry && entry->isPlaylist && _onOpenPlaylist)
                    {
                        _onOpenPlaylist(entry->path);
                    }
                    else if (entry && (entry->isAudio || entry->isStream) && _onActivate)
                    {
                        _onActivate({entry->path});
                    }
                }
                else
                {
                    clearMultiSelection();
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
