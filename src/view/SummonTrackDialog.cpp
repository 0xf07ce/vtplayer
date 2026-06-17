// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "SummonTrackDialog.h"

#include <ventty/art/AsciiArt.h>
#include <ventty/core/Utf8.h>

#include <algorithm>
#include <array>
#include <string_view>

namespace vtplayer
{

namespace
{

using Key = ventty::KeyEvent::Key;

constexpr std::size_t kMaxResults = 32;

std::string copyCString(char const * s)
{
    return s ? std::string(s) : std::string();
}

std::string leftTruncateToWidth(std::string_view s, int maxWidth)
{
    if (maxWidth <= 0) return {};
    int total = ventty::stringWidth(s);
    if (total <= maxWidth) return std::string(s);
    std::size_t pos = 0;
    while (pos < s.size() && total > maxWidth)
    {
        char32_t const cp = ventty::decode(s, pos);
        total -= ventty::displayWidth(cp);
    }
    return std::string(s.substr(pos));
}

std::string rightTruncateToWidth(std::string_view s, int maxWidth, std::string_view suffix)
{
    if (maxWidth <= 0) return {};
    if (ventty::stringWidth(s) <= maxWidth) return std::string(s);

    int const suffixW = ventty::stringWidth(suffix);
    if (suffixW >= maxWidth)
    {
        std::string out;
        int used = 0;
        std::size_t pos = 0;
        while (pos < suffix.size())
        {
            std::size_t probe = pos;
            char32_t const cp = ventty::decode(suffix, probe);
            int const w = ventty::displayWidth(cp);
            if (used + w > maxWidth) break;
            out.append(suffix.substr(pos, probe - pos));
            used += w;
            pos = probe;
        }
        return out;
    }

    std::string out;
    int used = 0;
    std::size_t pos = 0;
    int const bodyW = maxWidth - suffixW;
    while (pos < s.size())
    {
        std::size_t probe = pos;
        char32_t const cp = ventty::decode(s, probe);
        int const w = ventty::displayWidth(cp);
        if (used + w > bodyW) break;
        out.append(s.substr(pos, probe - pos));
        used += w;
        pos = probe;
    }
    out.append(suffix);
    return out;
}

std::size_t prevCodepointStart(std::string const & s, std::size_t pos)
{
    if (pos == 0) return 0;
    --pos;
    while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80)
        --pos;
    return pos;
}

std::size_t nextCodepointStart(std::string const & s, std::size_t pos)
{
    if (pos >= s.size()) return s.size();
    std::size_t p = pos;
    ventty::decode(s, p);
    return p;
}

std::string formatResultRow(SummonTrackDialog::ResultRow const & row)
{
    std::string out = row.title;
    if (!row.channel.empty() && row.channel != "NA")
    {
        out += " - ";
        out += row.channel;
    }
    if (!row.duration.empty() && row.duration != "NA")
    {
        out += "  ";
        out += row.duration;
    }
    return out;
}

std::string statusText(SummonTrackDialog::SearchStatus status, std::size_t resultCount)
{
    switch (status)
    {
        case SummonTrackDialog::SearchStatus::Searching:
            return "searching";
        case SummonTrackDialog::SearchStatus::Results:
            return std::to_string(resultCount) + " result" + (resultCount == 1 ? "" : "s");
        case SummonTrackDialog::SearchStatus::NoResults:
            return "no results";
        case SummonTrackDialog::SearchStatus::Failed:
            return "search failed";
        case SummonTrackDialog::SearchStatus::Downloading:
            return "downloading";
        case SummonTrackDialog::SearchStatus::Downloaded:
            return "downloaded";
        case SummonTrackDialog::SearchStatus::DownloadSkipped:
            return "skipped";
        case SummonTrackDialog::SearchStatus::DownloadFailed:
            return "download failed";
        case SummonTrackDialog::SearchStatus::Idle:
        default:
            return "idle";
    }
}

std::filesystem::path fallbackCurrentDirectory()
{
    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path(".") : cwd;
}

} // namespace

