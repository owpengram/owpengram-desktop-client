/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "owpengram/claude/claude_process.h"

#include <rpl/producer.h>

#include <QtCore/QString>

namespace Owpengram::Claude {

// Sign-in of the local Claude Code (`claude auth status / login`) of the
// selected account profile, the one the panel sessions use. Main thread only.
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

// `claude auth login --claudeai` for the selected profile: opens the
// browser to sign in with the Claude subscription, refreshes the status.
void StartLogin();
// The same into a new profile folder; on success the profile is saved with
// its email and selected, else its folder is removed.
void AddAccount();
void CancelLogin();

// The email of the system claude sign-in (no CLAUDE_CONFIG_DIR override),
// empty if signed out or not checked yet.
[[nodiscard]] rpl::producer<QString> SystemEmailValue();
[[nodiscard]] QString SystemEmail();

// Makes the profile the one panel sessions use (empty id - system).
void SelectAccount(const QString &profileId);

// Subscription usage of the selected account, from the last claude reply
// (unknown until then, reset on SelectAccount).
[[nodiscard]] rpl::producer<ProcessEvents::RateLimits> UsageLimitsValue();
[[nodiscard]] ProcessEvents::RateLimits CurrentUsageLimits();
void SetUsageLimits(const ProcessEvents::RateLimits &limits);

} // namespace Owpengram::Claude
