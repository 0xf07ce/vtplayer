// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "Keybindings.h"

#include "Actions.h"

#include <ventty/input/KeymapFile.h>

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string_view>

namespace vtplayer
{
    std::filesystem::path Keybindings::presetDir()
    {
        char const * const home = std::getenv("HOME");
        if (home == nullptr || *home == '\0')
            return {};
        return std::filesystem::path(home) / ".config" / "vtplayer" / "keybindings";
    }

    void Keybindings::materializePresets()
    {
        std::filesystem::path const dir = presetDir();
        if (dir.empty())
            return;

        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec)
            return;

        struct Preset
        {
            char const * file;
            char const * text;
        };
        Preset const presets[] = {
            { "default.keys", defaultKeysText() },
            { "vi.keys", viKeysText() },
        };

        for (auto const & p : presets)
        {
            std::filesystem::path const path = dir / p.file;
            if (std::filesystem::exists(path))
                continue; // never clobber a user-edited preset
            std::ofstream out(path, std::ios::trunc);
            if (out.is_open())
                out << p.text;
        }
    }

    bool Keybindings::load(std::string const & name, ventty::InputEngine & engine,
                           std::vector<std::string> & warnings)
    {
        // Read the preset file, falling back to the built-in text.
        std::string text;
        std::filesystem::path const path = presetDir() / (name + ".keys");
        std::ifstream in(path);
        if (in.is_open())
        {
            std::ostringstream ss;
            ss << in.rdbuf();
            text = ss.str();
        }
        else
        {
            text = (name == "vi") ? viKeysText() : defaultKeysText();
        }

        ventty::KeymapConfig cfg =
            ventty::parseKeymap(text, [](std::string const & token) { return isKnownAction(token); });
        for (auto const & w : cfg.warnings)
            warnings.push_back(w);

        if (!cfg.ok())
        {
            // Unusable preset: a single empty mode means every key is a
            // passthrough, i.e. the built-in handlers take over entirely.
            std::unordered_map<std::string, ventty::Keymap> empty;
            empty["normal"];
            engine.configure({ "normal" }, std::move(empty), /*counts=*/false);
            return false;
        }

        engine.configure(std::move(cfg.modes), std::move(cfg.keymaps), cfg.counts);
        return true;
    }

    std::vector<KeyBinding> Keybindings::activeBindings(std::string const & name)
    {
        // Resolve the preset text the same way load() does.
        std::string text;
        std::filesystem::path const path = presetDir() / (name + ".keys");
        std::ifstream in(path);
        if (in.is_open())
        {
            std::ostringstream ss;
            ss << in.rdbuf();
            text = ss.str();
        }
        else
        {
            text = (name == "vi") ? viKeysText() : defaultKeysText();
        }

        std::vector<KeyBinding> out;
        std::string_view const all(text);
        std::size_t pos = 0;
        while (pos <= all.size())
        {
            std::size_t const nl = all.find('\n', pos);
            std::string_view line =
                (nl == std::string_view::npos) ? all.substr(pos) : all.substr(pos, nl - pos);
            pos = (nl == std::string_view::npos) ? all.size() + 1 : nl + 1;

            // Strip a trailing comment ('#' at line start or after whitespace).
            for (std::size_t i = 0; i < line.size(); ++i)
            {
                if (line[i] == '#' && (i == 0 || std::isspace(static_cast<unsigned char>(line[i - 1]))))
                {
                    line = line.substr(0, i);
                    break;
                }
            }

            // Split into whitespace-separated fields.
            std::vector<std::string> fields;
            std::size_t i = 0;
            while (i < line.size() && fields.size() < 8)
            {
                while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
                    ++i;
                std::size_t const start = i;
                while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i])))
                    ++i;
                if (i > start)
                    fields.emplace_back(line.substr(start, i - start));
            }

            if (fields.size() >= 4 && fields[0] == "map")
            {
                Action const action = actionFromToken(fields[3]);
                if (action != Action::None)
                    out.push_back({ fields[1], fields[2], action });
            }
        }
        return out;
    }

    char const * Keybindings::defaultKeysText()
    {
        return
            "# default.keys — the standard, non-modal key map.\n"
            "#\n"
            "# Every command is a single key: no modes, no chord sequences, no repeat\n"
            "# counts. This is the opposite of the \"vi\" preset. The active preset is\n"
            "# chosen once at startup ([keybindings] preset in config.ini) and never\n"
            "# switches at runtime, so a default session stays default and a vi\n"
            "# session stays vi.\n"
            "#\n"
            "# Edit freely — vtplayer never rewrites this file. Delete a line to fall\n"
            "# back to that key's built-in behavior. Keys not listed here (mouse,\n"
            "# Shift+arrow multi-select, and the Visualizer screen's 0-9 / scroll\n"
            "# keys) are handled internally. See docs/keybindings.md.\n"
            "\n"
            "modes  = normal\n"
            "counts = off\n"
            "\n"
            "# -- playback --\n"
            "map normal <Space> play-pause\n"
            "map normal x       stop\n"
            "map normal ]       next-track\n"
            "map normal [       prev-track\n"
            "map normal <lt>    seek-back        # the '<' key\n"
            "map normal <gt>    seek-fwd         # the '>' key\n"
            "map normal r       repeat-cycle\n"
            "map normal s       shuffle-toggle\n"
            "map normal g       gain-toggle\n"
            "\n"
            "# -- screens & panels --\n"
            "map normal v       visualizer-toggle\n"
            "map normal h       help-toggle\n"
            "map normal l       panel-toggle\n"
            "map normal q       quit\n"
            "map normal <Tab>   focus-next\n"
            "map normal 1       left-album\n"
            "map normal 2       left-artist\n"
            "map normal 3       left-directory\n"
            "map normal 4       left-files\n"
            "map normal 5       left-playlists\n"
            "\n"
            "# -- library / queue commands --\n"
            "map normal a       append\n"
            "map normal b       add-playlist\n"
            "map normal t       tag-edit\n"
            "map normal /       search\n"
            "map normal n       search-next\n"
            "map normal N       search-prev\n"
            "map normal <C-Up>   move-up\n"
            "map normal <C-Down> move-down\n"
            "\n"
            "# -- list navigation (whichever panel has focus) --\n"
            "map normal <Up>       cursor-up\n"
            "map normal <Down>     cursor-down\n"
            "map normal <Left>     collapse\n"
            "map normal <Right>    expand\n"
            "map normal <PageUp>   page-up\n"
            "map normal <PageDown> page-down\n"
            "map normal <Home>     cursor-home\n"
            "map normal <End>      cursor-end\n"
            "map normal <CR>       activate\n"
            "map normal <Del>      remove\n"
            "map normal <BS>       go-back\n"
            "map normal <C-a>      select-all\n"
            "map normal <C-e>      playlist-edit\n"
            "map normal <C-s>      playlist-save\n"
            "map normal <F5>       refresh\n";
    }

    char const * Keybindings::viKeysText()
    {
        return
            "# vi.keys — modal, vi-style key map.\n"
            "#\n"
            "# This is the opposite of the \"default\" preset: motions, chord\n"
            "# sequences (dd, gg, <C-w>l), numeric repeat counts (3j, 5dd) and a\n"
            "# Visual mode. The active preset is chosen once at startup\n"
            "# ([keybindings] preset in config.ini) and never switches at runtime.\n"
            "#\n"
            "# h / l / v / g are motions/operators here, so their command meanings\n"
            "# move to other keys: H = help, L = toggle panel, V = visualizer,\n"
            "# Ctrl+G = gain. Esc cancels a pending count/chord and leaves Visual\n"
            "# mode (built in). Edit freely — vtplayer never rewrites this file.\n"
            "\n"
            "modes  = normal, visual\n"
            "counts = on\n"
            "\n"
            "# -- motions (normal) --\n"
            "map normal j        cursor-down\n"
            "map normal k        cursor-up\n"
            "map normal h        collapse      # library tree: collapse / parent\n"
            "map normal l        expand        # library tree: expand\n"
            "map normal gg       cursor-home\n"
            "map normal G        cursor-end\n"
            "map normal <C-f>    page-down\n"
            "map normal <C-b>    page-up\n"
            "map normal <Up>     cursor-up\n"
            "map normal <Down>   cursor-down\n"
            "map normal <Left>   collapse\n"
            "map normal <Right>  expand\n"
            "map normal <CR>     activate\n"
            "\n"
            "# -- play queue --\n"
            "map normal dd       remove        # delete focused / selected item(s)\n"
            "map normal <Del>    remove\n"
            "map normal <C-Up>   move-up       # reorder selection up\n"
            "map normal <C-Down> move-down     # reorder selection down\n"
            "map normal <C-a>    select-all\n"
            "\n"
            "# -- window / panel movement (chords) --\n"
            "map normal <C-w>w   focus-next\n"
            "map normal <C-w>l   focus-right\n"
            "map normal <C-w>h   focus-left\n"
            "map normal <C-w>1   left-album\n"
            "map normal <C-w>2   left-artist\n"
            "map normal <C-w>3   left-directory\n"
            "map normal <C-w>4   left-files\n"
            "map normal <C-w>5   left-playlists\n"
            "map normal <Tab>    focus-next\n"
            "\n"
            "# -- enter Visual mode --\n"
            "map normal v        enter-visual\n"
            "\n"
            "# -- playback & commands (single keys, like default) --\n"
            "map normal <Space>  play-pause\n"
            "map normal x        stop\n"
            "map normal ]        next-track\n"
            "map normal [        prev-track\n"
            "map normal <lt>     seek-back\n"
            "map normal <gt>     seek-fwd\n"
            "map normal r        repeat-cycle\n"
            "map normal s        shuffle-toggle\n"
            "map normal q        quit\n"
            "map normal V        visualizer-toggle\n"
            "map normal H        help-toggle\n"
            "map normal L        panel-toggle\n"
            "map normal <C-g>    gain-toggle\n"
            "map normal a        append\n"
            "map normal b        add-playlist\n"
            "map normal t        tag-edit\n"
            "map normal /        search\n"
            "map normal n        search-next\n"
            "map normal N        search-prev\n"
            "map normal <F5>     refresh\n"
            "map normal <BS>     go-back\n"
            "\n"
            "# -- Visual mode: motions extend the selection, d deletes it --\n"
            "map visual j        cursor-down\n"
            "map visual k        cursor-up\n"
            "map visual <Down>   cursor-down\n"
            "map visual <Up>     cursor-up\n"
            "map visual <C-f>    page-down\n"
            "map visual <C-b>    page-up\n"
            "map visual d        remove\n"
            "map visual <Del>    remove\n"
            "map visual v        exit-visual\n";
    }
} // namespace vtplayer
