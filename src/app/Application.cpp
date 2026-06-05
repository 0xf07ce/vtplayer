// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "Application.h"

#include "../library/LibraryRepository.h"
#include "../library/LibraryScanner.h"
#include "../playqueue/PlayQueueCache.h"
#include "../plugin/DecoderRegistry.h"
#include "../util/M3uReader.h"
#include "../util/PlsReader.h"
#include "../util/TagWriter.h"
#include "../util/UnicodeNormalize.h"
#include "../visualizer/DebugBars.h"
#include "../visualizer/MatrixRain.h"
#include "../visualizer/Oscilloscope.h"
#include "../visualizer/TagInfoView.h"
#include "../visualizer/VinylVis.h"

#include <ventty/terminal/Terminal.h>

#include <ventty/art/AsciiArt.h>

#include <algorithm>
#include <chrono>
#include <random>
#include <string_view>
#include <system_error>
#include <thread>

#include <poll.h>
#include <unistd.h>

namespace vtplayer
{

    using Key = ventty::KeyEvent::Key;

    // Container formats decoded directly by libav. This is a property of the
    // binary, not user configuration — hence no [formats] config knob. Plugins
    // add further extensions dynamically through DecoderRegistry, which every
    // consumer below merges in on top of this list.
    static constexpr std::string_view kBuiltinExtensions =
        "mp3,wav,ogg,flac,m4a,mp4,aac,opus,wma,webm";

    // Map Korean Hangul Jamo (ㅂ, ㅈ, etc.) to their QWERTY equivalents
    // so that shortcuts work regardless of IME state.
    static char32_t hangulToQwerty(char32_t ch)
    {
        // clang-format off
        switch (ch)
        {
        // Consonants (initial)
        case U'ㅂ': return 'q';  case U'ㅃ': return 'Q';
        case U'ㅈ': return 'w';  case U'ㅉ': return 'W';
        case U'ㄷ': return 'e';  case U'ㄸ': return 'E';
        case U'ㄱ': return 'r';  case U'ㄲ': return 'R';
        case U'ㅅ': return 't';  case U'ㅆ': return 'T';
        case U'ㅛ': return 'y';
        case U'ㅕ': return 'u';
        case U'ㅑ': return 'i';
        case U'ㅐ': return 'o';  case U'ㅒ': return 'O';
        case U'ㅔ': return 'p';  case U'ㅖ': return 'P';
        case U'ㅁ': return 'a';
        case U'ㄴ': return 's';
        case U'ㅇ': return 'd';
        case U'ㄹ': return 'f';
        case U'ㅎ': return 'g';
        case U'ㅗ': return 'h';
        case U'ㅓ': return 'j';
        case U'ㅏ': return 'k';
        case U'ㅣ': return 'l';
        case U'ㅋ': return 'z';
        case U'ㅌ': return 'x';
        case U'ㅊ': return 'c';
        case U'ㅍ': return 'v';
        case U'ㅠ': return 'b';
        case U'ㅜ': return 'n';
        case U'ㅡ': return 'm';
        default:    return ch;
        }
        // clang-format on
    }

    namespace
    {
        LeftMode leftModeFromConfig(std::string const &s)
        {
            // Config strings preserved across the v0.11→v0.12 mode redesign:
            // "album" still selects the slot labelled "Album" (now the
            // AlbumArtist tree) and "artist" the slot labelled "Artist"
            // (now the Artist tree). The user's last UI label survives.
            if (s == "artist")
                return LeftMode::ArtistTree;
            if (s == "directory")
                return LeftMode::Directory;
            if (s == "playlists")
                return LeftMode::Playlists;
            // Legacy "radio" mode (v0.9.x and earlier) falls back to album —
            // RadioView was removed in v0.10.0 when streaming moved into the
            // unified library.
            return LeftMode::AlbumArtistTree; // default; "filebrowser" is never persisted
        }

        char const *leftModeToConfig(LeftMode m)
        {
            switch (m)
            {
            case LeftMode::ArtistTree:
                return "artist";
            case LeftMode::Directory:
                return "directory";
            case LeftMode::Playlists:
                return "playlists";
            case LeftMode::AlbumArtistTree:
                return "album";
            // FileBrowser is transient — normalize so a fresh run starts
            // back in the indexed library.
            case LeftMode::FileBrowser:
                return "album";
            }
            return "album";
        }

        /// Greedy word-wrap of ASCII help text to `width` columns. Splits on
        /// spaces; a single word longer than `width` is hard-broken. Always
        /// returns at least one (possibly empty) line.
        std::vector<std::string> wrapWords(std::string const &text, int width)
        {
            std::vector<std::string> lines;
            if (width < 1)
            {
                lines.push_back(text);
                return lines;
            }

            std::string line;
            std::size_t i = 0;
            while (i < text.size())
            {
                // Skip a run of spaces (collapse at wrap points).
                while (i < text.size() && text[i] == ' ')
                    ++i;
                if (i >= text.size())
                    break;

                std::size_t end = text.find(' ', i);
                if (end == std::string::npos)
                    end = text.size();
                std::string word = text.substr(i, end - i);
                i = end;

                // Hard-break a word that cannot fit on its own line.
                while (static_cast<int>(word.size()) > width)
                {
                    if (!line.empty())
                    {
                        lines.push_back(line);
                        line.clear();
                    }
                    lines.push_back(word.substr(0, width));
                    word = word.substr(width);
                }

                if (line.empty())
                {
                    line = word;
                }
                else if (static_cast<int>(line.size() + 1 + word.size()) <= width)
                {
                    line += ' ';
                    line += word;
                }
                else
                {
                    lines.push_back(line);
                    line = word;
                }
            }
            lines.push_back(line);
            return lines;
        }
    } // namespace

    Application::Application() = default;

    Application::~Application()
    {
        joinScanThread();
        _audio.shutdown();
    }

    int Application::run()
    {
        init();
        if (!_terminal)
        {
            _audio.shutdown();
            return 1;
        }
        _running = true;

        while (_running && _terminal->isRunning())
        {
            while (_terminal->pollEvent())
                ;

            // The background ingest reported completion: join it and reload
            // `_library` from the refreshed repository before draw().
            if (_ingestFinished.load())
                finalizeScan();

            updateUI();
            draw();

            // Drive ventty's hardware cursor for whichever text-input
            // modal currently owns input.
            if (_tagEditDialog && _tagEditDialog->wantsCursor())
            {
                _terminal->setCursorPos(_tagEditDialog->cursorScreenX(),
                                        _tagEditDialog->cursorScreenY());
                _terminal->setCursorVisible(true);
            }
            else if (_searchDialog && _searchDialog->wantsCursor())
            {
                _terminal->setCursorPos(_searchDialog->cursorScreenX(),
                                        _searchDialog->cursorScreenY());
                _terminal->setCursorVisible(true);
            }
            else if (_textInputDialog && _textInputDialog->wantsCursor())
            {
                _terminal->setCursorPos(_textInputDialog->cursorScreenX(),
                                        _textInputDialog->cursorScreenY());
                _terminal->setCursorVisible(true);
            }
            else
            {
                _terminal->setCursorVisible(false);
            }

            _terminal->render();

            // Idle-aware pacing. Block on STDIN for the screen-appropriate
            // window instead of unconditionally spinning at ~60 Hz: Browser
            // and Help can sit for hundreds of ms when nothing is changing,
            // dropping idle CPU close to zero. The Visualizer screen still
            // wakes at the configured FPS so its animation stays smooth.
            waitForInputOrTimeout(computeIdleTimeoutMs());
        }

        cleanup();
        return 0;
    }

    void Application::quit()
    {
        _audio.stop();
        _running = false;
        if (_terminal)
            _terminal->quit();
    }

    int Application::computeIdleTimeoutMs() const
    {
        // Visualizer is animated — pace it at the configured FPS regardless
        // of input. Each visualizer can request a lower cadence via
        // preferredFps(): a positive value caps the rate (the global setting
        // stays the ceiling), while kStaticFps marks a static view (e.g.
        // TagInfoView) that wants no periodic wake at all — fall through to
        // the input-driven idle pacing below so its idle CPU drops to ~0.
        if (_screen == Screen::Visualizer && _visualizerView &&
            _visualizerView->preferredFps() != Visualizer::kStaticFps)
        {
            int fps = std::clamp(_config.visualizerFps, 15, 60);
            if (int const pref = _visualizerView->preferredFps(); pref > 0)
                fps = std::min(fps, pref);
            return std::max(1, 1000 / fps);
        }

        // Inline collect (pass 1) keeps the loop interactive for ESC even
        // while it blocks; tighten the timeout so the count refreshes.
        if (_collectActive.load() || _ingestActive.load())
        {
            return 100;
        }

        // While playing (or buffering a stream), wake often enough for the
        // TransportBar second counter / buffering indicator to refresh. 250
        // ms is comfortably under one second and still ~4 redraws/sec —
        // negligible CPU.
        auto const state = _audio.state();
        if (state == PlayState::Playing || _audio.isStreamBuffering())
        {
            return 250;
        }

        // Fully idle: block up to 1 s. Keystrokes wake the poll
        // immediately; this just caps how long background signals
        // (track-end, ingest completion) can wait before being noticed.
        return 1000;
    }

    void Application::waitForInputOrTimeout(int ms) const
    {
        if (ms <= 0) return;
        struct pollfd pfd{};
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        // Any POLLIN return value (or EINTR, or timeout) is fine — the run
        // loop will simply pollEvent() again and decide what to do.
        ::poll(&pfd, 1, ms);
    }

    void Application::initTerminal()
    {
        auto term = std::make_unique<ventty::Terminal>();
        if (!term->init())
        {
            return;
        }
        _terminal = std::move(term);
    }

