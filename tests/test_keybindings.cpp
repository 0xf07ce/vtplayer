// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "test_framework.h"

#include "config/Config.h"
#include "input/Actions.h"
#include "input/Keybindings.h"

#include <ventty/input/InputEngine.h>
#include <ventty/input/KeyChord.h>
#include <ventty/input/KeymapFile.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using namespace vtplayer;

namespace
{
    ventty::KeyChord K(std::string_view s)
    {
        auto const k = ventty::KeyChord::parse(s);
        CHECK(k.has_value());
        return k.value_or(ventty::KeyChord{});
    }

    /// Configure an engine from preset text, asserting it parsed cleanly with
    /// no unknown-action warnings (every token the shipped presets use must be
    /// a real Action).
    ventty::InputEngine engineFrom(char const * text)
    {
        ventty::KeymapConfig cfg =
            ventty::parseKeymap(text, [](std::string const & t) { return isKnownAction(t); });
        CHECK(cfg.ok());
        CHECK(cfg.warnings.empty());
        ventty::InputEngine eng;
        eng.configure(cfg.modes, cfg.keymaps, cfg.counts);
        return eng;
    }
} // namespace

TEST_CASE("Action token table round-trips and rejects typos")
{
    CHECK_EQ(actionFromToken("cursor-down"), Action::CursorDown);
    CHECK_EQ(actionFromToken("focus-right"), Action::FocusRight);
    CHECK_EQ(actionFromToken("left-streaming"), Action::LeftModeStreaming);
    CHECK_EQ(actionFromToken("enter-visual"), Action::EnterVisual);
    CHECK_EQ(actionFromToken("remove"), Action::Remove);
    CHECK_EQ(actionFromToken("nope-not-real"), Action::None);

    CHECK(isKnownAction("play-pause"));
    CHECK(isKnownAction("left-playlists"));
    CHECK(isKnownAction("left-artist")); // legacy alias, dispatched as Album
    CHECK(!isKnownAction("play_pause")); // wrong separator
}

TEST_CASE("Built-in vi preset binds the expected actions")
{
    ventty::InputEngine eng = engineFrom(Keybindings::viKeysText());

    using RK = ventty::InputEngine::ResultKind;

    // 3j -> cursor-down with count 3.
    CHECK_EQ(eng.feed(K("3")).kind, RK::None);
    auto r = eng.feed(K("j"));
    CHECK_EQ(r.kind, RK::Emit);
    CHECK_EQ(actionFromToken(r.token), Action::CursorDown);
    CHECK_EQ(r.count, 3);

    // dd -> remove.
    CHECK_EQ(eng.feed(K("d")).kind, RK::None); // pending
    r = eng.feed(K("d"));
    CHECK_EQ(actionFromToken(r.token), Action::Remove);

    // Backspace is navigation only; deletion stays on Del / D.
    r = eng.feed(K("<BS>"));
    CHECK_EQ(actionFromToken(r.token), Action::GoBack);
    r = eng.feed(K("<Del>"));
    CHECK_EQ(actionFromToken(r.token), Action::Remove);

    // <C-w>l -> focus-right (the window-movement chord).
    CHECK_EQ(eng.feed(K("<C-w>")).kind, RK::None);
    r = eng.feed(K("l"));
    CHECK_EQ(actionFromToken(r.token), Action::FocusRight);

    // <C-w>2 -> left-directory (panel select, conflict-free with counts).
    CHECK_EQ(eng.feed(K("<C-w>")).kind, RK::None);
    r = eng.feed(K("2"));
    CHECK_EQ(actionFromToken(r.token), Action::LeftModeDirectory);

    CHECK_EQ(eng.feed(K("<C-w>")).kind, RK::None);
    r = eng.feed(K("4"));
    CHECK_EQ(actionFromToken(r.token), Action::LeftModeStreaming);

    // gg -> cursor-home.
    CHECK_EQ(eng.feed(K("g")).kind, RK::None);
    r = eng.feed(K("g"));
    CHECK_EQ(actionFromToken(r.token), Action::CursorHome);

    // v enters Visual mode; j there still resolves (extension handled by the app).
    r = eng.feed(K("v"));
    CHECK_EQ(actionFromToken(r.token), Action::EnterVisual);
    eng.setMode("visual");
    r = eng.feed(K("d"));
    CHECK_EQ(actionFromToken(r.token), Action::Remove);
    // Esc leaves Visual mode.
    CHECK(eng.feedEsc());
    CHECK_EQ(eng.mode(), std::string("normal"));
}

