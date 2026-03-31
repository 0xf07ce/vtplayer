// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "../audio/AudioEngine.h"
#include "../config/Config.h"
#include "../view/FileBrowser.h"
#include "../view/HeaderBar.h"
#include "../view/PlaylistView.h"
#include "../view/Theme.h"
#include "../view/TransportBar.h"
#include "../view/VisualizerView.h"
#include "../visualizer/AudioSpectrum.h"

#include <ventty/terminal/TerminalBase.h>

#include <memory>

namespace vtamp
{

    enum class Screen
    {
        Browser,
        Visualizer,
    };

    enum class FocusPanel
    {
        FileBrowser,
        Playlist,
    };

    class Application
    {
    public:
        Application();
        ~Application();

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
        void updateUI();

        void handleInput(ventty::KeyEvent const &event);
        void handleMouse(ventty::MouseEvent const &event);
        void handleGlobalKeys(ventty::KeyEvent const &event);

        void playTrack(int index);
        void playNext();
        void playPrev();
        void addToPlaylist(std::filesystem::path const &path);

        bool _running = false;
        std::unique_ptr<ventty::TerminalBase> _terminal;
        ventty::Window *_rootWindow = nullptr;

        // Audio
        AudioEngine _audio;
        Config _config;

        // UI state
        Screen _screen = Screen::Browser;
        FocusPanel _focus = FocusPanel::FileBrowser;
        Theme _theme;

        // Views
        std::unique_ptr<HeaderBar> _headerBar;
        std::unique_ptr<FileBrowser> _fileBrowser;
        std::unique_ptr<PlaylistView> _playlistView;
        std::unique_ptr<TransportBar> _transportBar;
        std::unique_ptr<VisualizerView> _visualizerView;
    };

} // namespace vtamp
