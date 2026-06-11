/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"

namespace Storage {
class Domain;
enum class StartResult : uchar;
} // namespace Storage

namespace MTP {
enum class Environment : uchar;
} // namespace MTP

namespace Main {

class Account;
class Session;

class Domain final : public base::has_weak_ptr {
public:
	struct AccountWithIndex {
		int index = 0;
		std::unique_ptr<Account> account;
	};

	static constexpr auto kMaxAccounts = 3;
	static constexpr auto kPremiumMaxAccounts = 6;
	// Hard cap on the total number of accounts of any server. Custom self-hosted
	// accounts don't count toward the Telegram free/premium limit, so the overall
	// cap is higher than the Telegram-only kPremiumMaxAccounts.
	static constexpr auto kMaxTotalAccounts = 10;

	explicit Domain(const QString &dataName);
	~Domain();

	[[nodiscard]] bool started() const;
	[[nodiscard]] Storage::StartResult start(const QByteArray &passcode);
	void resetWithForgottenPasscode();
	void finish();

	[[nodiscard]] int maxAccounts() const;
	[[nodiscard]] rpl::producer<int> maxAccountsChanges() const;

	[[nodiscard]] Storage::Domain &local() const {
		return *_local;
	}

	[[nodiscard]] auto accounts() const
		-> const std::vector<AccountWithIndex> &;
	[[nodiscard]] std::vector<not_null<Account*>> orderedAccounts() const;
	[[nodiscard]] rpl::producer<Account*> activeValue() const;
	[[nodiscard]] rpl::producer<> accountsChanges() const;
	[[nodiscard]] Account *maybeLastOrSomeAuthedAccount();
	[[nodiscard]] int accountsAuthedCount() const;
	// Authorized accounts on the official Telegram server (custom self-hosted
	// servers are excluded). This is the count the Telegram limit applies to.
	[[nodiscard]] int telegramAccountsCount() const;
	[[nodiscard]] static bool AccountIsTelegram(not_null<Account*> account);

	// Expects(started());
	[[nodiscard]] Account &active() const;
	[[nodiscard]] rpl::producer<not_null<Account*>> activeChanges() const;

	[[nodiscard]] rpl::producer<Session*> activeSessionValue() const;
	[[nodiscard]] rpl::producer<Session*> activeSessionChanges() const;

	[[nodiscard]] int unreadBadge() const;
	[[nodiscard]] bool unreadBadgeMuted() const;
	[[nodiscard]] rpl::producer<> unreadBadgeChanges() const;
	void notifyUnreadBadgeChanged();

	[[nodiscard]] not_null<Main::Account*> add(MTP::Environment environment);
	void maybeActivate(not_null<Main::Account*> account);
	void activate(not_null<Main::Account*> account);
	void addActivated(MTP::Environment environment, bool newWindow = false);

	// Interface for Storage::Domain.
	void accountAddedInStorage(AccountWithIndex accountWithIndex);
	void activateFromStorage(int index);
	[[nodiscard]] int activeForStorage() const;

private:
	void activateAfterStarting();
	// Blocks input with a modal while a freshly-activated account connects to its
	// server; on timeout switches to a working account (Telegram if available).
	void guardServerConnection(not_null<Main::Account*> account);
	void switchToFallbackAccount(not_null<Main::Account*> failed);
	void closeAccountWindows(not_null<Main::Account*> account);
	bool removePasscodeIfEmpty();
	void removeRedundantAccounts();
	void watchSession(not_null<Account*> account);
	void scheduleWriteAccounts();
	void checkForLastProductionConfig(not_null<Main::Account*> account);
	void updateUnreadBadge();
	void scheduleUpdateUnreadBadge();
	void suggestExportIfNeeded();

	const QString _dataName;
	const std::unique_ptr<Storage::Domain> _local;

	std::vector<AccountWithIndex> _accounts;
	rpl::event_stream<> _accountsChanges;
	rpl::variable<Account*> _active = nullptr;
	int _accountToActivate = -1;
	int _lastActiveIndex = -1;
	bool _writeAccountsScheduled = false;

	rpl::event_stream<Session*> _activeSessions;

	rpl::event_stream<> _unreadBadgeChanges;
	int _unreadBadge = 0;
	bool _unreadBadgeMuted = true;
	bool _unreadBadgeUpdateScheduled = false;

	rpl::variable<int> _lastMaxAccounts;

	rpl::lifetime _activeLifetime;
	rpl::lifetime _lifetime;

};

} // namespace Main