SummonTrackDialog::~SummonTrackDialog()
{
    requestCancelRunningWorker();
    if (_searchThread.joinable()) _searchThread.join();
}

void SummonTrackDialog::setProviders(std::vector<Provider> providers)
{
    requestCancelRunningWorker();
    if (_searchThread.joinable()) _searchThread.join();

    _providers = std::move(providers);
    _states.assign(_providers.size(), ProviderState{});
    _activeProviderIndex = 0;
}

SummonTrackDialog::ProviderState & SummonTrackDialog::activeState()
{
    return _states[_activeProviderIndex];
}

SummonTrackDialog::ProviderState const & SummonTrackDialog::activeState() const
{
    return _states[_activeProviderIndex];
}

SummonTrackDialog::Provider const & SummonTrackDialog::activeProvider() const
{
    return _providers[_activeProviderIndex];
}

void SummonTrackDialog::open()
{
    pollSearch();
    _open = true;
    _cursorScreenX = -1;
    _cursorScreenY = -1;
    _lastDownloadPath.clear();
    _activeProviderIndex = std::min(_activeProviderIndex,
                                    _providers.empty() ? std::size_t{0}
                                                       : _providers.size() - 1);

    for (auto & state : _states)
    {
        state.query.clear();
        state.cursorBytePos = 0;
        state.results.clear();
        state.status = SearchStatus::Idle;
        state.selectedIndex = 0;
        state.scrollOffset = 0;
        ++state.generation;
    }

    std::lock_guard<std::mutex> lock(_searchMutex);
    if (_workerRunning && !_workerDone && _workerProviderIndex < _states.size())
    {
        _states[_workerProviderIndex].status =
            _workerKind == WorkerKind::Download ? SearchStatus::Downloading
                                                : SearchStatus::Searching;
    }
}

void SummonTrackDialog::close()
{
    _open = false;
    _cursorScreenX = -1;
    _cursorScreenY = -1;
    for (auto & state : _states)
        ++state.generation;
    requestCancelRunningWorker();
}

void SummonTrackDialog::switchProvider(int delta)
{
    pollSearch();
    if (_providers.size() < 2) return;
    int const count = static_cast<int>(_providers.size());
    int next = static_cast<int>(_activeProviderIndex) + delta;
    while (next < 0) next += count;
    next %= count;
    _activeProviderIndex = static_cast<std::size_t>(next);
}

void SummonTrackDialog::requestCancelRunningWorker()
{
    VtpSummonPlugin const * plugin = nullptr;
    void * handle = nullptr;
    {
        std::lock_guard<std::mutex> lock(_searchMutex);
        _cancelWorker = true;
        if (_workerRunning && _workerProviderIndex < _providers.size())
        {
            plugin = _providers[_workerProviderIndex].plugin;
            handle = _providers[_workerProviderIndex].handle;
        }
    }
    if (plugin && plugin->cancel)
        plugin->cancel(handle);
}

void SummonTrackDialog::pollSearch()
{
    bool done = false;
    bool ok = false;
    bool stale = false;
    bool skipped = false;
    std::size_t providerIndex = 0;
    WorkerKind kind = WorkerKind::Search;
    std::vector<ResultRow> rows;
    std::filesystem::path outputPath;
    {
        std::lock_guard<std::mutex> lock(_searchMutex);
        done = _workerDone;
        if (done)
        {
            providerIndex = _workerProviderIndex;
            kind = _workerKind;
            ok = _workerOk;
            skipped = _workerSkipped;
            if (providerIndex < _states.size())
                stale = _workerGeneration != _states[providerIndex].generation;
            else
                stale = true;
            rows = std::move(_workerRows);
            outputPath = std::move(_workerOutputPath);
            _workerRows.clear();
            _workerOutputPath.clear();
            _workerSkipped = false;
            _workerDone = false;
        }
    }

    if (done && _searchThread.joinable())
        _searchThread.join();

    if (!done || stale || providerIndex >= _states.size())
        return;

    ProviderState & state = _states[providerIndex];
    if (kind == WorkerKind::Download)
    {
        _lastDownloadPath = outputPath;
        if (skipped)
        {
            state.status = SearchStatus::DownloadSkipped;
        }
        else if (ok)
        {
            state.status = SearchStatus::Downloaded;
            if (_open && _onDownloadFinished)
                _onDownloadFinished(outputPath);
        }
        else
        {
            state.status = SearchStatus::DownloadFailed;
        }
        return;
    }

    state.results = std::move(rows);
    state.selectedIndex = 0;
    state.scrollOffset = 0;
    if (!ok)
        state.status = SearchStatus::Failed;
    else if (state.results.empty())
        state.status = SearchStatus::NoResults;
    else
        state.status = SearchStatus::Results;
}