    void Application::init()
    {
        // Load config
        _config.load();

        // Apply theme colors from config
        _theme = Theme::retro();
        if (!_config.themeColors.empty())
        {
            _theme.applyColors(_config.themeColors);
        }

        _audio.init();
        _audio.setVolume(1.0f);
        _audio.setGainNorm(_config.gainNorm);
        _audio.setStreamBuffer(_config.streamBufferSeconds,
                               _config.streamPrebufferSeconds);
        _audio.setStreamDebug(_debug);

        // Load plugins before the terminal comes up: input plugins register
        // their file extensions into DecoderRegistry, which the FileBrowser,
        // library scanner and AudioEngine all consult below. Loader/plugin
        // diagnostics print to stderr only with --debug (keeps the TUI clean).
        _pluginHost.setDebug(_debug);
        _pluginHost.loadAll();

        // Init terminal
        initTerminal();
        if (!_terminal)
            return;

        _rootWindow = _terminal->createWindow(0, 0, _terminal->cols(), _terminal->rows());

        // Input callbacks
        _terminal->onKey([this](ventty::KeyEvent const &ev)
                         { handleInput(ev); });

        _terminal->onMouse([this](ventty::MouseEvent const &ev)
                           { handleMouse(ev); });

        _terminal->onResize([this](ventty::ResizeEvent const &ev)
                            {
        if (_rootWindow)
        {
            _rootWindow->resize(ev.cols, ev.rows);
            _rootWindow->setPosition(0, 0);
        }
        resize();
        _terminal->forceRedraw(); });

        // Create views
        _headerBar = std::make_unique<HeaderBar>();
        _headerBar->setTheme(_theme);

        _fileBrowser = std::make_unique<FileBrowser>();
        _fileBrowser->setTheme(_theme);
        _fileBrowser->setFocused(true);
        _fileBrowser->setShowHidden(_config.showHidden);
        {
            // Show plugin-handled formats in the browser too: append every
            // extension claimed by a loaded input plugin to the configured set.
            std::string exts{kBuiltinExtensions};
            for (auto const & e : DecoderRegistry::instance().extensions())
            {
                exts += ',';
                exts += e;
            }
            _fileBrowser->setAllowedExtensions(exts);
        }
        {
            // FileBrowser opens at the directory implied by the startup
            // argument (a directory itself, or the parent of a file), and
            // otherwise at the directory the player was launched from.
            std::error_code ec;
            std::filesystem::path startDir;
            if (!_initialDir.empty())
            {
                startDir = _initialDir;
            }
            else if (!_initialFile.empty())
            {
                startDir = _initialFile.parent_path();
            }
            else
            {
                auto cwd = std::filesystem::current_path(ec);
                startDir = ec ? std::filesystem::path("/") : cwd;
            }
            _fileBrowser->setDirectory(startDir);
        }
        _fileBrowser->setOnActivate([this](std::vector<std::filesystem::path> const &paths)
                                    { activateFromBrowser(paths); });
        _fileBrowser->setOnOpenPlaylist([this](std::filesystem::path const &path)
                                        { appendPlayQueueFile(path); });

        _playQueueView = std::make_unique<PlayQueueView>();
        _playQueueView->setTheme(_theme);
        _playQueueView->setOnPlay([this](int index)
                                  { playTrack(index); });
        _playQueueView->setOnPlayingRemoved([this]
                                            {
                                               _audio.stop();
                                               _playQueueView->setPlayingIndex(-1); });
        // Any queue mutation (add / remove / reorder / clear / replace) drops
        // the header back to the default title. loadPlaylistIntoQueue() runs
        // setTracks() first, then re-stamps the name, so it survives the load
        // while later edits revert it.
        _playQueueView->setOnContentsChanged([this]
                                             {
                                                 _currentPlaylistName.clear();
                                                 applyQueueTitle(); });

        auto const sendToQueue = [this](std::vector<TrackInfo> tracks, bool replace)
        {
            if (!_playQueueView || tracks.empty())
                return;
            if (replace)
            {
                _playQueueView->setTracks(std::move(tracks));
                playTrack(0);
            }
            else
            {
                for (auto const &t : tracks)
                    _playQueueView->addTrack(t);
            }
        };

        _libraryView = std::make_unique<LibraryView>();
        _libraryView->setTheme(_theme);
        _libraryView->setOnSendToQueue(sendToQueue);
        _libraryView->setOnSearch([this]
                                  {
                                      // Search is library-only; never open it
                                      // over the FileBrowser panel.
                                      if (_searchDialog && leftIsLibrary())
                                          _searchDialog->open(); });

        _searchDialog = std::make_unique<LibrarySearchDialog>();
        _searchDialog->setTheme(_theme);
        _searchDialog->setOnLocate([this](std::filesystem::path const &path)
                                   {
                                       // Nav (n / N) can fire after the user has
                                       // switched into FileBrowser, where the
                                       // library tree isn't visible. Re-enter a
                                       // library projection so the locate is
                                       // actually seen.
                                       if (!leftIsLibrary())
                                           setLeftMode(LeftMode::AlbumArtistTree);
                                       if (_libraryView)
                                           _libraryView->locate(path);
                                       if (_terminal)
                                           _terminal->forceRedraw(); });

        _tagEditDialog = std::make_unique<TagEditDialog>();
        _tagEditDialog->setTheme(_theme);
        _tagEditDialog->setOnSave([this](std::vector<std::filesystem::path> const &targets,
                                          TagUpdate const &upd)
                                  { applyTagEdit(targets, upd); });

        _transportBar = std::make_unique<TransportBar>();
        _transportBar->setTheme(_theme);

        _visualizerView = std::make_unique<VisualizerView>();
        _visualizerView->setTheme(_theme);
        // Restore last-used visualizer from config; fall back to spectrum
        // if the saved index is out of range or no longer implemented.
        _visualizerIndex = 1;
        _visualizerView->setVisualizer(std::make_unique<AudioSpectrum>(_config.barCount));
        setVisualizerByIndex(_config.visualizerIndex);

        _contextMenu = std::make_unique<ContextMenu>();
        _contextMenu->setTheme(_theme);
        _contextMenu->setTitle("Menu");
        // Items are rebuilt per-open in openContextMenu() since the visible
        // set depends on the current left-panel mode.
        _contextMenu->setOnSelect([this](int idx)
                                  { onContextMenuSelect(idx); });

        _playlistsView = std::make_unique<PlaylistsView>();
        _playlistsView->setTheme(_theme);
        _playlistsView->setOnActivate([this](std::string const &name)
                                      { loadPlaylistIntoQueue(name); });

        // Create/Delete are driven from the ESC menu; the per-action OnConfirm
        // callbacks are bound at open time in onContextMenuSelect().
        _textInputDialog = std::make_unique<TextInputDialog>();
        _textInputDialog->setTheme(_theme);
        _confirmDialog = std::make_unique<ConfirmDialog>();
        _confirmDialog->setTheme(_theme);

        resize();

        // Open the media library index. Failure is non-fatal — the player
        // still works without a library; only library-backed features are off.
        _libraryRepo = std::make_unique<LibraryRepository>(LibraryRepository::defaultPath());
        if (_libraryRepo->open())
        {
            _libraryRepo->loadInto(_library);
        }
        // Set the root now, not just in scanLibrary(): Directory mode builds
        // its tree from library-root-relative paths, and setLeftMode() below
        // may build it before the (possibly skipped) scan would set the root.
        // Without this the tree falls back to absolute paths from "/".
        if (!_config.libraryRoot.empty())
            _library.setRoot(_config.libraryRoot);
        _libraryView->setLibrary(&_library);
        _searchDialog->setLibrary(&_library);

        // Resolve the initial left-panel mode. The persisted library mode is
        // honored unless the index is empty — then fall back to FileBrowser
        // so the user can navigate to a folder and register a library root.
        {
            LeftMode initMode = leftModeFromConfig(_config.leftMode);
            if (initMode != LeftMode::FileBrowser && _library.empty())
            {
                initMode = LeftMode::FileBrowser;
            }
            // A startup path argument always lands in the FileBrowser so the
            // requested directory is the one shown.
            if (!_initialDir.empty() || !_initialFile.empty())
            {
                initMode = LeftMode::FileBrowser;
            }
            setLeftMode(initMode);

            // Restore the cursor saved at last exit. Seed the in-session
            // anchor too so it carries through subsequent mode switches.
            // If the index isn't ready yet (scan pending), finalizeScan()
            // re-applies it after the tree is rebuilt.
            _libraryAnchor = _config.libraryFocus;
            bool const initIsLibrary = (initMode == LeftMode::AlbumArtistTree
                                        || initMode == LeftMode::ArtistTree
                                        || initMode == LeftMode::Directory);
            if (!_libraryAnchor.empty() && _libraryView && initIsLibrary)
                _libraryView->locateForMode(_libraryAnchor);

            // Populate the playlist panel before first draw when the restored
            // mode is Playlists; setLeftMode() doesn't list from disk.
            if (initMode == LeftMode::Playlists)
                refreshPlaylists();
        }

        if (!_initialFile.empty())
        {
            // A file argument means "play exactly this": the queue holds only
            // that one track and playback starts immediately. The persisted
            // queue is intentionally ignored in this case.
            activateFromBrowser({_initialFile});
        }
        else
        {
            // Restore the previous session's play queue (path list, resolved
            // against the library index for full metadata).
            auto restored = PlayQueueCache::restore(_library);
            if (!restored.empty())
            {
                _playQueueView->setTracks(std::move(restored));
            }
        }

        // Kick off the filesystem reconcile last: every step above reads the
        // DB-loaded `_library` snapshot (mode resolution, queue restore).
        // scanLibrary() runs pass 1 inline (the run loop has not started yet,
        // but the terminal is up so the tick can repaint) then backgrounds
        // pass 2; the run loop starts right after.
        scanLibrary();
    }

    void Application::cleanup()
    {
        // Stop any in-flight scan before tearing down: the worker references
        // `_library`/`_libraryRepo`, which outlive it only if joined here.
        joinScanThread();

        // Sync runtime-mutable settings back before persisting.
        _config.gainNorm = _audio.gainNormEnabled();
        _config.visualizerIndex = _visualizerIndex;
        _config.leftMode = leftModeToConfig(_leftMode);
        // Capture the live cursor (the session may have stayed in one library
        // mode without ever switching) so focus survives the next launch.
        if (leftIsLibrary() && _libraryView)
        {
            auto cur = _libraryView->selectedTrackPath();
            if (!cur.empty())
                _libraryAnchor = cur;
        }
        _config.libraryFocus = _libraryAnchor;
        _config.save();

        // Snapshot the play queue (path list only) so the next run can
        // restore it. Failure is silent — the queue is volatile by design.
        if (_playQueueView)
        {
            PlayQueueCache::save(_playQueueView->tracks());
        }

        // Audio must stop before terminal restores — otherwise audio thread
        // output can corrupt the restored terminal.
        _audio.shutdown();

        // Unload plugins only after the audio engine has fully stopped: a live
        // PluginSource holds pointers into the module's code, so dlclose() any
        // earlier would risk a use-after-unmap crash.
        _pluginHost.shutdown();

        if (_terminal)
        {
            _terminal->shutdown();
            _terminal.reset();
        }
    }

    void Application::resize()
    {
        int const w = _terminal->cols();
        int const h = _terminal->rows();

        // HeaderBar: top row
        _headerBar->setRect(0, 0, w, 1);

        // TransportBar: bottom row carries the box-bottom border with the
        // current track info overlaid.
        _transportBar->setRect(0, h - 1, w, 1);

        // Content area: between header and transport. Set rects for both
        // layouts so toggling screens doesn't require a resize cycle.
        int const contentY = 1;
        int const contentH = h - 2;

        // Browser split: FileBrowser (left 40%) | PlayQueueView (right 60%).
        // When the left panel is hidden (`l`), PlayQueue spans full width and
        // the left widgets get a zero-width rect so hit-testing skips them.
        if (_libraryPanelVisible)
        {
            int browserW = (w * 2) / 5;
            if (browserW < 20)
                browserW = 20;
            int playQueueW = w - browserW;
            _fileBrowser->setRect(0, contentY, browserW, contentH);
            _libraryView->setRect(0, contentY, browserW, contentH);
            if (_playlistsView)
                _playlistsView->setRect(0, contentY, browserW, contentH);
            _playQueueView->setRect(browserW, contentY, playQueueW, contentH);
        }
        else
        {
            _fileBrowser->setRect(0, contentY, 0, contentH);
            _libraryView->setRect(0, contentY, 0, contentH);
            if (_playlistsView)
                _playlistsView->setRect(0, contentY, 0, contentH);
            _playQueueView->setRect(0, contentY, w, contentH);
        }

        // Visualizer takes the full content area.
        _visualizerView->setRect(0, contentY, w, contentH);
    }

