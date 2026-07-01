// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "Application.h"

#include "../input/Keybindings.h"
#include "../library/LibraryRepository.h"
#include "../library/LibraryScanner.h"
#include "../playqueue/PlayQueueCache.h"
#include "../plugin/DecoderRegistry.h"
#include "../util/M3uReader.h"
#include "../util/PlsReader.h"
#include "../util/TagReader.h"
#include "../util/TagWriter.h"
#include "../util/UnicodeNormalize.h"
#include "../visualizer/DebugBars.h"
#include "../visualizer/MatrixRain.h"
#include "../visualizer/Oscilloscope.h"
#include "../visualizer/TagInfoView.h"
#include "../visualizer/VinylVis.h"

#include <ventty/input/KeyChord.h>
#include <ventty/terminal/Terminal.h>

#include <ventty/art/AsciiArt.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <random>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>

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
            if (s == "files")
                return LeftMode::FileBrowser;
            if (s == "directory")
                return LeftMode::Directory;
            if (s == "album" || s == "artist")
                return LeftMode::AlbumArtistTree;
            if (s == "playlists")
                return LeftMode::Playlists;
            if (s == "streaming" || s == "radio")
                return LeftMode::Streaming;
            return LeftMode::AlbumArtistTree;
        }

        char const *leftModeToConfig(LeftMode m)
        {
            switch (m)
            {
            case LeftMode::FileBrowser:
                return "files";
            case LeftMode::Directory:
                return "directory";
            case LeftMode::AlbumArtistTree:
                return "album";
            case LeftMode::Playlists:
                return "playlists";
            case LeftMode::Streaming:
                return "streaming";
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
            else if (_summonTrackDialog && _summonTrackDialog->wantsCursor())
            {
                _terminal->setCursorPos(_summonTrackDialog->cursorScreenX(),
                                        _summonTrackDialog->cursorScreenY());
                _terminal->setCursorVisible(true);
            }
            else if (_fileRenameDialog && _fileRenameDialog->wantsCursor())
            {
                _terminal->setCursorPos(_fileRenameDialog->cursorScreenX(),
                                        _fileRenameDialog->cursorScreenY());
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
        {
            std::error_code ec;
            auto const dir = radioDir();
            if (!dir.empty())
                std::filesystem::create_directories(dir, ec);
        }

        _theme = Theme::fromName(_config.themeName);

        // Keybindings: materialize the built-in presets (first run only) and
        // configure the input engine from the selected one. The "default"
        // preset binds nothing, so every key passes through to the built-in
        // handlers — only "vi" changes behavior. Warnings are non-fatal.
        Keybindings::materializePresets();
        std::vector<std::string> kbWarnings;
        Keybindings::load(_config.keymapPreset, _inputEngine, kbWarnings);

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
        // the header back to the default title. A future caller that wants the
        // header to name a source can re-stamp _currentPlaylistName after the
        // mutation.
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

        _summonTrackDialog = std::make_unique<SummonTrackDialog>();
        _summonTrackDialog->setTheme(_theme);
        {
            std::vector<SummonTrackDialog::Provider> providers;
            for (auto const & provider : _pluginHost.summonProviders())
            {
                providers.push_back(SummonTrackDialog::Provider{
                    provider.plugin,
                    provider.handle,
                    provider.label,
                });
            }
            _summonTrackDialog->setProviders(std::move(providers));
        }
        _summonTrackDialog->setDownloadDirectoryProvider([this]
                                                         {
                                                             if (_fileBrowser)
                                                                 return _fileBrowser->currentDirectory();
                                                             std::error_code ec;
                                                             auto cwd = std::filesystem::current_path(ec);
                                                             return ec ? std::filesystem::path(".") : cwd;
                                                         });
        _summonTrackDialog->setOnDownloadFinished([this](std::filesystem::path const &path)
                                                  {
                                                      if (_fileBrowser
                                                          && _fileBrowser->currentDirectory()
                                                                 == path.parent_path())
                                                      {
                                                          _fileBrowser->refresh();
                                                      }
                                                      if (_terminal)
                                                          _terminal->forceRedraw();
                                                  });

        _tagEditDialog = std::make_unique<TagEditDialog>();
        _tagEditDialog->setTheme(_theme);
        _tagEditDialog->setOnSave([this](std::vector<std::filesystem::path> const &targets,
                                          TagUpdate const &upd)
                                  { applyTagEdit(targets, upd); });

        _fileRenameDialog = std::make_unique<FileRenameDialog>();
        _fileRenameDialog->setTheme(_theme);
        _fileRenameDialog->setOnConfirm(
            [this](std::filesystem::path const &path,
                   std::string const &newName) -> std::optional<std::string>
            {
                return applyFileRename(path, newName);
            });

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

        _addToPlaylistMenu = std::make_unique<ContextMenu>();
        _addToPlaylistMenu->setTheme(_theme);
        _addToPlaylistMenu->setTitle("Add to Playlist");
        // Items are rebuilt per-open in openAddToPlaylistMenu() from the current
        // playlist set (and the session MRU order).
        _addToPlaylistMenu->setOnSelect([this](int idx)
                                        { onAddToPlaylistSelect(idx); });

        _playlistsView = std::make_unique<PlaylistsView>();
        _playlistsView->setTheme(_theme);
        _playlistsView->setTitle("Playlists");
        _playlistsView->setEmptyHint("No playlists - press ESC to create one");
        // Enter on a playlist row drills into its tracks (FileBrowser-style),
        // it no longer replaces the queue. Enter inside then replaces the queue
        // with the selection (multi-selection ∪ cursor) and plays, just like the
        // library / file browser do (replace + play).
        _playlistsView->setOnOpen([this](std::string const &name)
                                  { openPlaylistContents(name); });
        _playlistsView->setOnPlayTracks([this, sendToQueue](std::vector<TrackInfo> const &tracks)
                                        { sendToQueue(tracks, /*replace=*/true); });
        // Ctrl+S in the contents-view edit mode persists the reordered / trimmed
        // track list back to the .m3u8 file.
        _playlistsView->setOnSaveTracks(
            [this](std::string const &name, std::vector<TrackInfo> const &tracks)
            { return _playlistStore.write(name, tracks); });

        _streamingView = std::make_unique<PlaylistsView>();
        _streamingView->setTheme(_theme);
        _streamingView->setTitle("Streaming");
        _streamingView->setEmptyHint("No radio playlists in ~/.config/vtplayer/radio");
        _streamingView->setReadOnly(true);
        _streamingView->setOnOpen([this](std::string const &name)
                                  { openStreamingContents(name); });
        _streamingView->setOnPlayTracks([this, sendToQueue](std::vector<TrackInfo> const &tracks)
                                        { sendToQueue(tracks, /*replace=*/true); });

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
                                        || initMode == LeftMode::Directory);
            if (!_libraryAnchor.empty() && _libraryView && initIsLibrary)
                _libraryView->locateForMode(_libraryAnchor);

            // Populate the playlist panel before first draw when the restored
            // mode is Playlists; setLeftMode() doesn't list from disk.
            if (initMode == LeftMode::Playlists)
                refreshPlaylists();
            if (initMode == LeftMode::Streaming)
                refreshStreaming();
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
        // Cut playback first on every exit path. The rest of cleanup may wait
        // for scans, save config, or tear down plugins, but none of that
        // should leave audio playing.
        _audio.stop();

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

        // The summon dialog may own worker threads calling plugin interfaces.
        // Tear it down before PluginHost destroys provider handles and dlcloses.
        if (_summonTrackDialog)
        {
            _summonTrackDialog->close();
            _summonTrackDialog.reset();
        }

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

        // Browser split: source panel (left 40%) | PlayQueueView (right 60%).
        // Either side can be hidden, but never both at once. Hidden widgets get
        // zero-width rects so hit-testing skips them.
        if (_libraryPanelVisible && _playQueuePanelVisible)
        {
            int browserW = (w * 2) / 5;
            if (browserW < 20)
                browserW = 20;
            int playQueueW = w - browserW;
            _fileBrowser->setRect(0, contentY, browserW, contentH);
            _libraryView->setRect(0, contentY, browserW, contentH);
            if (_playlistsView)
                _playlistsView->setRect(0, contentY, browserW, contentH);
            if (_streamingView)
                _streamingView->setRect(0, contentY, browserW, contentH);
            _playQueueView->setRect(browserW, contentY, playQueueW, contentH);
        }
        else if (_playQueuePanelVisible)
        {
            _fileBrowser->setRect(0, contentY, 0, contentH);
            _libraryView->setRect(0, contentY, 0, contentH);
            if (_playlistsView)
                _playlistsView->setRect(0, contentY, 0, contentH);
            if (_streamingView)
                _streamingView->setRect(0, contentY, 0, contentH);
            _playQueueView->setRect(0, contentY, w, contentH);
        }
        else
        {
            _fileBrowser->setRect(0, contentY, w, contentH);
            _libraryView->setRect(0, contentY, w, contentH);
            if (_playlistsView)
                _playlistsView->setRect(0, contentY, w, contentH);
            if (_streamingView)
                _streamingView->setRect(0, contentY, w, contentH);
            _playQueueView->setRect(0, contentY, 0, contentH);
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

        // Track summon dialog (overlay).
        if (_summonTrackDialog && _summonTrackDialog->isOpen())
        {
            _summonTrackDialog->draw(*_rootWindow);
        }

        // Tag-edit dialog (overlay).
        if (_tagEditDialog && _tagEditDialog->isOpen())
        {
            _tagEditDialog->draw(*_rootWindow);
        }

        // File rename dialog (overlay).
        if (_fileRenameDialog && _fileRenameDialog->isOpen())
        {
            _fileRenameDialog->draw(*_rootWindow);
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
        if (_addToPlaylistMenu)
        {
            _addToPlaylistMenu->draw(*_rootWindow);
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
            case LeftSlot::Streaming:
                if (_streamingView)
                    _streamingView->draw(*_rootWindow);
                break;
            case LeftSlot::FileBrowser:
                _fileBrowser->draw(*_rootWindow);
                break;
            }
        }
        if (_playQueuePanelVisible)
            _playQueueView->draw(*_rootWindow);

        if (_libraryPanelVisible && _playQueuePanelVisible)
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
        else if (!_libraryPanelVisible)
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
        else
        {
            // No play queue panel: the left widget spans full width, so draw
            // the right edge normally supplied by the separator/queue panel.
            auto const &r = _fileBrowser->rect();
            int const x = r.x + r.width - 1;
            for (int y = 0; y < r.height; ++y)
            {
                _rootWindow->putChar(x, r.y + y, ventty::DOUBLE_BOX.v,
                                     ventty::Style{_theme.border, _theme.browserBg});
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
        _helpRows.clear();

        // Prettify a vim-notation key token for display: plain special keys get
        // a friendly label; chords and Ctrl-combos keep their config notation so
        // they correlate with the .keys file.
        auto pretty = [](std::string const & lhs) -> std::string {
            static std::unordered_map<std::string, std::string> const m = {
                {"<Space>", "Space"}, {"<CR>", "Enter"}, {"<Esc>", "Esc"},
                {"<Tab>", "Tab"},     {"<BS>", "Bksp"},  {"<Del>", "Del"},
                {"<Home>", "Home"},   {"<End>", "End"},  {"<PageUp>", "PgUp"},
                {"<PageDown>", "PgDn"}, {"<Up>", "Up"},  {"<Down>", "Down"},
                {"<Left>", "Left"},   {"<Right>", "Right"}, {"<lt>", "<"}, {"<gt>", ">"},
                {"<S-Up>", "Shift+Up"}, {"<S-Down>", "Shift+Down"},
                {"<S-Left>", "Shift+Left"}, {"<S-Right>", "Shift+Right"},
                {"<C-Up>", "Ctrl+Up"}, {"<C-Down>", "Ctrl+Down"},
            };
            auto const it = m.find(lhs);
            return it != m.end() ? it->second : lhs;
        };

        // Collect the normal-mode key(s) bound to each action in the active
        // preset, and note whether the preset defines a Visual mode.
        auto const bindings = Keybindings::activeBindings(_config.keymapPreset);
        std::unordered_map<int, std::string> keysFor;
        bool hasVisual = false;
        for (auto const & b : bindings)
        {
            if (b.mode == "visual")
            {
                hasVisual = true;
                continue;
            }
            if (b.mode != "normal")
                continue;
            std::string & s = keysFor[static_cast<int>(b.action)];
            s = s.empty() ? pretty(b.keys) : s + " / " + pretty(b.keys);
        }

        _helpRows.push_back({std::string("VT-PLAYER " VTPLAYER_VERSION
                                         " — Keyboard shortcuts (preset: ")
                                 + _config.keymapPreset + ")",
                             "", true});

        // Keymap-derived sections: a header per category (inserted on change),
        // then each bound action's key(s) and description. Unbound actions are
        // skipped, so the help always matches the active preset.
        bool started = false;
        ActionCategory cur = ActionCategory::Playback;
        for (auto const & e : actionHelpEntries())
        {
            auto const it = keysFor.find(static_cast<int>(e.action));
            if (it == keysFor.end())
                continue;
            if (!started || e.category != cur)
            {
                _helpRows.push_back({"", "", false});
                _helpRows.push_back({categoryTitle(e.category), "", true});
                cur = e.category;
                started = true;
            }
            _helpRows.push_back({"  " + it->second, e.description, false});
        }

        // Visual mode (only when the active preset defines one).
        if (hasVisual)
        {
            auto const v = keysFor.find(static_cast<int>(Action::EnterVisual));
            std::string const vkey = (v != keysFor.end()) ? v->second : std::string("v");
            _helpRows.push_back({"", "", false});
            _helpRows.push_back({"Visual mode", "", true});
            _helpRows.push_back({"  " + vkey, "Enter Visual mode", false});
            _helpRows.push_back({"  motions", "Extend the selection (j/k, etc.)", false});
            _helpRows.push_back({"  d", "Delete the selection", false});
            _helpRows.push_back({"  Esc", "Leave Visual mode", false});
            _helpRows.push_back({"  [count]", "Repeat a motion, e.g. 3j / 5dd", false});
        }

        // Static sections — the Visualizer screen's keys and the help overlay's
        // own navigation are not driven by the keymap.
        _helpRows.push_back({"", "", false});
        _helpRows.push_back({"Visualizer screen", "", true});
        _helpRows.push_back({"  0 - 5", "Pick style: Tag info / Spectrum / Matrix / Debug / Scope / Vinyl", false});

        _helpRows.push_back({"", "", false});
        _helpRows.push_back({"Help & misc", "", true});
        _helpRows.push_back({"  Up/Down/PgUp/PgDn/Home/End", "Scroll this help", false});
        _helpRows.push_back({"  Tab / Left / Right", "Switch help tab (Shortcuts / Plugins)", false});
        _helpRows.push_back({"  Esc", "Close help / open menu", false});
        _helpRows.push_back({"  Mouse", "Click to focus, click bar to seek, wheel to scroll", false});

        _helpRows.push_back({"", "", false});
        _helpRows.push_back({"Note", "Enter / Append / Remove / Search act on the focused panel", false});
        _helpRows.push_back({"Remap", "~/.config/vtplayer/keybindings/" + _config.keymapPreset + ".keys", false});

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

        if (_summonTrackDialog && _summonTrackDialog->isOpen())
        {
            _summonTrackDialog->handleKey(event);
            if (!_summonTrackDialog->isOpen() && _terminal)
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

        if (_fileRenameDialog && _fileRenameDialog->isOpen())
        {
            _fileRenameDialog->handleKey(event);
            if (!_fileRenameDialog->isOpen() && _terminal)
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

        if (_addToPlaylistMenu && _addToPlaylistMenu->isOpen())
        {
            _addToPlaylistMenu->handleKey(event);
            if (!_addToPlaylistMenu->isOpen() && _terminal)
                _terminal->forceRedraw();
            return;
        }

        // ESC opens the context menu. On the visualizer screen, ESC first
        // falls back to the browser (preserving prior behavior); a second
        // ESC opens the menu. In Help mode, ESC dismisses the help overlay.
        if (event.key == Key::Escape)
        {
            // First let the keybinding engine swallow ESC: it cancels a pending
            // count/chord or leaves Visual mode. Only when it has nothing to
            // cancel (and we're in the initial mode) does ESC fall through to
            // its built-in meaning below.
            if (_screen == Screen::Browser && _inputEngine.feedEsc())
                return;
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

        // Keybinding engine (Browser screen). Resolve the key through the
        // active preset: an Emit dispatches its action; a count digit or live
        // chord prefix is absorbed; a Passthrough (no binding, not mid-chord)
        // falls through to the built-in handlers below. The Visualizer screen
        // bypasses the engine so its 0-9 hotkeys aren't eaten by count entry.
        if (_screen == Screen::Browser)
        {
            ventty::KeyEvent norm = event;
            if (norm.key == Key::Char)
                norm.ch = hangulToQwerty(norm.ch);
            ventty::InputEngine::Result const r = _inputEngine.feed(ventty::KeyChord::from(norm));
            if (r.kind == ventty::InputEngine::ResultKind::Emit)
            {
                dispatch(actionFromToken(r.token), r.count);
                return;
            }
            if (r.kind == ventty::InputEngine::ResultKind::None)
            {
                // Absorbed (count digit / chord prefix). The run loop repaints
                // every iteration, so the pending-keys hint updates via the
                // normal diff render — no forced full redraw needed.
                return;
            }
            // Passthrough: fall through to the built-in handlers.
        }

        // Built-in handlers: the Browser-screen passthrough, and every key on
        // the Visualizer screen (dispatchToFocusedView is a no-op off Browser).
        handleGlobalKeys(event);
        dispatchToFocusedView(event);
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
            else if (slot == LeftSlot::Playlists && _playlistsView
                     && _playlistsView->rect().contains(event.x, event.y))
            {
                _playlistsView->handleMouse(event);
            }
            else if (slot == LeftSlot::Streaming && _streamingView
                     && _streamingView->rect().contains(event.x, event.y))
            {
                _streamingView->handleMouse(event);
            }
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
        //   1 Album, 2 Directory, 3 Playlists, 4 Streaming, 5 Files.
        if (_screen == Screen::Browser && event.key == Key::Char && !event.alt && !event.ctrl
            && (ch == '1' || ch == '2' || ch == '3' || ch == '4' || ch == '5'))
        {
            LeftMode const target = (ch == '1')   ? LeftMode::AlbumArtistTree
                                    : (ch == '2') ? LeftMode::Directory
                                    : (ch == '3') ? LeftMode::Playlists
                                    : (ch == '4') ? LeftMode::Streaming
                                                  : LeftMode::FileBrowser;
            applyLeftMode(target);
            return;
        }

        // l: toggle the Browser-screen left panel (Library / FileBrowser).
        // Hidden -> PlayQueue spans the full width. Disabled while the play
        // queue panel is hidden.
        if (_screen == Screen::Browser && event.key == Key::Char && ch == 'l' && !event.alt && !event.ctrl)
        {
            setLibraryPanelVisible(!_libraryPanelVisible);
            return;
        }

        // Shift+L: toggle the Browser-screen right panel (PlayQueueView).
        // Hidden -> the left panel spans the full width. Disabled while the
        // left panel is hidden.
        if (_screen == Screen::Browser && event.key == Key::Char && ch == 'L' && !event.alt && !event.ctrl)
        {
            setPlayQueuePanelVisible(!_playQueuePanelVisible);
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
        if (event.key == Key::Tab && _screen == Screen::Browser)
        {
            focusNextPanel();
            return;
        }

        if (_screen == Screen::Browser && event.key == Key::F3
            && _focus == FocusPanel::FileBrowser
            && (leftIsLibrary() || activeLeftWidget() == LeftSlot::FileBrowser))
        {
            openTagEditor(/*editImmediately=*/true);
            return;
        }

        if (_screen == Screen::Browser && event.key == Key::F5)
        {
            if (openFileRenameDialog())
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
            appendSelection();
            return;
        }

        // b: add the focused/selected track(s) to a saved playlist via a modal
        // picker. The track set depends on screen/focus (see
        // collectAddToPlaylistTracks); the picker self-suppresses when empty.
        if (event.key == Key::Char && (ch == 'b' || ch == 'B') && !event.alt && !event.ctrl)
        {
            openAddToPlaylistMenu();
            return;
        }

        // Ctrl+Up/Down: move play-queue item
        if (event.ctrl && event.key == Key::Up)
        {
            _playQueueView->moveSelectionUp();
            return;
        }
        if (event.ctrl && event.key == Key::Down)
        {
            _playQueueView->moveSelectionDown();
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

    void Application::applyLeftMode(LeftMode target)
    {
        LeftMode const previous = _leftMode;
        bool const previousIsLibrary = leftIsLibrary();
        bool const targetIsLibrary = (target == LeftMode::AlbumArtistTree
                                      || target == LeftMode::Directory);

        bool trackModeTransfer = false;
        std::filesystem::path trackAnchor;
        if (previousIsLibrary && targetIsLibrary && previous != target
            && _focus == FocusPanel::FileBrowser && _libraryView)
        {
            auto const sel = _libraryView->currentSelection();
            if (sel.kind == LibraryView::SelectionKind::Track && !sel.tracks.empty())
            {
                trackAnchor = sel.tracks.front().path;
                if (!trackAnchor.empty())
                {
                    _libraryAnchor = trackAnchor;
                    trackModeTransfer = true;
                }
            }
        }
        else if (previousIsLibrary && _libraryView)
        {
            // Leaving a library projection for another left slot: remember a
            // representative track so startup/scan restoration still has an
            // anchor, but do not use it to force 1/2 cross-mode expansion.
            auto cur = _libraryView->selectedTrackPath();
            if (!cur.empty())
                _libraryAnchor = cur;
        }

        // Picking a left mode implies the panel should be visible.
        setLibraryPanelVisible(true);
        if (targetIsLibrary && target == previous)
            return;

        setLeftMode(target);

        // Playlists: re-list from disk so the panel is current.
        if (target == LeftMode::Playlists)
            refreshPlaylists();
        if (target == LeftMode::Streaming)
            refreshStreaming();

        // Only track-to-track 1/2 switches carry the current track across
        // projections. Group/folder selections keep each projection's own
        // saved cursor and fold state.
        if (trackModeTransfer && _libraryView)
            _libraryView->locate(trackAnchor);
    }

    void Application::appendSelection()
    {
        // Playlists panel: in the contents view, append the selection
        // (multi-selection unioned with the cursor) to the queue. In the list
        // view there's no track to append.
        if (leftIsPlaylists())
        {
            if (_playlistsView && _playlistsView->inContents())
            {
                for (auto const &track : _playlistsView->selectedTracks())
                    _playQueueView->addTrack(track);
            }
            return;
        }
        if (leftIsStreaming())
        {
            if (_streamingView && _streamingView->inContents())
            {
                for (auto const &track : _streamingView->selectedTracks())
                    _playQueueView->addTrack(track);
            }
            return;
        }
        // Library panel: append the selected artist / album / track.
        if (leftIsLibrary())
        {
            _libraryView->sendSelectionToQueue(/*replace=*/false);
            return;
        }
        // File browser: append the selected file (or every audio file in the
        // selected directory).
        auto const *entry = _fileBrowser->selectedEntry();
        if (entry && entry->isAudio)
        {
            addToPlayQueue(entry->path);
        }
        else if (entry && entry->isDirectory)
        {
            for (auto const &p : _fileBrowser->collectAudioFiles(entry->path))
                addToPlayQueue(p);
        }
    }

    void Application::focusNextPanel()
    {
        // No-op when either side is hidden: only one panel is focusable.
        if (!_libraryPanelVisible || !_playQueuePanelVisible)
            return;
        focusPanel(_focus == FocusPanel::FileBrowser ? FocusPanel::PlayQueue
                                                     : FocusPanel::FileBrowser);
    }

    void Application::focusPanel(FocusPanel panel)
    {
        if (panel == FocusPanel::PlayQueue)
        {
            if (!_playQueuePanelVisible)
                return;
            // Only switch to the play queue if it isn't empty.
            if (_playQueueView->empty())
                return;
            _focus = FocusPanel::PlayQueue;
            setLeftFocused(false);
            _playQueueView->setFocused(true);
        }
        else
        {
            if (!_libraryPanelVisible)
                return;
            _focus = FocusPanel::FileBrowser;
            setLeftFocused(true);
            _playQueueView->setFocused(false);
        }
    }

    void Application::dispatchToFocusedView(ventty::KeyEvent const &event)
    {
        if (_screen != Screen::Browser)
            return;
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
            case LeftSlot::Streaming:
                if (_streamingView)
                    _streamingView->handleKey(event);
                break;
            case LeftSlot::FileBrowser:
                _fileBrowser->handleKey(event);
                break;
            }
        }
        else
        {
            if (_playQueuePanelVisible)
                _playQueueView->handleKey(event);
        }
    }

    void Application::dispatch(Action action, int count)
    {
        int const reps = (count > 0) ? count : 1;
        // In Visual mode, cursor motions extend the selection: synthesize the
        // Shift-modified arrow the views already interpret as "extend".
        bool const visual = (_inputEngine.mode() == "visual");

        auto sendView = [&](Key key, bool shift)
        {
            ventty::KeyEvent ev;
            ev.key = key;
            ev.shift = shift;
            for (int i = 0; i < reps; ++i)
                dispatchToFocusedView(ev);
        };
        auto sendOnce = [&](Key key)
        {
            ventty::KeyEvent ev;
            ev.key = key;
            dispatchToFocusedView(ev);
        };

        switch (action)
        {
        // -- global --
        case Action::Quit:
            quit();
            break;
        case Action::ToggleVisualizer:
            _screen = (_screen == Screen::Browser) ? Screen::Visualizer : Screen::Browser;
            resize();
            if (_terminal)
                _terminal->forceRedraw();
            break;
        case Action::ToggleHelp:
            toggleHelp();
            break;
        case Action::ToggleLeftPanel:
            setLibraryPanelVisible(!_libraryPanelVisible);
            break;
        case Action::ToggleRightPanel:
            setPlayQueuePanelVisible(!_playQueuePanelVisible);
            break;
        case Action::PlayPause:
        {
            auto state = _audio.state();
            if (state == PlayState::Playing)
                _audio.pause();
            else if (state == PlayState::Paused)
                _audio.play();
            else if (!_playQueueView->empty())
                playTrack(_playQueueView->selectedIndex());
            break;
        }
        case Action::Stop:
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
            break;
        case Action::NextTrack:
            for (int i = 0; i < reps; ++i)
                playNext();
            break;
        case Action::PrevTrack:
            for (int i = 0; i < reps; ++i)
                playPrev();
            break;
        case Action::SeekForward:
            _audio.seek(_audio.position() + 5.0f * static_cast<float>(reps));
            break;
        case Action::SeekBack:
            _audio.seek(_audio.position() - 5.0f * static_cast<float>(reps));
            break;
        case Action::CycleRepeat:
            switch (_repeatMode)
            {
            case RepeatMode::None: _repeatMode = RepeatMode::All; break;
            case RepeatMode::All: _repeatMode = RepeatMode::One; break;
            case RepeatMode::One: _repeatMode = RepeatMode::None; break;
            }
            break;
        case Action::ToggleShuffle:
            toggleShuffleMode();
            break;
        case Action::ToggleGain:
            _audio.setGainNorm(!_audio.gainNormEnabled());
            break;
        case Action::FocusNext:
            focusNextPanel();
            break;
        case Action::FocusLeft:
            focusPanel(FocusPanel::FileBrowser);
            break;
        case Action::FocusRight:
            focusPanel(FocusPanel::PlayQueue);
            break;
        case Action::LeftModeAlbum:
            applyLeftMode(LeftMode::AlbumArtistTree);
            break;
        case Action::LeftModeArtist:
            applyLeftMode(LeftMode::AlbumArtistTree);
            break;
        case Action::LeftModeDirectory:
            applyLeftMode(LeftMode::Directory);
            break;
        case Action::LeftModeFiles:
            applyLeftMode(LeftMode::FileBrowser);
            break;
        case Action::LeftModePlaylists:
            applyLeftMode(LeftMode::Playlists);
            break;
        case Action::LeftModeStreaming:
            applyLeftMode(LeftMode::Streaming);
            break;
        case Action::Append:
            if (_screen == Screen::Browser)
                appendSelection();
            break;
        case Action::AddToPlaylist:
            openAddToPlaylistMenu();
            break;
        case Action::TagEdit:
            if (_screen == Screen::Browser)
                openTagEditor();
            break;
        case Action::TagEditImmediate:
            if (_screen == Screen::Browser && _focus == FocusPanel::FileBrowser
                && (leftIsLibrary() || activeLeftWidget() == LeftSlot::FileBrowser))
            {
                openTagEditor(/*editImmediately=*/true);
            }
            break;
        case Action::RenameFile:
            if (_screen == Screen::Browser)
                openFileRenameDialog();
            break;
        case Action::Search:
        {
            // `/` is handled by the focused list view (LibraryView opens the
            // search dialog); other views ignore it, matching the built-in.
            ventty::KeyEvent ev;
            ev.key = Key::Char;
            ev.ch = U'/';
            dispatchToFocusedView(ev);
            break;
        }
        case Action::SearchNext:
            if (_searchDialog && _searchDialog->hasNav())
                _searchDialog->navigateNext();
            break;
        case Action::SearchPrev:
            if (_searchDialog && _searchDialog->hasNav())
                _searchDialog->navigatePrev();
            break;

        // -- focused-view (synthesized key events) --
        case Action::CursorDown:
            sendView(Key::Down, visual);
            break;
        case Action::CursorUp:
            sendView(Key::Up, visual);
            break;
        case Action::CursorPageDown:
            sendView(Key::PageDown, false);
            break;
        case Action::CursorPageUp:
            sendView(Key::PageUp, false);
            break;
        case Action::CursorHome:
            sendOnce(Key::Home);
            break;
        case Action::CursorEnd:
            sendOnce(Key::End);
            break;
        case Action::Expand:
            sendOnce(Key::Right);
            break;
        case Action::Collapse:
            sendOnce(Key::Left);
            break;
        case Action::Activate:
            sendOnce(Key::Enter);
            break;
        case Action::Remove:
            if (visual)
            {
                // Delete the whole Visual selection in one shot, then leave
                // Visual mode (vim's `d` on a selection).
                sendOnce(Key::Delete);
                _inputEngine.setMode("normal");
            }
            else
            {
                // `Ndd` removes N items, one per Delete.
                for (int i = 0; i < reps; ++i)
                    sendOnce(Key::Delete);
            }
            break;
        case Action::MoveUp:
            sendView(Key::Left, /*shift=*/true); // Shift+Left reorders selection up
            break;
        case Action::MoveDown:
            sendView(Key::Right, /*shift=*/true); // Shift+Right reorders selection down
            break;
        case Action::ExtendSelectionUp:
            sendView(Key::Up, /*shift=*/true); // Shift+Up extends the multi-selection
            break;
        case Action::ExtendSelectionDown:
            sendView(Key::Down, /*shift=*/true);
            break;
        case Action::SelectAll:
        {
            ventty::KeyEvent ev;
            ev.key = Key::Char;
            ev.ch = U'a';
            ev.ctrl = true;
            dispatchToFocusedView(ev);
            break;
        }
        case Action::Refresh:
            if (!openFileRenameDialog())
                sendOnce(Key::F5);
            break;
        case Action::GoBack:
            sendOnce(Key::Backspace);
            break;
        case Action::PlaylistEdit:
        {
            ventty::KeyEvent ev;
            ev.key = Key::Char;
            ev.ch = U'e';
            ev.ctrl = true;
            dispatchToFocusedView(ev);
            break;
        }
        case Action::PlaylistSave:
        {
            ventty::KeyEvent ev;
            ev.key = Key::Char;
            ev.ch = U's';
            ev.ctrl = true;
            dispatchToFocusedView(ev);
            break;
        }
        case Action::EnterVisual:
            _inputEngine.setMode("visual");
            break;
        case Action::ExitVisual:
            _inputEngine.setMode("normal");
            break;
        case Action::None:
            break;
        }
    }

    Application::MenuContext Application::classifyMenuContext() const
    {
        MenuContext ctx;
        ctx.queueFocused = (_focus == FocusPanel::PlayQueue);
        ctx.leftSlot = activeLeftWidget();
        ctx.playlistsEmpty = !_playlistsView || _playlistsView->empty();
        ctx.playlistsInContents = _playlistsView && _playlistsView->inContents();
        ctx.playlistsEditMode = _playlistsView && _playlistsView->inEditMode();
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
        auto const addThemeToggle = [&]
        {
            std::string const next = (_config.themeName == "light") ? "Dark" : "Light";
            add("Change theme: " + next, MenuAction::ChangeTheme);
        };

        // The play queue (right panel) owns its menu: it never borrows the left
        // panel's management actions (create / rename / delete playlist, rescan,
        // set library root). Keyed on focus so switching the left mode while the
        // queue is focused can't leak those items in. "Focus playing track"
        // adapts to queue focus in locatePlayingInLibrary().
        if (ctx.queueFocused)
        {
            add("Focus playing track", MenuAction::LocatePlaying);
            addThemeToggle();
            add("Exit", MenuAction::Exit);
            return;
        }

        // Below here the left panel holds focus, keyed on the active left mode.
        // FileBrowser is the only context that omits "Focus playing track" (it
        // would jump into the library tree, out of place here) and leads with
        // the library-root items instead.
        if (ctx.leftSlot == LeftSlot::FileBrowser)
        {
            if (ctx.libraryRootConfigured)
                add("Go to library root", MenuAction::GoToLibraryRoot);
            add("Set current directory as library root", MenuAction::SetLibraryRoot);
            add("Summon Track", MenuAction::SummonTrack);
            addThemeToggle();
            add("Exit", MenuAction::Exit);
            return;
        }

        // The library context keeps "Focus playing track" first; its action
        // already adapts to queue-vs-library focus in locatePlayingInLibrary().
        // The Playlists panel omits it (a playlist isn't where you'd locate the
        // currently-playing track) and only manages the collection.
        if (ctx.leftSlot != LeftSlot::Playlists && ctx.leftSlot != LeftSlot::Streaming)
            add("Focus playing track", MenuAction::LocatePlaying);

        switch (ctx.leftSlot)
        {
        case LeftSlot::Playlists:
            if (ctx.playlistsInContents)
            {
                // Track view: the edit-mode entry leads. "Edit playlist" arms
                // editing (mirrors Ctrl+E); while editing, "Save playlist"
                // persists (Ctrl+S) and "Discard changes" rolls back to the
                // on-disk version. Ctrl+E is not a toggle, so the menu is the
                // way out without saving (besides exiting via `..`).
                if (ctx.playlistsEditMode)
                {
                    add("Save playlist", MenuAction::SavePlaylist);
                    add("Discard changes", MenuAction::CancelPlaylistEdit);
                }
                else
                    add("Edit playlist", MenuAction::EditPlaylist);
            }
            else
            {
                // List view manages the playlist collection.
                add("Create playlist", MenuAction::CreatePlaylist);
                if (!ctx.playlistsEmpty)
                {
                    add("Rename playlist", MenuAction::RenamePlaylist);
                    add("Delete playlist", MenuAction::DeletePlaylist);
                }
            }
            break;
        case LeftSlot::Streaming:
            break;
        case LeftSlot::Library:
            add("Rescan library", MenuAction::RescanLibrary);
            break;
        case LeftSlot::FileBrowser:
            break; // handled above
        }

        addThemeToggle();
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

    void Application::applyTheme()
    {
        _theme = Theme::fromName(_config.themeName);

        if (_headerBar) _headerBar->setTheme(_theme);
        if (_fileBrowser) _fileBrowser->setTheme(_theme);
        if (_libraryView) _libraryView->setTheme(_theme);
        if (_playQueueView) _playQueueView->setTheme(_theme);
        if (_playlistsView) _playlistsView->setTheme(_theme);
        if (_streamingView) _streamingView->setTheme(_theme);
        if (_transportBar) _transportBar->setTheme(_theme);
        if (_visualizerView) _visualizerView->setTheme(_theme);
        if (_contextMenu) _contextMenu->setTheme(_theme);
        if (_addToPlaylistMenu) _addToPlaylistMenu->setTheme(_theme);
        if (_searchDialog) _searchDialog->setTheme(_theme);
        if (_summonTrackDialog) _summonTrackDialog->setTheme(_theme);
        if (_tagEditDialog) _tagEditDialog->setTheme(_theme);
        if (_fileRenameDialog) _fileRenameDialog->setTheme(_theme);
        if (_textInputDialog) _textInputDialog->setTheme(_theme);
        if (_confirmDialog) _confirmDialog->setTheme(_theme);
    }

    void Application::toggleTheme()
    {
        _config.themeName = (_config.themeName == "light") ? "dark" : "light";
        applyTheme();
        _config.save();
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
            case LeftMode::Directory:
                _libraryView->setMode(LibraryView::Mode::Directory);
                break;
            case LeftMode::FileBrowser:
            case LeftMode::Playlists:
            case LeftMode::Streaming:
                break;
            }
        }
        // Keep keyboard focus on whichever widget now occupies the left slot.
        // Critical for playlist/streaming modes: without this, input never reaches PlaylistsView.
        if (_focus == FocusPanel::FileBrowser)
            setLeftFocused(true);
        if (_terminal)
            _terminal->forceRedraw();
    }

    void Application::refreshPlaylists()
    {
        if (!_playlistsView)
            return;
        // Re-listing implies the list view: drop any drilled-in contents so a
        // renamed / deleted playlist can't leave a stale track list on screen,
        // and re-pressing `5` returns to the top-level list.
        _playlistsView->closeContents();
        _playlistsView->setItems(_playlistStore.list());
    }

    std::filesystem::path Application::radioDir() const
    {
        char const * home = std::getenv("HOME");
        if (!home || *home == '\0') return {};
        return std::filesystem::path(home) / ".config" / "vtplayer" / "radio";
    }

    std::filesystem::path Application::radioPathFor(std::string const &name) const
    {
        if (name.empty()) return {};
        return radioDir() / (name + ".pls");
    }

    void Application::refreshStreaming()
    {
        if (!_streamingView)
            return;

        _streamingView->closeContents();
        std::vector<std::string> names;
        std::filesystem::path const dir = radioDir();
        if (!dir.empty())
        {
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            if (!ec)
            {
                for (auto const &entry : std::filesystem::directory_iterator(dir, ec))
                {
                    if (ec) break;
                    std::error_code fec;
                    if (!entry.is_regular_file(fec)) continue;
                    auto const p = entry.path();
                    std::string ext = p.extension().string();
                    for (char &c : ext)
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (ext == ".pls")
                        names.push_back(p.stem().string());
                }
            }
        }
        std::sort(names.begin(), names.end(),
                  [](std::string const &a, std::string const &b)
                  {
                      return std::lexicographical_compare(
                          a.begin(), a.end(), b.begin(), b.end(),
                          [](unsigned char x, unsigned char y)
                          {
                              return std::tolower(x) < std::tolower(y);
                          });
                  });
        _streamingView->setItems(std::move(names));
    }

    void Application::applyQueueTitle()
    {
        if (_playQueueView)
            _playQueueView->setTitle(_currentPlaylistName.empty() ? "Play Queue"
                                                                  : _currentPlaylistName);
    }

    std::optional<std::vector<TrackInfo>>
    Application::resolvePlaylistTracks(std::string const &name) const
    {
        auto parsed = M3uReader::read(_playlistStore.pathFor(name));
        if (!parsed) // unreadable / missing
            return std::nullopt;

        // Re-resolve each entry against the library so the drilled-in tracks
        // carry the richer indexed metadata (album / grouping / ReplayGain)
        // that the bare M3U parse lacks; fall back to the parsed entry for
        // external paths. Mirrors PlayQueueCache::restore().
        std::vector<TrackInfo> resolved;
        resolved.reserve(parsed->size());
        for (auto const &t : *parsed)
        {
            if (auto const *indexed = _library.find(t.path))
                resolved.push_back(*indexed);
            else
                resolved.push_back(t);
        }
        return resolved;
    }

    void Application::openPlaylistContents(std::string const &name)
    {
        if (!_playlistsView)
            return;

        auto resolved = resolvePlaylistTracks(name);
        if (!resolved) // unreadable / missing — stay on the playlist list
            return;

        _playlistsView->showContents(name, std::move(*resolved));
    }

    std::optional<std::vector<TrackInfo>>
    Application::resolveStreamingTracks(std::string const &name) const
    {
        auto parsed = PlsReader::read(radioPathFor(name));
        if (!parsed)
            return std::nullopt;

        std::vector<TrackInfo> resolved;
        resolved.reserve(parsed->size());
        for (auto const &t : *parsed)
        {
            if (!t.isStream())
            {
                if (auto const *indexed = _library.find(t.path))
                {
                    resolved.push_back(*indexed);
                    continue;
                }
            }
            resolved.push_back(t);
        }
        return resolved;
    }

    void Application::openStreamingContents(std::string const &name)
    {
        if (!_streamingView)
            return;
        auto resolved = resolveStreamingTracks(name);
        if (!resolved)
            return;
        _streamingView->showContents(name, std::move(*resolved));
    }

    void Application::openAddToPlaylistMenu()
    {
        if (!_addToPlaylistMenu)
            return;

        // What gets added depends on the current screen/focus. An empty set
        // means the action is disabled here (e.g. Playlists focus, the
        // top-level Grouping axis, or nothing playing on the Visualizer).
        std::string title;
        std::vector<TrackInfo> tracks = collectAddToPlaylistTracks(title);
        if (tracks.empty())
            return;

        // Mode-5 order is exactly PlaylistStore::list(); the session's most
        // recently used playlist (if still present) floats to the top.
        std::vector<std::string> names = _playlistStore.list();
        if (names.empty())
            return;

        if (!_lastAddedPlaylist.empty())
        {
            auto it = std::find(names.begin(), names.end(), _lastAddedPlaylist);
            if (it != names.end())
                std::rotate(names.begin(), it, it + 1);
        }

        _addToPlaylistTracks = std::move(tracks);
        _addToPlaylistNames = names;
        _addToPlaylistMenu->setTitle(std::move(title));
        _addToPlaylistMenu->setItems(std::move(names));
        _addToPlaylistMenu->open();
        if (_terminal)
            _terminal->forceRedraw();
    }

    void Application::onAddToPlaylistSelect(int index)
    {
        if (index < 0 || index >= static_cast<int>(_addToPlaylistNames.size()))
            return;
        if (_addToPlaylistTracks.empty())
            return;

        std::string const &name = _addToPlaylistNames[index];
        if (_playlistStore.append(name, _addToPlaylistTracks))
        {
            _lastAddedPlaylist = name; // float to the top on the next open

            // If that same playlist is currently drilled into in the track
            // view, reflect the appended tracks right away. Skip while editing:
            // a reload would clobber the unsaved reorder / trim in progress.
            if (_playlistsView && _playlistsView->inContents() &&
                !_playlistsView->inEditMode() &&
                _playlistsView->selectedName() == name)
            {
                if (auto resolved = resolvePlaylistTracks(name))
                {
                    _playlistsView->reloadContents(std::move(*resolved));
                    if (_terminal)
                        _terminal->forceRedraw();
                }
            }
        }
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
        if (_streamingView)
            _streamingView->setFocused(on && slot == LeftSlot::Streaming);
    }

    void Application::setLibraryPanelVisible(bool visible)
    {
        if (_libraryPanelVisible == visible)
            return;
        if (!visible && !_playQueuePanelVisible)
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

    void Application::setPlayQueuePanelVisible(bool visible)
    {
        if (_playQueuePanelVisible == visible)
            return;
        if (!visible && !_libraryPanelVisible)
            return;
        _playQueuePanelVisible = visible;

        if (!visible)
        {
            // Nothing to focus on the right anymore: pin focus to the left.
            _focus = FocusPanel::FileBrowser;
            setLeftFocused(true);
            if (_playQueueView)
                _playQueueView->setFocused(false);
        }
        else
        {
            // Restoring the queue mirrors the existing l-toggle: the restored
            // panel receives focus so keyboard queue actions work immediately.
            _focus = FocusPanel::PlayQueue;
            setLeftFocused(false);
            if (_playQueueView)
                _playQueueView->setFocused(true);
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

    void Application::openTagEditor(bool editImmediately)
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
                case LibraryView::SelectionKind::MultiSelection: kindLabel = "Selection"; break;
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

        _tagEditDialog->open(std::move(headerText), std::move(tracks),
                             readOnly, editImmediately);
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

    bool Application::openFileRenameDialog()
    {
        if (!_fileRenameDialog || _screen != Screen::Browser
            || _focus != FocusPanel::FileBrowser)
        {
            return false;
        }

        TrackInfo track;
        bool found = false;

        if (leftIsLibrary() && _libraryView)
        {
            auto sel = _libraryView->currentSelection();
            if (sel.kind == LibraryView::SelectionKind::Track
                && sel.tracks.size() == 1 && !sel.tracks.front().isStream())
            {
                track = sel.tracks.front();
                found = true;
            }
        }
        else if (activeLeftWidget() == LeftSlot::FileBrowser && _fileBrowser)
        {
            auto const *entry = _fileBrowser->selectedEntry();
            if (entry && entry->isAudio && !entry->isDirectory && !entry->isPlaylist)
            {
                if (auto const *indexed = _library.find(entry->path))
                    track = *indexed;
                else
                    track = readTrackInfo(entry->path, /*filenameTitleFallback=*/false);
                track.path = entry->path;
                if (track.format == AudioFormat::Unknown)
                    track.format = TrackInfo::formatFromPath(entry->path);
                found = true;
            }
        }

        if (!found || track.path.empty())
            return false;

        TrackInfo realTags = readTrackInfo(track.path, /*filenameTitleFallback=*/false);
        realTags.path = track.path;
        if (realTags.format == AudioFormat::Unknown)
            realTags.format = track.format == AudioFormat::Unknown
                                  ? TrackInfo::formatFromPath(track.path)
                                  : track.format;
        track = std::move(realTags);

        _fileRenameDialog->open(std::move(track));
        if (_terminal)
            _terminal->forceRedraw();
        return true;
    }

    std::optional<std::string>
    Application::applyFileRename(std::filesystem::path const & path,
                                 std::string const & newName)
    {
        if (path.empty())
            return std::string("No file selected");
        if (newName.empty() || newName == "." || newName == "..")
            return std::string("Invalid file name");
        if (newName.find('/') != std::string::npos
            || newName.find('\\') != std::string::npos)
        {
            return std::string("Use a file name, not a path");
        }

        std::filesystem::path const leaf(newName);
        if (leaf.has_parent_path() || leaf.filename().string() != newName)
            return std::string("Use a file name, not a path");

        std::filesystem::path const target = path.parent_path() / leaf;
        if (target == path)
            return std::nullopt;

        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || ec)
            return std::string("Original file no longer exists");

        ec.clear();
        bool const targetExists = std::filesystem::exists(target, ec);
        if (ec)
            return ec.message();

        bool sameExistingFile = false;
        if (targetExists)
        {
            ec.clear();
            sameExistingFile = std::filesystem::equivalent(path, target, ec);
            if (ec) sameExistingFile = false;
        }
        if (targetExists && !sameExistingFile)
            return std::string("File already exists");

        TrackInfo replacement;
        bool const wasIndexed = (_library.find(path) != nullptr);
        if (auto const *existing = _library.find(path))
            replacement = *existing;

        ec.clear();
        std::filesystem::rename(path, target, ec);
        if (ec)
            return ec.message();

        TrackInfo diskInfo = readTrackInfo(target, /*filenameTitleFallback=*/true);
        if (replacement.path.empty())
            replacement = diskInfo;
        else
        {
            replacement.path = target;
            replacement.format = diskInfo.format;
            replacement.duration = diskInfo.duration > 0.0f ? diskInfo.duration
                                                            : replacement.duration;
        }
        replacement.path = target;
        if (replacement.format == AudioFormat::Unknown)
            replacement.format = TrackInfo::formatFromPath(target);

        std::int64_t newMtime = 0;
        std::int64_t newSize = 0;
        ec.clear();
        if (auto t = std::filesystem::last_write_time(target, ec); !ec)
            newMtime = fileTimeToUnix(t);
        ec.clear();
        if (auto sz = std::filesystem::file_size(target, ec); !ec)
            newSize = static_cast<std::int64_t>(sz);
        replacement.mtime = newMtime;
        if (newSize > 0) replacement.size = newSize;

        if (wasIndexed)
        {
            _library.erase(path);
            _library.upsert(replacement);
            if (_libraryRepo && _libraryRepo->isOpen())
            {
                _libraryRepo->erase(path);
                _libraryRepo->upsert(replacement);
            }
        }

        if (_playQueueView)
            _playQueueView->replaceTrackPath(path, replacement);

        for (auto & p : _shuffleOrder)
        {
            if (p == path)
                p = target;
        }

        if (_audio.currentTrack().path == path)
            _audio.updateCurrentTrackMeta(replacement);

        if (_libraryAnchor == path)
            _libraryAnchor = target;

        if (_libraryView)
        {
            _libraryView->rebuild();
            _libraryView->locate(target);
        }
        if (_searchDialog)
            _searchDialog->invalidateNav();

        if (_fileBrowser && _fileBrowser->currentDirectory() == target.parent_path())
        {
            _fileBrowser->refresh();
            _fileBrowser->locate(target);
        }

        if (_terminal)
            _terminal->forceRedraw();
        return std::nullopt;
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
                auto const root = _fileBrowser->currentDirectory();
                if (_confirmDialog && shouldConfirmLibraryRootChange(root))
                {
                    _confirmDialog->setOnConfirm(
                        [this, root](bool yes)
                        {
                            if (yes)
                                setLibraryRoot(root);
                            if (_terminal)
                                _terminal->forceRedraw();
                        });
                    _confirmDialog->open("Replace Library Root",
                                         "Discard current library root and rescan here?",
                                         /*defaultYes=*/false);
                    if (_terminal)
                        _terminal->forceRedraw();
                }
                else
                {
                    setLibraryRoot(root);
                }
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
        case MenuAction::SummonTrack:
            if (_summonTrackDialog)
                _summonTrackDialog->open();
            break;
        case MenuAction::RescanLibrary:
            scanLibrary(/*force=*/true);
            break;
        case MenuAction::LocatePlaying:
            locatePlayingInLibrary();
            break;
        case MenuAction::ChangeTheme:
            toggleTheme();
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
        case MenuAction::EditPlaylist:
            if (_playlistsView)
                _playlistsView->enterEditMode();
            break;
        case MenuAction::SavePlaylist:
            if (_playlistsView)
                _playlistsView->saveEdits();
            break;
        case MenuAction::CancelPlaylistEdit:
            // Roll back: re-read the playlist file and leave edit mode,
            // dropping the unsaved reorder / trim.
            if (_playlistsView && _playlistsView->inEditMode())
            {
                if (auto reloaded = resolvePlaylistTracks(_playlistsView->selectedName()))
                    _playlistsView->discardEdits(std::move(*reloaded));
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

    std::vector<TrackInfo> Application::collectAddToPlaylistTracks(std::string &outTitle) const
    {
        // A non-empty set means the action is enabled; the picker gets a short,
        // constant heading (drawn centered in the dialog's top border).
        auto heading = [&outTitle](std::vector<TrackInfo> tracks,
                                   std::string const & /*label*/) -> std::vector<TrackInfo> {
            if (tracks.empty())
                return {};
            outTitle = "Add to playlist";
            return tracks;
        };

        // Visualizer screen keeps the legacy behavior: add the playing track.
        if (_screen == Screen::Visualizer)
        {
            if (!_playQueueView)
                return {};
            TrackInfo const *t = _playQueueView->track(_playQueueView->playingIndex());
            if (!t)
                return {};
            return heading({*t}, {});
        }

        // Browser screen: act on whatever is focused.
        if (_focus == FocusPanel::PlayQueue)
        {
            if (!_playQueueView)
                return {};
            return heading(_playQueueView->selectedTracks(), {});
        }

        switch (activeLeftWidget())
        {
        case LeftSlot::Library:
        {
            if (!_libraryView)
                return {};
            auto sel = _libraryView->currentSelection();
            using SK = LibraryView::SelectionKind;
            // Disabled with no selection and at the top-level Grouping axis.
            if (sel.kind == SK::None || sel.kind == SK::Grouping)
                return {};
            return heading(std::move(sel.tracks), sel.label);
        }
        case LeftSlot::Playlists:
            return {}; // disabled while the playlist browser is focused
        case LeftSlot::Streaming:
        {
            if (!_streamingView || !_streamingView->inContents())
                return {};
            return heading(_streamingView->selectedTracks(), {});
        }
        case LeftSlot::FileBrowser:
        {
            if (!_fileBrowser)
                return {};
            auto const *entry = _fileBrowser->selectedEntry();
            if (!entry)
                return {};

            std::vector<std::filesystem::path> paths = _fileBrowser->selectedAudioPaths();
            std::string label;
            if (!paths.empty())
            {
                label = paths.size() == 1 ? paths.front().filename().string()
                                          : std::to_string(paths.size()) + " files";
            }
            else if (entry->isDirectory)
            {
                paths = _fileBrowser->collectAudioFiles(entry->path);
                label = entry->path.filename().string();
            }
            if (paths.empty())
                return {};

            // Prefer the indexed metadata, falling back to a minimal record for
            // files outside the library root (mirrors openPlaylistContents).
            std::vector<TrackInfo> tracks;
            tracks.reserve(paths.size());
            for (auto const &p : paths)
            {
                if (auto const *indexed = _library.find(p))
                    tracks.push_back(*indexed);
                else
                    tracks.push_back(trackInfoFromBrowserPath(p));
            }
            return heading(std::move(tracks), label);
        }
        }
        return {};
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
        // Scan status takes the corner while a scan is in flight (yellow);
        // otherwise the keybinding engine's mode / pending-chord hint shows
        // there (vim-style showcmd, cyan), Browser screen only.
        ventty::Color fg{0xE6, 0xC8, 0x4A};
        if (_collectActive.load())
        {
            text = "Collecting " + std::to_string(_collectCount.load()) + "\xE2\x80\xA6";
        }
        else if (_ingestActive.load())
        {
            text = std::to_string(_ingestPercent.load()) + "%";
        }
        else if (_screen == Screen::Browser)
        {
            std::string const pending = _inputEngine.pendingDisplay();
            bool const visual = (_inputEngine.mode() == "visual");
            if (visual)
                text = pending.empty() ? "-- VISUAL --" : ("-- VISUAL -- " + pending);
            else
                text = pending; // empty unless a count/chord is mid-entry
            fg = ventty::Color{0x5A, 0xC8, 0xE6};
        }

        if (text.empty())
            return; // idle

        ventty::Window &win = *_rootWindow;
        int const w = win.width();
        int const h = win.height();
        if (w < 4 || h < 1)
            return;

        // Bottom-right, one cell of right padding.
        ventty::Style const style{fg, _theme.background, ventty::Attr::Bold};
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

    bool Application::shouldConfirmLibraryRootChange(std::filesystem::path const & root) const
    {
        if (_config.libraryRoot.empty() || !_libraryRepo)
            return false;

        std::error_code ec;
        if (!std::filesystem::exists(_libraryRepo->path(), ec) || ec)
            return false;

        ec.clear();
        if (std::filesystem::equivalent(_config.libraryRoot, root, ec) && !ec)
            return false;

        return true;
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
