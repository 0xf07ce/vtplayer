// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "ConfirmDialog.h"

#include "../util/UnicodeNormalize.h"

#include <ventty/core/Utf8.h>

#include <algorithm>

namespace vtplayer
{

using Key = ventty::KeyEvent::Key;

void ConfirmDialog::open(std::string title, std::string message, bool defaultYes)
{
    _open = true;
    _title = std::move(title);
    _message = std::move(message);
    _yes = defaultYes;
}

void ConfirmDialog::close()
{
    _open = false;
}

bool ConfirmDialog::handleKey(ventty::KeyEvent const & event)
{
    if (!_open) return false;

    if (event.key == Key::Escape) { close(); return true; }

    if (event.key == Key::Left || event.key == Key::Right || event.key == Key::Tab)
    {
        _yes = !_yes;
        return true;
    }

    if (event.key == Key::Char && !event.ctrl && !event.alt)
    {
        if (event.ch == 'y' || event.ch == 'Y') { _yes = true; return true; }
        if (event.ch == 'n' || event.ch == 'N') { _yes = false; return true; }
    }

    if (event.key == Key::Enter)
    {
        bool const yes = _yes;
        OnConfirm cb = _onConfirm;
        close();
        if (cb) cb(yes);
        return true;
    }

    return true; // swallow all other keys while modal
}

void ConfirmDialog::draw(ventty::Window & window)
{
    if (!_open) return;

    int const screenW = window.width();
    int const screenH = window.height();

    int const boxW = std::min(60, std::max(40, static_cast<int>(_message.size()) + 6));
    int const boxH = 7;
    int const bx = (screenW - boxW) / 2;
    int const by = (screenH - boxH) / 2;

    ventty::Style const frame { _theme.border,          _theme.background};
    ventty::Style const body  { _theme.browserFg,       _theme.browserBg};
    ventty::Style const accent{ _theme.browserHeaderFg, _theme.browserBg, ventty::Attr::Bold};
    ventty::Style const sel   { _theme.browserSelFg,    _theme.browserSelBg};

    window.fill(bx, by, boxW, boxH, U' ', body);
    window.drawBox(bx, by, boxW, boxH, frame, /*doubleLine=*/true);

    std::string const title = " " + _title + " ";
    window.drawText(bx + 2, by, title, accent);

    std::string const promptCut = truncateToWidth(_message, boxW - 4, "...");
    int const promptX = bx + (boxW - static_cast<int>(ventty::stringWidth(promptCut))) / 2;
    window.drawText(promptX, by + 2, promptCut, body);

    std::string const yesLabel = "  Yes  ";
    std::string const noLabel  = "  No  ";
    int const btnRowY = by + 4;
    int const yesX = bx + boxW / 2 - 8;
    int const noX  = bx + boxW / 2 + 2;

    window.drawText(yesX, btnRowY, yesLabel, _yes ? sel : body);
    window.drawText(noX,  btnRowY, noLabel,  !_yes ? sel : body);
}

} // namespace vtplayer
