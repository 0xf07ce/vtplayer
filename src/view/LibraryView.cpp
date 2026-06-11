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

        /// How far a "derived" album-artist/album node is blended toward the
        /// background — a slightly darker tint than its normal level color.
        constexpr float kDerivedDim = 0.4f;

        /// Title (or filename stem when the title tag is empty).
        std::string trackTitle(TrackInfo const &t)
        {
            return t.title.empty() ? t.path.stem().string() : t.title;
        }

        /// Stream channels carry no meaningful disc/track numbering, so they
        /// keep the simple "N. title" / "title" form.
        std::string formatStreamLabel(TrackInfo const &t)
        {
            if (t.trackNumber > 0)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "%d. ", t.trackNumber);
                return std::string(buf) + trackTitle(t);
            }
            return trackTitle(t);
        }

        /// Label every leaf of one album, deciding disc/track display from the
        /// album as a whole:
        ///   - any track missing a track number  → every row shows just {title}
        ///   - else discs shown only when every track has one AND they differ:
        ///       disc shown  → "{disc}.{track}. {title}"
        ///       disc hidden → "{track}. {title}"
        void formatAlbumTrackLabels(std::vector<TrackInfo const *> const &tracks,
                                    std::vector<std::string> &out)
        {
            out.clear();
            out.reserve(tracks.size());

            bool anyMissingTrack = false;
            bool anyMissingDisc = false;
            bool allSameDisc = true;
            int firstDisc = tracks.empty() ? 0 : tracks.front()->discNumber;
            for (auto const *t : tracks)
            {
                if (t->trackNumber <= 0) anyMissingTrack = true;
                if (t->discNumber <= 0) anyMissingDisc = true;
                if (t->discNumber != firstDisc) allSameDisc = false;
            }
            bool const showDisc = !anyMissingDisc && !allSameDisc;

            for (auto const *t : tracks)
            {
                if (anyMissingTrack)
                {
                    out.push_back(trackTitle(*t));
                    continue;
                }
                char buf[16];
                if (showDisc)
                    std::snprintf(buf, sizeof(buf), "%d.%d. ", t->discNumber, t->trackNumber);
                else
                    std::snprintf(buf, sizeof(buf), "%d. ", t->trackNumber);
                out.push_back(std::string(buf) + trackTitle(*t));
            }
        }

        /// Strip ASCII leading/trailing whitespace. Combined with `toNfc`
        /// this folds the two most common "looks identical, sorts apart"
        /// causes for tag values — NFC/NFD divergence and accidental
        /// trailing spaces (e.g. "015B" vs "015B ").
        std::string trimAscii(std::string s)
        {
            auto issp = [](unsigned char c) {
                return c == ' ' || c == '\t' || c == '\r' || c == '\n';
            };
            std::size_t b = 0;
            while (b < s.size() && issp(static_cast<unsigned char>(s[b])))
                ++b;
            std::size_t e = s.size();
            while (e > b && issp(static_cast<unsigned char>(s[e - 1])))
                --e;
            return s.substr(b, e - b);
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
        // current mode's key resets the tree to its default fold state
        // (every mode starts fully collapsed).
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

        // Header count reflects what the UI label promises. Computed once
        // here rather than per-frame in draw():
        //   Album label  (AlbumArtistTree) → #albums   (depth-2 groups)
        //   Artist label (ArtistTree)      → #artists  (depth-1 groups)
        //   Directory                       → #tracks
        if (_mode == Mode::ArtistTree)
        {
            for (auto const &n : _nodes)
            {
                if (n.kind == Node::Kind::Group && n.depth == 1)
                    ++_headerCount;
            }
        }
        else if (_mode == Mode::AlbumArtistTree)
        {
            for (auto const &n : _nodes)
            {
                if (n.kind == Node::Kind::Group && n.depth == 2)
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
            // Stream channels carry a synthetic path `<abs_pls>#CH<N>`.
            // Promote the .pls file itself to a group so its channels nest
            // underneath it (labelled by the channel title) instead of
            // appearing as a single leaf called "foo.pls#CH1".
            bool isStreamChannel = false;
            std::filesystem::path filePath = t.path;
            if (t.isStream())
            {
                auto const &sp = t.path.native();
                auto const hash = sp.rfind(
                    static_cast<std::filesystem::path::value_type>('#'));
                if (hash != std::filesystem::path::string_type::npos
                    && sp.compare(hash, 3,
                                  std::filesystem::path::string_type{'#', 'C', 'H'})
                           == 0)
                {
                    filePath = std::filesystem::path(sp.substr(0, hash));
                    isStreamChannel = true;
                }
            }

            // Path components relative to the library root.
            std::filesystem::path rel;
            if (!root.empty())
            {
                rel = filePath.lexically_relative(root);
                if (rel.empty() || rel.native()[0] == '.')
                    rel = filePath;
            }
            else
            {
                rel = filePath;
            }

            std::vector<std::string> parts;
            for (auto const &p : rel)
                parts.push_back(toNfc(p.string()));
            if (parts.empty())
                continue;

            std::size_t parent = kNoIdx;
            int depth = 0;
            // Stream channels treat every path component — including the
            // `.pls` filename — as a group, since the real leaf is the
            // channel. Regular files keep the last component as the leaf.
            std::size_t const groupCount =
                isStreamChannel ? parts.size() : parts.size() - 1;
            for (std::size_t i = 0; i < groupCount; ++i)
            {
                parent = findOrCreateGroup(parent, parts[i], depth);
                depth++;
            }

            Node trackNode;
            trackNode.kind = Node::Kind::Track;
            if (isStreamChannel)
            {
                // Channel name parsed by PlsReader (TitleN= or hostname
                // fallback). The .pls filename is the group above.
                trackNode.label = toNfc(
                    !t.title.empty() ? t.title : t.path.filename().string());
            }
            else
            {
                // Directory mode is a filesystem view, so show the actual
                // file name (matching FileBrowser) rather than "N. Title"
                // — that tag-based form is reserved for the Artist/Album
                // modes.
                trackNode.label = toNfc(t.path.filename().string());
            }
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
            // Both library projection modes start fully collapsed; locate()
            // unfolds only the ancestors of the focused track on demand.
            n.expanded = false;
            n.parent = parentIdx;
            std::size_t const idx = _nodes.size();
            _nodes.push_back(std::move(n));
            if (parentIdx == kNoIdx)
                _roots.push_back(idx);
            else
                _nodes[parentIdx].children.push_back(idx);
            return idx;
        };

        // Local-file tracks build a four-level tree: Grouping → Artist →
        // Album → Track.
        //
        // Depth-0 key is `TrackInfo::grouping` (ID3v2 TIT1 / Vorbis GROUPING
        // / MP4 ©grp) — a single user-defined top-level bucket like "kpop",
        // "pop", "jazz". Empty grouping → `(ungrouped)` (depth 0 uses its
        // own "missing" label to keep the empty-grouping case visually
        // distinct from empty-artist `(null)` one level down).
        //
        // Depth-1 (album-artist axis):
        //   Mode 2 (ArtistTree, UI "Artist") reads `artist`; empty → the
        //   `#unknown_artist` sentinel.
        //   Mode 1 (AlbumArtistTree, UI "Album") reads `albumArtist`, falling
        //   back to `artist` when `albumArtist` is empty. A track grouped via
        //   that fallback is "derived".
        //
        // Depth-2 (album axis) only exists for tracks that carry a *real*
        // `albumArtist`. A derived track (no album-artist, grouped under the
        // `artist` fallback) skips the album axis entirely: it renders as a
        // title-only leaf directly under the artist node (depth 2, where an
        // album would sit), a shade darker so "minor was tagged by artist
        // only" reads distinctly from a real album-artist's tracks. The
        // artist node itself renders dark only when it has *no* real
        // album-artist album under it (every track derived). When both tags
        // are empty the track is *not* derived (no fallback happened) → it
        // stays in the album structure under `#unknown_artist`.
        //
        // Tag values are treated as opaque strings ("거미,휘성", "거미, 휘성",
        // "휘성,거미" → three distinct groups; no comma-splitting), but
        // visually-identical values are folded together: keys are
        // NFC-normalized and trimmed before grouping, so a track tagged
        // "015B" and another tagged "015B " (or NFD-encoded "015B") land
        // in the same group.
        //
        // Stream channels (URL entries inside `.pls` playlists) build a
        // *shallower* three-level subtree under a forced `#stream` group:
        // `#stream` → <pls stem> → channel. The album axis is skipped
        // entirely because PlsReader pins both artist and album to the
        // pls stem, which would otherwise show the same label twice in a
        // row. Channel leaves live at depth 2 (instead of 3 for local
        // files); the rest of the view treats depth opaquely so the
        // mixed-depth shape is safe.
        //
        // std::map keeps every axis alphabetically ordered for stable
        // display. Root and depth-1 order is then overridden by
        // sortNodeChildren() to pin `#stream`, `#ungrouped`/`#unknown_artist`
        // to the top.
        bool const albumArtistMode = (_mode == Mode::AlbumArtistTree);
        // One bucket per (grouping, artist). Real album-artist tracks group by
        // album name; derived tracks (album_artist empty → artist fallback)
        // skip the album axis and become title-only leaves directly under the
        // artist. In ArtistTree mode nothing is derived, so `directTracks`
        // stays empty and every track lands in `albums` as before.
        struct ArtistBucket
        {
            std::map<std::string, std::vector<TrackInfo const *>> albums;
            std::vector<TrackInfo const *> directTracks;
        };
        // grouping → artist label → bucket.
        std::map<std::string, std::map<std::string, ArtistBucket>> tree;
        std::map<std::string, std::vector<TrackInfo const *>> streamTree;
        for (auto const &t : _library->tracks())
        {
            if (t.isStream())
            {
                std::string plsStem = trimAscii(toNfc(t.album));
                if (plsStem.empty())
                    plsStem = "#unknown_stream";
                streamTree[plsStem].push_back(&t);
                continue;
            }
            std::string grouping = trimAscii(toNfc(t.grouping));
            if (grouping.empty())
                grouping = "#ungrouped";

            // Resolve the album-artist identity {label, derived}.
            std::string artist;
            bool derived = false;
            if (albumArtistMode)
            {
                artist = trimAscii(toNfc(t.albumArtist));
                if (artist.empty())
                {
                    std::string const fallback = trimAscii(toNfc(t.artist));
                    if (!fallback.empty())
                    {
                        artist = fallback;
                        derived = true;
                    }
                }
            }
            else
            {
                artist = trimAscii(toNfc(t.artist));
            }
            if (artist.empty())
                artist = "#unknown_artist";

            ArtistBucket &bucket = tree[grouping][artist];
            if (derived)
            {
                // album_artist absent → album axis skipped; title-only leaf.
                bucket.directTracks.push_back(&t);
            }
            else
            {
                std::string albumRaw = trimAscii(toNfc(t.album));
                std::string album = albumRaw.empty() ? "#unknown_album" : std::move(albumRaw);
                bucket.albums[std::move(album)].push_back(&t);
            }
        }

        // Stream branch (mixed depth: tracks land at depth 2).
        if (!streamTree.empty())
        {
            std::size_t const streamIdx = createGroup(kNoIdx, "#stream", 0);
            for (auto const &[plsStem, tracks] : streamTree)
            {
                std::size_t const plsIdx = createGroup(streamIdx, plsStem, 1);
                for (auto const *t : tracks)
                {
                    Node trackNode;
                    trackNode.kind = Node::Kind::Track;
                    trackNode.label = formatStreamLabel(*t);
                    trackNode.depth = 2;
                    trackNode.track = t;
                    trackNode.parent = plsIdx;
                    std::size_t const idx = _nodes.size();
                    _nodes.push_back(std::move(trackNode));
                    _nodes[plsIdx].children.push_back(idx);
                }
            }
        }

        // Local-file branch: real-album tracks at depth 3, derived (album-
        // artist-less) tracks as depth-2 leaves directly under the artist.
        std::vector<std::string> leafLabels;
        for (auto const &[groupKey, artists] : tree)
        {
            std::size_t const groupIdx = createGroup(kNoIdx, groupKey, 0);
            for (auto const &[artistKey, bucket] : artists)
            {
                std::size_t const artistIdx = createGroup(groupIdx, artistKey, 1);
                // Derived-dark only when the artist has no real album-artist
                // album at all (every track under it came via the fallback).
                _nodes[artistIdx].derived = bucket.albums.empty();

                for (auto const &[albumName, tracks] : bucket.albums)
                {
                    std::size_t const albumIdx =
                        createGroup(artistIdx, albumName, 2);

                    formatAlbumTrackLabels(tracks, leafLabels);
                    for (std::size_t k = 0; k < tracks.size(); ++k)
                    {
                        Node trackNode;
                        trackNode.kind = Node::Kind::Track;
                        trackNode.label = leafLabels[k];
                        trackNode.depth = 3;
                        trackNode.track = tracks[k];
                        trackNode.parent = albumIdx;
                        std::size_t const idx = _nodes.size();
                        _nodes.push_back(std::move(trackNode));
                        _nodes[albumIdx].children.push_back(idx);
                    }
                }

                // Derived tracks: title-only, sit where an album would (depth
                // 2), rendered a shade darker via the Track `derived` flag.
                for (auto const *t : bucket.directTracks)
                {
                    Node trackNode;
                    trackNode.kind = Node::Kind::Track;
                    trackNode.label = trackTitle(*t);
                    trackNode.depth = 2;
                    trackNode.derived = true;
                    trackNode.track = t;
                    trackNode.parent = artistIdx;
                    std::size_t const idx = _nodes.size();
                    _nodes.push_back(std::move(trackNode));
                    _nodes[artistIdx].children.push_back(idx);
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

        // Pin synthetic labels to the top in a fixed order, ahead of the
        // alphabetical run. Pinning is depth-aware: groupings (depth 0)
        // pin `#stream` then `#ungrouped`; artists inside a grouping
        // (depth 1) pin `#unknown_artist`. Directory mode keeps pure
        // alphabetical ordering at every depth.
        auto groupingPriority = [&](std::string const &label) -> int {
            if (_mode == Mode::Directory) return 3;
            if (label == "#stream")    return 0;
            if (label == "#ungrouped") return 1;
            return 3;
        };
        auto artistPriority = [&](std::string const &label) -> int {
            if (_mode == Mode::Directory) return 3;
            // `#stream` appears at this depth only as the sole child of
            // the `#stream` grouping — pinning is harmless either way.
            if (label == "#stream")          return 0;
            if (label == "#unknown_artist")  return 1;
            return 3;
        };
        auto pinnedCmp = [&](auto priority) {
            return [&, priority](std::size_t a, std::size_t b) {
                int const pa = priority(_nodes[a].label);
                int const pb = priority(_nodes[b].label);
                if (pa != pb) return pa < pb;
                return cmp(a, b);
            };
        };

        std::sort(_roots.begin(), _roots.end(), pinnedCmp(groupingPriority));
        for (auto &n : _nodes)
        {
            // Children of a depth-0 grouping node are depth-1 artists —
            // apply the artist-level pinning there. Everywhere else
            // (album children, track children) use the default cmp.
            if (n.kind == Node::Kind::Group && n.depth == 0
                && _mode != Mode::Directory)
            {
                std::sort(n.children.begin(), n.children.end(),
                          pinnedCmp(artistPriority));
            }
            else
            {
                std::sort(n.children.begin(), n.children.end(), cmp);
            }
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
            // mode's grouping level. Both AlbumArtistTree and ArtistTree
            // are Grouping(0) > Artist(1) > Album(2) > Track(3); cross-
            // axis locate stops at the album so the user lands on the
            // album group regardless of which axis they came from.
            // Directory keeps drilling to the track itself (folders
            // expanded so the file is revealed).
            std::size_t sel = i;
            if (_mode == Mode::AlbumArtistTree || _mode == Mode::ArtistTree)
            {
                int const wantDepth = 2; // album level
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
        header += (_mode == Mode::AlbumArtistTree) ? "Album"
                  : (_mode == Mode::ArtistTree)    ? "Artist"
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

            // Color by hierarchy. ArtistAlbum mode is a four-level tree
            // (grouping/artist/album/track), so each depth gets its own
            // tint. Directory mode has no artist/album concept — groups
            // are just folders, so use the neutral file-browser directory
            // color and only the track tint. The synthetic labels —
            // `#ungrouped` at depth 0, `#unknown_artist` at depth 1,
            // `#unknown_album` at depth 2, and `#stream` wherever it
            // appears — each get the "missing tag" tint to set them apart
            // from real values. Derived album-artist / album nodes (the label
            // came from the `artist` fallback) render a shade darker.
            Color levelFg;
            if (n.kind == Node::Kind::Track)
                // Album-artist-less tracks shown directly under the artist
                // carry `derived` → dimmed, like the album nodes used to be.
                levelFg = n.derived
                    ? ventty::lerpColor(_theme.libraryTrackFg, _theme.browserBg, kDerivedDim)
                    : _theme.libraryTrackFg;
            else if (_mode == Mode::Directory)
                levelFg = _theme.playQueueFg;
            else if (n.label == "#stream")
                levelFg = _theme.libraryStreamFg;
            else if (n.depth == 0 && n.label == "#ungrouped")
                levelFg = _theme.libraryNullFg;
            else if (n.depth == 1 && n.label == "#unknown_artist")
                levelFg = _theme.libraryNullFg;
            else if (n.depth == 2 && n.label == "#unknown_album")
                levelFg = _theme.libraryNullFg;
            else if (n.depth == 0)
                levelFg = _theme.libraryGroupingFg;
            else if (n.depth == 1)
                levelFg = n.derived
                    ? ventty::lerpColor(_theme.libraryArtistFg, _theme.browserBg, kDerivedDim)
                    : _theme.libraryArtistFg;
            else
                levelFg = n.derived
                    ? ventty::lerpColor(_theme.libraryAlbumFg, _theme.browserBg, kDerivedDim)
                    : _theme.libraryAlbumFg;

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
                s.kind = SelectionKind::Grouping;
            else if (n.depth == 1)
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
