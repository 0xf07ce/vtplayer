// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "Theme.h"

#include "vtplayer/plugin.h"

#include <ventty/core/Window.h>
#include <ventty/terminal/TerminalBase.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vtplayer
{

/// Modal for provider-backed track search/download into the current
/// FileBrowser directory.
class SummonTrackDialog
{
public:
    struct Provider
    {
        VtpSummonPlugin const * plugin = nullptr;
        void *                  handle = nullptr;
        std::string             label;
    };

    struct ResultRow
    {
        std::string title;
        std::string channel;
        std::string duration;
        std::string url;
        std::string opaque;
        std::uint32_t flags = 0;
    };

    using DownloadDirectoryProvider = std::function<std::filesystem::path()>;
    using OnDownloadFinished = std::function<void(std::filesystem::path const &)>;

    ~SummonTrackDialog();

    void setTheme(Theme const & theme) { _theme = theme; }
    void setDownloadDirectoryProvider(DownloadDirectoryProvider cb)
    {
        _downloadDirectoryProvider = std::move(cb);
    }
    void setOnDownloadFinished(OnDownloadFinished cb) { _onDownloadFinished = std::move(cb); }
    void setProviders(std::vector<Provider> providers);

    void open();
    void close();
    bool isOpen() const { return _open; }

    bool handleKey(ventty::KeyEvent const & event);
    void draw(ventty::Window & window);

    bool wantsCursor() const { return _open && _cursorScreenX >= 0; }
    int cursorScreenX() const { return _cursorScreenX; }
    int cursorScreenY() const { return _cursorScreenY; }

public:
    enum class SearchStatus
    {
        Idle,
        Searching,
        SearchingNext,
        Results,
        NoResults,
        Failed,
        Downloading,
        Downloaded,
        DownloadSkipped,
        DownloadFailed,
    };

private:
    enum class WorkerKind
    {
        Search,
        Download,
    };

    struct SearchOutcome
    {
        bool ok = false;
        std::vector<ResultRow> rows;
        std::filesystem::path outputPath;
        bool skipped = false;
    };

    struct ProviderState
    {
        std::string query;
        int cursorBytePos = 0;
        std::vector<ResultRow> results;
        SearchStatus status = SearchStatus::Idle;
        int selectedIndex = 0;
        int scrollOffset = 0;
        bool hasMore = false;
        bool nextSearchFailed = false;
        std::uint64_t generation = 0;
    };

    bool hasProviders() const { return !_providers.empty(); }
    ProviderState & activeState();
    ProviderState const & activeState() const;
    Provider const & activeProvider() const;
    void switchProvider(int delta);
    void startSearch(bool nextPage = false);
    void startDownload();
    void pollSearch();
    void requestCancelRunningWorker();
    SearchOutcome runProviderSearch(std::size_t providerIndex,
                                    std::string query,
                                    std::filesystem::path currentDir,
                                    std::size_t resultOffset,
                                    std::size_t maxResults,
                                    std::uint64_t generation);
    SearchOutcome runProviderDownload(std::size_t providerIndex,
                                      ResultRow row,
                                      std::filesystem::path targetDir,
                                      std::uint64_t generation);
    void clearResultsForEdit();
    void insertUtf8(char32_t ch);
    void backspaceUtf8();
    void deleteForward();
    void moveCursorLeft();
    void moveCursorRight();
    void moveCursorHome();
    void moveCursorEnd();

    Theme _theme;
    DownloadDirectoryProvider _downloadDirectoryProvider;
    OnDownloadFinished _onDownloadFinished;
    std::vector<Provider> _providers;
    std::vector<ProviderState> _states;
    std::size_t _activeProviderIndex = 0;
    bool _open = false;

    std::thread _searchThread;
    std::mutex _searchMutex;
    bool _workerRunning = false;
    bool _workerDone = false;
    bool _workerOk = false;
    bool _cancelWorker = false;
    std::size_t _workerProviderIndex = 0;
    std::uint64_t _workerGeneration = 0;
    WorkerKind _workerKind = WorkerKind::Search;
    std::vector<ResultRow> _workerRows;
    std::filesystem::path _workerOutputPath;
    bool _workerSkipped = false;
    std::filesystem::path _lastDownloadPath;

    int _cursorScreenX = -1;
    int _cursorScreenY = -1;
};

} // namespace vtplayer