void SummonTrackDialog::clearResultsForEdit()
{
    if (!hasProviders()) return;
    ProviderState & state = activeState();
    if (state.status != SearchStatus::Searching && state.status != SearchStatus::Downloading)
        state.status = SearchStatus::Idle;
    state.results.clear();
    state.selectedIndex = 0;
    state.scrollOffset = 0;
}

void SummonTrackDialog::startSearch()
{
    pollSearch();
    if (!hasProviders()) return;

    ProviderState & state = activeState();
    if (state.query.empty()) return;

    {
        std::lock_guard<std::mutex> lock(_searchMutex);
        if (_workerRunning && !_workerDone) return;
    }
    if (_searchThread.joinable()) _searchThread.join();

    std::filesystem::path currentDir =
        _downloadDirectoryProvider ? _downloadDirectoryProvider() : fallbackCurrentDirectory();

    state.results.clear();
    state.selectedIndex = 0;
    state.scrollOffset = 0;
    state.status = SearchStatus::Searching;
    std::uint64_t const generation = ++state.generation;
    std::size_t const providerIndex = _activeProviderIndex;
    std::string query = state.query;

    {
        std::lock_guard<std::mutex> lock(_searchMutex);
        _workerRunning = true;
        _workerDone = false;
        _workerOk = false;
        _cancelWorker = false;
        _workerProviderIndex = providerIndex;
        _workerGeneration = generation;
        _workerKind = WorkerKind::Search;
        _workerRows.clear();
        _workerOutputPath.clear();
        _workerSkipped = false;
    }

    _searchThread = std::thread([this,
                                 providerIndex,
                                 query = std::move(query),
                                 currentDir = std::move(currentDir),
                                 generation]() mutable {
        SearchOutcome outcome = runProviderSearch(providerIndex,
                                                  std::move(query),
                                                  std::move(currentDir),
                                                  generation);
        std::lock_guard<std::mutex> lock(_searchMutex);
        _workerRunning = false;
        _workerDone = true;
        _workerOk = outcome.ok && !_cancelWorker;
        _workerRows = std::move(outcome.rows);
        _workerOutputPath = std::move(outcome.outputPath);
        _workerSkipped = outcome.skipped;
    });
}

