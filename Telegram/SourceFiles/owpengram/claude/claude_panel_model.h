/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer_id.h"
#include "owpengram/claude/claude_access_rules.h"
#include "owpengram/claude/claude_process.h"

#include <rpl/event_stream.h>
#include <rpl/lifetime.h>
#include <rpl/producer.h>
#include <rpl/variable.h>

#include <QtCore/QString>

#include <memory>
#include <vector>

class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace Owpengram::Claude {

class Session;

enum class FeedEntryType {
	User,
	Assistant,
	Tool,
	McpDisabled,
	NotInstalled,
	Failed,
	Error,
};

struct FeedEntry {
	FeedEntryType type = FeedEntryType::User;
	QString text;
};

// What a not yet created session will be (created on the first send).
struct SessionTarget {
	SessionScope scope = SessionScope::Chat;
	QString accountKey;
	PeerId peer;
};

// One conversation shown in the panel: the session, its claude process and
// the in-memory feed. Outlives the panel widget (see PanelState), so the
// feed survives the panel being closed and reopened. Main thread only.
class PanelModel final
	: public std::enable_shared_from_this<PanelModel> {
public:
	// Reuses the model of this session if one is alive.
	[[nodiscard]] static std::shared_ptr<PanelModel> ForSession(
		std::shared_ptr<Session> session);
	[[nodiscard]] static std::shared_ptr<PanelModel> Pending(
		SessionTarget target);

	~PanelModel();

	[[nodiscard]] SessionScope scope() const;
	[[nodiscard]] const QString &accountKey() const;
	[[nodiscard]] PeerId peer() const;
	// Null until the first message of a pending model.
	[[nodiscard]] Session *session() const;

	[[nodiscard]] const std::vector<FeedEntry> &entries() const;
	// Index of the added entry in entries().
	[[nodiscard]] rpl::producer<int> appended() const;
	[[nodiscard]] rpl::producer<bool> thinkingValue() const;
	[[nodiscard]] rpl::producer<std::vector<Connector>> connectorsValue() const;
	[[nodiscard]] rpl::producer<QString> modelNameValue() const;

	void send(const QString &text);
	// Restarts the process (Failed notice, connector "Reconnect").
	void reconnect();
	// Enables the MCP server and re-sends the undelivered message.
	void enableMcpAndRetry();

private:
	struct PrivateTag {
	};

public:
	PanelModel(PrivateTag, SessionTarget target);

private:
	void attach(std::shared_ptr<Session> session);
	void ensureProcess();
	void handle(const ProcessEvent &event);
	void add(FeedEntryType type, QString text);
	void loadTranscript();

	SessionTarget _target;
	std::shared_ptr<Session> _session;
	std::unique_ptr<ClaudeProcess> _process;
	std::vector<FeedEntry> _entries;
	// The last message that did not reach claude (MCP off / not installed).
	QString _undelivered;
	rpl::event_stream<int> _appended;
	rpl::variable<bool> _thinking = false;
	std::vector<Connector> _connectors;
	rpl::event_stream<> _connectorsChanges;
	rpl::variable<QString> _modelName;
	rpl::lifetime _processLifetime;

};

// Per window: the conversation the panel shows. The current model is held
// strongly for the window lifetime, so closing the panel keeps the
// conversation (and a Temporary session) alive. A model is released when
// another one replaces it (new session, another chat's session, history),
// which ends its process and, for a Temporary session, the session itself.
struct PanelState {
	rpl::variable<std::shared_ptr<PanelModel>> current;
	// Keeps the panel open on chat switch (persisted by Task 5).
	rpl::variable<bool> pinned = false;
};

[[nodiscard]] PanelState &PanelStateFor(
	not_null<Window::SessionController*> controller);

// The model for the panel opened in this chat (or with no chat): the
// current one if it is bound to the same chat, else the newest Chat-scope
// session of the chat, else a pending Chat-scope session created on the
// first message. Becomes the current model of the window.
std::shared_ptr<PanelModel> ChooseModelForChat(
	not_null<Window::SessionController*> controller,
	PeerData *peer);

// Replaces the current model with a pending session of this scope bound to
// the active chat (Temporary / Chat need one, else nothing happens).
void StartNewSession(
	not_null<Window::SessionController*> controller,
	SessionScope scope);

// Continues a saved session in the panel (for the session history).
void ShowSavedSession(
	not_null<Window::SessionController*> controller,
	const QString &id);

} // namespace Owpengram::Claude
