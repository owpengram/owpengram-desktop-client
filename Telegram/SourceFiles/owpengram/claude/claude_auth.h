/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>

#include <QtCore/QString>

namespace Owpengram::Claude {

// Sign-in of the local Claude Code (`claude auth status / login`), the
// account the panel sessions use. Main thread only.
struct AuthStatus {
	// False until the first `claude auth status` finished (or when claude
	// is not installed: the panel shows its own notice for that).
	bool known = false;
	bool loggedIn = false;
	QString email;
	// `claude auth login` is running (waits for the browser sign-in).
	bool loginRunning = false;
	// The sign-in page printed by claude, if the browser did not open.
	QString loginUrl;

	friend inline bool operator==(
		const AuthStatus &,
		const AuthStatus &) = default;
};

// Starts a status check on the first subscription.
[[nodiscard]] rpl::producer<AuthStatus> AuthStatusValue();
[[nodiscard]] AuthStatus CurrentAuthStatus();
void RefreshAuthStatus();

// `claude auth login --claudeai`: opens the browser to sign in with the
// Claude subscription, refreshes the status when it ends.
void StartLogin();
void CancelLogin();

} // namespace Owpengram::Claude
