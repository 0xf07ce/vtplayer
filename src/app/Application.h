// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "../audio/AudioEngine.h"
#include "../config/Config.h"
#include "../library/MediaLibrary.h"
#include "../view/ContextMenu.h"
#include "../view/FileBrowser.h"
#include "../view/HeaderBar.h"
#include "../view/LibraryView.h"
#include "../view/LibrarySearchDialog.h"
#include "../view/PlayQueueView.h"
#include "../view/TagEditDialog.h"
#include "../view/Theme.h"
#include "../view/TransportBar.h"
#include "../view/VisualizerView.h"
#include "../visualizer/AudioSpectrum.h"

#include <ventty/terminal/TerminalBase.h>

#include "../library/LibraryScanner.h"

#include <atomic>
#include <memory>
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

    /// Left-panel mode on the Browser screen. Artist/Album/Directory are
    /// projections of the indexed MediaLibrary (rendered by LibraryView);
    /// FileBrowser is live filesystem navigation from the launch CWD. Bound
    /// to 1/2/3/4 respectively.
    enum class LeftMode
    {
        Artist,
        Album,
        Directory,
        FileBrowser,
    };

    /// Actions in the ESC context menu. The visible item set is built
    /// dynamically per `LeftMode`, so selection maps through an action
    /// list rather than fixed indices.
    enum class MenuAction
    {
        SetLibraryRoot,
        RescanLibrary,
        LocatePlaying,
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
        void drawBrowserScreen();
        void drawVisualizerScreen();
        void drawHelpScreen();
        void updateUI();
        void toggleHelp();
        void buildHelpRows();
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

        /// True when the left panel is a MediaLibrary projection (Artist /
        /// Album / Directory) rather than the live FileBrowser.
        // During a scan the LibraryView tree is dropped (pass 1) and not
        // rebuilt until finalizeScan() (its results land all at once), so the
        // left panel behaves as the FileBrowser for drawing, input and mouse
        // routing for the whole scan — both passes.
        bool leftIsLibrary() const
        {
            return (_leftMode == LeftMode::Artist
                    || _leftMode == LeftMode::Album
                    || _leftMode == LeftMode::Directory)
                   && !_collectActive.load() && !_ingestActive.load();
        }

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

        // Media library (track index of the configured root directory)
        MediaLibrary _library;
        std::unique_ptr<LibraryRepository> _libraryRepo;

        // UI state
        Screen _screen = Screen::Browser;
        Screen _previousScreen = Screen::Browser; // restored when leaving Help
        FocusPanel _focus = FocusPanel::FileBrowser;
        LeftMode _leftMode = LeftMode::Album;
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

        int _helpScroll = 0;
        Theme _theme;
        int _visualizerIndex = 1; // 1 = AudioSpectrum (default), 0 = Oscilloscope
        RepeatMode _repeatMode = RepeatMode::None;

        // Views
        std::unique_ptr<HeaderBar> _headerBar;
        std::unique_ptr<FileBrowser> _fileBrowser;
        std::unique_ptr<LibraryView> _libraryView;
        std::unique_ptr<PlayQueueView> _playQueueView;
        std::unique_ptr<TransportBar> _transportBar;
        std::unique_ptr<VisualizerView> _visualizerView;
        std::unique_ptr<ContextMenu> _contextMenu;
        /// Parallel to the menu's visible items: maps the selected index
        /// back to an action (the item set varies with `_leftMode`).
        std::vector<MenuAction> _contextMenuActions;
        std::unique_ptr<LibrarySearchDialog> _searchDialog;
        std::unique_ptr<TagEditDialog> _tagEditDialog;

        // Startup positional argument. At most one is set: _initialFile is a
        // single track to queue+play; _initialDir is a folder to open in the
        // FileBrowser. Either forces FileBrowser (4) mode at launch.
        std::filesystem::path _initialFile;
        std::filesystem::path _initialDir;

        // --debug: keep ffmpeg's stderr on the terminal instead of /dev/null.
        bool _debug = false;
    };

} // namespace vtplayer
