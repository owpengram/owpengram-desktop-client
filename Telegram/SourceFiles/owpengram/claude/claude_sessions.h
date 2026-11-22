/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"
#include "data/data_peer_id.h"
#include "owpengram/claude/claude_access_rules.h"

#include <memory>
#include <vector>

#include <QtCore/QDateTime>
#include <QtCore/QString>

namespace Owpengram::Claude {

struct SessionInfo {
	QString id; // UUID, also the claude --session-id.
	SessionScope scope = SessionScope::Temporary;
	QString accountKey;
	PeerId peer;
	QString title;
	QDateTime created;
	QString folder; // Absolute path, empty for Temporary.
};

// Main thread only. Created by Sessions.
class Session final {
public:
	[[nodiscard]] const SessionInfo &info() const;

	// Temporary sessions write nothing.
	void appendTranscript(QString role, QString text);
	void rename(QString title);

	[[nodiscard]] QString claudeSessionId() const;
	void setClaudeSessionId(QString id);

	[[nodiscard]] QString transcriptPath() const;

private:
	friend class Sessions;

	explicit Session(SessionInfo info, QString claudeSessionId);

	[[nodiscard]] bool persistent() const;
	void writeMeta() const;

	SessionInfo _info;
	QString _claudeSessionId;

};

// Main thread only.
class Sessions final {
public:
	std::shared_ptr<Session> create(
		SessionScope scope,
		QString accountKey,
		PeerId peer);

	// Newest first. Reads ai/sessions/*/session.json.
	[[nodiscard]] std::vector<SessionInfo> history() const;

	// Continues a saved session (returns the open one if it is open).
	std::shared_ptr<Session> open(const QString &id);

	// Only currently open sessions (for MCP).
	[[nodiscard]] std::shared_ptr<Session> find(const QString &id) const;

	// Newest Chat-scope session of this chat: open ones first, then saved.
	[[nodiscard]] std::shared_ptr<Session> lastForChat(
		const QString &accountKey,
		PeerId peer) const;

	// cWorkingDir() + "ai/"
	[[nodiscard]] QString aiPath() const;

private:
	mutable base::flat_map<QString, std::weak_ptr<Session>> _open;

};

[[nodiscard]] Sessions &AllSessions();

} // namespace Owpengram::Claude