    void Application::updateUI()
    {
        auto state = _audio.state();

        // Auto-advance: audio callback signals track ended via flag,
        // then UI thread safely stops and loads next track.
        if (_audio.hasTrackEnded())
        {
            _audio.stop();
            int current = _playQueueView->playingIndex();
            int count = _playQueueView->trackCount();
            if (current >= 0)
            {
                if (_repeatMode == RepeatMode::One)
                {
                    playTrack(current);
                }
                else if (_shuffleMode)
                {
                    // Walk the shuffle order without wrapping; a finished
                    // pass ends playback unless repeat-all is on, in which
                    // case we re-shuffle a fresh pass and start over.
                    int idx = shuffleAdvance(+1, /*wrap=*/false);
                    if (idx < 0 && _repeatMode == RepeatMode::All && count > 0)
                    {
                        rebuildShuffleOrder(/*seedIndex=*/-1);
                        idx = currentShuffleQueueIndex();
                    }
                    if (idx >= 0)
                    {
                        playTrack(idx);
                    }
                    else
                    {
                        _playQueueView->setPlayingIndex(-1);
                    }
                }
                else if (current + 1 < count)
                {
                    playTrack(current + 1);
                }
                else if (_repeatMode == RepeatMode::All && count > 0)
                {
                    playTrack(0);
                }
                else
                {
                    _playQueueView->setPlayingIndex(-1);
                }
            }
        }

        // Update transport
        _transportBar->setState(state);
        _transportBar->setRepeatMode(_repeatMode);
        _transportBar->setShuffleMode(_shuffleMode);
        _transportBar->setTrackName(_audio.currentTrack().title);
        _transportBar->setPosition(_audio.position());
        _transportBar->setDuration(_audio.duration());
        _transportBar->setLive(_audio.isStream());
        _transportBar->setBuffering(_audio.isStreamBuffering());
        _transportBar->setGainNorm(_audio.gainNormEnabled(), _audio.gainNormDb(), _audio.gainSource());

        // Update visualizer
        if (_screen == Screen::Visualizer)
        {
            _visualizerView->update(_audio);
        }
    }

    void Application::draw()
    {
        _rootWindow->clear(ventty::Style{_theme.foreground, _theme.background});

        // Header
        _headerBar->draw(*_rootWindow);

        // Content
        if (_screen == Screen::Browser)
        {
            drawBrowserScreen();
        }
        else if (_screen == Screen::Visualizer)
        {
            drawVisualizerScreen();
        }
        else
        {
            drawHelpScreen();
        }

        // Transport
        _transportBar->draw(*_rootWindow);

        // Library search dialog (overlay).
        if (_searchDialog && _searchDialog->isOpen())
        {
            _searchDialog->draw(*_rootWindow);
        }

        // Tag-edit dialog (overlay).
        if (_tagEditDialog && _tagEditDialog->isOpen())
        {
            _tagEditDialog->draw(*_rootWindow);
        }

        // Playlist create / delete dialogs (overlays).
        if (_textInputDialog && _textInputDialog->isOpen())
        {
            _textInputDialog->draw(*_rootWindow);
        }
        if (_confirmDialog && _confirmDialog->isOpen())
        {
            _confirmDialog->draw(*_rootWindow);
        }

        // Context menu overlay (drawn last so it sits on top)
        if (_contextMenu)
        {
            _contextMenu->draw(*_rootWindow);
        }

        // Unobtrusive scan status, bottom-right, above everything.
        drawScanStatus();
    }

    void Application::drawBrowserScreen()
    {
        if (_libraryPanelVisible)
        {
            switch (activeLeftWidget())
            {
            case LeftSlot::Library:
                _libraryView->draw(*_rootWindow);
                break;
            case LeftSlot::Playlists:
                if (_playlistsView)
                    _playlistsView->draw(*_rootWindow);
                break;
            case LeftSlot::FileBrowser:
                _fileBrowser->draw(*_rootWindow);
                break;
            }
        }
        _playQueueView->draw(*_rootWindow);

        if (_libraryPanelVisible)
        {
            // Draw vertical separator between panels
            int sepX = _fileBrowser->rect().width;
            int sepY = _fileBrowser->rect().y;
            int sepH = _fileBrowser->rect().height;

            for (int y = 0; y < sepH; ++y)
            {
                _rootWindow->putChar(sepX, sepY + y, ventty::SINGLE_BOX.v,
                                     ventty::Style{_theme.separatorFg, _theme.background});
            }
        }
        else
        {
            // No left panel: PlayQueue spans full width but leaves column 0
            // for the surrounding box's left edge (its content starts at
            // r.x+1). Draw that border ourselves so it isn't missing.
            auto const &r = _playQueueView->rect();
            for (int y = 0; y < r.height; ++y)
            {
                _rootWindow->putChar(r.x, r.y + y, ventty::DOUBLE_BOX.v,
                                     ventty::Style{_theme.border, _theme.playQueueBg});
            }
        }
    }

    void Application::drawVisualizerScreen()
    {
        _visualizerView->draw(*_rootWindow);
    }

    void Application::buildHelpRows()
    {
        switch (_helpTab)
        {
        case HelpTab::Plugins:
            buildPluginRows();
            break;
        case HelpTab::Shortcuts:
        default:
            buildShortcutRows();
            break;
        }
    }

    void Application::buildShortcutRows()
    {
#ifndef VTPLAYER_VERSION
#define VTPLAYER_VERSION "unknown"
#endif
        // clang-format off
        _helpRows = {
            {"VT-PLAYER " VTPLAYER_VERSION " — Keyboard shortcuts", "", true},
            {"", "", false},
            {"Playback", "", true},
            {"  Space",                 "Play / Pause", false},
            {"  X",                     "Stop playback", false},
            {"  [ / ]",                 "Previous / Next track", false},
            {"  < / >",                 "Seek -5s / +5s", false},
            {"  R",                     "Cycle repeat: none -> all -> one", false},
            {"  S",                     "Toggle shuffle mode (next/prev follow a random order)", false},
            {"  G",                     "Toggle gain normalization (ReplayGain / auto-gain)", false},
            {"", "", false},
            {"Browser - Library", "", true},
            {"  1 / 2 / 3 / 4 / 5",     "Left panel: Album / Artist / Directory / Files / Playlists", false},
            {"  L",                     "Toggle left panel (play queue full-width when hidden)", false},
            {"  Tab",                   "Switch focus (browser <-> play queue)", false},
            {"  Left / Right",          "Collapse / expand selected group", false},
            {"  Enter",                 "Replace play queue with artist/album/track and play", false},
            {"  A",                     "Append artist/album/track to play queue", false},
            {"  /",                     "Search library (Tab cycles filter: Any/Artist/Album/Title/Year)", false},
            {"  N / Shift+N",           "Jump to next / previous search result", false},
            {"  T",                     "Edit tags (artist/album/track or folder)", false},
            {"", "", false},
            {"Browser - Files", "", true},
            {"  Enter",                 "Replace play queue with selection and play", false},
            {"  A",                     "Add selected file (or every audio file in selected dir) to play queue", false},
            {"  T",                     "Edit tags of selected audio file", false},
            {"  Backspace",             "Go up to parent directory", false},
            {"", "", false},
            {"Play Queue", "", true},
            {"  Enter",                 "Play selected track", false},
            {"  Del / D / Backspace",   "Remove selection", false},
            {"  Ctrl+Up / Ctrl+Down",   "Move selected track", false},
            {"  Ctrl+A",                "Select all", false},
            {"  T",                     "Edit tags (multi-selection if any, else cursor track)", false},
            {"", "", false},
            {"Visualizer", "", true},
            {"  V",                     "Toggle visualizer screen", false},
            {"  0",                     "Tag info", false},
            {"  1",                     "Spectrum analyzer", false},
            {"  2",                     "Matrix rain", false},
            {"  3",                     "Debug bars", false},
            {"  4",                     "Oscilloscope", false},
            {"  5",                     "Vinyl / CD disc", false},
            {"", "", false},
            {"Misc", "", true},
            {"  H / Up / Down / PgUp / PgDn", "Show / scroll this help", false},
            {"  Tab / Left / Right",    "Switch help tab (Shortcuts / Plugins)", false},
            {"  ESC",                   "Open menu / dismiss overlay", false},
            {"  Q",                     "Quit", false},
        };
        // clang-format on

        // Force a re-flow on next draw/scroll-clamp.
        _helpLines.clear();
        _helpLayoutWidth = -1;
    }

    void Application::buildPluginRows()
    {
        _helpRows.clear();

        auto const plugins = _pluginHost.plugins();
        _helpRows.push_back({"Loaded plugins", "", true});
        _helpRows.push_back({"", "", false});

        if (plugins.empty())
        {
            _helpRows.push_back({"  (no plugins loaded)", "", false});
        }
        else
        {
            for (auto const &p : plugins)
                _helpRows.push_back({"  " + p.name, p.version, false});
        }

        // Force a re-flow on next draw/scroll-clamp.
        _helpLines.clear();
        _helpLayoutWidth = -1;
    }

    void Application::setHelpTab(HelpTab tab)
    {
        if (_helpTab == tab)
            return;
        _helpTab = tab;
        _helpScroll = 0;
        buildHelpRows();
        if (_terminal)
            _terminal->forceRedraw();
    }

    void Application::drawHelpTabBar(int row)
    {
        if (!_terminal)
            return;
        int const w = _terminal->cols();

        struct TabDef
        {
            HelpTab tab;
            char const *label;
        };
        static constexpr TabDef kTabs[] = {
            {HelpTab::Shortcuts, "Shortcuts"},
            {HelpTab::Plugins, "Plugins"},
        };

        // Mirror the LibrarySearchDialog filter bar: the active tab is marked
        // by a selection-colored background rather than bracket glyphs.
        ventty::Style const activeStyle{_theme.browserSelFg, _theme.browserSelBg, ventty::Attr::Bold};
        ventty::Style const inactiveStyle{_theme.headerFg, _theme.background};

        int x = 2;
        for (auto const &t : kTabs)
        {
            bool const active = (t.tab == _helpTab);
            std::string label = std::string(" ") + t.label + " ";
            if (x + static_cast<int>(label.size()) > w - 1)
                break;
            _rootWindow->drawText(x, row, label, active ? activeStyle : inactiveStyle);
            x += static_cast<int>(label.size()) + 1;
        }
    }

