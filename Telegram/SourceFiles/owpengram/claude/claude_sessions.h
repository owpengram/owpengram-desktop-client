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
	// Absolute path. Empty for Temporary and until the first transcript
	// message of a persistent session (the folder is created lazily).
	QString folder;
};

// Main thread only. Created by Sessions.
class Session final {
public:
	[[nodiscard]] const SessionInfo &info() const;

	// Temporary sessions write nothing. For others the first call creates
	// the folder + session.json (an empty title becomes the first words of
	// the first user message) and, if the folder vanished, restores it.
	void appendTranscript(QString role, QString text);
	// Updates only the title in session.json, the folder is never renamed.
	void rename(QString title);

	[[nodiscard]] QString claudeSessionId() const;
	void setClaudeSessionId(QString id);

	[[nodiscard]] QString transcriptPath() const;

private:
	friend class Sessions;

	explicit Session(SessionInfo info, QString claudeSessionId);

	[[nodiscard]] bool persistent() const;
	void ensureFolder(const QString &firstUserText);
	void writeMeta() const;

	SessionInfo _info;
	QString _claudeSessionId;

};

// Main thread only. Sessions keeps only weak references to open sessions:
// the owner (panel / claude process) must hold the shared_ptr for the
// session to stay findable by MCP via find().
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
		PeerId peer);

	// cWorkingDir() + "ai/"
	[[nodiscard]] QString aiPath() const;

private:
	void pruneOpen() const;

	mutable base::flat_map<QString, std::weak_ptr<Session>> _open;

};

[[nodiscard]] Sessions &AllSessions();

} // namespace Owpengram::Claude