void SummonTrackDialog::startDownload()
{
    pollSearch();
    if (!hasProviders()) return;

    ProviderState & state = activeState();
    if (state.results.empty()) return;
    if (state.selectedIndex < 0 || state.selectedIndex >= static_cast<int>(state.results.size()))
        return;

    ResultRow row = state.results[state.selectedIndex];
    if ((row.flags & VTP_SUMMON_RESULT_DISABLED) != 0)
        return;

    {
        std::lock_guard<std::mutex> lock(_searchMutex);
        if (_workerRunning && !_workerDone) return;
    }
    if (_searchThread.joinable()) _searchThread.join();

    std::filesystem::path targetDir =
        _downloadDirectoryProvider ? _downloadDirectoryProvider() : fallbackCurrentDirectory();

    state.status = SearchStatus::Downloading;
    _lastDownloadPath.clear();
    std::uint64_t const generation = ++state.generation;
    std::size_t const providerIndex = _activeProviderIndex;

    {
        std::lock_guard<std::mutex> lock(_searchMutex);
        _workerRunning = true;
        _workerDone = false;
        _workerOk = false;
        _cancelWorker = false;
        _workerProviderIndex = providerIndex;
        _workerGeneration = generation;
        _workerKind = WorkerKind::Download;
        _workerRows.clear();
        _workerOutputPath.clear();
        _workerSkipped = false;
    }

    _searchThread = std::thread([this,
                                 providerIndex,
                                 row = std::move(row),
                                 targetDir = std::move(targetDir),
                                 generation]() mutable {
        SearchOutcome outcome = runProviderDownload(providerIndex,
                                                    std::move(row),
                                                    std::move(targetDir),
                                                    generation);
        std::lock_guard<std::mutex> lock(_searchMutex);
        _workerRunning = false;
        _workerDone = true;
        _workerOk = outcome.ok && !_cancelWorker;
        _workerRows.clear();
        _workerOutputPath = std::move(outcome.outputPath);
        _workerSkipped = outcome.skipped;
    });
}

SummonTrackDialog::SearchOutcome SummonTrackDialog::runProviderSearch(
    std::size_t providerIndex,
    std::string query,
    std::filesystem::path currentDir,
    std::uint64_t generation)
{
    SearchOutcome outcome;
    if (providerIndex >= _providers.size())
        return outcome;

    Provider const provider = _providers[providerIndex];
    if (!provider.plugin || !provider.plugin->query)
        return outcome;

    std::array<VtpSummonResult, kMaxResults> rawResults{};
    for (auto & result : rawResults)
        result.struct_size = sizeof(VtpSummonResult);

    std::string currentDirString = currentDir.string();
    VtpSummonQueryRequest request{};
    request.struct_size = sizeof(request);
    request.query = query.c_str();
    request.current_dir = currentDirString.c_str();
    request.max_results = static_cast<std::uint32_t>(rawResults.size());

    std::size_t count = rawResults.size();
    int rc = provider.plugin->query(provider.handle, &request, rawResults.data(), &count);
    {
        std::lock_guard<std::mutex> lock(_searchMutex);
        if (_cancelWorker || providerIndex >= _states.size()
            || generation != _states[providerIndex].generation)
        {
            return outcome;
        }
    }

    if (rc != 0)
        return outcome;

    count = std::min(count, rawResults.size());
    outcome.rows.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        VtpSummonResult const & src = rawResults[i];
        ResultRow row;
        row.title = copyCString(src.title);
        row.channel = copyCString(src.channel);
        row.duration = copyCString(src.duration);
        row.url = copyCString(src.url);
        row.opaque = copyCString(src.opaque);
        row.flags = src.flags;
        if (!row.title.empty())
            outcome.rows.push_back(std::move(row));
    }
    outcome.ok = true;
    return outcome;
}

SummonTrackDialog::SearchOutcome SummonTrackDialog::runProviderDownload(
    std::size_t providerIndex,
    ResultRow row,
    std::filesystem::path targetDir,
    std::uint64_t generation)
{
    SearchOutcome outcome;
    if (providerIndex >= _providers.size())
        return outcome;

    Provider const provider = _providers[providerIndex];
    if (!provider.plugin || !provider.plugin->download)
        return outcome;

    std::string targetDirString = targetDir.string();
    VtpSummonResult selected{};
    selected.struct_size = sizeof(selected);
    selected.title = row.title.c_str();
    selected.channel = row.channel.c_str();
    selected.duration = row.duration.c_str();
    selected.url = row.url.c_str();
    selected.opaque = row.opaque.c_str();
    selected.flags = row.flags;

    VtpSummonDownloadRequest request{};
    request.struct_size = sizeof(request);
    request.current_dir = targetDirString.c_str();
    request.selected_result = &selected;

    VtpSummonDownloadOut out{};
    out.struct_size = sizeof(out);
    int rc = provider.plugin->download(provider.handle, &request, &out);
    {
        std::lock_guard<std::mutex> lock(_searchMutex);
        if (_cancelWorker || providerIndex >= _states.size()
            || generation != _states[providerIndex].generation)
        {
            return outcome;
        }
    }

    if (rc != 0)
        return outcome;

    outcome.ok = true;
    outcome.skipped = out.skipped != 0;
    outcome.outputPath = out.output_path;
    return outcome;
}

