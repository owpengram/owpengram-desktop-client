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

	// Text is sent as typed (only an all-whitespace text is ignored).
	void send(const QString &text);
	// Restarts the process (Failed notice, connector "Reconnect").
	void reconnect();
	// Enables the MCP server and re-sends all undelivered messages.
	void enableMcpAndRetry();
	// Ends an idle process, the next send() resumes the conversation.
	void stopProcess();
	// The panel was closed: stop now, or after the reply in flight.
	void stopWhenIdle();
	// The panel shows this model again: keep the process.
	void cancelStopWhenIdle();

private:
	struct PrivateTag {
	};

public:
	PanelModel(PrivateTag, SessionTarget target);

private:
	void attach(std::shared_ptr<Session> session);
	void ensureProcess();
	void handle(const ProcessEvent &event);
	// Writes the text to claude, false if the process is not running.
	bool deliver(const QString &text);
	void setPending(int pending);
	void add(FeedEntryType type, QString text);
	void loadTranscript();

	SessionTarget _target;
	std::shared_ptr<Session> _session;
	std::unique_ptr<ClaudeProcess> _process;
	std::vector<FeedEntry> _entries;
	// Messages that did not reach claude because MCP was off.
	std::vector<QString> _undelivered;
	// Sent messages without a result yet ("Thinking…" while > 0).
	int _pending = 0;
	// Incremented on Failed / NotInstalled (they clear the queue).
	int _failures = 0;
	bool _mcpNoticeShown = false;
	bool _stopWhenIdle = false;
	rpl::event_stream<int> _appended;
	rpl::variable<bool> _thinking = false;
	std::vector<Connector> _connectors;
	rpl::event_stream<> _connectorsChanges;
	rpl::lifetime _processLifetime;

};

// Per window: the conversation the panel shows. The current model is held
// strongly for the window lifetime, so closing the panel keeps the
// conversation (and a Temporary session) alive. A model is released when
// another one replaces it (new session, another chat's session, history),
// which ends its process and, for a Temporary session, the session itself.
struct PanelState {
	rpl::variable<std::shared_ptr<PanelModel>> current;
	// Keeps the panel open on chat switch (persisted in AccessStore).
	rpl::variable<bool> pinned = false;
	// The user opened the panel and did not close it (see
	// claude_panel_toggle.h). Persisted, restored only when pinned.
	rpl::variable<bool> open = false;
	// Count of alive panel widgets (third column or layer).
	rpl::variable<int> widgets = 0;
	// The panel is shown as a layer (the window is too narrow).
	bool inLayer = false;
	// The active chat the panel last saw (HandleActiveChatChanged also
	// fires for the same chat when its send rights change).
	PeerData *chat = nullptr;
	rpl::lifetime lifetime;
};

[[nodiscard]] PanelState &PanelStateFor(
	not_null<Window::SessionController*> controller);
// Null if PanelStateFor was not called or the window is gone.
[[nodiscard]] PanelState *PanelStateIfExists(
	not_null<Window::SessionController*> controller);

// The model for the panel opened in this chat (or with no chat): the
// current one if it covers this chat (All, Account of the same account,
// Chat / Temporary of the same chat), else the newest Chat-scope
// session of the chat, else a pending Chat-scope session created on the
// first message. Becomes the current model of the window.
std::shared_ptr<PanelModel> ChooseModelForChat(
	not_null<Window::SessionController*> controller,
	PeerData *peer);

// The user closed the panel: a Temporary session is released, any other
// keeps its feed but, unless the panel is pinned, its idle claude process
// is stopped (the next message resumes it with --resume).
void HandlePanelClosed(not_null<Window::SessionController*> controller);

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
