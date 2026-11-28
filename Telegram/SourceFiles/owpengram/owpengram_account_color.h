/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>

#include <optional>

#include <QtGui/QColor>

namespace Main {
class Account;
} // namespace Main

namespace Owpengram {

// A per-account accent color that tints the window (a strip under the
// title bar, plus the title bar itself on macOS) so it is obvious at a
// glance which account is active. Stored locally in the account's own
// encrypted prefs, never sent to any server.
//
// Auto picks a color from a fixed palette by the account's position in
// the domain, so accounts get distinct colors without any setup. Auto is
// only shown while more than one account is signed in; an explicitly
// chosen color is always shown. Off disables the tint for that account.
enum class AccountColorMode {
	Auto,
	Custom,
	Off,
};

struct AccountColorSetting {
	AccountColorMode mode = AccountColorMode::Auto;
	QColor custom;
};

[[nodiscard]] AccountColorSetting ReadAccountColor(
	not_null<Main::Account*> account);
void WriteAccountColor(
	not_null<Main::Account*> account,
	const AccountColorSetting &setting);

[[nodiscard]] QColor AutoAccountColor(not_null<Main::Account*> account);

// The color to paint for this account right now, or std::nullopt if the
// account should not be tinted (turned off, or Auto with a single account).
[[nodiscard]] std::optional<QColor> ResolveAccountColor(
	not_null<Main::Account*> account);

[[nodiscard]] rpl::producer<> AccountColorChanges();

} // namespace Owpengram
