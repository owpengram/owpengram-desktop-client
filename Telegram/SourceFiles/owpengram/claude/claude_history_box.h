/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace Ui {
class BoxContent;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Owpengram::Claude {

struct SessionInfo;

// Saved Claude sessions, newest first: date and time, title, type and the
// bound chat (its name only if Claude may see it). Search by title, open,
// delete (with confirmation, removes the session folder).
[[nodiscard]] object_ptr<Ui::BoxContent> HistoryBox(
	not_null<Window::SessionController*> controller,
	Fn<void(const SessionInfo &info)> open);

// The panel's history button: HistoryBox that continues the chosen
// session in the panel (switching to its chat first).
void ShowHistoryBox(not_null<Window::SessionController*> controller);

} // namespace Owpengram::Claude