void SummonTrackDialog::insertUtf8(char32_t ch)
{
    if (!hasProviders()) return;
    ProviderState & state = activeState();
    char buf[4];
    int n = 0;
    if (ch < 0x80)
    {
        buf[n++] = static_cast<char>(ch);
    }
    else if (ch < 0x800)
    {
        buf[n++] = static_cast<char>(0xC0 | ((ch >> 6) & 0x1F));
        buf[n++] = static_cast<char>(0x80 | (ch & 0x3F));
    }
    else if (ch < 0x10000)
    {
        buf[n++] = static_cast<char>(0xE0 | ((ch >> 12) & 0x0F));
        buf[n++] = static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
        buf[n++] = static_cast<char>(0x80 | (ch & 0x3F));
    }
    else
    {
        buf[n++] = static_cast<char>(0xF0 | ((ch >> 18) & 0x07));
        buf[n++] = static_cast<char>(0x80 | ((ch >> 12) & 0x3F));
        buf[n++] = static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
        buf[n++] = static_cast<char>(0x80 | (ch & 0x3F));
    }
    state.query.insert(state.cursorBytePos, buf, n);
    state.cursorBytePos += n;
    clearResultsForEdit();
}

void SummonTrackDialog::backspaceUtf8()
{
    if (!hasProviders()) return;
    ProviderState & state = activeState();
    if (state.cursorBytePos == 0) return;
    std::size_t const prev = prevCodepointStart(state.query, state.cursorBytePos);
    state.query.erase(prev, state.cursorBytePos - prev);
    state.cursorBytePos = static_cast<int>(prev);
    clearResultsForEdit();
}

void SummonTrackDialog::deleteForward()
{
    if (!hasProviders()) return;
    ProviderState & state = activeState();
    if (state.cursorBytePos >= static_cast<int>(state.query.size())) return;
    std::size_t const next = nextCodepointStart(state.query, state.cursorBytePos);
    state.query.erase(state.cursorBytePos, next - state.cursorBytePos);
    clearResultsForEdit();
}

void SummonTrackDialog::moveCursorLeft()
{
    if (!hasProviders()) return;
    ProviderState & state = activeState();
    if (state.cursorBytePos == 0) return;
    state.cursorBytePos = static_cast<int>(prevCodepointStart(state.query, state.cursorBytePos));
}

void SummonTrackDialog::moveCursorRight()
{
    if (!hasProviders()) return;
    ProviderState & state = activeState();
    if (state.cursorBytePos >= static_cast<int>(state.query.size())) return;
    state.cursorBytePos = static_cast<int>(nextCodepointStart(state.query, state.cursorBytePos));
}

void SummonTrackDialog::moveCursorHome()
{
    if (!hasProviders()) return;
    activeState().cursorBytePos = 0;
}

void SummonTrackDialog::moveCursorEnd()
{
    if (!hasProviders()) return;
    ProviderState & state = activeState();
    state.cursorBytePos = static_cast<int>(state.query.size());
}

