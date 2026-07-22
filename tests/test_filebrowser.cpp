// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "test_framework.h"

#include "view/FileBrowser.h"

#include <ventty/terminal/TerminalBase.h>

#include <chrono>
#include <filesystem>
#include <string>

namespace
{

using Key = ventty::KeyEvent::Key;

std::filesystem::path makeTempRoot()
{
    auto const stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto root = std::filesystem::temp_directory_path()
              / ("vtplayer-filebrowser-" + std::to_string(stamp));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

ventty::KeyEvent keyEvent(Key key)
{
    ventty::KeyEvent ev;
    ev.key = key;
    return ev;
}

struct TempTree
{
    std::filesystem::path root = makeTempRoot();

    ~TempTree()
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void buildSiblingTree(std::filesystem::path const & root)
{
    std::filesystem::create_directories(root / "Applications");
    std::filesystem::create_directories(root / "Music" / "Child");
    std::filesystem::create_directories(root / "Zzz");
}

} // namespace

TEST_CASE("FileBrowser Backspace focuses the directory it came from")
{
    TempTree tree;
    buildSiblingTree(tree.root);

    vtplayer::FileBrowser browser;
    browser.setDirectory(tree.root / "Music" / "Child");

    CHECK(browser.handleKey(keyEvent(Key::Backspace)));

    auto const * selected = browser.selectedEntry();
    CHECK_EQ(browser.currentDirectory(), tree.root / "Music");
    CHECK(selected != nullptr);
    if (selected)
    {
        CHECK_EQ(selected->path, tree.root / "Music" / "Child");
    }
}

TEST_CASE("FileBrowser parent entry focuses the directory it came from")
{
    TempTree tree;
    buildSiblingTree(tree.root);

    vtplayer::FileBrowser browser;
    browser.setDirectory(tree.root / "Music" / "Child");

    auto const * before = browser.selectedEntry();
    CHECK(before != nullptr);
    CHECK(before && before->isParent);
    CHECK(browser.handleKey(keyEvent(Key::Enter)));

    auto const * selected = browser.selectedEntry();
    CHECK_EQ(browser.currentDirectory(), tree.root / "Music");
    CHECK(selected != nullptr);
    if (selected)
    {
        CHECK_EQ(selected->path, tree.root / "Music" / "Child");
    }
}
