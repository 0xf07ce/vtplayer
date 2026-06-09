// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "../audio/AudioEngine.h"
#include "../config/Config.h"
#include "../library/MediaLibrary.h"
#include "../plugin/PluginHost.h"
#include "../util/PlaylistStore.h"
#include "../view/ConfirmDialog.h"
#include "../view/ContextMenu.h"
#include "../view/FileBrowser.h"
#include "../view/HeaderBar.h"
#include "../view/LibraryView.h"
#include "../view/LibrarySearchDialog.h"
#include "../view/PlaylistsView.h"
#include "../view/PlayQueueView.h"
#include "../view/TagEditDialog.h"
#include "../view/TextInputDialog.h"
#include "../view/Theme.h"
#include "../view/TransportBar.h"
#include "../view/VisualizerView.h"
#include "../visualizer/AudioSpectrum.h"

#include <ventty/terminal/TerminalBase.h>

#include "../library/LibraryScanner.h"

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace vtplayer
{

    class LibraryRepository;

    enum class Screen
    {
        Browser,
        Visualizer,
        Help,
    };

    enum class FocusPanel
    {
        FileBrowser,
        PlayQueue,
    };

    /// Tabs on the Help screen, switched with Tab / Left / Right.
    enum class HelpTab
    {
        Shortcuts, ///< keyboard shortcut reference (default)
        Plugins,   ///< list of currently loaded plugins
    };

    /// Left-panel mode on the Browser screen. Three projections of the
    /// indexed MediaLibrary (rendered by LibraryView) plus the live
    /// FileBrowser:
    ///   1 (AlbumArtistTree) — Grouping > AlbumArtist > Album > Track, label "Album"
    ///   2 (ArtistTree)      — Grouping > Artist      > Album > Track, label "Artist"
    ///   3 (Directory)       — folder tree under the library root
    ///   4 (FileBrowser)     — live filesystem from the launch CWD
    /// Both library tree modes share the same four-level shape; the
    /// depth-0 axis is `TrackInfo::grouping` (ID3v2 TIT1 / Vorbis GROUPING
    /// / MP4 ©grp) and the depth-1 axis differs by mode.
    enum class LeftMode
    {
        AlbumArtistTree,
        ArtistTree,
        Directory,
        FileBrowser,
        Playlists,  // 5 — saved-playlist browser (PlaylistsView)
    };

    /// Which widget currently occupies the Browser-screen left slot. The slot
    /// used to be a binary (library vs filebrowser); Playlists adds a third
    /// occupant, so draw/input/mouse/focus routing switches on this instead of
    /// the `leftIsLibrary()` bool.
    enum class LeftSlot
    {
        Library,
        FileBrowser,
        Playlists,
    };

    /// Actions in the ESC context menu. The visible item set is built
    /// dynamically per `LeftMode`, so selection maps through an action
    /// list rather than fixed indices.
    enum class MenuAction
    {
        SetLibraryRoot,
        GoToLibraryRoot,
        RescanLibrary,
        LocatePlaying,
        CreatePlaylist,
        RenamePlaylist,
        DeletePlaylist,
        EditPlaylist,
        SavePlaylist,
        CancelPlaylistEdit,
        Exit,
    };

    class Application
    {
    public:
        Application();
        ~Application();

        void setInitialFile(std::filesystem::path path) { _initialFile = std::move(path); }
        void setInitialDirectory(std::filesystem::path path) { _initialDir = std::move(path); }
        void setDebug(bool debug) { _debug = debug; }

        int run();
        void quit();

    private:
        void init();
        void initTerminal();
        void cleanup();

        void resize();
        void draw();

        /// Compute the next idle-timeout (ms) for the main loop. Visualizer
        /// screen uses the configured FPS; Browser/Help screens fall back to
        /// a long sleep when idle so the process doesn't spin at 60 Hz.
        int  computeIdleTimeoutMs() const;
        /// Block on STDIN for up to `ms` milliseconds. Returns when input is
        /// available, on EINTR, or on timeout. Used in place of a fixed
        /// sleep_for so the loop wakes immediately on the next keystroke.
        void waitForInputOrTimeout(int ms) const;
        void drawBrowserScreen();
        void drawVisualizerScreen();
        void drawHelpScreen();
        void updateUI();
        void toggleHelp();
        /// Rebuild `_helpRows` for the active tab (`_helpTab`).
        void buildHelpRows();
        /// Keyboard-shortcut reference rows (Shortcuts tab).
        void buildShortcutRows();
        /// One row per loaded plugin (Plugins tab).
        void buildPluginRows();
        /// Select a help tab, rebuild its rows, and reset the scroll offset.
        void setHelpTab(HelpTab tab);
        /// Draw the tab strip on the Help screen's first content row.
        void drawHelpTabBar(int row);
        /// (Re)flow _helpRows into _helpLines for the current terminal width,
        /// word-wrapping descriptions. No-op if the width is unchanged.
        void ensureHelpLayout() const;
        int helpVisibleRows() const;
        int helpMaxScroll() const;

        void handleInput(ventty::KeyEvent const &event);
        void handleMouse(ventty::MouseEvent const &event);
        void handleGlobalKeys(ventty::KeyEvent const &event);
        void openContextMenu();
        void onContextMenuSelect(int index);

        /// Snapshot of "where focus is and what is selected" at the moment the
        /// ESC menu opens. classifyMenuContext() fills it from current state;
        /// buildContextMenu() turns it into the prepared item set. This replaces
        /// the old inline if/else chain so per-context menus extend cleanly.
        struct MenuContext
        {
            bool     queueFocused = false;            ///< right (PlayQueue) panel focused
            LeftSlot leftSlot = LeftSlot::Library;    ///< active left widget otherwise
            bool     playlistsEmpty = true;
            bool     playlistsInContents = false;     ///< PlaylistsView drilled into a playlist
            bool     playlistsEditMode = false;        ///< contents view armed for editing
            bool     libraryRootConfigured = false;
            // Extension point: per-selection menus can branch on the focused
            // widget's selection kind, already queryable via
            // LibraryView::currentSelection().kind and
            // FileBrowser::selectedEntry()->isDirectory. Not needed for the
            // current item sets, so deliberately not snapshotted here yet.
        };
        MenuContext classifyMenuContext() const;
        void buildContextMenu(MenuContext const & ctx,
                              std::vector<std::string> & items,
                              std::vector<MenuAction> & actions) const;

        /// 't' handler: figure out what the user is pointing at in the
        /// focused panel and open the tag editor with the right scope.
        void openTagEditor();

        /// Save callback: write tags to disk, refresh the in-memory library
        /// + repository, and rebuild dependent views.
        void applyTagEdit(std::vector<std::filesystem::path> const & targets,
                          TagUpdate const & update);

        /// Switch the Browser-screen left panel. Applies the corresponding
        /// LibraryView grouping (for the library modes), fixes focus on the
        /// now-visible widget, and requests a redraw.
        void setLeftMode(LeftMode mode);

        /// True when the left panel is a MediaLibrary projection
        /// (AlbumArtistTree / ArtistTree / Directory) rather than the live
        /// FileBrowser.
        // During a scan the LibraryView tree is dropped (pass 1) and not
        // rebuilt until finalizeScan() (its results land all at once), so the
        // left panel behaves as the FileBrowser for drawing, input and mouse
        // routing for the whole scan — both passes.
        bool leftIsLibrary() const
        {
            return (_leftMode == LeftMode::AlbumArtistTree
                    || _leftMode == LeftMode::ArtistTree
                    || _leftMode == LeftMode::Directory)
                   && !_collectActive.load() && !_ingestActive.load();
        }

        /// True when the left panel is the saved-playlist browser (mode 5).
        bool leftIsPlaylists() const { return _leftMode == LeftMode::Playlists; }

        /// Which widget currently fills the left slot. Keeps the existing
        /// `leftIsLibrary()` semantics intact: Playlists is its own slot, and
        /// anything that isn't Library or Playlists falls back to FileBrowser
        /// (including during a scan, as before).
        LeftSlot activeLeftWidget() const
        {
            if (_leftMode == LeftMode::Playlists) return LeftSlot::Playlists;
            return leftIsLibrary() ? LeftSlot::Library : LeftSlot::FileBrowser;
        }

        /// Re-list playlists from disk into PlaylistsView.
        void refreshPlaylists();

        /// Drill into a playlist (Enter on a PlaylistsView list row): read the
        /// named playlist, resolve each entry against the library for richer
        /// metadata, and hand the tracks to PlaylistsView::showContents() so the
        /// panel switches to its FileBrowser-style contents view. Does not touch
        /// the play queue — that happens later from the contents view (Enter on
        /// a track = replace + play, `a` = append).
        void openPlaylistContents(std::string const & name);

        /// Read a playlist file and resolve each entry against the library for
        /// richer metadata (album / grouping / ReplayGain), falling back to the
        /// bare M3U entry for external paths. nullopt when the file is
        /// unreadable. Shared by openPlaylistContents() and the add-to-playlist
        /// refresh path.
        std::optional<std::vector<TrackInfo>>
        resolvePlaylistTracks(std::string const & name) const;

        /// Push the play queue header title: the active playlist's name, or the
        /// default "Play Queue" when `_currentPlaylistName` is empty.
        void applyQueueTitle();

        /// Map a failed create()/rename() to a user-facing hint for the name
        /// dialog: a collision with an existing playlist vs an invalid name.
        std::string playlistNameError(std::string const & name) const;

        /// `b` handler: open a modal picker listing the saved playlists so the
        /// focused/selected track(s) can be appended to one. The track set
        /// depends on context (collectAddToPlaylistTracks): the playing track on
        /// the Visualizer screen, the play-queue selection, a Library group's
        /// tracks (disabled at the top-level Grouping axis), or the FileBrowser
        /// selection. No-op when the set is empty or no playlists exist. The
        /// list order mirrors the mode-5 PlaylistsView (PlaylistStore::list),
        /// except the playlist most recently used by this picker in the current
        /// session is floated to the top (`_lastAddedPlaylist`).
        void openAddToPlaylistMenu();

        /// Gather the tracks the add-to-playlist picker should append, given the
        /// current screen/focus, and fill `outTitle` with a picker heading. An
        /// empty result means the action is disabled in the current context.
        std::vector<TrackInfo> collectAddToPlaylistTracks(std::string & outTitle) const;

        /// Selection callback for the add-to-playlist picker: append the
        /// captured `_addToPlaylistTracks` to `_addToPlaylistNames[index]` and
        /// remember it as the session's most-recently-used playlist.
        void onAddToPlaylistSelect(int index);

        /// Focus whichever of the three left widgets is currently active (per
        /// activeLeftWidget()) and unfocus the others. Replaces the old
        /// two-widget focus sync.
        void setLeftFocused(bool on);

        /// Show/hide the Browser-screen left panel (Library / FileBrowser).
        /// When hidden, PlayQueueView takes the full content width and focus
        /// is pinned to it. Bound to the `l` key.
        void setLibraryPanelVisible(bool visible);
        void setVisualizerByIndex(int index);

        void playTrack(int index);
        void playNext();
        void playPrev();
        void addToPlayQueue(std::filesystem::path const &path);
        void activateFromBrowser(std::vector<std::filesystem::path> const &paths);

        /// Flip shuffle mode. Turning it on rebuilds `_shuffleOrder` with the
        /// currently-playing (or otherwise about-to-play) track at the head;
        /// turning it off discards the order. The visible play queue order
        /// never changes — only future prev/next/auto-advance lookups do.
        void toggleShuffleMode();

        /// Build `_shuffleOrder` from the current play queue. When
        /// `seedIndex` is a valid queue index, that track is pinned at
        /// position 0 and the rest is shuffled after it; otherwise the
        /// whole queue is shuffled with no seed.
        void rebuildShuffleOrder(int seedIndex);

        /// Walk `_shuffleOrder` forward (`dir=+1`) or backward (`dir=-1`),
        /// skipping any entries no longer in the play queue, and return
        /// the play-queue index of the next reachable track. -1 means the
        /// pass is exhausted in that direction. When `wrap` is true the
        /// walk wraps cyclically; otherwise it stops at the end.
        /// `_shufflePos` is advanced to the returned position.
        int shuffleAdvance(int dir, bool wrap);

        /// After an explicit play (Enter, library activate, etc.), keep
        /// the shuffle pointer in sync: jump `_shufflePos` to the track's
        /// position in `_shuffleOrder`, or rebuild the order with this
        /// track as the new head if it isn't present. No-op when shuffle
        /// mode is off.
        void syncShuffleTo(int queueIndex);

        /// Resolve the path at `_shuffleOrder[_shufflePos]` to a current
        /// play-queue index. -1 if the order is empty or stale.
        int currentShuffleQueueIndex() const;

        /// Read an .m3u file and append its tracks to the current play queue.
        void appendPlayQueueFile(std::filesystem::path const &path);

        /// Incremental scan of the configured `libraryRoot`. Pass 1 (the
        /// filesystem walk) runs *inline on the calling (UI) thread*, pumping
        /// input and repainting via the tick callback so ESC can cancel it;
        /// it blocks the run loop while it walks. Pass 2 (tag reading +
        /// repository writes) is then launched on `_ingestThread` and writes
        /// only `_libraryRepo`, never `_library`, so the UI keeps using the
        /// pre-scan `_library` snapshot. No-op when the root is unset, the
        /// repository failed to open, or an ingest is already running.
        ///
        /// When `force` is false (startup path) and the library root's
        /// signature still matches the one persisted after the last scan
        /// (`Config::scanSig`) and the DB index is non-empty, the whole scan
        /// is skipped — the persisted index is trusted. `force` (menu rescan,
        /// root change) always scans.
        void scanLibrary(bool force = false);

        /// UI-thread completion handler: joins the ingest worker, reloads
        /// `_library` from the now-updated repository, rebuilds the
        /// LibraryView and clears ingest state. Called from the run loop once
        /// `_ingestFinished` is observed.
        void finalizeScan();

        /// Join the ingest worker (if any). Safe to call when none is active.
        /// Used on shutdown — pass 2 is not cancellable, so this waits.
        void joinScanThread();

        /// Draw the unobtrusive bottom-right scan status into `_rootWindow`:
        /// "Collecting N" during pass 1, "NN%" during pass 2, nothing when
        /// idle. Called near the end of draw(); does not flush.
        void drawScanStatus();

        /// Re-point the library at `root`, wiping any prior index, then scan.
        void setLibraryRoot(std::filesystem::path root);

        /// Switch the left panel to Library and move the cursor to the track
        /// that is currently playing. No-op if nothing is playing or the
        /// track isn't in the index.
        void locatePlayingInLibrary();

        bool _running = false;
        std::unique_ptr<ventty::TerminalBase> _terminal;
        ventty::Window *_rootWindow = nullptr;

        // Audio
        AudioEngine _audio;
        Config _config;

        // Dynamically loaded plugins. Loaded early in init() (so plugin file
        // extensions are known before the browser/scanner are configured) and
        // unloaded in cleanup() AFTER the audio engine stops — an active
        // source can hold pointers into a plugin's code pages.
        PluginHost _pluginHost;

        // Media library (track index of the configured root directory)
        MediaLibrary _library;
        std::unique_ptr<LibraryRepository> _libraryRepo;

        // UI state
        Screen _screen = Screen::Browser;
        Screen _previousScreen = Screen::Browser; // restored when leaving Help
        FocusPanel _focus = FocusPanel::FileBrowser;
        LeftMode _leftMode = LeftMode::AlbumArtistTree;
        bool _libraryPanelVisible = true; // `l` toggles the left panel

        /// Last focused track in a library projection (1/2/3). Saved when
        /// leaving such a mode and re-applied (locate) when entering one, so
        /// the cursor survives mode switches and FileBrowser round-trips.
        std::filesystem::path _libraryAnchor;

        // Two-phase library scan.
        //
        // Pass 1 (collect) runs inline on the UI thread; `_collectActive`
        // gates input to ESC-only and `_collectCancel` (set by ESC) aborts
        // the walk. `_collectCount` is the running file count for the status.
        //
        // Pass 2 (ingest) runs on `_ingestThread` and writes only the
        // repository, so the UI may keep reading the pre-scan `_library`.
        // `_ingestActive` is true while it runs (the LibraryView is hidden —
        // see leftIsLibrary()); `_ingestPercent` is the 0..100 status value;
        // the worker sets `_ingestFinished` when done and the run loop then
        // calls finalizeScan() (the join is the synchronization barrier).
        std::thread       _ingestThread;
        std::atomic<bool> _collectActive{false};
        std::atomic<bool> _collectCancel{false};
        std::atomic<int>  _collectCount{0};
        std::atomic<bool> _ingestActive{false};
        std::atomic<bool> _ingestFinished{false};
        std::atomic<bool> _ingestStop{false}; ///< shutdown: bail ingest early
        std::atomic<int>  _ingestPercent{0};
        // Root signature captured just before pass 1 (the state we actually
        // scanned). Persisted to Config::scanSig in finalizeScan() once the
        // ingest completes, so the next startup can skip an unchanged tree.
        std::string       _pendingScanSig;

        struct HelpRow
        {
            std::string left;
            std::string right;
            bool isHeader = false;
        };
        std::vector<HelpRow> _helpRows;

        // Help is laid out into physical display lines for the current
        // width: long descriptions word-wrap, and scrolling counts wrapped
        // lines. Rebuilt lazily when the width changes (see ensureHelpLayout).
        struct HelpSpan
        {
            int x = 0;
            std::string text;
            int kind = 0; ///< 0 = header, 1 = key, 2 = description
        };
        struct HelpLine
        {
            std::vector<HelpSpan> spans;
        };
        mutable std::vector<HelpLine> _helpLines;
        mutable int _helpLayoutWidth = -1;

        // Help screen reserves its first two content rows for the tab strip
        // (tab labels + a blank spacer); the scrollable body starts below it.
        static constexpr int kHelpTabRows = 2;
        HelpTab _helpTab = HelpTab::Shortcuts;
        int _helpScroll = 0;
        Theme _theme;
        int _visualizerIndex = 1; // 1 = AudioSpectrum (default), 0 = Oscilloscope
        RepeatMode _repeatMode = RepeatMode::None;

        // Shuffle state. Session-only — not persisted to config.
        //
        // `_shuffleOrder` is the playback order while shuffle is on, stored
        // as paths so it survives reorders, removes, and inserts in the
        // visible play queue. `_shufflePos` is the index into that order
        // representing the currently-playing entry; -1 when the order is
        // empty/unset. Walks (prev/next/auto-advance) consult this order
        // instead of the queue's natural index sequence.
        bool _shuffleMode = false;
        std::vector<std::filesystem::path> _shuffleOrder;
        int _shufflePos = -1;

        // Optional source name shown as the play-queue header title, or empty
        // for the default "Play Queue". Session-only — currently always empty
        // (any queue mutation clears it via PlayQueueView::OnContentsChanged);
        // kept as an extension point for a future "queue names its source" UX.
        std::string _currentPlaylistName;

        // Saved playlist most recently used by the `b` (add-to-playlist) picker
        // this session. Floated to the top of the picker on the next open so a
        // run of additions to the same playlist needs no re-navigation. Empty
        // until the first successful append; session-only (not persisted).
        std::string _lastAddedPlaylist;

        // Names backing the add-to-playlist picker's visible rows, in display
        // order. Parallel to the menu items so onAddToPlaylistSelect() can map
        // the selected index back to a playlist name.
        std::vector<std::string> _addToPlaylistNames;

        // Tracks captured when the add-to-playlist picker opened; appended to
        // the chosen playlist on confirm. Snapshotting at open time keeps the
        // modal robust against selection changes (it can't change while modal).
        std::vector<TrackInfo> _addToPlaylistTracks;

        // Views
        std::unique_ptr<HeaderBar> _headerBar;
        std::unique_ptr<FileBrowser> _fileBrowser;
        std::unique_ptr<LibraryView> _libraryView;
        std::unique_ptr<PlaylistsView> _playlistsView;
        std::unique_ptr<PlayQueueView> _playQueueView;
        std::unique_ptr<TransportBar> _transportBar;
        std::unique_ptr<VisualizerView> _visualizerView;
        std::unique_ptr<ContextMenu> _contextMenu;
        /// Separate modal picker for the `b` add-to-playlist flow, kept distinct
        /// from `_contextMenu` so the ESC menu's title / items / callback are
        /// never clobbered.
        std::unique_ptr<ContextMenu> _addToPlaylistMenu;
        /// Parallel to the menu's visible items: maps the selected index
        /// back to an action (the item set varies with `_leftMode`).
        std::vector<MenuAction> _contextMenuActions;
        std::unique_ptr<LibrarySearchDialog> _searchDialog;
        std::unique_ptr<TagEditDialog> _tagEditDialog;
        std::unique_ptr<TextInputDialog> _textInputDialog;
        std::unique_ptr<ConfirmDialog> _confirmDialog;

        // Saved-playlist storage (~/.config/vtplayer/playlists/). Fixed dir.
        PlaylistStore _playlistStore;

        // Startup positional argument. At most one is set: _initialFile is a
        // single track to queue+play; _initialDir is a folder to open in the
        // FileBrowser. Either forces FileBrowser (4) mode at launch.
        std::filesystem::path _initialFile;
        std::filesystem::path _initialDir;

        // --debug: keep ffmpeg's stderr on the terminal instead of /dev/null.
        bool _debug = false;
    };

} // namespace vtplayer
