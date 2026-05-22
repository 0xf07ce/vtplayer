// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "LibraryView.h"

#include "../library/MediaLibrary.h"
#include "../util/UnicodeNormalize.h"

#include <ventty/art/AsciiArt.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>

namespace vtplayer
{

    namespace
    {

        using Key = ventty::KeyEvent::Key;
        constexpr std::size_t kNoIdx = static_cast<std::size_t>(-1);

        std::string formatTrackLabel(TrackInfo const &t)
        {
            if (t.trackNumber > 0)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "%02d. ", t.trackNumber);
                return std::string(buf) + (t.title.empty() ? t.path.stem().string() : t.title);
            }
            return t.title.empty() ? t.path.stem().string() : t.title;
        }

    } // namespace

    void LibraryView::setLibrary(MediaLibrary const *library)
    {
        _library = library;
        rebuild();
    }

    void LibraryView::setMode(Mode mode)
    {
        // Always rebuild, even when the mode is unchanged: re-pressing the
        // current mode's key resets the tree to that mode's default fold
        // state (1 → collapse back to the artist list, 2 → expand to
        // albums, 3 → all directories collapsed).
        _mode = mode;
        rebuild();
    }

    void LibraryView::clear()
    {
        _nodes.clear();
        _roots.clear();
        _visible.clear();
        _selectedIndex = 0;
        _scrollOffset = 0;
        _headerCount = 0;
    }

    void LibraryView::rebuild()
    {
        _nodes.clear();
        _roots.clear();
        _visible.clear();
        _selectedIndex = 0;
        _scrollOffset = 0;
        _headerCount = 0;

        if (!_library)
            return;

        if (_mode == Mode::Directory)
            buildDirectoryTree();
        else
            buildArtistAlbumTree();

        sortNodeChildren();
        recomputeVisible();

        // Header count reflects the grouping axis. Computed once here rather
        // than per-frame in draw(): Artist → #artists, Album → #albums,
        // Directory → #tracks.
        if (_mode == Mode::Artist)
        {
            _headerCount = _roots.size();
        }
        else if (_mode == Mode::Album)
        {
            for (auto const &n : _nodes)
            {
                if (n.kind == Node::Kind::Group && n.depth == 1)
                    ++_headerCount;
            }
        }
        else
        {
            _headerCount = _library->size();
        }
    }

    void LibraryView::buildDirectoryTree()
    {
        auto const &root = _library->root();

        auto findOrCreateGroup =
            [&](std::size_t parentIdx, std::string const &label, int depth) -> std::size_t
        {
            auto &children = (parentIdx == kNoIdx) ? _roots : _nodes[parentIdx].children;
            for (auto idx : children)
            {
                if (_nodes[idx].kind == Node::Kind::Group && _nodes[idx].label == label)
                {
                    return idx;
                }
            }
            Node n;
            n.kind = Node::Kind::Group;
            n.label = label;
            n.depth = depth;
            n.expanded = false; // every directory starts collapsed
            n.parent = parentIdx;
            std::size_t const idx = _nodes.size();
            _nodes.push_back(std::move(n));
            children.push_back(idx);
            return idx;
        };

        for (auto const &t : _library->tracks())
        {
            // Path components relative to the library root.
            std::filesystem::path rel;
            if (!root.empty())
            {
                rel = t.path.lexically_relative(root);
                if (rel.empty() || rel.native()[0] == '.')
                    rel = t.path;
            }
            else
            {
                rel = t.path;
            }

            std::vector<std::string> parts;
            for (auto const &p : rel)
                parts.push_back(toNfc(p.string()));
            if (parts.empty())
                continue;

            std::size_t parent = kNoIdx;
            int depth = 0;
            for (std::size_t i = 0; i + 1 < parts.size(); ++i)
            {
                parent = findOrCreateGroup(parent, parts[i], depth);
                depth++;
            }

            Node trackNode;
            trackNode.kind = Node::Kind::Track;
            // Directory mode is a filesystem view, so show the actual file
            // name (matching FileBrowser) rather than "NN. Title" — that
            // tag-based form is reserved for the Artist/Album modes.
            trackNode.label = toNfc(t.path.filename().string());
            trackNode.depth = depth;
            trackNode.track = &t;
            trackNode.parent = parent;

            std::size_t const idx = _nodes.size();
            _nodes.push_back(std::move(trackNode));
            if (parent == kNoIdx)
                _roots.push_back(idx);
            else
                _nodes[parent].children.push_back(idx);
        }
    }

    void LibraryView::buildArtistAlbumTree()
    {
        auto createGroup = [&](std::size_t parentIdx, std::string const &label, int depth) -> std::size_t
        {
            Node n;
            n.kind = Node::Kind::Group;
            n.label = label;
            n.depth = depth;
            // Artist mode: everything collapsed (artists only). Album mode:
            // artists pre-expanded so albums are visible, tracks still folded.
            n.expanded = (depth == 0 && _mode == Mode::Album);
            n.parent = parentIdx;
            std::size_t const idx = _nodes.size();
            _nodes.push_back(std::move(n));
            if (parentIdx == kNoIdx)
                _roots.push_back(idx);
            else
                _nodes[parentIdx].children.push_back(idx);
            return idx;
        };

        // Group tracks by AlbumArtist (fall back to Artist) → Album.
        // std::map keeps both axes alphabetically ordered for stable display.
        //
        // Stream tracks (URL channels from PLS playlists) are collapsed under
        // a single virtual `(stream)` artist node regardless of their tags.
        // The parenthesized label can never collide with a real artist
        // string, so a local file tagged `artist=MBC` and a `(stream)` group
        // can coexist as separate top-level nodes.
        std::map<std::string, std::map<std::string, std::vector<TrackInfo const *>>> tree;
        for (auto const &t : _library->tracks())
        {
            std::string artist;
            if (t.isStream())
            {
                artist = "(stream)";
            }
            else
            {
                artist = !t.albumArtist.empty() ? t.albumArtist : t.artist;
                if (artist.empty())
                    artist = "(Unknown Artist)";
            }
            std::string album = t.album.empty() ? "(Unknown Album)" : t.album;
            tree[artist][album].push_back(&t);
        }

        for (auto const &[artistKey, albums] : tree)
        {
            std::size_t const artistIdx = createGroup(kNoIdx, artistKey, 0);
            for (auto const &[albumKey, tracks] : albums)
            {
                std::size_t const albumIdx = createGroup(artistIdx, albumKey, 1);
                for (auto const *t : tracks)
                {
                    Node trackNode;
                    trackNode.kind = Node::Kind::Track;
                    trackNode.label = formatTrackLabel(*t);
                    trackNode.depth = 2;
                    trackNode.track = t;
                    trackNode.parent = albumIdx;
                    std::size_t const idx = _nodes.size();
                    _nodes.push_back(std::move(trackNode));
                    _nodes[albumIdx].children.push_back(idx);
                }
            }
        }
    }

    void LibraryView::sortNodeChildren()
    {
        auto cmp = [&](std::size_t a, std::size_t b)
        {
            auto const &na = _nodes[a];
            auto const &nb = _nodes[b];
            // Groups before tracks at the same level.
            if (na.kind != nb.kind)
                return na.kind == Node::Kind::Group;
            if (na.kind == Node::Kind::Track && na.track && nb.track)
            {
                if (na.track->discNumber != nb.track->discNumber)
                {
                    return na.track->discNumber < nb.track->discNumber;
                }
                if (na.track->trackNumber != nb.track->trackNumber)
                {
                    return na.track->trackNumber < nb.track->trackNumber;
                }
            }
            return na.label < nb.label;
        };

        std::sort(_roots.begin(), _roots.end(), cmp);
        for (auto &n : _nodes)
        {
            std::sort(n.children.begin(), n.children.end(), cmp);
        }
    }

    void LibraryView::recomputeVisible()
    {
        _visible.clear();
        std::function<void(std::vector<std::size_t> const &)> walk =
            [&](std::vector<std::size_t> const &list)
        {
            for (auto idx : list)
            {
                _visible.push_back(idx);
                auto const &n = _nodes[idx];
                if (n.kind == Node::Kind::Group && n.expanded && !n.children.empty())
                {
                    walk(n.children);
                }
            }
        };
        walk(_roots);

        if (_visible.empty())
            _selectedIndex = 0;
        else if (_selectedIndex >= static_cast<int>(_visible.size()))
        {
            _selectedIndex = static_cast<int>(_visible.size()) - 1;
        }
        if (_selectedIndex < 0)
            _selectedIndex = 0;
        scrollToSelected();
    }

    void LibraryView::scrollToSelected()
    {
        int listH = rect().height - 2;
        if (listH <= 0)
            return;
        if (_selectedIndex < _scrollOffset)
        {
            _scrollOffset = _selectedIndex;
        }
        if (_selectedIndex >= _scrollOffset + listH)
        {
            _scrollOffset = _selectedIndex - listH + 1;
        }
    }

    void LibraryView::collectTracks(std::size_t nodeIdx, std::vector<TrackInfo> &out) const
    {
        if (nodeIdx >= _nodes.size())
            return;
        auto const &n = _nodes[nodeIdx];
        if (n.kind == Node::Kind::Track && n.track)
        {
            out.push_back(*n.track);
            return;
        }
        for (auto child : n.children)
            collectTracks(child, out);
    }

    bool LibraryView::expandPath(std::size_t nodeIdx)
    {
        if (nodeIdx >= _nodes.size())
            return false;
        bool changed = false;
        for (std::size_t cur = _nodes[nodeIdx].parent; cur != kNoIdx; cur = _nodes[cur].parent)
        {
            if (!_nodes[cur].expanded)
            {
                _nodes[cur].expanded = true;
                changed = true;
            }
        }
        return changed;
    }

    void LibraryView::locate(std::filesystem::path const &path)
    {
        if (path.empty())
            return;
        auto const target = path.string();
        for (std::size_t i = 0; i < _nodes.size(); ++i)
        {
            auto const &n = _nodes[i];
            if (n.kind != Node::Kind::Track || !n.track)
                continue;
            if (n.track->path.string() != target)
                continue;

            bool changed = expandPath(i);
            if (changed)
                recomputeVisible();
            for (std::size_t v = 0; v < _visible.size(); ++v)
            {
                if (_visible[v] == i)
                {
                    _selectedIndex = static_cast<int>(v);
                    scrollToSelected();
                    break;
                }
            }
            return;
        }
    }

    void LibraryView::locateForMode(std::filesystem::path const &path)
    {
        if (path.empty())
            return;
        auto const target = path.string();
        for (std::size_t i = 0; i < _nodes.size(); ++i)
        {
            auto const &n = _nodes[i];
            if (n.kind != Node::Kind::Track || !n.track)
                continue;
            if (n.track->path.string() != target)
                continue;

            // Choose the node that represents this track at the current
            // mode's grouping level. Artist/Album trees are always
            // Artist(0) > Album(1) > Track(2). Directory keeps drilling to
            // the track itself (folders expanded so the file is revealed).
            std::size_t sel = i;
            if (_mode == Mode::Artist || _mode == Mode::Album)
            {
                int const wantDepth = (_mode == Mode::Artist) ? 0 : 1;
                for (std::size_t cur = i; cur != kNoIdx; cur = _nodes[cur].parent)
                {
                    if (_nodes[cur].kind == Node::Kind::Group
                        && _nodes[cur].depth == wantDepth)
                    {
                        sel = cur;
                        break;
                    }
                }
            }

            // Expand only the ancestors of the chosen node — never the node
            // itself — so its fold level (collapsed artist / album / folder)
            // survives the jump.
            bool changed = expandPath(sel);
            if (changed)
                recomputeVisible();
            for (std::size_t v = 0; v < _visible.size(); ++v)
            {
                if (_visible[v] == sel)
                {
                    _selectedIndex = static_cast<int>(v);
                    scrollToSelected();
                    break;
                }
            }
            return;
        }
    }

    std::filesystem::path LibraryView::selectedTrackPath() const
    {
        if (_selectedIndex < 0 || _selectedIndex >= static_cast<int>(_visible.size()))
            return {};

        // Descend through the first child of each group until a Track is hit,
        // so a collapsed artist/album/folder still yields a usable anchor.
        std::size_t idx = _visible[static_cast<std::size_t>(_selectedIndex)];
        while (idx < _nodes.size())
        {
            auto const &n = _nodes[idx];
            if (n.kind == Node::Kind::Track)
                return n.track ? n.track->path : std::filesystem::path{};
            if (n.children.empty())
                return {};
            idx = n.children.front();
        }
        return {};
    }

    void LibraryView::onFocusChanged()
    {
        // Nothing yet — kept for parity with PlayQueueView.
    }

    void LibraryView::draw(ventty::Window &window)
    {
        auto const &r = rect();

        // Left border (panel separator handled by Application). LibraryView
        // shares the left-panel slot with FileBrowser, so the box border must
        // sit on the left edge, matching FileBrowser's framing.
        for (int y = 0; y < r.height; ++y)
        {
            window.putChar(r.x, r.y + y, ventty::DOUBLE_BOX.v,
                           ventty::Style{_theme.border, _theme.browserBg});
        }

        // Header
        ventty::Style headerStyle{_theme.browserHeaderFg, _theme.browserBg, ventty::Attr::Bold};
        window.fill(r.x + 1, r.y, r.width - 1, 1, U' ', headerStyle);

        std::string header = " Library \xC2\xB7 ";
        header += (_mode == Mode::Artist)    ? "Artist"
                  : (_mode == Mode::Album)   ? "Album"
                                             : "Directory";
        if (_library)
        {
            header += " (" + std::to_string(_headerCount) + ")";
        }
        header = truncateToWidth(header, r.width - 2, "...");
        window.drawText(r.x + 1, r.y, header, headerStyle);

        // Separator
        ventty::Style sepStyle{_theme.border, _theme.browserBg};
        for (int x = r.x + 1; x < r.x + r.width; ++x)
        {
            window.putChar(x, r.y + 1, ventty::HR_THIN, sepStyle);
        }

        int const listH = r.height - 2;
        int const contentW = r.width - 2;

        // Empty state: guide the user to register a library root.
        if (_visible.empty())
        {
            ventty::Style hintStyle{_theme.browserFg, _theme.browserBg};
            char const *lines[] = {
                "Library is empty.",
                "",
                "Press 4 to browse files, navigate to your",
                "music folder, then ESC -> menu:",
                "  Set current directory as library root",
            };
            constexpr int kLineCount = static_cast<int>(sizeof(lines) / sizeof(lines[0]));
            int const top = r.y + 2 + std::max(0, (listH - kLineCount) / 2);
            for (int i = 0; i < kLineCount && i < listH; ++i)
            {
                std::string const text = truncateToWidth(lines[i], contentW - 1);
                int const cx = r.x + 2;
                window.drawText(cx, top + i, text, hintStyle);
            }
            return;
        }

        for (int i = 0; i < listH; ++i)
        {
            int const visIdx = _scrollOffset + i;
            int const y = r.y + 2 + i;

            if (visIdx >= static_cast<int>(_visible.size()))
            {
                window.fill(r.x + 1, y, r.width - 1, 1, U' ',
                            ventty::Style{_theme.browserFg, _theme.browserBg});
                continue;
            }

            auto const &n = _nodes[_visible[visIdx]];
            bool const cursor = (visIdx == _selectedIndex) && isFocused();

            // Color by hierarchy. ArtistAlbum mode has real artist/album/
            // track semantics, so color each level. Directory mode has no
            // artist/album concept — groups are just folders, so use the
            // neutral file-browser directory color and only the track tint.
            Color levelFg;
            if (n.kind == Node::Kind::Track)
                levelFg = _theme.libraryTrackFg;
            else if (_mode == Mode::Directory)
                levelFg = _theme.playQueueFg;
            else if (n.depth == 0)
                levelFg = _theme.libraryArtistFg;
            else
                levelFg = _theme.libraryAlbumFg;

            Color fg = cursor ? _theme.browserSelFg : levelFg;
            Color bg = cursor ? _theme.browserSelBg : _theme.browserBg;
            ventty::Style style{fg, bg};
            window.fill(r.x + 1, y, r.width - 1, 1, U' ', style);

            // Indent: 1 cell per depth — colors carry the hierarchy now, so
            // keep it tight (album = 1, track = 2).
            int const indent = n.depth;
            int x = r.x + 1 + indent;

            // Disclosure marker for groups, blank for tracks.
            char const *marker = " ";
            if (n.kind == Node::Kind::Group)
            {
                // U+25BE ▾ (expanded) / U+25B8 ▸ (collapsed): compact
                // disclosure triangles, lighter than ASCII "v"/">".
                marker = n.expanded ? "\xE2\x96\xBE " : "\xE2\x96\xB8 ";
            }
            if (x + 2 < r.x + r.width - 1)
            {
                window.drawText(x, y, marker, style);
                x += 2;
            }

            int const labelMax = (r.x + r.width - 1) - x - 1;
            if (labelMax > 0)
            {
                std::string label = truncateToWidth(n.label, labelMax);
                window.drawText(x, y, label, style);
            }
        }
    }

    LibraryView::Selection LibraryView::currentSelection() const
    {
        Selection s;
        if (_visible.empty())
            return s;
        if (_selectedIndex < 0 || _selectedIndex >= static_cast<int>(_visible.size()))
            return s;

        std::size_t const nodeIdx = _visible[static_cast<std::size_t>(_selectedIndex)];
        auto const & n = _nodes[nodeIdx];

        if (n.kind == Node::Kind::Track)
        {
            s.kind = SelectionKind::Track;
            s.label = n.track ? (n.track->title.empty() ? n.track->path.stem().string()
                                                        : n.track->title)
                              : n.label;
        }
        else
        {
            if (_mode == Mode::Directory)
                s.kind = SelectionKind::DirectoryGroup;
            else if (n.depth == 0)
                s.kind = SelectionKind::Artist;
            else
                s.kind = SelectionKind::Album;
            s.label = n.label;
        }

        collectTracks(nodeIdx, s.tracks);
        return s;
    }

    void LibraryView::sendSelectionToQueue(bool replace)
    {
        if (!_onSend)
            return;
        if (_visible.empty())
            return;
        if (_selectedIndex < 0 || _selectedIndex >= static_cast<int>(_visible.size()))
            return;

        std::vector<TrackInfo> out;
        collectTracks(_visible[_selectedIndex], out);
        if (out.empty())
            return;
        _onSend(std::move(out), replace);
    }

    bool LibraryView::handleKey(ventty::KeyEvent const &event)
    {
        if (_visible.empty())
            return false;

        auto const visSize = static_cast<int>(_visible.size());

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
            if (_selectedIndex < visSize - 1)
            {
                _selectedIndex++;
                scrollToSelected();
            }
            return true;
        }
        if (event.key == Key::PageUp)
        {
            int const step = std::max(1, rect().height - 2);
            _selectedIndex = std::max(0, _selectedIndex - step);
            scrollToSelected();
            return true;
        }
        if (event.key == Key::PageDown)
        {
            int const step = std::max(1, rect().height - 2);
            _selectedIndex = std::min(visSize - 1, _selectedIndex + step);
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
            _selectedIndex = visSize - 1;
            scrollToSelected();
            return true;
        }

        auto &current = _nodes[_visible[_selectedIndex]];

        if (event.key == Key::Right)
        {
            if (current.kind == Node::Kind::Group && !current.expanded)
            {
                current.expanded = true;
                recomputeVisible();
            }
            return true;
        }
        if (event.key == Key::Left)
        {
            if (current.kind == Node::Kind::Group && current.expanded)
            {
                current.expanded = false;
                recomputeVisible();
            }
            else if (current.parent != kNoIdx)
            {
                // Move cursor up to the parent.
                for (std::size_t v = 0; v < _visible.size(); ++v)
                {
                    if (_visible[v] == current.parent)
                    {
                        _selectedIndex = static_cast<int>(v);
                        scrollToSelected();
                        break;
                    }
                }
            }
            return true;
        }

        // Enter: replace the play queue with every track under the selection
        // (artist / album / single track) and start playing. Use Left/Right to
        // expand or collapse groups instead. ('a' to append is handled by the
        // global key handler so Hangul-IME normalization applies.)
        if (event.key == Key::Enter)
        {
            sendSelectionToQueue(/*replace=*/true);
            return true;
        }

        if (event.key == Key::Char && event.ch == U'/' && !event.ctrl && !event.alt)
        {
            if (_onSearch)
                _onSearch();
            return true;
        }

        return false;
    }

    bool LibraryView::handleMouse(ventty::MouseEvent const &event)
    {
        auto const &r = rect();
        if (!r.contains(event.x, event.y))
            return false;

        using Button = ventty::MouseEvent::Button;
        using Action = ventty::MouseEvent::Action;

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
            if (_selectedIndex < static_cast<int>(_visible.size()) - 1)
            {
                _selectedIndex++;
                scrollToSelected();
            }
            return true;
        }

        if (event.button == Button::Left && event.action == Action::Press)
        {
            int const listY = r.y + 2;
            if (event.y >= listY)
            {
                int const row = event.y - listY;
                int const visIdx = _scrollOffset + row;
                if (visIdx >= 0 && visIdx < static_cast<int>(_visible.size()))
                {
                    _selectedIndex = visIdx;
                }
            }
            return true;
        }
        return false;
    }

} // namespace vtplayer
