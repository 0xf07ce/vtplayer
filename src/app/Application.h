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
#include "../view/Theme.h"
#include "../view/TransportBar.h"
#include "../view/VisualizerView.h"
#include "../visualizer/AudioSpectrum.h"

#include <ventty/terminal/TerminalBase.h>

#include <memory>
#include <string>
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

    enum class BrowserLeft
    {
        Files,
        Library,
    };

    class Application
    {
    public:
        Application();
        ~Application();

        void setInitialFile(std::filesystem::path path) { _initialFile = std::move(path); }

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
        int helpVisibleRows() const;
        int helpMaxScroll() const;

        void handleInput(ventty::KeyEvent const &event);
        void handleMouse(ventty::MouseEvent const &event);
        void handleGlobalKeys(ventty::KeyEvent const &event);
        void openContextMenu();
        void onContextMenuSelect(int index);
        void setVisualizerByIndex(int index);

        void playTrack(int index);
        void playNext();
        void playPrev();
        void addToPlayQueue(std::filesystem::path const &path);
        void activateFromBrowser(std::vector<std::filesystem::path> const &paths, bool quietAppend);

        /// Read an .m3u file and append its tracks to the current play queue.
        void appendPlayQueueFile(std::filesystem::path const &path);

        /// Scan the configured `libraryRoot` (incremental: skips files whose
        /// mtime matches the SQLite cache). No-op when the root is unset or
        /// the repository failed to open.
        void scanLibrary();

        /// Re-point the library at `root`, wiping any prior index, then scan.
        void setLibraryRoot(std::filesystem::path root);

        /// Replace the play queue with every track currently in the library.
        /// Stops audio if a track was playing (PlayQueueView::setTracks fires
        /// the onPlayingRemoved callback).
        void sendLibraryToPlayQueue();

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
        BrowserLeft _browserLeft = BrowserLeft::Files;

        struct HelpRow
        {
            std::string left;
            std::string right;
            bool isHeader = false;
        };
        std::vector<HelpRow> _helpRows;
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
        std::unique_ptr<LibrarySearchDialog> _searchDialog;

        std::filesystem::path _initialFile;
    };

} // namespace vtplayer
