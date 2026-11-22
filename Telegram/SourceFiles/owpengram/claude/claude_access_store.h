/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"
#include "base/timer.h"
#include "data/data_peer_id.h"
#include "owpengram/claude/claude_access_rules.h"

#include <rpl/event_stream.h>
#include <rpl/producer.h>

#include <optional>

#include <QtCore/QString>

class PeerData;

namespace Main {
class Account;
} // namespace Main

namespace Owpengram::Claude {

inline constexpr auto kDefaultMcpPort = 47800;

// "<server scope or 'telegram'>:<userId>", empty if the account has no session.
[[nodiscard]] QString AccountKey(not_null<Main::Account*> account);

class AccessStore final {
public:
	AccessStore();

	[[nodiscard]] Level accountDefault(const QString &accountKey) const;
	[[nodiscard]] std::optional<Level> chatOverride(
		const QString &accountKey,
		PeerId peer) const;
	[[nodiscard]] Level effective(not_null<PeerData*> peer) const;

	void setAccountDefault(const QString &accountKey, Level level);
	void setChatOverride(
		const QString &accountKey,
		PeerId peer,
		std::optional<Level> level); // nullopt = same as account.

	[[nodiscard]] rpl::producer<> changes() const;

	[[nodiscard]] bool mcpEnabled() const;
	void setMcpEnabled(bool enabled);
	[[nodiscard]] int mcpPort() const;
	[[nodiscard]] QString mcpToken();
	void regenerateMcpToken();

	// Writes pending changes right now (used on quit).
	void flush();

private:
	struct AccountData {
		Level defaultLevel = Level::Forbidden;
		base::flat_map<PeerId, Level> chats;
	};

	void load();
	void scheduleSave();
	void save();

	base::flat_map<QString, AccountData> _accounts;
	bool _mcpEnabled = false;
	int _mcpPort = kDefaultMcpPort;
	QString _mcpToken;

	base::Timer _saveTimer;
	bool _dirty = false;
	rpl::event_stream<> _changes;

};

// Lazily created, lives until process exit (flushed on aboutToQuit).
[[nodiscard]] AccessStore &Access();

} // namespace Owpengram::Claude
