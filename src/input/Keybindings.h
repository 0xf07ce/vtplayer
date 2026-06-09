// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Actions.h"

#include <ventty/input/InputEngine.h>

#include <filesystem>
#include <string>
#include <vector>

namespace vtplayer
{
    /// One parsed binding from a preset, for introspection (e.g. the help
    /// screen). `keys` is the vim-notation key/sequence as written in the file.
    struct KeyBinding
    {
        std::string mode;
        std::string keys;
        Action action = Action::None;
    };

    /// Loads keybinding presets from ~/.config/vtplayer/keybindings/.
    ///
    /// Two presets ship built-in: "default" (no overrides — the standard keys
    /// stay built-in) and "vi" (modal, vi-style navigation). The active one is
    /// chosen in config.ini ([keybindings] preset). Preset files are
    /// materialized on first run and never rewritten, so hand edits and
    /// comments survive.
    class Keybindings
    {
    public:
        /// ~/.config/vtplayer/keybindings/ (empty if $HOME is unset).
        static std::filesystem::path presetDir();

        /// Write the built-in default.keys / vi.keys into presetDir() only when
        /// they do not already exist. Never overwrites a user-edited file.
        static void materializePresets();

        /// Configure `engine` from preset `name`. Reads
        /// <presetDir>/<name>.keys, falling back to the built-in text when the
        /// file is missing or unreadable. Parse warnings (unknown actions, bad
        /// keys) are appended to `warnings`. On an unusable preset the engine is
        /// configured to pass every key through, and false is returned.
        static bool load(std::string const & name, ventty::InputEngine & engine,
                         std::vector<std::string> & warnings);

        /// Parse the active preset's `map` directives into a flat binding list
        /// (for the help screen). Mirrors load()'s text resolution: reads
        /// <presetDir>/<name>.keys, falling back to the built-in text. Bindings
        /// whose action token is unknown are skipped.
        static std::vector<KeyBinding> activeBindings(std::string const & name);

        static char const * defaultKeysText();
        static char const * viKeysText();
    };
} // namespace vtplayer