TEST_CASE("Built-in default preset binds standard single keys")
{
    ventty::InputEngine eng = engineFrom(Keybindings::defaultKeysText());
    using RK = ventty::InputEngine::ResultKind;

    // Single mode, no counts: every command resolves immediately (no chords).
    auto emit = [&](std::string_view key) {
        auto r = eng.feed(K(key));
        CHECK_EQ(r.kind, RK::Emit);
        return actionFromToken(r.token);
    };

    CHECK_EQ(emit("<Space>"), Action::PlayPause);
    CHECK_EQ(emit("q"), Action::Quit);
    CHECK_EQ(emit("x"), Action::Stop);
    CHECK_EQ(emit("h"), Action::ToggleHelp);     // h = help (NOT a motion here)
    CHECK_EQ(emit("l"), Action::ToggleLeftPanel);// l = panel (NOT a motion here)
    CHECK_EQ(emit("1"), Action::LeftModeAlbum);  // digits are commands, not counts
    CHECK_EQ(emit("2"), Action::LeftModeDirectory);
    CHECK_EQ(emit("3"), Action::LeftModePlaylists);
    CHECK_EQ(emit("4"), Action::LeftModeStreaming);
    CHECK_EQ(emit("5"), Action::LeftModeFiles);
    CHECK_EQ(emit("<Up>"), Action::CursorUp);
    CHECK_EQ(emit("<CR>"), Action::Activate);
    CHECK_EQ(emit("/"), Action::Search);
    CHECK_EQ(emit("<BS>"), Action::GoBack);
    CHECK_EQ(emit("<Del>"), Action::Remove);
    CHECK_EQ(emit("<C-a>"), Action::SelectAll);
    CHECK_EQ(emit("<S-Up>"), Action::ExtendSelectionUp);   // multi-select extend
    CHECK_EQ(emit("<S-Down>"), Action::ExtendSelectionDown);
    CHECK_EQ(emit("<S-Left>"), Action::MoveUp);            // reorder selection
    CHECK_EQ(emit("<S-Right>"), Action::MoveDown);
    CHECK_EQ(emit("<F5>"), Action::Refresh);
    CHECK_EQ(emit("<lt>"), Action::SeekBack);    // the '<' key in vim notation

    // A digit never starts a count here (counts = off): every press emits.
    CHECK_EQ(emit("3"), Action::LeftModePlaylists);

    // An unbound key still falls through to the built-in handlers.
    CHECK_EQ(eng.feed(K("z")).kind, RK::Passthrough);
}

TEST_CASE("Preset header drives update-vs-preserve")
{
    std::string const body = Keybindings::defaultKeysText();
    std::string const stamped = Keybindings::stampPreset(body);

    // A pristine, auto-managed file against the same built-in: no update.
    CHECK(!Keybindings::presetNeedsUpdate(stamped, body));

    // Pristine file, but the built-in changed: should update.
    std::string const body2 = body + "map normal z quit\n";
    CHECK(Keybindings::presetNeedsUpdate(stamped, body2));

    // User-edited file (body diverged from the recorded hash): preserve it.
    std::string const edited = stamped + "map normal z quit\n";
    CHECK(!Keybindings::presetNeedsUpdate(edited, body2));

    // Legacy file with no managed header: leave it alone.
    CHECK(!Keybindings::presetNeedsUpdate(body, body2));
}

TEST_CASE("Config reads and defaults the keybindings preset")
{
    auto dir = std::filesystem::temp_directory_path() / "vtplayer_tests";
    std::filesystem::create_directories(dir);

    auto const viPath = dir / "kb_vi.ini";
    std::ofstream(viPath, std::ios::trunc) << "[keybindings]\npreset = vi\n";
    Config a;
    a.loadFrom(viPath);
    CHECK_EQ(a.keymapPreset, std::string("vi"));

    auto const emptyPath = dir / "kb_empty.ini";
    std::ofstream(emptyPath, std::ios::trunc) << "[audio]\ngain_norm = true\n";
    Config b;
    b.loadFrom(emptyPath);
    CHECK_EQ(b.keymapPreset, std::string("default")); // struct default preserved
}
