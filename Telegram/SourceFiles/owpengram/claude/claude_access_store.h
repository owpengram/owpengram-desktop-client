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
#include <vector>

#include <QtCore/QString>

class PeerData;

namespace Main {
class Account;
} // namespace Main

namespace Owpengram::Claude {

inline constexpr auto kDefaultMcpPort = 47800;

// "<server scope or 'telegram'>:<userId>", empty if the account has no session.
[[nodiscard]] QString AccountKey(not_null<Main::Account*> account);

// Main thread only (not thread-safe).
class AccessStore final {
public:
	AccessStore();

	[[nodiscard]] Level accountDefault(const QString &accountKey) const;
	[[nodiscard]] std::optional<Level> chatOverride(
		const QString &accountKey,
		PeerId peer) const;
	// Some chat override of this account is >= level.
	[[nodiscard]] bool hasOverrideAtLeast(
		const QString &accountKey,
		Level level) const;
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

	// Claude Code accounts added in OwpenGram, each in its own claude
	// config folder (ClaudeProfilePath). The empty id is the system
	// claude sign-in (no CLAUDE_CONFIG_DIR override).
	struct ClaudeProfile {
		QString id;
		QString email;
	};
	[[nodiscard]] const std::vector<ClaudeProfile> &claudeProfiles() const;
	void setClaudeProfile(const QString &id, const QString &email);
	void removeClaudeProfile(const QString &id);
	// Empty = the system sign-in.
	[[nodiscard]] QString selectedClaudeProfile() const;
	void selectClaudeProfile(const QString &id);

	// Panel session options passed to claude: model alias / full name and
	// effort level (empty - claude's default), permission mode ("manual" or
	// "plan", the only modes where every action still reaches approve).
	[[nodiscard]] QString panelModel() const;
	void setPanelModel(const QString &model);
	[[nodiscard]] QString panelEffort() const;
	void setPanelEffort(const QString &effort);
	[[nodiscard]] QString panelMode() const;
	void setPanelMode(const QString &mode);

	// Claude side panel: open (restored only when pinned) and pinned.
	[[nodiscard]] bool panelOpen() const;
	void setPanelOpen(bool open);
	[[nodiscard]] bool panelPinned() const;
	void setPanelPinned(bool pinned);

	// Writes pending changes right now (used on quit).
	void flush();

private:
	struct AccountData {
		Level defaultLevel = Level::Forbidden;
		base::flat_map<PeerId, Level> chats;
	};

	void load();
	void hookQuit();
	void scheduleSave();
	void save();

	base::flat_map<QString, AccountData> _accounts;
	bool _mcpEnabled = false;
	int _mcpPort = kDefaultMcpPort;
	QString _mcpToken;
	std::vector<ClaudeProfile> _claudeProfiles;
	QString _selectedClaudeProfile;
	QString _panelModel;
	QString _panelEffort;
	QString _panelMode;
	bool _panelOpen = false;
	bool _panelPinned = false;

	base::Timer _saveTimer;
	bool _dirty = false;
	bool _quitHooked = false;
	rpl::event_stream<> _changes;

};

// tdata/owpengram_claude_profiles/<id>/ (holds the account's credentials).
[[nodiscard]] QString ClaudeProfilePath(const QString &id);

// Lazily created, lives until process exit (flushed on aboutToQuit).
[[nodiscard]] AccessStore &Access();

} // namespace Owpengram::Claude