bool SummonTrackDialog::handleKey(ventty::KeyEvent const & event)
{
    if (!_open) return false;
    pollSearch();

    if (event.key == Key::Escape)
    {
        close();
        return true;
    }

    if (!hasProviders())
        return true;

    if (event.key == Key::Tab)
    {
        switchProvider(event.shift ? -1 : +1);
        return true;
    }
    if (event.shift && event.key == Key::Left)
    {
        switchProvider(-1);
        return true;
    }
    if (event.shift && event.key == Key::Right)
    {
        switchProvider(+1);
        return true;
    }

    ProviderState & state = activeState();
    if (state.status == SearchStatus::Searching || state.status == SearchStatus::Downloading)
        return true;

    if (event.key == Key::Enter)
    {
        if (!state.results.empty())
            startDownload();
        else
            startSearch();
        return true;
    }

    if (event.key == Key::Backspace)
    {
        backspaceUtf8();
        return true;
    }

    if (event.key == Key::Delete)
    {
        deleteForward();
        return true;
    }

    if (event.key == Key::Left)
    {
        moveCursorLeft();
        return true;
    }
    if (event.key == Key::Right)
    {
        moveCursorRight();
        return true;
    }
    if (event.key == Key::Home)
    {
        moveCursorHome();
        return true;
    }
    if (event.key == Key::End)
    {
        moveCursorEnd();
        return true;
    }

    if (event.key == Key::Up)
    {
        if (state.selectedIndex > 0) --state.selectedIndex;
        return true;
    }
    if (event.key == Key::Down)
    {
        if (state.selectedIndex < static_cast<int>(state.results.size()) - 1)
            ++state.selectedIndex;
        return true;
    }
    if (event.key == Key::PageUp)
    {
        state.selectedIndex = std::max(0, state.selectedIndex - 8);
        return true;
    }
    if (event.key == Key::PageDown)
    {
        if (!state.results.empty())
            state.selectedIndex = std::min(static_cast<int>(state.results.size()) - 1,
                                           state.selectedIndex + 8);
        return true;
    }

    if (event.key == Key::Char && !event.ctrl && !event.alt && event.ch >= 0x20)
    {
        insertUtf8(event.ch);
        return true;
    }
    return true;
}

