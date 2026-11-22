/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "owpengram/claude/claude_access_rules.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QString>

#include <vector>

class PeerData;
class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace Owpengram::Claude {

struct ToolResult {
	QJsonValue content;
	bool isError = false;
	QString errorText;
};

struct ToolCall {
	QString name;
	QJsonObject arguments;
	QString clientName;
};

struct ToolDef {
	QString name;
	QString description;
	QJsonObject inputSchema;
	Fn<void(ToolCall, Fn<void(ToolResult)> done)> handler;
};

// Main thread only. All handlers run on the main thread and call `done`
// exactly once.
class ToolRegistry final {
public:
	void add(ToolDef def);

	[[nodiscard]] const std::vector<ToolDef> &list() const {
		return _tools;
	}
	// Unknown name -> isError result.
	void call(ToolCall call, Fn<void(ToolResult)> done);

private:
	std::vector<ToolDef> _tools;

};

// Lazily created; fills itself via RegisterReadTools and RegisterActTools.
[[nodiscard]] ToolRegistry &Tools();

// Defined in claude_tools_read.cpp.
void RegisterReadTools(ToolRegistry &registry);
// Defined in claude_tools_act.cpp.
void RegisterActTools(ToolRegistry &registry);

// ---- Shared helpers (used by read and action tools). ----

// The one error for unknown account / unknown chat / insufficient level.
[[nodiscard]] QString NotAllowedText();
[[nodiscard]] ToolResult OkResult(QJsonValue content);
[[nodiscard]] ToolResult ErrorResult(QString text);
[[nodiscard]] ToolResult NotAllowedResult();

// Authorized account session for AccountKey, or nullptr.
[[nodiscard]] Main::Session *FindSession(const QString &accountKey);
// All authorized sessions.
[[nodiscard]] std::vector<not_null<Main::Session*>> AuthorizedSessions();
[[nodiscard]] QString SessionKey(not_null<Main::Session*> session);

// Peer for (account, chat) if the effective level allows `capability`,
// otherwise nullptr (indistinguishable from "unknown").
[[nodiscard]] PeerData *FindAllowedPeer(
	const QString &accountKey,
	const QString &chatId,
	Capability capability);
[[nodiscard]] bool PeerAllows(
	not_null<PeerData*> peer,
	Capability capability);
// The level PeerAllows works with: the peer's effective level, and for a
// migrated basic group also the supergroup's one (the stricter of the two).
[[nodiscard]] Level EffectiveLevel(not_null<PeerData*> peer);

// Chats from the main chats list that the account's level lets Claude see.
[[nodiscard]] std::vector<not_null<PeerData*>> VisibleChats(
	not_null<Main::Session*> session);
// Account level >= Visible, or some listed chat is visible.
[[nodiscard]] bool AccountVisible(not_null<Main::Session*> session);

[[nodiscard]] QString ChatIdString(not_null<PeerData*> peer);
[[nodiscard]] QString PeerTypeName(not_null<PeerData*> peer);
[[nodiscard]] QString LevelString(Level level);
[[nodiscard]] QString IsoDate(TimeId date);

[[nodiscard]] QString ArgString(const QJsonObject &args, const QString &key);
[[nodiscard]] int ArgInt(
	const QJsonObject &args,
	const QString &key,
	int fallback,
	int min,
	int max);

// {msg_id, date, from, text, reply_to?, media?: {type, name, size}}.
// Never includes media contents.
[[nodiscard]] QJsonObject MessageToJson(not_null<HistoryItem*> item);

// Wraps `done` so that it is called exactly once: either with the result, or
// with an error if the session is destroyed before the result arrives.
[[nodiscard]] Fn<void(ToolResult)> GuardedBySession(
	not_null<Main::Session*> session,
	Fn<void(ToolResult)> done);

} // namespace Owpengram::Claude
