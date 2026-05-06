// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "Application.h"

#include "../playlist/PlaylistRepository.h"
#include "../util/UnicodeNormalize.h"
#include "../visualizer/Oscilloscope.h"

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
        _audio.setAutoGain(_config.autoGain);

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
        _fileBrowser->setDirectory(_config.startDirectory);
        _fileBrowser->setOnActivate([this](std::vector<std::filesystem::path> const &paths, bool quietAppend)
                                    { activateFromBrowser(paths, quietAppend); });
        _fileBrowser->setOnOpenPlaylist([this](std::filesystem::path const &path)
                                        { openPlaylist(path); });

        _playlistView = std::make_unique<PlaylistView>();
        _playlistView->setTheme(_theme);
        _playlistView->setOnPlay([this](int index)
                                 { playTrack(index); });
        _playlistView->setOnPlayingRemoved([this]
                                           {
                                               _audio.stop();
                                               _playlistView->setPlayingIndex(-1);
                                           });

        // Bootstrap current playlist: prefer Config.playlistCurrentPath, otherwise default.m3u.
        {
            PlaylistRepository::ensureDirectory();
            auto target = _config.playlistCurrentPath;
            bool usingDefault = false;
            if (target.empty() || !std::filesystem::exists(target))
            {
                target = PlaylistRepository::defaultPlaylistPath();
                usingDefault = true;
            }

            if (auto loaded = Playlist::load(target))
            {
                _currentPlaylist = std::move(*loaded);
            }
            else
            {
                _currentPlaylist = PlaylistRepository::ensureDefault();
                usingDefault = true;
            }

            _playlistView->setTracks(_currentPlaylist.tracks());
            _playlistView->setCurrentPlaylistName(_currentPlaylist.name());

            if (usingDefault || _config.playlistCurrentPath != _currentPlaylist.path())
            {
                _config.playlistCurrentPath = _currentPlaylist.path();
                _config.save();
            }
        }

        _transportBar = std::make_unique<TransportBar>();
        _transportBar->setTheme(_theme);

        _visualizerView = std::make_unique<VisualizerView>();
        _visualizerView->setTheme(_theme);
        _visualizerView->setVisualizer(std::make_unique<AudioSpectrum>(_config.barCount));

        _contextMenu = std::make_unique<ContextMenu>();
        _contextMenu->setTheme(_theme);
        _contextMenu->setTitle("Menu");
        _contextMenu->setItems({
            "New playlist",
            "Save playlist",
            "Set current directory as start directory",
            "Exit",
        });
        _contextMenu->setOnSelect([this](int idx) { onContextMenuSelect(idx); });

        resize();

        // If an initial file was provided, add it to the playlist and play
        if (!_initialFile.empty())
        {
            addToPlaylist(_initialFile);
            playTrack(0);
        }
    }

    void Application::cleanup()
    {
        // Sync runtime-mutable settings back before persisting.
        _config.autoGain = _audio.autoGainEnabled();
        _config.save();

        // Persist the current playlist's track list to disk.
        if (!_currentPlaylist.path().empty() && _playlistView)
        {
            _currentPlaylist.setTracks(_playlistView->tracks());
            _currentPlaylist.save();
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

        // Browser split: FileBrowser (left 40%) | PlaylistView (right 60%)
        int browserW = (w * 2) / 5;
        if (browserW < 20)
            browserW = 20;
        int playlistW = w - browserW;
        _fileBrowser->setRect(0, contentY, browserW, contentH);
        _playlistView->setRect(browserW, contentY, playlistW, contentH);

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
            int current = _playlistView->playingIndex();
            int count = _playlistView->trackCount();
            if (current >= 0)
            {
                if (current + 1 < count)
                {
                    playTrack(current + 1);
                }
                else if (_repeatEnabled && count > 0)
                {
                    playTrack(0);
                }
                else
                {
                    _playlistView->setPlayingIndex(-1);
                }
            }
        }

        // Update transport
        _transportBar->setState(state);
        _transportBar->setTrackName(_audio.currentTrack().title);
        _transportBar->setPosition(_audio.position());
        _transportBar->setDuration(_audio.duration());
        _transportBar->setAutoGain(_audio.autoGainEnabled(), _audio.autoGainDb());

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

        // Context menu overlay (drawn last so it sits on top)
        if (_contextMenu)
        {
            _contextMenu->draw(*_rootWindow);
        }
    }

    void Application::drawBrowserScreen()
    {
        _fileBrowser->draw(*_rootWindow);
        _playlistView->draw(*_rootWindow);

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

    void Application::drawVisualizerScreen()
    {
        _visualizerView->draw(*_rootWindow);
    }

    void Application::buildHelpRows()
    {
        // clang-format off
        _helpRows = {
            {"Keyboard shortcuts", "", true},
            {"", "", false},
            {"Playback", "", true},
            {"  Space",                 "Play / Pause", false},
            {"  N / P",                 "Next / Previous track", false},
            {"  < / >",                 "Seek -5s / +5s", false},
            {"  R",                     "Toggle repeat", false},
            {"  S",                     "Shuffle playlist", false},
            {"  G",                     "Toggle auto-gain", false},
            {"", "", false},
            {"Browser", "", true},
            {"  Tab",                   "Switch focus (browser <-> playlist)", false},
            {"  Enter",                 "Replace playlist with selection and play", false},
            {"  Shift+Enter",           "Append selection to playlist", false},
            {"  A",                     "Add selected file to playlist", false},
            {"  Backspace",             "Go up to parent directory", false},
            {"  F5",                    "Refresh listing", false},
            {"", "", false},
            {"Playlist", "", true},
            {"  Enter",                 "Play selected track", false},
            {"  Del / D / Backspace",   "Remove selection", false},
            {"  Ctrl+Up / Ctrl+Down",   "Move selected track", false},
            {"  Ctrl+A",                "Select all", false},
            {"  Ctrl+S",                "Save current playlist", false},
            {"", "", false},
            {"Visualizer", "", true},
            {"  V",                     "Toggle visualizer screen", false},
            {"  0 - 9",                 "Switch visualizer style", false},
            {"", "", false},
            {"Misc", "", true},
            {"  H / Up / Down / PgUp / PgDn", "Show / scroll this help", false},
            {"  ESC",                   "Open menu / dismiss overlay", false},
            {"  Q",                     "Quit", false},
        };
        // clang-format on
    }

    int Application::helpVisibleRows() const
    {
        if (!_terminal) return 0;
        int const h = _terminal->rows();
        int const top = 1;
        int const bottom = h - 2;
        return std::max(0, bottom - top + 1);
    }

    int Application::helpMaxScroll() const
    {
        int const total = static_cast<int>(_helpRows.size());
        int const visible = helpVisibleRows();
        return std::max(0, total - visible);
    }

    void Application::drawHelpScreen()
    {
        if (!_terminal) return;
        int const w = _terminal->cols();
        int const h = _terminal->rows();
        int const top = 1;          // below header
        int const bottom = h - 2;   // above transport row
        if (bottom < top) return;

        // Clamp scroll in case the terminal shrank since the last key event.
        int const maxScroll = helpMaxScroll();
        if (_helpScroll > maxScroll) _helpScroll = maxScroll;
        if (_helpScroll < 0) _helpScroll = 0;

        // Clear the content area to the help background.
        ventty::Style bgStyle{_theme.foreground, _theme.background};
        _rootWindow->fill(0, top, w, bottom - top + 1, U' ', bgStyle);

        // Side borders matching the browser/playlist box so the surrounding
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

        int const leftX = 4;
        int const colWidth = 32;
        int const visible = bottom - top + 1;
        int const total = static_cast<int>(_helpRows.size());
        int const drawRows = std::min(visible, total - _helpScroll);

        for (int i = 0; i < drawRows; ++i)
        {
            int const y = top + i;
            auto const & row = _helpRows[_helpScroll + i];
            if (row.isHeader)
            {
                _rootWindow->drawText(leftX - 2, y, row.left, headerStyle);
            }
            else if (!row.left.empty())
            {
                _rootWindow->drawText(leftX, y, row.left, keyStyle);
                if (!row.right.empty())
                {
                    _rootWindow->drawText(leftX + colWidth, y, row.right, descStyle);
                }
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
            if (_helpRows.empty()) buildHelpRows();
            _helpScroll = 0;
            _previousScreen = _screen;
            _screen = Screen::Help;
        }
        if (_terminal) _terminal->forceRedraw();
    }

    void Application::handleInput(ventty::KeyEvent const &event)
    {
        if (event.key == Key::None)
            return;

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
                _fileBrowser->handleKey(event);
            }
            else
            {
                _playlistView->handleKey(event);
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
            auto const &playlistRect = _playlistView->rect();

            // Click to switch focus
            if (event.button == Button::Left && event.action == Action::Press)
            {
                if (browserRect.contains(event.x, event.y))
                {
                    if (_focus != FocusPanel::FileBrowser)
                    {
                        _focus = FocusPanel::FileBrowser;
                        _fileBrowser->setFocused(true);
                        _playlistView->setFocused(false);
                    }
                }
                else if (playlistRect.contains(event.x, event.y))
                {
                    if (_focus != FocusPanel::Playlist)
                    {
                        _focus = FocusPanel::Playlist;
                        _fileBrowser->setFocused(false);
                        _playlistView->setFocused(true);
                    }
                }
            }

            // Delegate to focused panel
            if (_fileBrowser->rect().contains(event.x, event.y))
            {
                _fileBrowser->handleMouse(event);
            }
            else if (_playlistView->rect().contains(event.x, event.y))
            {
                _playlistView->handleMouse(event);
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

        // Tab: switch focus between panels (browser screen only)
        if (event.key == Key::Tab && _screen == Screen::Browser)
        {
            if (_focus == FocusPanel::FileBrowser)
            {
                // Only switch to playlist if it's not empty
                if (!_playlistView->empty())
                {
                    _focus = FocusPanel::Playlist;
                    _fileBrowser->setFocused(false);
                    _playlistView->setFocused(true);
                }
            }
            else
            {
                _focus = FocusPanel::FileBrowser;
                _fileBrowser->setFocused(true);
                _playlistView->setFocused(false);
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
            else if (!_playlistView->empty())
            {
                // Start playing selected or first track
                int idx = _playlistView->selectedIndex();
                playTrack(idx);
            }
            return;
        }

        // r: toggle repeat (loop back to first track when the last one ends)
        if (event.key == Key::Char && (ch == 'r' || ch == 'R') && !event.alt && !event.ctrl)
        {
            _repeatEnabled = !_repeatEnabled;
            return;
        }

        // s: shuffle current playlist (one-shot reorder)
        if (event.key == Key::Char && (ch == 's' || ch == 'S') && !event.alt && !event.ctrl)
        {
            _playlistView->shuffle();
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

        // g: toggle auto-gain (runtime loudness normalization)
        if (event.key == Key::Char && (ch == 'g' || ch == 'G') && !event.alt && !event.ctrl)
        {
            _audio.setAutoGain(!_audio.autoGainEnabled());
            return;
        }

        // a: add selected file to playlist
        if (event.key == Key::Char && (ch == 'a' || ch == 'A') && !event.alt && !event.ctrl && _screen == Screen::Browser)
        {
            auto const *entry = _fileBrowser->selectedEntry();
            if (entry && entry->isAudio)
            {
                addToPlaylist(entry->path);
            }
            return;
        }

        // Ctrl+Up/Down: move playlist item
        if (event.ctrl && event.key == Key::Up)
        {
            _playlistView->moveSelectedUp();
            return;
        }
        if (event.ctrl && event.key == Key::Down)
        {
            _playlistView->moveSelectedDown();
            return;
        }

        // F5: refresh
        if (event.key == Key::F5)
        {
            _fileBrowser->refresh();
            _terminal->forceRedraw();
            return;
        }

        // Ctrl+S: save current playlist
        if (event.ctrl && event.key == Key::Char && (ch == 's' || ch == 'S' || ch == 19))
        {
            saveCurrentPlaylist();
            return;
        }
    }

    void Application::openContextMenu()
    {
        if (!_contextMenu) return;
        _contextMenu->open();
        _terminal->forceRedraw();
    }

    void Application::setVisualizerByIndex(int index)
    {
        if (!_visualizerView) return;
        if (index < 0 || index > 9) return;

        std::unique_ptr<Visualizer> vis;
        switch (index)
        {
        case 0:
            vis = std::make_unique<Oscilloscope>();
            break;
        case 1:
            vis = std::make_unique<AudioSpectrum>(_config.barCount);
            break;
        default:
            // Slots 2-9 reserved; ignore until implemented.
            return;
        }

        _visualizerIndex = index;
        _visualizerView->setVisualizer(std::move(vis));
        if (_terminal) _terminal->forceRedraw();
    }

    void Application::onContextMenuSelect(int index)
    {
        // Menu order (Exit is always last):
        //   0 = New playlist
        //   1 = Save playlist
        //   2 = Set current directory as start directory
        //   3 = Exit
        switch (index)
        {
        case 0:
            newPlaylist();
            break;
        case 1:
            saveCurrentPlaylist();
            break;
        case 2:
            if (_fileBrowser)
            {
                _config.startDirectory = _fileBrowser->currentDirectory();
                _config.save();
            }
            break;
        case 3:
            quit();
            break;
        default:
            break;
        }
        if (_terminal) _terminal->forceRedraw();
    }

    void Application::playTrack(int index)
    {
        auto const *track = _playlistView->track(index);
        if (!track)
            return;

        _audio.stop();
        if (_audio.load(track->path))
        {
            _audio.play();
            _playlistView->setPlayingIndex(index);
        }
        else
        {
            _playlistView->setPlayingIndex(-1);
        }
    }

    void Application::playNext()
    {
        int current = _playlistView->playingIndex();
        int count = _playlistView->trackCount();
        if (count == 0)
            return;

        int next = (current + 1) % count;
        playTrack(next);
    }

    void Application::playPrev()
    {
        int current = _playlistView->playingIndex();
        int count = _playlistView->trackCount();
        if (count == 0)
            return;

        int prev = (current - 1 + count) % count;
        playTrack(prev);
    }

    void Application::addToPlaylist(std::filesystem::path const &path)
    {
        TrackInfo info;
        info.path = path;
        info.title = toNfc(path.stem().string());
        info.format = TrackInfo::formatFromPath(path);

        // Try to get duration by briefly loading
        // For now just add with unknown duration
        _playlistView->addTrack(info);
    }

    void Application::activateFromBrowser(std::vector<std::filesystem::path> const &paths, bool quietAppend)
    {
        if (paths.empty()) return;

        auto buildInfo = [](std::filesystem::path const &p)
        {
            TrackInfo info;
            info.path = p;
            info.title = toNfc(p.stem().string());
            info.format = TrackInfo::formatFromPath(p);
            return info;
        };

        if (quietAppend)
        {
            // Shift+Enter: append to the end of the playlist without disturbing
            // current playback.
            for (auto const &p : paths) _playlistView->addTrack(buildInfo(p));
            return;
        }

        // Enter: replace the current playlist with the selected files and play
        // the first newly-added track. setTracks fires onPlayingRemoved which
        // stops audio if a track was playing.
        std::vector<TrackInfo> newTracks;
        newTracks.reserve(paths.size());
        for (auto const &p : paths) newTracks.push_back(buildInfo(p));
        _playlistView->setTracks(std::move(newTracks));
        playTrack(0);
    }

    void Application::openPlaylist(std::filesystem::path const &path)
    {
        // Persist any pending edits in the outgoing playlist before switching.
        if (!_currentPlaylist.path().empty() && _playlistView)
        {
            _currentPlaylist.setTracks(_playlistView->tracks());
            _currentPlaylist.save();
        }

        _audio.stop();
        if (_playlistView) _playlistView->setPlayingIndex(-1);

        auto loaded = Playlist::load(path);
        if (!loaded)
        {
            return;
        }

        _currentPlaylist = std::move(*loaded);
        _playlistView->setTracks(_currentPlaylist.tracks());
        _playlistView->setCurrentPlaylistName(_currentPlaylist.name());

        _config.playlistCurrentPath = _currentPlaylist.path();
        _config.save();
    }

    void Application::newPlaylist()
    {
        // Save the outgoing playlist's edits first.
        if (!_currentPlaylist.path().empty() && _playlistView)
        {
            _currentPlaylist.setTracks(_playlistView->tracks());
            _currentPlaylist.save();
        }

        _audio.stop();
        if (_playlistView) _playlistView->setPlayingIndex(-1);

        auto path = PlaylistRepository::newUntitledPath();
        _currentPlaylist = Playlist(path);
        _currentPlaylist.save();  // materialize the empty file on disk

        if (_playlistView)
        {
            _playlistView->setTracks({});
            _playlistView->setCurrentPlaylistName(_currentPlaylist.name());
        }

        _config.playlistCurrentPath = _currentPlaylist.path();
        _config.save();
    }

    void Application::saveCurrentPlaylist()
    {
        if (_currentPlaylist.path().empty() || !_playlistView) return;

        _currentPlaylist.setTracks(_playlistView->tracks());
        _currentPlaylist.save();
    }

} // namespace vtplayer
