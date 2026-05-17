// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "Application.h"

#include "../library/LibraryRepository.h"
#include "../library/LibraryScanner.h"
#include "../playqueue/PlayQueueCache.h"
#include "../util/M3uReader.h"
#include "../util/UnicodeNormalize.h"
#include "../visualizer/DebugBars.h"
#include "../visualizer/MatrixRain.h"
#include "../visualizer/Oscilloscope.h"
#include "../visualizer/TagInfoView.h"
#include "../visualizer/VinylVis.h"

#ifdef VTPLAYER_BUILD_BUNDLE
#include <ventty/ventty_gfx.h>
#else
#include <ventty/terminal/Terminal.h>
#endif

#include <ventty/art/AsciiArt.h>

#include <chrono>
#include <thread>

namespace vtplayer
{

    using Key = ventty::KeyEvent::Key;

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
            if (s == "artist")
                return LeftMode::Artist;
            if (s == "directory")
                return LeftMode::Directory;
            return LeftMode::Album; // default; "filebrowser" is never persisted
        }

        char const *leftModeToConfig(LeftMode m)
        {
            switch (m)
            {
            case LeftMode::Artist:
                return "artist";
            case LeftMode::Directory:
                return "directory";
            case LeftMode::Album:
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
        _audio.shutdown();
    }

    int Application::run()
    {
        init();
        _running = true;

        while (_running && _terminal->isRunning())
        {
            while (_terminal->pollEvent())
                ;

            updateUI();
            draw();
            _terminal->render();

            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        cleanup();
        return 0;
    }

    void Application::quit()
    {
        _audio.stop();
        _running = false;
        _terminal->quit();
    }

    void Application::initTerminal()
    {
#ifdef VTPLAYER_BUILD_BUNDLE
        auto term = std::make_unique<ventty::GfxTerminal>();
        if (!term->init(100, 35, "VT-PLAYER", 1))
        {
            return;
        }
        term->loadBuiltinFont();
        _terminal = std::move(term);
#else
        auto term = std::make_unique<ventty::Terminal>();
        if (!term->init())
        {
            return;
        }
        _terminal = std::move(term);
#endif
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
        _fileBrowser->setAllowedExtensions(_config.extensions);
        {
            // FileBrowser always opens at the directory the player was
            // launched from (no persisted start directory anymore).
            std::error_code ec;
            auto cwd = std::filesystem::current_path(ec);
            _fileBrowser->setDirectory(ec ? std::filesystem::path("/") : cwd);
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
                                       if (_libraryView)
                                           _libraryView->locate(path);
                                       if (_terminal)
                                           _terminal->forceRedraw(); });

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

        resize();

        // Open the media library index. Failure is non-fatal — the player
        // still works without a library; only library-backed features are off.
        _libraryRepo = std::make_unique<LibraryRepository>(LibraryRepository::defaultPath());
        if (_libraryRepo->open())
        {
            _libraryRepo->loadInto(_library);
            scanLibrary();
        }
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
            setLeftMode(initMode);
        }

        // Restore the previous session's play queue (path list, resolved
        // against the library index for full metadata).
        {
            auto restored = PlayQueueCache::restore(_library);
            if (!restored.empty())
            {
                _playQueueView->setTracks(std::move(restored));
            }
        }

        // If an initial file was provided, add it to the play queue and play
        if (!_initialFile.empty())
        {
            addToPlayQueue(_initialFile);
            playTrack(0);
        }
    }

    void Application::cleanup()
    {
        // Sync runtime-mutable settings back before persisting.
        _config.gainNorm = _audio.gainNormEnabled();
        _config.visualizerIndex = _visualizerIndex;
        _config.leftMode = leftModeToConfig(_leftMode);
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
            _playQueueView->setRect(browserW, contentY, playQueueW, contentH);
        }
        else
        {
            _fileBrowser->setRect(0, contentY, 0, contentH);
            _libraryView->setRect(0, contentY, 0, contentH);
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
        _transportBar->setTrackName(_audio.currentTrack().title);
        _transportBar->setPosition(_audio.position());
        _transportBar->setDuration(_audio.duration());
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

        // Context menu overlay (drawn last so it sits on top)
        if (_contextMenu)
        {
            _contextMenu->draw(*_rootWindow);
        }
    }

    void Application::drawBrowserScreen()
    {
        if (_libraryPanelVisible)
        {
            if (leftIsLibrary())
            {
                _libraryView->draw(*_rootWindow);
            }
            else
            {
                _fileBrowser->draw(*_rootWindow);
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
#ifndef VTPLAYER_VERSION
#define VTPLAYER_VERSION "unknown"
#endif
        // clang-format off
        _helpRows = {
            {"VT-PLAYER " VTPLAYER_VERSION " — Keyboard shortcuts", "", true},
            {"", "", false},
            {"Playback", "", true},
            {"  Space",                 "Play / Pause", false},
            {"  N / P",                 "Next / Previous track", false},
            {"  < / >",                 "Seek -5s / +5s", false},
            {"  R",                     "Cycle repeat: none -> all -> one", false},
            {"  S",                     "Shuffle play queue", false},
            {"  G",                     "Toggle gain normalization (ReplayGain / auto-gain)", false},
            {"", "", false},
            {"Browser - Library", "", true},
            {"  F1 / F2 / F3 / F4",     "Left panel: Artist / Album / Directory / Files", false},
            {"  L",                     "Toggle left panel (play queue full-width when hidden)", false},
            {"  Tab",                   "Switch focus (browser <-> play queue)", false},
            {"  Left / Right",          "Collapse / expand selected group", false},
            {"  Enter",                 "Replace play queue with artist/album/track and play", false},
            {"  A",                     "Append artist/album/track to play queue", false},
            {"  /",                     "Search library", false},
            {"", "", false},
            {"Browser - Files", "", true},
            {"  Enter",                 "Replace play queue with selection and play", false},
            {"  A",                     "Add selected file (or every audio file in selected dir) to play queue", false},
            {"  Backspace",             "Go up to parent directory", false},
            {"  F5",                    "Refresh listing", false},
            {"", "", false},
            {"Play Queue", "", true},
            {"  Enter",                 "Play selected track", false},
            {"  Del / D / Backspace",   "Remove selection", false},
            {"  Ctrl+Up / Ctrl+Down",   "Move selected track", false},
            {"  Ctrl+A",                "Select all", false},
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
            {"  ESC",                   "Open menu / dismiss overlay", false},
            {"  Q",                     "Quit", false},
        };
        // clang-format on

        // Force a re-flow on next draw/scroll-clamp.
        _helpLines.clear();
        _helpLayoutWidth = -1;
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
        int const top = 1;
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

        ventty::Style headerStyle{_theme.browserHeaderFg, _theme.background, ventty::Attr::Bold};
        ventty::Style keyStyle{_theme.browserAudioFg, _theme.background};
        ventty::Style descStyle{_theme.foreground, _theme.background};

        ensureHelpLayout();
        int const visible = bottom - top + 1;
        int const total = static_cast<int>(_helpLines.size());
        int const drawRows = std::min(visible, total - _helpScroll);

        for (int i = 0; i < drawRows; ++i)
        {
            int const y = top + i;
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
            if (_helpRows.empty())
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

        // Modal search dialog consumes all input while open.
        if (_searchDialog && _searchDialog->isOpen())
        {
            _searchDialog->handleKey(event);
            return;
        }

        // Modal context menu consumes all input while open.
        if (_contextMenu && _contextMenu->isOpen())
        {
            _contextMenu->handleKey(event);
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
                if (leftIsLibrary())
                {
                    _libraryView->handleKey(event);
                }
                else
                {
                    _fileBrowser->handleKey(event);
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
                        _fileBrowser->setFocused(!leftIsLibrary());
                        _libraryView->setFocused(leftIsLibrary());
                        _playQueueView->setFocused(false);
                    }
                }
                else if (playQueueRect.contains(event.x, event.y))
                {
                    if (_focus != FocusPanel::PlayQueue)
                    {
                        _focus = FocusPanel::PlayQueue;
                        _fileBrowser->setFocused(false);
                        _libraryView->setFocused(false);
                        _playQueueView->setFocused(true);
                    }
                }
            }

            // Delegate to focused panel
            if (leftIsLibrary() && _libraryView->rect().contains(event.x, event.y))
            {
                _libraryView->handleMouse(event);
            }
            else if (!leftIsLibrary() && _fileBrowser->rect().contains(event.x, event.y))
            {
                _fileBrowser->handleMouse(event);
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

        // F1-F4: pick the Browser-screen left panel directly.
        //   F1 Artist · F2 Album · F3 Directory (all from the library index)
        //   F4 FileBrowser (live filesystem from the launch CWD)
        if (_screen == Screen::Browser && (event.key == Key::F1 || event.key == Key::F2 || event.key == Key::F3 || event.key == Key::F4))
        {
            // Picking a left mode implies the panel should be visible.
            setLibraryPanelVisible(true);
            switch (event.key)
            {
            case Key::F1:
                setLeftMode(LeftMode::Artist);
                break;
            case Key::F2:
                setLeftMode(LeftMode::Album);
                break;
            case Key::F3:
                setLeftMode(LeftMode::Directory);
                break;
            case Key::F4:
                setLeftMode(LeftMode::FileBrowser);
                break;
            default:
                break;
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
                    _fileBrowser->setFocused(false);
                    _libraryView->setFocused(false);
                    _playQueueView->setFocused(true);
                }
            }
            else
            {
                _focus = FocusPanel::FileBrowser;
                _fileBrowser->setFocused(!leftIsLibrary());
                _libraryView->setFocused(leftIsLibrary());
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

        // s: shuffle current play queue (one-shot reorder)
        if (event.key == Key::Char && (ch == 's' || ch == 'S') && !event.alt && !event.ctrl)
        {
            _playQueueView->shuffle();
            return;
        }

        // n: next track
        if (event.key == Key::Char && (ch == 'n' || ch == 'N') && !event.alt && !event.ctrl)
        {
            playNext();
            return;
        }

        // p: previous track
        if (event.key == Key::Char && (ch == 'p' || ch == 'P') && !event.alt && !event.ctrl)
        {
            playPrev();
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

        // F5: refresh
        if (event.key == Key::F5)
        {
            _fileBrowser->refresh();
            _terminal->forceRedraw();
            return;
        }
    }

    void Application::openContextMenu()
    {
        if (!_contextMenu)
            return;

        // Build the item set for the current left-panel mode:
        //   FileBrowser → "Set current directory as library root"
        //   library     → "Rescan library"
        std::vector<std::string> items;
        _contextMenuActions.clear();

        if (!leftIsLibrary())
        {
            items.emplace_back("Set current directory as library root");
            _contextMenuActions.push_back(MenuAction::SetLibraryRoot);
        }
        else
        {
            items.emplace_back("Rescan library");
            _contextMenuActions.push_back(MenuAction::RescanLibrary);
        }

        items.emplace_back("Locate playing track in library");
        _contextMenuActions.push_back(MenuAction::LocatePlaying);

        items.emplace_back("Exit");
        _contextMenuActions.push_back(MenuAction::Exit);

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
            case LeftMode::Artist:
                _libraryView->setMode(LibraryView::Mode::Artist);
                break;
            case LeftMode::Album:
                _libraryView->setMode(LibraryView::Mode::Album);
                break;
            case LeftMode::Directory:
                _libraryView->setMode(LibraryView::Mode::Directory);
                break;
            case LeftMode::FileBrowser:
                break;
            }
        }
        // Keep keyboard focus on whichever widget now occupies the left slot.
        if (_focus == FocusPanel::FileBrowser)
        {
            bool const fb = (mode == LeftMode::FileBrowser);
            if (_fileBrowser)
                _fileBrowser->setFocused(fb);
            if (_libraryView)
                _libraryView->setFocused(!fb);
        }
        if (_terminal)
            _terminal->forceRedraw();
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
            if (_fileBrowser)
                _fileBrowser->setFocused(false);
            if (_libraryView)
                _libraryView->setFocused(false);
            if (_playQueueView)
                _playQueueView->setFocused(true);
        }
        else
        {
            // Hand focus back to whichever widget occupies the left slot.
            _focus = FocusPanel::FileBrowser;
            bool const lib = leftIsLibrary();
            if (_fileBrowser)
                _fileBrowser->setFocused(!lib);
            if (_libraryView)
                _libraryView->setFocused(lib);
            if (_playQueueView)
                _playQueueView->setFocused(false);
        }

        resize();
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
        case MenuAction::RescanLibrary:
            scanLibrary();
            break;
        case MenuAction::LocatePlaying:
            locatePlayingInLibrary();
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
        if (_audio.load(track->path))
        {
            _audio.play();
            _playQueueView->setPlayingIndex(index);
        }
        else
        {
            _playQueueView->setPlayingIndex(-1);
        }
    }

    void Application::playNext()
    {
        int current = _playQueueView->playingIndex();
        int count = _playQueueView->trackCount();
        if (count == 0)
            return;

        int next = (current + 1) % count;
        playTrack(next);
    }

    void Application::playPrev()
    {
        int current = _playQueueView->playingIndex();
        int count = _playQueueView->trackCount();
        if (count == 0)
            return;

        int prev = (current - 1 + count) % count;
        playTrack(prev);
    }

    void Application::addToPlayQueue(std::filesystem::path const &path)
    {
        TrackInfo info;
        info.path = path;
        info.title = toNfc(path.stem().string());
        info.format = TrackInfo::formatFromPath(path);

        // Try to get duration by briefly loading
        // For now just add with unknown duration
        _playQueueView->addTrack(info);
    }

    void Application::activateFromBrowser(std::vector<std::filesystem::path> const &paths)
    {
        if (paths.empty())
            return;

        auto buildInfo = [](std::filesystem::path const &p)
        {
            TrackInfo info;
            info.path = p;
            info.title = toNfc(p.stem().string());
            info.format = TrackInfo::formatFromPath(p);
            return info;
        };

        // Enter: replace the current play queue with the selected files and play
        // the first newly-added track. setTracks fires onPlayingRemoved which
        // stops audio if a track was playing.
        std::vector<TrackInfo> newTracks;
        newTracks.reserve(paths.size());
        for (auto const &p : paths)
            newTracks.push_back(buildInfo(p));
        _playQueueView->setTracks(std::move(newTracks));
        playTrack(0);
    }

    void Application::appendPlayQueueFile(std::filesystem::path const &path)
    {
        if (!_playQueueView)
            return;

        auto loaded = M3uReader::read(path);
        if (!loaded)
            return;

        for (auto const &track : *loaded)
        {
            _playQueueView->addTrack(track);
        }
    }

    void Application::scanLibrary()
    {
        if (!_libraryRepo || !_libraryRepo->isOpen())
            return;
        if (_config.libraryRoot.empty())
            return;

        _library.setRoot(_config.libraryRoot);

        // Split "mp3,wav,ogg,flac" → ["mp3","wav","ogg","flac"].
        std::vector<std::string> exts;
        std::string token;
        for (char c : _config.extensions)
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

        LibraryScanner scanner(_library, *_libraryRepo);
        scanner.scan(_config.libraryRoot, exts);

        if (_libraryView)
            _libraryView->rebuild();
    }

    void Application::setLibraryRoot(std::filesystem::path root)
    {
        _config.libraryRoot = std::move(root);
        _config.save();

        // Wipe the previous root's entries before scanning the new one;
        // otherwise tracks from outside the new root would linger as dead
        // entries.
        _library.clear();
        if (_libraryRepo && _libraryRepo->isOpen())
        {
            _libraryRepo->clear();
        }

        scanLibrary();
    }

    void Application::locatePlayingInLibrary()
    {
        if (!_libraryView)
            return;
        auto const &path = _audio.currentTrack().path;
        if (path.empty())
            return;

        // Locate only makes sense in a library projection; switch out of
        // FileBrowser into Album mode if needed.
        if (!leftIsLibrary())
        {
            setLeftMode(LeftMode::Album);
        }
        _libraryView->locate(path);
    }

} // namespace vtplayer