void SummonTrackDialog::draw(ventty::Window & window)
{
    pollSearch();
    _cursorScreenX = -1;
    _cursorScreenY = -1;
    if (!_open) return;

    int const screenW = window.width();
    int const screenH = window.height();
    int const dlgW = std::min(86, std::max(52, screenW - 8));
    int const dlgH = std::min(20, std::max(9, screenH - 6));
    int const x = (screenW - dlgW) / 2;
    int const y = (screenH - dlgH) / 2;

    ventty::Style const frame{_theme.border,           _theme.background};
    ventty::Style const body { _theme.browserFg,       _theme.browserBg};
    ventty::Style const dim  { _theme.headerFg,        _theme.browserBg};
    ventty::Style const accent{_theme.browserHeaderFg, _theme.browserBg, ventty::Attr::Bold};
    ventty::Style const tabSel{_theme.browserSelFg,    _theme.browserSelBg, ventty::Attr::Bold};
    ventty::Style const sel  { _theme.browserSelFg,    _theme.browserSelBg};

    window.fill(x, y, dlgW, dlgH, U' ', body);
    window.drawBox(x, y, dlgW, dlgH, frame, /*doubleLine=*/true);
    window.drawText(x + 2, y, " Summon Track ", accent);

    if (!hasProviders())
    {
        std::string const msg = "No summon providers available";
        int const msgW = ventty::stringWidth(msg);
        int const msgX = x + std::max(2, (dlgW - msgW) / 2);
        int const msgY = y + dlgH / 2;
        window.drawText(msgX, msgY, msg, dim);
        return;
    }

    ProviderState & state = activeState();
    int const tabY = y + 1;
    int tabX = x + 2;
    int const tabRight = x + dlgW - 16;
    for (std::size_t i = 0; i < _providers.size() && tabX < tabRight; ++i)
    {
        std::string label = " " + (_providers[i].label.empty() ? "Provider" : _providers[i].label) + " ";
        int available = tabRight - tabX;
        if (ventty::stringWidth(label) > available)
            label = rightTruncateToWidth(label, available, "...");
        window.drawText(tabX, tabY, label, i == _activeProviderIndex ? tabSel : dim);
        tabX += ventty::stringWidth(label) + 1;
    }

    std::string const status = statusText(state.status, state.results.size());
    int const statusW = ventty::stringWidth(status);
    window.drawText(x + dlgW - 2 - statusW, tabY, status, dim);

    int const queryY = y + 2;
    std::string const label = " Query: ";
    window.drawText(x + 2, queryY, label, accent);
    int const queryX = x + 2 + static_cast<int>(ventty::stringWidth(label));
    int const queryW = dlgW - (queryX - x) - 2;

    int const prefixW = ventty::stringWidth(
        std::string_view{state.query.data(), static_cast<std::size_t>(state.cursorBytePos)});
    int const showW = std::max(1, queryW - 1);
    std::string visiblePrefix =
        leftTruncateToWidth(std::string_view{state.query.data(),
                                             static_cast<std::size_t>(state.cursorBytePos)},
                            showW);
    int const droppedW = prefixW - ventty::stringWidth(visiblePrefix);
    std::string const tail =
        state.cursorBytePos < static_cast<int>(state.query.size())
            ? state.query.substr(state.cursorBytePos)
            : std::string{};
    std::string visibleQuery = visiblePrefix;
    int const remaining = showW - ventty::stringWidth(visiblePrefix);
    if (remaining > 0 && !tail.empty())
    {
        std::size_t pos = 0;
        int taken = 0;
        while (pos < tail.size())
        {
            std::size_t probe = pos;
            char32_t const cp = ventty::decode(tail, probe);
            int const w = ventty::displayWidth(cp);
            if (taken + w > remaining) break;
            taken += w;
            pos = probe;
        }
        visibleQuery.append(tail, 0, pos);
    }
    window.drawText(queryX, queryY, visibleQuery, body);

    int const cursorCol = queryX + (prefixW - droppedW);
    if (cursorCol >= queryX && cursorCol < x + dlgW - 1)
    {
        _cursorScreenX = cursorCol;
        _cursorScreenY = queryY;
    }

    int const sepY = y + 3;
    for (int i = 1; i < dlgW - 1; ++i)
        window.putChar(x + i, sepY, ventty::HR_THIN, frame);

    int const listY = sepY + 1;
    int const listH = dlgH - (listY - y) - 1;
    int const innerW = dlgW - 4;

    auto drawCentered = [&](std::string const & msg) {
        int const msgW = ventty::stringWidth(msg);
        int const msgX = x + std::max(2, (dlgW - msgW) / 2);
        int const msgY = listY + std::max(0, listH / 2);
        window.drawText(msgX, msgY, msg, dim);
    };

    if (state.status == SearchStatus::Searching)
    {
        drawCentered("Searching...");
        return;
    }
    if (state.status == SearchStatus::Downloading)
    {
        drawCentered("Downloading...");
        return;
    }
    if (state.status == SearchStatus::Failed)
    {
        drawCentered("Search failed");
        return;
    }
    if (state.status == SearchStatus::NoResults)
    {
        drawCentered("No results");
        return;
    }
    if (state.results.empty())
    {
        drawCentered("Enter a query");
        return;
    }

    if (state.selectedIndex < state.scrollOffset) state.scrollOffset = state.selectedIndex;
    if (state.selectedIndex >= state.scrollOffset + listH)
        state.scrollOffset = state.selectedIndex - listH + 1;
    if (state.scrollOffset < 0) state.scrollOffset = 0;

    for (int i = 0; i < listH; ++i)
    {
        int const idx = state.scrollOffset + i;
        if (idx >= static_cast<int>(state.results.size())) break;

        int const ry = listY + i;
        bool const cursor = idx == state.selectedIndex;
        bool const disabled = (state.results[idx].flags & VTP_SUMMON_RESULT_DISABLED) != 0;
        ventty::Style const rowStyle = cursor ? sel : (disabled ? dim : body);
        window.fill(x + 1, ry, dlgW - 2, 1, U' ', rowStyle);

        std::string text = rightTruncateToWidth(formatResultRow(state.results[idx]), innerW, "...");
        window.drawText(x + 2, ry, text, rowStyle);
    }
}

} // namespace vtplayer