    void Application::ensureHelpLayout() const
    {
        if (!_terminal)
            return;
        int const w = _terminal->cols();
        if (w == _helpLayoutWidth)
            return; // already laid out for this width
        _helpLayoutWidth = w;
        _helpLines.clear();

        // Last drawable column is w-2 (w-1 holds the right border), so a span
        // starting at x can hold (w-1)-x characters.
        int const headerX = 2;
        int const keyX = 4;
        int const desiredDescX = keyX + 32;
        constexpr int kMinDescW = 12;
        bool const twoColumn = ((w - 1) - desiredDescX) >= kMinDescW;
        int const descX = twoColumn ? desiredDescX : (keyX + 2);
        int const descW = std::max(1, (w - 1) - descX);
        int const headerW = std::max(1, (w - 1) - headerX);

        for (auto const &row : _helpRows)
        {
            if (row.isHeader)
            {
                for (auto const &seg : wrapWords(row.left, headerW))
                {
                    HelpLine ln;
                    ln.spans.push_back({headerX, seg, 0});
                    _helpLines.push_back(std::move(ln));
                }
                continue;
            }
            if (row.left.empty() && row.right.empty())
            {
                _helpLines.push_back(HelpLine{}); // blank spacer
                continue;
            }

            auto segs = wrapWords(row.right, descW);
            if (twoColumn)
            {
                HelpLine first;
                first.spans.push_back({keyX, row.left, 1});
                if (!row.right.empty())
                    first.spans.push_back({descX, segs[0], 2});
                _helpLines.push_back(std::move(first));
                for (std::size_t k = 1; k < segs.size(); ++k)
                {
                    HelpLine ln;
                    ln.spans.push_back({descX, segs[k], 2});
                    _helpLines.push_back(std::move(ln));
                }
            }
            else
            {
                HelpLine keyLine;
                keyLine.spans.push_back({keyX, row.left, 1});
                _helpLines.push_back(std::move(keyLine));
                if (!row.right.empty())
                {
                    for (auto const &seg : segs)
                    {
                        HelpLine ln;
                        ln.spans.push_back({descX, seg, 2});
                        _helpLines.push_back(std::move(ln));
                    }
                }
            }
        }
    }

    int Application::helpVisibleRows() const
    {
        if (!_terminal)
            return 0;
        int const h = _terminal->rows();
        int const top = 1 + kHelpTabRows; // below header + tab strip
        int const bottom = h - 2;
        return std::max(0, bottom - top + 1);
    }

    int Application::helpMaxScroll() const
    {
        ensureHelpLayout();
        int const total = static_cast<int>(_helpLines.size());
        int const visible = helpVisibleRows();
        return std::max(0, total - visible);
    }

