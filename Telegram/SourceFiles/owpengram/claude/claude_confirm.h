/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QString>

namespace Main {
class Session;
} // namespace Main

namespace Owpengram::Claude {

enum class ConfirmResult {
	Approved,
	Denied, // "Deny" pressed or the box closed in any other way.
	TimedOut, // No answer for 2 minutes.
	Unavailable, // No window for this account / session destroyed.
};

// Asks the user (a Yes / No box in a window of this account) to allow an
// action that Claude wants to perform. Main thread only.
//
// `done(true)` only if the user pressed "Allow". `done(false)` if the user
// denied, closed the box, 2 minutes passed, the account has no window or the
// account session was destroyed. `done` is called exactly once, and never
// synchronously with true.
void RequestConfirmation(
	not_null<Main::Session*> session,
	QString clientName,
	QString summary,
	Fn<void(bool approved)> done);

// Same, but tells why the action was not approved. `done` is called exactly
// once, and never synchronously with Approved.
void RequestConfirmationWithReason(
	not_null<Main::Session*> session,
	QString clientName,
	QString summary,
	Fn<void(ConfirmResult result)> done);

} // namespace Owpengram::Claude
