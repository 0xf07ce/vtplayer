// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "Keybindings.h"

#include "Actions.h"

#include <ventty/input/KeymapFile.h>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>

#ifndef VTPLAYER_VERSION
#define VTPLAYER_VERSION "0.0.0"
#endif

namespace vtplayer
{
    namespace
    {
        // FNV-1a 64-bit, hex — a stable content fingerprint for change/edit
        // detection (not cryptographic).
        std::string fnv1aHex(std::string_view s)
        {
            std::uint64_t h = 1469598103934665603ull;
            for (unsigned char const c : s)
            {
                h ^= c;
                h *= 1099511628211ull;
            }
            char buf[17];
            char const * const hex = "0123456789abcdef";
            for (int i = 15; i >= 0; --i)
            {
                buf[i] = hex[h & 0xF];
                h >>= 4;
            }
            buf[16] = '\0';
            return std::string(buf);
        }

        constexpr std::string_view kMarkerPrefix = "#@vtplayer-preset";

        // Extract the hash from a managed header line, or "" if it isn't one.
        std::string parseMarkerHash(std::string_view firstLine)
        {
            if (firstLine.substr(0, kMarkerPrefix.size()) != kMarkerPrefix)
                return {};
            std::size_t pos = firstLine.find("hash=");
            if (pos == std::string_view::npos)
                return {};
            pos += 5;
            std::size_t end = pos;
            while (end < firstLine.size()
                   && std::isxdigit(static_cast<unsigned char>(firstLine[end])))
                ++end;
            return std::string(firstLine.substr(pos, end - pos));
        }
    } // namespace

    std::filesystem::path Keybindings::presetDir()
    {
        char const * const home = std::getenv("HOME");
        if (home == nullptr || *home == '\0')
            return {};
        return std::filesystem::path(home) / ".config" / "vtplayer" / "keybindings";
    }

    std::string Keybindings::stampPreset(std::string const & body)
    {
        // First line is an auto-managed marker (a '#' comment, so the keymap
        // parser ignores it); the body follows verbatim and is what gets hashed.
        return "#@vtplayer-preset v=" VTPLAYER_VERSION " hash=" + fnv1aHex(body)
               + "  (auto-managed; delete this file to reset to the built-in)\n" + body;
    }

    bool Keybindings::presetNeedsUpdate(std::string const & existingContent,
                                        std::string const & builtinBody)
    {
        std::string_view const all(existingContent);
        std::size_t const nl = all.find('\n');
        std::string_view const first = (nl == std::string_view::npos) ? all : all.substr(0, nl);
        std::string_view const body =
            (nl == std::string_view::npos) ? std::string_view{} : all.substr(nl + 1);

        std::string const stored = parseMarkerHash(first);
        if (stored.empty())
            return false; // legacy / no managed header — leave it alone
        if (fnv1aHex(body) != stored)
            return false; // the user edited the body — preserve it
        // Pristine, auto-managed file: refresh only if the built-in changed.
        return stored != fnv1aHex(builtinBody);
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
            std::string const body = p.text;

            bool write = false;
            if (!std::filesystem::exists(path))
            {
                write = true; // first run — create it
            }
            else
            {
                std::ifstream in(path);
                if (!in.is_open())
                    continue;
                std::string const existing((std::istreambuf_iterator<char>(in)),
                                           std::istreambuf_iterator<char>());
                write = presetNeedsUpdate(existing, body);
            }

            if (write)
            {
                std::ofstream out(path, std::ios::trunc);
                if (out.is_open())
                    out << stampPreset(body);
            }
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
            "map normal L       queue-toggle\n"
            "map normal q       quit\n"
            "map normal <Tab>   focus-next\n"
            "map normal 1       left-album\n"
            "map normal 2       left-directory\n"
            "map normal 3       left-playlists\n"
            "map normal 4       left-streaming\n"
            "map normal 5       left-files\n"
            "\n"
            "# -- library / queue commands --\n"
            "map normal a       append\n"
            "map normal b       add-playlist\n"
            "map normal t       tag-edit\n"
            "map normal <F3>    tag-edit-immediate\n"
            "map normal <F5>    file-rename\n"
            "map normal /       search\n"
            "map normal n       search-next\n"
            "map normal N       search-prev\n"
            "map normal <S-Up>    select-up      # extend multi-selection up\n"
            "map normal <S-Down>  select-down    # extend multi-selection down\n"
            "map normal <S-Left>  move-up        # reorder selection up\n"
            "map normal <S-Right> move-down      # reorder selection down\n"
            "map normal <C-Up>    move-up        # (alternative to Shift+Left)\n"
            "map normal <C-Down>  move-down      # (alternative to Shift+Right)\n"
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
            "map normal <C-s>      playlist-save\n";
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
            "# move to other keys: H = help, L = toggle queue panel, V = visualizer,\n"
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
            "map normal <S-Up>   select-up     # extend multi-selection up\n"
            "map normal <S-Down> select-down   # extend multi-selection down\n"
            "map normal <S-Left>  move-up      # reorder selection up\n"
            "map normal <S-Right> move-down    # reorder selection down\n"
            "map normal <C-Up>   move-up       # (alternative to Shift+Left)\n"
            "map normal <C-Down> move-down     # (alternative to Shift+Right)\n"
            "map normal <C-a>    select-all\n"
            "\n"
            "# -- window / panel movement (chords) --\n"
            "map normal <C-w>w   focus-next\n"
            "map normal <C-w>l   focus-right\n"
            "map normal <C-w>h   focus-left\n"
            "map normal <C-w>1   left-album\n"
            "map normal <C-w>2   left-directory\n"
            "map normal <C-w>3   left-playlists\n"
            "map normal <C-w>4   left-streaming\n"
            "map normal <C-w>5   left-files\n"
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
            "map normal L        queue-toggle\n"
            "map normal <C-w>L   panel-toggle\n"
            "map normal <C-g>    gain-toggle\n"
            "map normal a        append\n"
            "map normal b        add-playlist\n"
            "map normal t        tag-edit\n"
            "map normal <F3>     tag-edit-immediate\n"
            "map normal <F5>     file-rename\n"
            "map normal /        search\n"
            "map normal n        search-next\n"
            "map normal N        search-prev\n"
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