    void Application::drawHelpScreen()
    {
        if (!_terminal)
            return;
        int const w = _terminal->cols();
        int const h = _terminal->rows();
        int const top = 1;        // below header
        int const bottom = h - 2; // above transport row
        if (bottom < top)
            return;

        // Clamp scroll in case the terminal shrank since the last key event.
        int const maxScroll = helpMaxScroll();
        if (_helpScroll > maxScroll)
            _helpScroll = maxScroll;
        if (_helpScroll < 0)
            _helpScroll = 0;

        // Clear the content area to the help background.
        ventty::Style bgStyle{_theme.foreground, _theme.background};
        _rootWindow->fill(0, top, w, bottom - top + 1, U' ', bgStyle);

        // Side borders matching the browser/play-queue box so the surrounding
        // frame stays continuous when help replaces the content panels.
        ventty::Style borderStyle{_theme.border, _theme.background};
        for (int y = top; y <= bottom; ++y)
        {
            _rootWindow->putChar(0, y, ventty::DOUBLE_BOX.v, borderStyle);
            _rootWindow->putChar(w - 1, y, ventty::DOUBLE_BOX.v, borderStyle);
        }

        // Tab strip on the first content row; the scrollable body sits below.
        drawHelpTabBar(top);
        int const contentTop = top + kHelpTabRows;

        ventty::Style headerStyle{_theme.browserHeaderFg, _theme.background, ventty::Attr::Bold};
        ventty::Style keyStyle{_theme.browserAudioFg, _theme.background};
        ventty::Style descStyle{_theme.foreground, _theme.background};

        ensureHelpLayout();
        int const visible = bottom - contentTop + 1;
        int const total = static_cast<int>(_helpLines.size());
        int const drawRows = std::min(visible, total - _helpScroll);

        for (int i = 0; i < drawRows; ++i)
        {
            int const y = contentTop + i;
            for (auto const &span : _helpLines[_helpScroll + i].spans)
            {
                int const maxChars = (w - 1) - span.x;
                if (maxChars <= 0)
                    continue;
                std::string text = span.text;
                if (static_cast<int>(text.size()) > maxChars)
                    text = text.substr(0, maxChars);
                ventty::Style const &style =
                    (span.kind == 0)   ? headerStyle
                    : (span.kind == 1) ? keyStyle
                                       : descStyle;
                _rootWindow->drawText(span.x, y, text, style);
            }
        }

        // Scroll hint when content overflows.
        if (maxScroll > 0)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), " %d / %d ",
                          _helpScroll + 1,
                          maxScroll + 1);
            std::string hint = buf;
            int const hintX = w - 2 - static_cast<int>(hint.size());
            if (hintX > 1)
            {
                _rootWindow->drawText(hintX, bottom, hint,
                                      ventty::Style{_theme.border, _theme.background});
            }
        }
    }

    void Application::toggleHelp()
    {
        if (_screen == Screen::Help)
        {
            _screen = _previousScreen;
        }
        else
        {
            // Rebuild for the active tab so the plugin list reflects the
            // current host state each time help opens.
            buildHelpRows();
            _helpScroll = 0;
            _previousScreen = _screen;
            _screen = Screen::Help;
        }
        if (_terminal)
            _terminal->forceRedraw();
    }

    void Application::handleInput(ventty::KeyEvent const &event)
    {
        if (event.key == Key::None)
            return;

        // Pass 1 (collect) runs inline on this thread and blocks the run
        // loop; we only get here because the collect tick pumps events. The
        // sole honored input is ESC, which cancels the walk.
        if (_collectActive.load())
        {
            if (event.key == Key::Escape)
                _collectCancel.store(true);
            return;
        }

        // Modal overlays consume all input while open. When a dialog closes
        // we ask the terminal for a full redraw: the diff renderer keys on
        // per-cell equality and skips the lead cell of a CJK fullwidth char
        // even when its companion cell was overwritten by the overlay's
        // frame, leaving the border glyph stuck in the second column of any
        // wide character the dialog had crossed. A full redraw re-emits the
        // wide char, restoring the underlying content.
        if (_searchDialog && _searchDialog->isOpen())
        {
            _searchDialog->handleKey(event);
            if (!_searchDialog->isOpen() && _terminal)
                _terminal->forceRedraw();
            return;
        }

        if (_tagEditDialog && _tagEditDialog->isOpen())
        {
            _tagEditDialog->handleKey(event);
            if (!_tagEditDialog->isOpen() && _terminal)
                _terminal->forceRedraw();
            return;
        }

        if (_textInputDialog && _textInputDialog->isOpen())
        {
            _textInputDialog->handleKey(event);
            if (!_textInputDialog->isOpen() && _terminal)
                _terminal->forceRedraw();
            return;
        }

        if (_confirmDialog && _confirmDialog->isOpen())
        {
            _confirmDialog->handleKey(event);
            if (!_confirmDialog->isOpen() && _terminal)
                _terminal->forceRedraw();
            return;
        }

        if (_contextMenu && _contextMenu->isOpen())
        {
            _contextMenu->handleKey(event);
            if (!_contextMenu->isOpen() && _terminal)
                _terminal->forceRedraw();
            return;
        }

        // ESC opens the context menu. On the visualizer screen, ESC first
        // falls back to the browser (preserving prior behavior); a second
        // ESC opens the menu. In Help mode, ESC dismisses the help overlay.
        if (event.key == Key::Escape)
        {
            if (_screen == Screen::Visualizer)
            {
                _screen = Screen::Browser;
                resize();
                _terminal->forceRedraw();
            }
            else if (_screen == Screen::Help)
            {
                toggleHelp();
            }
            else
            {
                openContextMenu();
            }
            return;
        }

        // In Help mode only H (dismiss), Q (quit), and scroll keys respond.
        // Everything else is swallowed so global hotkeys don't mutate state
        // behind the help overlay.
        if (_screen == Screen::Help)
        {
            int const maxScroll = helpMaxScroll();
            int const page = std::max(1, helpVisibleRows());
            char32_t const ch = (event.key == Key::Char) ? hangulToQwerty(event.ch) : event.ch;

            if (event.key == Key::Char && (ch == 'h' || ch == 'H') && !event.alt && !event.ctrl)
            {
                toggleHelp();
            }
            else if (event.key == Key::Char && (ch == 'q' || ch == 'Q') && !event.alt && !event.ctrl)
            {
                quit();
            }
            else if (event.key == Key::Tab || event.key == Key::Right)
            {
                setHelpTab(_helpTab == HelpTab::Shortcuts ? HelpTab::Plugins
                                                          : HelpTab::Shortcuts);
            }
            else if (event.key == Key::Left)
            {
                setHelpTab(_helpTab == HelpTab::Plugins ? HelpTab::Shortcuts
                                                        : HelpTab::Plugins);
            }
            else if (event.key == Key::Up)
            {
                _helpScroll = std::max(0, _helpScroll - 1);
            }
            else if (event.key == Key::Down)
            {
                _helpScroll = std::min(maxScroll, _helpScroll + 1);
            }
            else if (event.key == Key::PageUp)
            {
                _helpScroll = std::max(0, _helpScroll - page);
            }
            else if (event.key == Key::PageDown)
            {
                _helpScroll = std::min(maxScroll, _helpScroll + page);
            }
            else if (event.key == Key::Home)
            {
                _helpScroll = 0;
            }
            else if (event.key == Key::End)
            {
                _helpScroll = maxScroll;
            }
            return;
        }

        // Global keys (always active)
        handleGlobalKeys(event);

        // Screen-specific input
        if (_screen == Screen::Browser)
        {
            if (_focus == FocusPanel::FileBrowser)
            {
                switch (activeLeftWidget())
                {
                case LeftSlot::Library:
                    _libraryView->handleKey(event);
                    break;
                case LeftSlot::Playlists:
                    if (_playlistsView)
                        _playlistsView->handleKey(event);
                    break;
                case LeftSlot::FileBrowser:
                    _fileBrowser->handleKey(event);
                    break;
                }
            }
            else
            {
                _playQueueView->handleKey(event);
            }
        }
    }

    void Application::handleMouse(ventty::MouseEvent const &event)
    {
        // Mouse input is inert while pass 1 blocks the run loop. (Pass 2
        // runs in the background and the UI stays interactive.)
        if (_collectActive.load())
            return;

        using Button = ventty::MouseEvent::Button;
        using Action = ventty::MouseEvent::Action;

        // Transport bar: click on progress bar to seek
        float seekRatio = _transportBar->handleMouse(event);
        if (seekRatio >= 0.0f)
        {
            float dur = _audio.duration();
            _audio.seek(seekRatio * dur);
            return;
        }

        // Browser screen: click on panels
        if (_screen == Screen::Browser)
        {
            auto const &browserRect = _fileBrowser->rect();
            auto const &playQueueRect = _playQueueView->rect();

            // Click to switch focus
            if (event.button == Button::Left && event.action == Action::Press)
            {
                if (browserRect.contains(event.x, event.y))
                {
                    if (_focus != FocusPanel::FileBrowser)
                    {
                        _focus = FocusPanel::FileBrowser;
                        setLeftFocused(true);
                        _playQueueView->setFocused(false);
                    }
                }
                else if (playQueueRect.contains(event.x, event.y))
                {
                    if (_focus != FocusPanel::PlayQueue)
                    {
                        _focus = FocusPanel::PlayQueue;
                        setLeftFocused(false);
                        _playQueueView->setFocused(true);
                    }
                }
            }

            // Delegate to focused panel
            LeftSlot const slot = activeLeftWidget();
            if (slot == LeftSlot::Library && _libraryView->rect().contains(event.x, event.y))
            {
                _libraryView->handleMouse(event);
            }
            else if (slot == LeftSlot::FileBrowser
                     && _fileBrowser->rect().contains(event.x, event.y))
            {
                _fileBrowser->handleMouse(event);
            }
            // PlaylistsView has no mouse handler yet (keyboard-only this cut).
            else if (_playQueueView->rect().contains(event.x, event.y))
            {
                _playQueueView->handleMouse(event);
            }
        }
        else if (_screen == Screen::Visualizer && _visualizerView && event.action == Action::Press)
        {
            // One wheel tick → 3 lines; matches typical terminal scroll feel.
            constexpr int kWheelStep = 3;
            if (event.button == Button::ScrollUp)
            {
                _visualizerView->scrollBy(-kWheelStep);
            }
            else if (event.button == Button::ScrollDown)
            {
                _visualizerView->scrollBy(+kWheelStep);
            }
        }
    }

    void Application::handleGlobalKeys(ventty::KeyEvent const &event)
    {
        // Normalize Korean Jamo to QWERTY so shortcuts work under Hangul IME
        char32_t const ch = (event.key == Key::Char) ? hangulToQwerty(event.ch) : event.ch;

        // Quit
        if (event.key == Key::Char && (ch == 'q' || ch == 'Q') && !event.alt && !event.ctrl)
        {
            quit();
            return;
        }

        // v/V: toggle visualizer screen
        if (event.key == Key::Char && (ch == 'v' || ch == 'V') && !event.alt && !event.ctrl)
        {
            _screen = (_screen == Screen::Browser) ? Screen::Visualizer : Screen::Browser;
            resize();
            _terminal->forceRedraw();
            return;
        }

        // 1-5: pick the Browser-screen left panel directly.
        //   1 Album  (AlbumArtist > Album > Track tree)
        //   2 Artist (Artist      > Album > Track tree)
        //   3 Directory (folder tree from the library index)
        //   4 FileBrowser (live filesystem from the launch CWD)
        //   5 Playlists (saved-playlist browser)
        // Internet radio is no longer a separate mode — PLS playlists in the
        // library surface in modes 1/2/3 like any other track.
        if (_screen == Screen::Browser && event.key == Key::Char && !event.alt && !event.ctrl
            && (ch == '1' || ch == '2' || ch == '3' || ch == '4' || ch == '5'))
        {
            LeftMode const target = (ch == '1')   ? LeftMode::AlbumArtistTree
                                    : (ch == '2') ? LeftMode::ArtistTree
                                    : (ch == '3') ? LeftMode::Directory
                                    : (ch == '4') ? LeftMode::FileBrowser
                                                  : LeftMode::Playlists;
            // Leaving a library projection (1/2/3): remember the focused
            // track so it can be restored on the next entry — including after
            // a FileBrowser (4) round-trip.
            if (leftIsLibrary() && _libraryView)
            {
                auto cur = _libraryView->selectedTrackPath();
                if (!cur.empty())
                    _libraryAnchor = cur;
            }

            // Picking a left mode implies the panel should be visible.
            setLibraryPanelVisible(true);
            setLeftMode(target);

            // Playlists: re-list from disk so the panel is current.
            if (target == LeftMode::Playlists)
                refreshPlaylists();

            // Re-locate the anchor to its node at the new mode's grouping
            // level. This also runs when re-pressing the current mode's key:
            // setLeftMode() reset the fold to the mode default, and the user
            // expects the cursor to land on the focused item's group (e.g.
            // pressing 2 on a track jumps to that track's album), not reset
            // to the top.
            bool const targetIsLibrary = (target == LeftMode::AlbumArtistTree
                                          || target == LeftMode::ArtistTree
                                          || target == LeftMode::Directory);
            if (targetIsLibrary && !_libraryAnchor.empty() && _libraryView)
            {
                _libraryView->locateForMode(_libraryAnchor);
            }
            return;
        }

        // l/L: toggle the Browser-screen left panel (Library / FileBrowser).
        // Hidden -> PlayQueue spans the full width.
        if (_screen == Screen::Browser && event.key == Key::Char && (ch == 'l' || ch == 'L') && !event.alt && !event.ctrl)
        {
            setLibraryPanelVisible(!_libraryPanelVisible);
            return;
        }

        // h/H: open the help overlay (dismissed by H or ESC).
        if (event.key == Key::Char && (ch == 'h' || ch == 'H') && !event.alt && !event.ctrl)
        {
            toggleHelp();
            return;
        }

        // Number keys 0-9: switch visualizer style (visualizer screen only).
        if (_screen == Screen::Visualizer && event.key == Key::Char && ch >= '0' && ch <= '9')
        {
            setVisualizerByIndex(static_cast<int>(ch - '0'));
            return;
        }

        // Vertical scroll for the active visualizer (TagInfoView etc.).
        // Audio-reactive visualizers ignore these via the base class no-op.
        if (_screen == Screen::Visualizer && _visualizerView && !event.ctrl && !event.alt)
        {
            constexpr int kPage = 8;
            constexpr int kHomeEnd = 1 << 20;
            if (event.key == Key::Up && _visualizerView->scrollBy(-1))
                return;
            if (event.key == Key::Down && _visualizerView->scrollBy(+1))
                return;
            if (event.key == Key::PageUp && _visualizerView->scrollBy(-kPage))
                return;
            if (event.key == Key::PageDown && _visualizerView->scrollBy(+kPage))
                return;
            if (event.key == Key::Home && _visualizerView->scrollBy(-kHomeEnd))
                return;
            if (event.key == Key::End && _visualizerView->scrollBy(+kHomeEnd))
                return;
        }

        // Tab: switch focus between panels (browser screen only). No-op when
        // the left panel is hidden — there's only PlayQueue to focus.
        if (event.key == Key::Tab && _screen == Screen::Browser && _libraryPanelVisible)
        {
            if (_focus == FocusPanel::FileBrowser)
            {
                // Only switch to play queue if it's not empty
                if (!_playQueueView->empty())
                {
                    _focus = FocusPanel::PlayQueue;
                    setLeftFocused(false);
                    _playQueueView->setFocused(true);
                }
            }
            else
            {
                _focus = FocusPanel::FileBrowser;
                setLeftFocused(true);
                _playQueueView->setFocused(false);
            }
            return;
        }

        // Space: play/pause
        if (event.key == Key::Char && ch == ' ')
        {
            auto state = _audio.state();
            if (state == PlayState::Playing)
            {
                _audio.pause();
            }
            else if (state == PlayState::Paused)
            {
                _audio.play();
            }
            else if (!_playQueueView->empty())
            {
                // Start playing selected or first track
                int idx = _playQueueView->selectedIndex();
                playTrack(idx);
            }
            return;
        }

        // x: toggle hard stop / restart. While playing or paused it tears down
        // the decoder/stream and clears the ▶ marker. Pressing it again from
        // the stopped state restarts the queue: from the current track if one
        // is set, otherwise from the top — and in shuffle mode from the first
        // track of a freshly shuffled order.
        if (event.key == Key::Char && (ch == 'x' || ch == 'X') &&
            !event.alt && !event.ctrl)
        {
            if (_audio.state() != PlayState::Stopped)
            {
                _audio.stop();
                _playQueueView->setPlayingIndex(-1);
            }
            else if (_playQueueView->trackCount() > 0)
            {
                int idx = _playQueueView->playingIndex();
                if (idx < 0)
                {
                    if (_shuffleMode)
                    {
                        rebuildShuffleOrder(/*seedIndex=*/-1);
                        idx = currentShuffleQueueIndex();
                    }
                    if (idx < 0)
                        idx = 0;
                }
                playTrack(idx);
            }
            return;
        }

        // r: cycle repeat mode (none -> all -> one -> none)
        if (event.key == Key::Char && (ch == 'r' || ch == 'R') && !event.alt && !event.ctrl)
        {
            switch (_repeatMode)
            {
            case RepeatMode::None:
                _repeatMode = RepeatMode::All;
                break;
            case RepeatMode::All:
                _repeatMode = RepeatMode::One;
                break;
            case RepeatMode::One:
                _repeatMode = RepeatMode::None;
                break;
            }
            return;
        }

        // s: toggle shuffle mode. The visible play queue order is left
        // intact; prev/next/auto-advance instead walk an internal random
        // order seeded with the currently-playing track.
        if (event.key == Key::Char && (ch == 's' || ch == 'S') && !event.alt && !event.ctrl)
        {
            toggleShuffleMode();
            return;
        }

        // ]: next track
        if (event.key == Key::Char && ch == ']' && !event.alt && !event.ctrl)
        {
            playNext();
            return;
        }

        // [: previous track
        if (event.key == Key::Char && ch == '[' && !event.alt && !event.ctrl)
        {
            playPrev();
            return;
        }

        // n / N: vim-style next / previous search result. Active only on the
        // Browser screen and only when the library search dialog left a
        // navigable result set behind; otherwise the keys fall through and
        // do nothing.
        if (event.key == Key::Char && (ch == 'n' || ch == 'N') && !event.alt && !event.ctrl
            && _screen == Screen::Browser && _searchDialog && _searchDialog->hasNav())
        {
            if (ch == 'N') _searchDialog->navigatePrev();
            else           _searchDialog->navigateNext();
            return;
        }

        // < / >: seek
        if (event.key == Key::Char && ch == '<')
        {
            float pos = _audio.position();
            _audio.seek(pos - 5.0f);
            return;
        }
        if (event.key == Key::Char && ch == '>')
        {
            float pos = _audio.position();
            _audio.seek(pos + 5.0f);
            return;
        }

        // g: toggle gain normalization (ReplayGain tag → auto-gain fallback)
        if (event.key == Key::Char && (ch == 'g' || ch == 'G') && !event.alt && !event.ctrl)
        {
            _audio.setGainNorm(!_audio.gainNormEnabled());
            return;
        }

        // a: append the current selection to the bottom of the play queue,
        // keeping the existing queue intact.
        if (event.key == Key::Char && (ch == 'a' || ch == 'A') && !event.alt && !event.ctrl && _screen == Screen::Browser)
        {
            // Playlists panel has no track-append action yet (no contents
            // view this cut) — swallow `a` so it can't read stale FileBrowser
            // state below.
            if (leftIsPlaylists())
                return;
            // Library panel: append the selected artist / album / track.
            if (leftIsLibrary())
            {
                _libraryView->sendSelectionToQueue(/*replace=*/false);
                return;
            }
            // File browser: append the selected file (or every audio file in
            // the selected directory).
            auto const *entry = _fileBrowser->selectedEntry();
            if (entry && entry->isAudio)
            {
                addToPlayQueue(entry->path);
            }
            else if (entry && entry->isDirectory)
            {
                for (auto const &p : _fileBrowser->collectAudioFiles(entry->path))
                {
                    addToPlayQueue(p);
                }
            }
            return;
        }

        // Ctrl+Up/Down: move play-queue item
        if (event.ctrl && event.key == Key::Up)
        {
            _playQueueView->moveSelectedUp();
            return;
        }
        if (event.ctrl && event.key == Key::Down)
        {
            _playQueueView->moveSelectedDown();
            return;
        }

        // t: open the tag editor scoped to the current selection.
        if (event.key == Key::Char && (ch == 't' || ch == 'T') && !event.alt && !event.ctrl
            && _screen == Screen::Browser)
        {
            openTagEditor();
            return;
        }
    }

    Application::MenuContext Application::classifyMenuContext() const
    {
        MenuContext ctx;
        ctx.queueFocused = (_focus == FocusPanel::PlayQueue);
        ctx.leftSlot = activeLeftWidget();
        ctx.playlistsEmpty = !_playlistsView || _playlistsView->empty();
        ctx.libraryRootConfigured = !_config.libraryRoot.empty();
        return ctx;
    }

    void Application::buildContextMenu(MenuContext const &ctx,
                                       std::vector<std::string> &items,
                                       std::vector<MenuAction> &actions) const
    {
        auto const add = [&](std::string label, MenuAction action)
        {
            items.emplace_back(std::move(label));
            actions.push_back(action);
        };

        // FileBrowser is the only context that omits "Focus playing track" (it
        // would jump into the library tree, out of place here) and leads with
        // the library-root items instead. Keyed on the active left mode, not on
        // which panel holds focus.
        if (ctx.leftSlot == LeftSlot::FileBrowser)
        {
            if (ctx.libraryRootConfigured)
                add("Go to library root", MenuAction::GoToLibraryRoot);
            add("Set current directory as library root", MenuAction::SetLibraryRoot);
            add("Exit", MenuAction::Exit);
            return;
        }

        // Every other context keeps "Focus playing track" first; its action
        // already adapts to queue-vs-library focus in locatePlayingInLibrary().
        add("Focus playing track", MenuAction::LocatePlaying);

        switch (ctx.leftSlot)
        {
        case LeftSlot::Playlists:
            add("Create playlist", MenuAction::CreatePlaylist);
            if (!ctx.playlistsEmpty)
            {
                add("Rename playlist", MenuAction::RenamePlaylist);
                add("Delete playlist", MenuAction::DeletePlaylist);
            }
            break;
        case LeftSlot::Library:
            add("Rescan library", MenuAction::RescanLibrary);
            break;
        case LeftSlot::FileBrowser:
            break; // handled above
        }

        add("Exit", MenuAction::Exit);
    }

    void Application::openContextMenu()
    {
        if (!_contextMenu)
            return;

        // Classify the current focus/selection context, then build the prepared
        // menu for it. Adding a per-context menu later means extending
        // MenuContext + buildContextMenu(), not touching this entry point.
        std::vector<std::string> items;
        _contextMenuActions.clear();
        buildContextMenu(classifyMenuContext(), items, _contextMenuActions);

        _contextMenu->setItems(std::move(items));
        _contextMenu->open();
        _terminal->forceRedraw();
    }

    void Application::setVisualizerByIndex(int index)
    {
        if (!_visualizerView)
            return;
        if (index < 0 || index > 9)
            return;

        std::unique_ptr<Visualizer> vis;
        switch (index)
        {
        case 0:
            vis = std::make_unique<TagInfoView>();
            break;
        case 1:
            vis = std::make_unique<AudioSpectrum>(_config.barCount);
            break;
        case 2:
            vis = std::make_unique<MatrixRain>();
            break;
        case 3:
            vis = std::make_unique<DebugBars>();
            break;
        case 4:
            vis = std::make_unique<Oscilloscope>();
            break;
        case 5:
            vis = std::make_unique<VinylVis>();
            break;
        default:
            // Slots 6-9 reserved; ignore until implemented.
            return;
        }

        _visualizerIndex = index;
        _visualizerView->setVisualizer(std::move(vis));
        if (_terminal)
            _terminal->forceRedraw();
    }

    void Application::setLeftMode(LeftMode mode)
    {
        _leftMode = mode;
        if (_libraryView)
        {
            switch (mode)
            {
            case LeftMode::AlbumArtistTree:
                _libraryView->setMode(LibraryView::Mode::AlbumArtistTree);
                break;
            case LeftMode::ArtistTree:
                _libraryView->setMode(LibraryView::Mode::ArtistTree);
                break;
            case LeftMode::Directory:
                _libraryView->setMode(LibraryView::Mode::Directory);
                break;
            case LeftMode::FileBrowser:
            case LeftMode::Playlists:
                break;
            }
        }
        // Keep keyboard focus on whichever widget now occupies the left slot.
        // Critical for mode 5: without this, input never reaches PlaylistsView.
        if (_focus == FocusPanel::FileBrowser)
            setLeftFocused(true);
        if (_terminal)
            _terminal->forceRedraw();
    }

    void Application::refreshPlaylists()
    {
        if (_playlistsView)
            _playlistsView->setItems(_playlistStore.list());
    }

    void Application::applyQueueTitle()
    {
        if (_playQueueView)
            _playQueueView->setTitle(_currentPlaylistName.empty() ? "Play Queue"
                                                                  : _currentPlaylistName);
    }

    void Application::loadPlaylistIntoQueue(std::string const &name)
    {
        if (!_playQueueView)
            return;

        auto parsed = M3uReader::read(_playlistStore.pathFor(name));
        if (!parsed) // unreadable / missing — leave the queue untouched
            return;

        // Re-resolve each entry against the library so the queue carries the
        // richer indexed metadata (album / grouping / ReplayGain) that the bare
        // M3U parse lacks; fall back to the parsed entry for external paths.
        // Mirrors PlayQueueCache::restore().
        std::vector<TrackInfo> resolved;
        resolved.reserve(parsed->size());
        for (auto const &t : *parsed)
        {
            if (auto const *indexed = _library.find(t.path))
                resolved.push_back(*indexed);
            else
                resolved.push_back(t);
        }

        // setTracks() fires OnContentsChanged → clears _currentPlaylistName;
        // stamp the name afterwards so the header shows this playlist.
        _playQueueView->setTracks(std::move(resolved));
        _currentPlaylistName = name;
        applyQueueTitle();

        if (_playQueueView->trackCount() == 0)
        {
            // An empty playlist replaces the queue with nothing and stops.
            _audio.stop();
            _playQueueView->setPlayingIndex(-1);
            return;
        }

        int startIdx = 0;
        if (_shuffleMode)
        {
            // Fresh shuffle pass, then start from its first entry — same as the
            // `x`-key restart UX.
            rebuildShuffleOrder(/*seedIndex=*/-1);
            int const sidx = currentShuffleQueueIndex();
            if (sidx >= 0)
                startIdx = sidx;
        }
        playTrack(startIdx);
    }

    std::string Application::playlistNameError(std::string const &name) const
    {
        // pathFor() applies the same sanitization create()/rename() use, so an
        // existing target file means the failure was a name collision; anything
        // else (e.g. a name that sanitizes to empty) is an invalid name.
        std::error_code ec;
        if (std::filesystem::exists(_playlistStore.pathFor(name), ec))
            return "A playlist with that name already exists";
        return "Invalid playlist name";
    }

    void Application::setLeftFocused(bool on)
    {
        LeftSlot const slot = activeLeftWidget();
        if (_fileBrowser)
            _fileBrowser->setFocused(on && slot == LeftSlot::FileBrowser);
        if (_libraryView)
            _libraryView->setFocused(on && slot == LeftSlot::Library);
        if (_playlistsView)
            _playlistsView->setFocused(on && slot == LeftSlot::Playlists);
    }

    void Application::setLibraryPanelVisible(bool visible)
    {
        if (_libraryPanelVisible == visible)
            return;
        _libraryPanelVisible = visible;

        if (!visible)
        {
            // Nothing to focus on the left anymore: pin focus to the queue.
            _focus = FocusPanel::PlayQueue;
            setLeftFocused(false);
            if (_playQueueView)
                _playQueueView->setFocused(true);
        }
        else
        {
            // Hand focus back to whichever widget occupies the left slot.
            _focus = FocusPanel::FileBrowser;
            setLeftFocused(true);
            if (_playQueueView)
                _playQueueView->setFocused(false);
        }

        resize();
        if (_terminal)
            _terminal->forceRedraw();
    }

    namespace
    {
        /// Convert a filesystem time_point to unix seconds. Mirrors the helper
        /// LibraryScanner uses; lives in an anonymous namespace there, so we
        /// keep a local copy rather than exposing it project-wide.
        std::int64_t fileTimeToUnix(std::filesystem::file_time_type t)
        {
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                t - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
            return static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count());
        }
    }

    void Application::openTagEditor()
    {
        if (!_tagEditDialog)
            return;

        std::vector<TrackInfo> tracks;
        std::string headerText;

        if (_focus == FocusPanel::PlayQueue && _playQueueView)
        {
            // Multi-selection wins over the cursor; otherwise edit the
            // single track under the cursor. Streams (read-only metadata
            // descriptors) are skipped silently.
            int const count = _playQueueView->trackCount();
            for (int i = 0; i < count; ++i)
            {
                if (!_playQueueView->isMultiSelected(i)) continue;
                if (auto const *t = _playQueueView->track(i); t && !t->isStream())
                    tracks.push_back(*t);
            }
            if (tracks.empty())
            {
                if (auto const *t = _playQueueView->selectedTrack();
                    t && !t->isStream())
                    tracks.push_back(*t);
            }
            if (tracks.empty())
                return;
            if (tracks.size() == 1)
                headerText = "Track: " + (tracks[0].title.empty()
                                              ? tracks[0].path.filename().string()
                                              : tracks[0].title);
            else
                headerText = std::to_string(tracks.size())
                             + " tracks (queue)";
        }
        else if (_screen == Screen::Browser && _focus == FocusPanel::FileBrowser)
        {
            if (leftIsLibrary() && _libraryView)
            {
                auto sel = _libraryView->currentSelection();
                if (sel.kind == LibraryView::SelectionKind::None || sel.tracks.empty())
                    return;
                // Streams have no editable file-side tags; drop them.
                sel.tracks.erase(std::remove_if(sel.tracks.begin(), sel.tracks.end(),
                                                [](TrackInfo const & t) { return t.isStream(); }),
                                 sel.tracks.end());
                if (sel.tracks.empty())
                    return;
                tracks = std::move(sel.tracks);
                // Header reflects which library node the user opened the
                // dialog over — the dialog body itself is identical in all
                // cases, so this is purely informational.
                char const * kindLabel = "Selection";
                switch (sel.kind)
                {
                case LibraryView::SelectionKind::Grouping:       kindLabel = "Grouping"; break;
                case LibraryView::SelectionKind::Artist:         kindLabel = "Artist";   break;
                case LibraryView::SelectionKind::Album:          kindLabel = "Album";    break;
                case LibraryView::SelectionKind::DirectoryGroup: kindLabel = "Folder";   break;
                case LibraryView::SelectionKind::Track:          kindLabel = "Track";    break;
                case LibraryView::SelectionKind::None:           return;
                }
                if (sel.kind == LibraryView::SelectionKind::Track)
                {
                    headerText = std::string(kindLabel) + ": " + sel.label;
                }
                else
                {
                    headerText = std::string(kindLabel) + ": " + sel.label
                                 + "  (" + std::to_string(tracks.size()) + " tracks)";
                }
            }
            else if (activeLeftWidget() == LeftSlot::FileBrowser && _fileBrowser)
            {
                // FileBrowser handler is single-file only: directories,
                // playlists, and stream descriptors are ignored. Multi-
                // selection is intentionally not honored here — bulk edits
                // are reachable from the indexed library (folder group) or
                // the play queue (Space-marked rows).
                auto const *entry = _fileBrowser->selectedEntry();
                if (!entry) return;
                if (entry->isDirectory) return;
                if (entry->isPlaylist) return;
                if (!entry->isAudio)   return;

                // Prefer the library-indexed TrackInfo (it has parsed tags
                // already); fall back to a path-only TrackInfo otherwise so
                // dirty-field semantics still let the user write tags.
                TrackInfo seed;
                if (auto const *indexed = _library.find(entry->path))
                    seed = *indexed;
                else
                {
                    seed.path = entry->path;
                    seed.format = TrackInfo::formatFromPath(entry->path);
                }
                tracks.push_back(std::move(seed));
                headerText = "File: " + entry->path.filename().string();
            }
        }

        if (tracks.empty())
            return;

        // Files handled by an input plugin (VGM, ROL, …) have no
        // TagLib-writable tags — the plugin ABI exposes read_tags but no
        // write_tags, and TagWriter (TagLib) cannot open them. When every
        // target is such a file, open the dialog locked to its read-only
        // View so the user can still inspect the plugin-provided tags
        // without entering an edit that would silently fail on save.
        bool const readOnly = std::all_of(
            tracks.begin(), tracks.end(),
            [](TrackInfo const & t)
            {
                return t.format == AudioFormat::Plugin
                       || DecoderRegistry::instance().find(
                              t.path.extension().string()) != nullptr;
            });

        _tagEditDialog->open(std::move(headerText), std::move(tracks), readOnly);
        if (_terminal)
            _terminal->forceRedraw();
    }

    void Application::applyTagEdit(std::vector<std::filesystem::path> const &targets,
                                   TagUpdate const &update)
    {
        if (targets.empty() || update.empty())
            return;

        // Save the anchor *before* we rebuild the library tree so we can
        // restore the cursor near where the user was working. Prefer the
        // first edited path so the cursor lands on the freshly-edited row.
        std::filesystem::path anchor = targets.front();

        bool repoOpen = (_libraryRepo && _libraryRepo->isOpen());

        for (auto const &path : targets)
        {
            if (!applyTagUpdate(path, update))
                continue; // skip failed writes; nothing else changed for this file

            // Refresh mtime/size from disk so the next scan does not see
            // the row as "stale" and re-read TagLib unnecessarily.
            std::int64_t newMtime = 0;
            std::int64_t newSize  = 0;
            std::error_code ec;
            if (auto t = std::filesystem::last_write_time(path, ec); !ec)
                newMtime = fileTimeToUnix(t);
            ec.clear();
            if (auto sz = std::filesystem::file_size(path, ec); !ec)
                newSize = static_cast<std::int64_t>(sz);

            // Patch the in-memory library entry (if indexed). We rebuild
            // it from the existing row + the sparse update so unchanged
            // fields stay exactly as they were.
            TrackInfo merged;
            if (auto const *existing = _library.find(path))
            {
                merged = *existing;
            }
            else
            {
                merged.path = path;
                merged.format = TrackInfo::formatFromPath(path);
            }

            if (update.title)       merged.title       = *update.title;
            if (update.artist)      merged.artist      = *update.artist;
            if (update.album)       merged.album       = *update.album;
            if (update.albumArtist) merged.albumArtist = *update.albumArtist;
            if (update.genre)       merged.genre       = *update.genre;
            if (update.grouping)    merged.grouping    = *update.grouping;
            if (update.trackNumber) merged.trackNumber = *update.trackNumber;
            if (update.discNumber)  merged.discNumber  = *update.discNumber;
            if (update.year)        merged.year        = *update.year;
            merged.mtime = newMtime;
            if (newSize > 0) merged.size = newSize;

            // Only index files that already live under the library — a
            // FileBrowser edit outside the library root should not silently
            // add the file to the index.
            bool const wasIndexed = (_library.find(path) != nullptr);
            if (wasIndexed)
            {
                _library.upsert(merged);
                if (repoOpen) _libraryRepo->upsert(merged);
            }

            // If the just-edited track is the one currently loaded in the
            // audio engine, push the new metadata so transport / visualizer
            // pick it up without reloading.
            if (_audio.currentTrack().path == path)
            {
                TrackInfo cur = _audio.currentTrack();
                if (update.title)       cur.title       = *update.title;
                if (update.artist)      cur.artist      = *update.artist;
                if (update.album)       cur.album       = *update.album;
                if (update.albumArtist) cur.albumArtist = *update.albumArtist;
                if (update.genre)       cur.genre       = *update.genre;
                if (update.grouping)    cur.grouping    = *update.grouping;
                if (update.trackNumber) cur.trackNumber = *update.trackNumber;
                if (update.discNumber)  cur.discNumber  = *update.discNumber;
                if (update.year)        cur.year        = *update.year;
                _audio.updateCurrentTrackMeta(cur);
            }
        }

        // Rebuild the library projection (artist/album re-bucketing) and
        // restore the cursor near the edited row.
        if (_libraryView)
        {
            _libraryView->rebuild();
            if (!anchor.empty())
                _libraryView->locate(anchor);
        }
        if (_terminal)
            _terminal->forceRedraw();
    }

    void Application::onContextMenuSelect(int index)
    {
        if (index < 0 || index >= static_cast<int>(_contextMenuActions.size()))
            return;

        switch (_contextMenuActions[index])
        {
        case MenuAction::SetLibraryRoot:
            if (_fileBrowser)
            {
                setLibraryRoot(_fileBrowser->currentDirectory());
            }
            break;
        case MenuAction::GoToLibraryRoot:
            if (_fileBrowser && !_config.libraryRoot.empty())
            {
                std::error_code ec;
                if (std::filesystem::is_directory(_config.libraryRoot, ec))
                    _fileBrowser->setDirectory(_config.libraryRoot);
            }
            break;
        case MenuAction::RescanLibrary:
            scanLibrary(/*force=*/true);
            break;
        case MenuAction::LocatePlaying:
            locatePlayingInLibrary();
            break;
        case MenuAction::CreatePlaylist:
            if (_textInputDialog)
            {
                // Bind the action fresh: the dialog is generic, so each open
                // wires the specific callback it should run on confirm. The
                // callback returns an error to keep the dialog open (e.g. on a
                // name collision) or nullopt on success.
                _textInputDialog->setOnConfirm(
                    [this](std::string const &name) -> std::optional<std::string>
                    {
                        if (!_playlistStore.create(name))
                            return playlistNameError(name);
                        refreshPlaylists();
                        if (_terminal)
                            _terminal->forceRedraw();
                        return std::nullopt;
                    });
                _textInputDialog->open("New Playlist", "Name:");
            }
            break;
        case MenuAction::RenamePlaylist:
            if (_textInputDialog && _playlistsView)
            {
                std::string const sel = _playlistsView->selectedName();
                if (!sel.empty())
                {
                    _textInputDialog->setOnConfirm(
                        [this, sel](std::string const &name) -> std::optional<std::string>
                        {
                            // rename() rejects a name collision (and empty /
                            // invalid names); keep the dialog open with a hint
                            // so the user can edit and retry.
                            if (!_playlistStore.rename(sel, name))
                                return playlistNameError(name);
                            refreshPlaylists();
                            if (_terminal)
                                _terminal->forceRedraw();
                            return std::nullopt;
                        });
                    // Prefill with the current name so the user edits in place.
                    _textInputDialog->open("Rename Playlist", "Name:", sel);
                }
            }
            break;
        case MenuAction::DeletePlaylist:
            if (_confirmDialog && _playlistsView)
            {
                std::string const sel = _playlistsView->selectedName();
                if (!sel.empty())
                {
                    _confirmDialog->setOnConfirm([this, sel](bool yes)
                                                 {
                                                     if (yes)
                                                         _playlistStore.remove(sel);
                                                     refreshPlaylists();
                                                     if (_terminal)
                                                         _terminal->forceRedraw(); });
                    _confirmDialog->open("Delete Playlist",
                                         "Delete playlist \"" + sel + "\"?",
                                         /*defaultYes=*/false);
                }
            }
            break;
        case MenuAction::Exit:
            quit();
            break;
        }
        if (_terminal)
            _terminal->forceRedraw();
    }

    void Application::playTrack(int index)
    {
        auto const *track = _playQueueView->track(index);
        if (!track)
            return;

        _audio.stop();
        if (_audio.load(*track) && _audio.play())
        {
            _playQueueView->setPlayingIndex(index);
            syncShuffleTo(index);
        }
        else
        {
            _playQueueView->setPlayingIndex(-1);
        }
    }

    void Application::playNext()
    {
        int count = _playQueueView->trackCount();
        if (count == 0)
            return;

        if (_shuffleMode)
        {
            int idx = shuffleAdvance(+1, /*wrap=*/true);
            if (idx >= 0) playTrack(idx);
            return;
        }

        int current = _playQueueView->playingIndex();
        int next = (current + 1) % count;
        playTrack(next);
    }

    void Application::playPrev()
    {
        int count = _playQueueView->trackCount();
        if (count == 0)
            return;

        if (_shuffleMode)
        {
            int idx = shuffleAdvance(-1, /*wrap=*/true);
            if (idx >= 0) playTrack(idx);
            return;
        }

        int current = _playQueueView->playingIndex();
        int prev = (current - 1 + count) % count;
        playTrack(prev);
    }

    void Application::toggleShuffleMode()
    {
        _shuffleMode = !_shuffleMode;
        if (_shuffleMode)
        {
            int seed = _playQueueView->playingIndex();
            if (seed < 0) seed = _playQueueView->selectedIndex();
            rebuildShuffleOrder(seed);
        }
        else
        {
            _shuffleOrder.clear();
            _shufflePos = -1;
        }
    }

    void Application::rebuildShuffleOrder(int seedIndex)
    {
        _shuffleOrder.clear();
        _shufflePos = -1;
        int const count = _playQueueView->trackCount();
        if (count <= 0) return;

        if (seedIndex < 0 || seedIndex >= count) seedIndex = -1;

        std::vector<int> indices;
        indices.reserve(count);
        for (int i = 0; i < count; ++i)
        {
            if (i != seedIndex) indices.push_back(i);
        }

        static std::mt19937 rng{std::random_device{}()};
        std::shuffle(indices.begin(), indices.end(), rng);

        _shuffleOrder.reserve(count);
        if (seedIndex >= 0)
        {
            if (auto const *t = _playQueueView->track(seedIndex))
                _shuffleOrder.push_back(t->path);
        }
        for (int i : indices)
        {
            if (auto const *t = _playQueueView->track(i))
                _shuffleOrder.push_back(t->path);
        }
        if (!_shuffleOrder.empty()) _shufflePos = 0;
    }

    int Application::shuffleAdvance(int dir, bool wrap)
    {
        if (_shuffleOrder.empty() || dir == 0) return -1;
        int const orderSize = static_cast<int>(_shuffleOrder.size());

        // Skip over paths that have since been removed from the queue.
        // Bounded to one full pass so a fully stale order can't spin forever.
        for (int steps = 0; steps < orderSize; ++steps)
        {
            int next = _shufflePos + dir;
            if (next < 0 || next >= orderSize)
            {
                if (!wrap) return -1;
                next = (next % orderSize + orderSize) % orderSize;
            }
            _shufflePos = next;

            auto const &p = _shuffleOrder[_shufflePos];
            int const n = _playQueueView->trackCount();
            for (int i = 0; i < n; ++i)
            {
                auto const *t = _playQueueView->track(i);
                if (t && t->path == p) return i;
            }
        }
        return -1;
    }

    void Application::syncShuffleTo(int queueIndex)
    {
        if (!_shuffleMode) return;
        auto const *track = _playQueueView->track(queueIndex);
        if (!track) return;
        for (int i = 0; i < static_cast<int>(_shuffleOrder.size()); ++i)
        {
            if (_shuffleOrder[i] == track->path)
            {
                _shufflePos = i;
                return;
            }
        }
        // The picked track is not in the current shuffle order (queue was
        // replaced, or the user reached for a fresh track from the library).
        // Start a new pass with it at the head.
        rebuildShuffleOrder(queueIndex);
    }

    int Application::currentShuffleQueueIndex() const
    {
        if (_shuffleOrder.empty() || _shufflePos < 0 ||
            _shufflePos >= static_cast<int>(_shuffleOrder.size()))
            return -1;
        auto const &p = _shuffleOrder[_shufflePos];
        int const n = _playQueueView->trackCount();
        for (int i = 0; i < n; ++i)
        {
            auto const *t = _playQueueView->track(i);
            if (t && t->path == p) return i;
        }
        return -1;
    }

    namespace
    {
        /// Build a minimal TrackInfo for FileBrowser activation on a local
        /// audio file. Streaming tracks never reach this path — they only
        /// enter the queue via PlsReader, which already populates streamUrl.
        TrackInfo trackInfoFromBrowserPath(std::filesystem::path const &p)
        {
            TrackInfo info;
            info.path = p;
            info.title = toNfc(p.stem().string());
            info.format = TrackInfo::formatFromPath(p);
            return info;
        }
    } // namespace

    void Application::addToPlayQueue(std::filesystem::path const &path)
    {
        _playQueueView->addTrack(trackInfoFromBrowserPath(path));
    }

    void Application::activateFromBrowser(std::vector<std::filesystem::path> const &paths)
    {
        if (paths.empty())
            return;

        // Enter: replace the current play queue with the selected files and play
        // the first newly-added track. setTracks fires onPlayingRemoved which
        // stops audio if a track was playing.
        std::vector<TrackInfo> newTracks;
        newTracks.reserve(paths.size());
        for (auto const &p : paths)
        {
            newTracks.push_back(trackInfoFromBrowserPath(p));
        }
        _playQueueView->setTracks(std::move(newTracks));
        playTrack(0);
    }

    void Application::appendPlayQueueFile(std::filesystem::path const &path)
    {
        if (!_playQueueView)
            return;

        auto ext = path.extension().string();
        for (auto &c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        auto loaded = (ext == ".pls") ? PlsReader::read(path)
                                      : M3uReader::read(path);
        if (!loaded)
            return;

        for (auto const &track : *loaded)
        {
            // Defensive: a stream-typed entry with no URL is unplayable.
            if (track.format == AudioFormat::Stream && track.streamUrl.empty())
                continue;
            _playQueueView->addTrack(track);
        }
    }

    void Application::scanLibrary(bool force)
    {
        if (!_libraryRepo || !_libraryRepo->isOpen())
            return;
        if (_config.libraryRoot.empty())
            return;

        _library.setRoot(_config.libraryRoot);

        // Split "mp3,wav,ogg,flac" → ["mp3","wav","ogg","flac"].
        std::vector<std::string> exts;
        std::string token;
        for (char c : kBuiltinExtensions)
        {
            if (c == ',')
            {
                if (!token.empty())
                    exts.push_back(token);
                token.clear();
            }
            else if (c != ' ' && c != '\t')
            {
                token.push_back(c);
            }
        }
        if (!token.empty())
            exts.push_back(std::move(token));

        // Skip the whole scan when the root is unchanged since the last
        // completed scan and we already have a persisted index. `force`
        // (menu rescan / root change) bypasses this.
        std::string const sig = LibraryScanner::rootSignature(_config.libraryRoot);
        if (!force && !sig.empty() && sig == _config.scanSig && !_library.empty())
            return;

        // Serialize scans: an ingest still running (or not-yet-finalized)
        // owns the repository, and a collect already blocks the run loop.
        if (_ingestActive.load() || _ingestThread.joinable() || _collectActive.load())
            return;

        std::filesystem::path root = _config.libraryRoot;
        LibraryScanner scanner(*_libraryRepo);

        // Drop the LibraryView tree up front: its `Node::track` pointers
        // index into `_library`'s vector, which setLibraryRoot() may already
        // have cleared (and finalizeScan() will reload). leftIsLibrary()
        // reports FileBrowser for the whole scan, so the view is neither
        // drawn nor keyed until finalizeScan() rebuilds it.
        if (_libraryView)
            _libraryView->clear();

        // ---- Pass 1: filesystem walk, inline on this (UI) thread ----
        // Blocks the run loop; the tick pumps input and repaints so the
        // "Collecting N" status updates and ESC can cancel.
        _collectCount.store(0);
        _collectCancel.store(false);
        _collectActive.store(true);

        bool canceled = false;
        auto entries = scanner.collect(
            root, exts,
            [this](int collected) -> bool
            {
                _collectCount.store(collected);
                if (_terminal)
                {
                    while (_terminal->pollEvent())
                        ;
                    draw();
                    _terminal->render();
                }
                return !_collectCancel.load();
            },
            canceled);

        _collectActive.store(false);

        if (canceled)
        {
            if (_terminal)
                _terminal->forceRedraw();
            return;
        }

        // ---- Pass 2: tag reading + repository writes, background ----
        // Remember the tree state we just walked; finalizeScan() persists it
        // once the ingest completes so the next startup can skip the walk.
        _pendingScanSig = sig;

        _ingestPercent.store(0);
        _ingestFinished.store(false);
        _ingestStop.store(false);
        _ingestActive.store(true);

        _ingestThread = std::thread(
            [this, entries = std::move(entries)]() mutable
            {
                // Worker thread: writes only `_libraryRepo` (never `_library`
                // or any view), so the UI keeps using the pre-scan snapshot.
                LibraryScanner scanner(*_libraryRepo);
                scanner.ingest(entries,
                               [this](int pct) { _ingestPercent.store(pct); },
                               [this] { return _ingestStop.load(); });

                // Publish "done" last; the run loop observes this, then joins
                // (the join is the happens-before barrier for the repo).
                _ingestFinished.store(true);
            });
    }

    void Application::finalizeScan()
    {
        if (_ingestThread.joinable())
            _ingestThread.join();

        _ingestFinished.store(false);
        _ingestActive.store(false); // leftIsLibrary() true again after rebuild

        // The repository now holds the reconciled index. Rebuild the
        // in-memory library from it (the join synchronized the worker's
        // writes) and refresh the view.
        _library.clear();
        if (_libraryRepo && _libraryRepo->isOpen())
            _libraryRepo->loadInto(_library);
        if (_libraryView)
        {
            _libraryView->rebuild();
            // rebuild() resets the cursor; re-apply the saved/last anchor so
            // focus survives a startup scan or an explicit rescan.
            if (_leftMode != LeftMode::FileBrowser && !_libraryAnchor.empty())
                _libraryView->locateForMode(_libraryAnchor);
        }
        // Saved search nav (n / N) holds paths into the previous index —
        // drop them so we don't locate a track that was removed or
        // re-tagged out of the result set.
        if (_searchDialog)
            _searchDialog->invalidateNav();

        // Persist the scanned tree's signature so the next startup can skip
        // the walk. Only on a full completion (not a shutdown bail-out).
        if (!_ingestStop.load() && !_pendingScanSig.empty())
        {
            _config.scanSig = _pendingScanSig;
            _config.save();
        }
        _pendingScanSig.clear();
        if (_terminal)
            _terminal->forceRedraw();
    }

    void Application::joinScanThread()
    {
        if (!_ingestThread.joinable())
            return;
        // Shutdown: signal the worker to bail at the next file boundary so
        // exit doesn't block on the whole remaining tag scan.
        _ingestStop.store(true);
        _ingestThread.join();
        _ingestActive.store(false);
        _ingestFinished.store(false);
    }

    void Application::drawScanStatus()
    {
        if (!_rootWindow)
            return;

        std::string text;
        if (_collectActive.load())
            text = "Collecting " + std::to_string(_collectCount.load()) + "\xE2\x80\xA6";
        else if (_ingestActive.load())
            text = std::to_string(_ingestPercent.load()) + "%";
        else
            return; // idle (or 100% reached → status silently gone)

        ventty::Window &win = *_rootWindow;
        int const w = win.width();
        int const h = win.height();
        if (w < 4 || h < 1)
            return;

        // Yellow, bottom-right, one cell of right padding.
        ventty::Style const style{ventty::Color{0xE6, 0xC8, 0x4A},
                                   _theme.background, ventty::Attr::Bold};
        int const x = w - 1 - static_cast<int>(text.size());
        if (x < 0)
            return;
        win.drawText(x, h - 1, text, style);
    }

    void Application::setLibraryRoot(std::filesystem::path root)
    {
        // A scan in flight owns the repository (ingest) or blocks the loop
        // (collect); clearing `_library`/`_libraryRepo` here would race it.
        // Ignore the request until it finishes.
        if (_ingestActive.load() || _ingestThread.joinable() || _collectActive.load())
            return;

        _config.libraryRoot = std::move(root);
        _config.scanSig.clear(); // new root → old signature is meaningless
        _config.save();

        // Wipe the previous root's entries before scanning the new one;
        // otherwise tracks from outside the new root would linger as dead
        // entries.
        _library.clear();
        if (_libraryRepo && _libraryRepo->isOpen())
        {
            _libraryRepo->clear();
        }
        if (_searchDialog)
            _searchDialog->invalidateNav();

        scanLibrary(/*force=*/true);
    }

    void Application::locatePlayingInLibrary()
    {
        // Right-panel focus → scroll the play queue so the currently-playing
        // track is shown at the top of the visible area.
        if (_focus == FocusPanel::PlayQueue)
        {
            if (_playQueueView)
                _playQueueView->focusPlayingTrack();
            return;
        }

        // Left-panel focus → locate the playing track inside the library
        // tree. Switch out of FileBrowser into the "Album" slot (AlbumArtist
        // tree) if needed, since locate only makes sense in a library
        // projection.
        if (!_libraryView)
            return;
        auto const &path = _audio.currentTrack().path;
        if (path.empty())
            return;

        if (!leftIsLibrary())
        {
            setLeftMode(LeftMode::AlbumArtistTree);
        }
        _libraryView->locate(path);
    }

} // namespace vtplayer
