// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "Application.h"

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
        _fileBrowser->setOnAdd([this](std::filesystem::path const &path)
                               { addToPlaylist(path); });

        _playlistView = std::make_unique<PlaylistView>();
        _playlistView->setTheme(_theme);
        _playlistView->setOnPlay([this](int index)
                                 { playTrack(index); });

        _transportBar = std::make_unique<TransportBar>();
        _transportBar->setTheme(_theme);

        _visualizerView = std::make_unique<VisualizerView>();
        _visualizerView->setTheme(_theme);
        _visualizerView->setVisualizer(std::make_unique<AudioSpectrum>(_config.barCount));

        _contextMenu = std::make_unique<ContextMenu>();
        _contextMenu->setTheme(_theme);
        _contextMenu->setTitle("Menu");
        _contextMenu->setItems({
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

        // TransportBar: bottom 2 rows
        _transportBar->setRect(0, h - 2, w, 2);

        // Content area: between header and transport
        int contentY = 1;
        int contentH = h - 3;

        if (_screen == Screen::Browser)
        {
            // Split: FileBrowser (left 40%) | PlaylistView (right 60%)
            int browserW = (w * 2) / 5;
            if (browserW < 20)
                browserW = 20;
            int playlistW = w - browserW;

            _fileBrowser->setRect(0, contentY, browserW, contentH);
            _playlistView->setRect(browserW, contentY, playlistW, contentH);
        }
        else
        {
            // Visualizer takes full content area
            _visualizerView->setRect(0, contentY, w, contentH);
        }
    }

    void Application::updateUI()
    {
        // Update header
        auto state = _audio.state();
        bool playing = (state == PlayState::Playing);
        _headerBar->setPlaying(playing || state == PlayState::Paused);

        if (playing || state == PlayState::Paused)
        {
            _headerBar->setTrackName(_audio.currentTrack().title);

            float pos = _audio.position();
            float dur = _audio.duration();
            int pm = static_cast<int>(pos) / 60;
            int ps = static_cast<int>(pos) % 60;
            int dm = static_cast<int>(dur) / 60;
            int ds = static_cast<int>(dur) % 60;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%02d:%02d/%02d:%02d", pm, ps, dm, ds);
            _headerBar->setTrackTime(buf);
        }
        else
        {
            _headerBar->setTrackName("");
            _headerBar->setTrackTime("");
        }

        // Auto-advance: audio callback signals track ended via flag,
        // then UI thread safely stops and loads next track.
        if (_audio.hasTrackEnded())
        {
            _audio.stop();
            if (_playlistView->playingIndex() >= 0)
            {
                playNext();
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
            _visualizerView->setTrackName(_audio.currentTrack().title);

            // Build track info string
            std::string info;
            auto const &track = _audio.currentTrack();
            switch (track.format)
            {
            case AudioFormat::Mp3:
                info = "MP3";
                break;
            case AudioFormat::Ogg:
                info = "OGG";
                break;
            case AudioFormat::Flac:
                info = "FLAC";
                break;
            default:
                break;
            }
            info += " | " + std::to_string(AudioEngine::SAMPLE_RATE) + "Hz | Stereo";
            _visualizerView->setTrackInfo(info);
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
        else
        {
            drawVisualizerScreen();
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
        // ESC opens the menu.
        if (event.key == Key::Escape)
        {
            if (_screen == Screen::Visualizer)
            {
                _screen = Screen::Browser;
                resize();
                _terminal->forceRedraw();
            }
            else
            {
                openContextMenu();
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

        // s: stop
        if (event.key == Key::Char && (ch == 's' || ch == 'S') && !event.alt && !event.ctrl)
        {
            _audio.stop();
            _playlistView->setPlayingIndex(-1);
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
    }

    void Application::openContextMenu()
    {
        if (!_contextMenu) return;
        _contextMenu->open();
        _terminal->forceRedraw();
    }

    void Application::onContextMenuSelect(int index)
    {
        // Menu order (Exit is always last):
        //   0 = Set current directory as start directory
        //   1 = Exit
        switch (index)
        {
        case 0:
            if (_fileBrowser)
            {
                _config.startDirectory = _fileBrowser->currentDirectory();
                _config.save();
            }
            break;
        case 1:
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
            _headerBar->setTrackName("ERR: " + _audio.lastError());
            _headerBar->setPlaying(true);
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
        info.title = path.stem().string();
        info.format = TrackInfo::formatFromPath(path);

        // Try to get duration by briefly loading
        // For now just add with unknown duration
        _playlistView->addTrack(info);
    }

} // namespace vtplayer
