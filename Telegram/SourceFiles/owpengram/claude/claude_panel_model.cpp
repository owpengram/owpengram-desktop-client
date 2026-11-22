/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_panel_model.h"

#include "base/assertion.h"
#include "base/flat_map.h"
#include "base/variant.h"
#include "data/data_peer.h"
#include "dialogs/dialogs_key.h"
#include "main/main_session.h"
#include "owpengram/claude/claude_access_store.h"
#include "owpengram/claude/claude_sessions.h"
#include "window/window_session_controller.h"

#include <QtCore/QFile>
#include <QtCore/QRegularExpression>

namespace Owpengram::Claude {
namespace {

constexpr auto kOwpengramToolPrefix = "mcp__owpengram__";
constexpr auto kMaxTranscriptSize = 4 * 1024 * 1024;

base::flat_map<QString, std::weak_ptr<PanelModel>> &OpenModels() {
	static auto result = base::flat_map<QString, std::weak_ptr<PanelModel>>();
	return result;
}

base::flat_map<
	not_null<Window::SessionController*>,
	std::unique_ptr<PanelState>> &States() {
	static auto result = base::flat_map<
		not_null<Window::SessionController*>,
		std::unique_ptr<PanelState>>();
	return result;
}

[[nodiscard]] QString ToolName(const QString &name) {
	const auto prefix = QString::fromLatin1(kOwpengramToolPrefix);
	return name.startsWith(prefix) ? name.mid(prefix.size()) : name;
}

[[nodiscard]] PeerId ActivePeer(
		not_null<Window::SessionController*> controller) {
	const auto peer = controller->activeChatCurrent().peer();
	return peer ? peer->id : PeerId();
}

[[nodiscard]] bool CoversChat(
		const PanelModel &model,
		const QString &accountKey,
		PeerId peer) {
	switch (model.scope()) {
	case SessionScope::All:
		return true;
	case SessionScope::Account:
		return (model.accountKey() == accountKey);
	case SessionScope::Chat:
	case SessionScope::Temporary:
		return (model.accountKey() == accountKey)
			&& (!peer || model.peer() == peer);
	}
	Unexpected("Scope in CoversChat.");
}

} // namespace

std::shared_ptr<PanelModel> PanelModel::ForSession(
		std::shared_ptr<Session> session) {
	Expects(session != nullptr);

	const auto &info = session->info();
	auto &open = OpenModels();
	if (const auto i = open.find(info.id); i != end(open)) {
		if (auto result = i->second.lock()) {
			return result;
		}
	}
	auto result = std::make_shared<PanelModel>(PrivateTag(), SessionTarget{
		.scope = info.scope,
		.accountKey = info.accountKey,
		.peer = info.peer,
	});
	result->attach(std::move(session));
	result->loadTranscript();
	return result;
}

std::shared_ptr<PanelModel> PanelModel::Pending(SessionTarget target) {
	return std::make_shared<PanelModel>(PrivateTag(), std::move(target));
}

PanelModel::PanelModel(PrivateTag, SessionTarget target)
: _target(std::move(target)) {
}

PanelModel::~PanelModel() {
	_processLifetime.destroy();
	if (_process) {
		// May be released from inside one of its event handlers.
		_process.release()->deleteLater();
	}
	if (_session) {
		auto &open = OpenModels();
		const auto i = open.find(_session->info().id);
		if (i != end(open) && i->second.expired()) {
			open.erase(i);
		}
	}
}

void PanelModel::attach(std::shared_ptr<Session> session) {
	Expects(!_session);

	_session = std::move(session);
	OpenModels()[_session->info().id] = weak_from_this();
}

SessionScope PanelModel::scope() const {
	return _target.scope;
}

const QString &PanelModel::accountKey() const {
	return _target.accountKey;
}

PeerId PanelModel::peer() const {
	return _target.peer;
}

Session *PanelModel::session() const {
	return _session.get();
}

const std::vector<FeedEntry> &PanelModel::entries() const {
	return _entries;
}

rpl::producer<int> PanelModel::appended() const {
	return _appended.events();
}

rpl::producer<bool> PanelModel::thinkingValue() const {
	return _thinking.value();
}

rpl::producer<std::vector<Connector>> PanelModel::connectorsValue() const {
	return _connectorsChanges.events_starting_with({}) | rpl::map([=] {
		return _connectors;
	});
}

void PanelModel::add(FeedEntryType type, QString text) {
	_entries.push_back({ .type = type, .text = std::move(text) });
	_appended.fire(int(_entries.size()) - 1);
}

void PanelModel::ensureProcess() {
	if (!_session) {
		auto session = AllSessions().create(
			_target.scope,
			_target.accountKey,
			_target.peer);
		attach(std::move(session));
	}
	if (_process) {
		return;
	}
	_process = std::make_unique<ClaudeProcess>(_session);
	_process->events(
	) | rpl::on_next([=](const ProcessEvent &event) {
		handle(event);
	}, _processLifetime);
}

bool PanelModel::deliver(const QString &text) {
	ensureProcess();
	_process->send(text);
	if (!_process || !_process->running()) {
		return false;
	}
	_mcpNoticeShown = false;
	setPending(_pending + 1);
	return true;
}

void PanelModel::setPending(int pending) {
	_pending = std::max(pending, 0);
	_thinking = (_pending > 0);
}

void PanelModel::send(const QString &text) {
	if (text.trimmed().isEmpty()) {
		return;
	}
	add(FeedEntryType::User, text);
	const auto failures = _failures;
	if (!deliver(text) && failures == _failures) {
		_undelivered.push_back(text);
	}
}

void PanelModel::reconnect() {
	ensureProcess();
	setPending(0);
	_process->reconnect();
}

void PanelModel::stopProcess() {
	_stopWhenIdle = false;
	if (_process) {
		_process->stop();
	}
	setPending(0);
}

void PanelModel::stopWhenIdle() {
	if (_pending > 0) {
		_stopWhenIdle = true;
	} else {
		stopProcess();
	}
}

void PanelModel::cancelStopWhenIdle() {
	_stopWhenIdle = false;
}

void PanelModel::enableMcpAndRetry() {
	Access().setMcpEnabled(true);
	_mcpNoticeShown = false;
	// The texts are already in the feed, send them again without adding.
	for (const auto &text : base::take(_undelivered)) {
		const auto failures = _failures;
		if (!deliver(text)) {
			if (failures != _failures) {
				break; // The queue was dropped with a failure notice.
			}
			_undelivered.push_back(text);
		}
	}
}

void PanelModel::handle(const ProcessEvent &event) {
	using namespace ProcessEvents;
	v::match(event, [&](const AssistantText &data) {
		add(FeedEntryType::Assistant, data.text);
	}, [&](const ToolUse &data) {
		const auto name = ToolName(data.name);
		add(
			FeedEntryType::Tool,
			data.summary.isEmpty()
				? name
				: (name + QString::fromUtf8(" \xC2\xB7 ") + data.summary));
	}, [&](const Result &data) {
		setPending(_pending - 1);
		if (!_pending && _stopWhenIdle) {
			// Not from inside the process event handler. The panel may be
			// reopened (cancelStopWhenIdle) before this runs.
			crl::on_main([weak = weak_from_this()] {
				const auto strong = weak.lock();
				if (strong && strong->_stopWhenIdle && !strong->_pending) {
					strong->stopProcess();
				}
			});
		}
		if (data.isError) {
			add(FeedEntryType::Error, data.text);
		}
	}, [&](const Init &data) {
		_connectors = data.connectors;
		_connectorsChanges.fire({});
	}, [&](const Failed &data) {
		setPending(0);
		++_failures;
		_undelivered.clear();
		add(FeedEntryType::Failed, data.message);
	}, [&](const NotInstalled &) {
		setPending(0);
		++_failures;
		_undelivered.clear();
		add(FeedEntryType::NotInstalled, QString());
	}, [&](const McpDisabled &) {
		// One notice for all the messages queued while MCP is off.
		if (!_mcpNoticeShown) {
			_mcpNoticeShown = true;
			add(FeedEntryType::McpDisabled, QString());
		}
	}, [&](const Exited &) {
		// Silent: the next send() starts the process again.
		setPending(0);
	});
}

void PanelModel::loadTranscript() {
	const auto path = _session ? _session->transcriptPath() : QString();
	if (path.isEmpty()) {
		return;
	}
	auto file = QFile(path);
	if (file.size() > kMaxTranscriptSize
		|| !file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto content = QString::fromUtf8(file.readAll());
	static const auto header = QRegularExpression(
		u"^### (user|assistant) · "
		"\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}\\n\\n"_q,
		QRegularExpression::MultilineOption);
	auto matches = header.globalMatch(content);
	auto role = QString();
	auto start = -1;
	const auto flush = [&](int till) {
		if (start < 0) {
			return;
		}
		const auto text = content.mid(start, till - start).trimmed();
		if (!text.isEmpty()) {
			_entries.push_back({
				.type = ((role == u"user"_q)
					? FeedEntryType::User
					: FeedEntryType::Assistant),
				.text = text,
			});
		}
	};
	while (matches.hasNext()) {
		const auto match = matches.next();
		flush(match.capturedStart());
		role = match.captured(1);
		start = match.capturedEnd();
	}
	flush(content.size());
}

PanelState &PanelStateFor(not_null<Window::SessionController*> controller) {
	auto &states = States();
	if (const auto i = states.find(controller); i != end(states)) {
		return *i->second;
	}
	const auto result = states.emplace(
		controller,
		std::make_unique<PanelState>()).first->second.get();
	controller->lifetime().add([=] {
		States().erase(controller);
	});
	return *result;
}

std::shared_ptr<PanelModel> ChooseModelForChat(
		not_null<Window::SessionController*> controller,
		PeerData *peer) {
	auto &state = PanelStateFor(controller);
	const auto key = AccountKey(&controller->session().account());
	const auto peerId = peer ? peer->id : PeerId();
	if (const auto current = state.current.current()) {
		if (CoversChat(*current, key, peerId)) {
			return current;
		}
	}
	auto result = std::shared_ptr<PanelModel>();
	if (peerId) {
		if (auto session = AllSessions().lastForChat(key, peerId)) {
			result = PanelModel::ForSession(std::move(session));
		}
	}
	if (!result) {
		result = PanelModel::Pending({
			.scope = peerId ? SessionScope::Chat : SessionScope::Account,
			.accountKey = key,
			.peer = peerId,
		});
	}
	state.current = result;
	return result;
}

void HandlePanelClosed(not_null<Window::SessionController*> controller) {
	auto &state = PanelStateFor(controller);
	const auto current = state.current.current();
	if (!current) {
		return;
	} else if (current->scope() == SessionScope::Temporary) {
		state.current = nullptr;
	} else if (!state.pinned.current()) {
		current->stopWhenIdle();
	}
}

void StartNewSession(
		not_null<Window::SessionController*> controller,
		SessionScope scope) {
	const auto peer = ActivePeer(controller);
	if (!peer
		&& (scope == SessionScope::Temporary
			|| scope == SessionScope::Chat)) {
		return;
	}
	PanelStateFor(controller).current = PanelModel::Pending({
		.scope = scope,
		.accountKey = AccountKey(&controller->session().account()),
		.peer = peer,
	});
}

void ShowSavedSession(
		not_null<Window::SessionController*> controller,
		const QString &id) {
	if (auto session = AllSessions().open(id)) {
		PanelStateFor(controller).current = PanelModel::ForSession(
			std::move(session));
	}
}

} // namespace Owpengram::Claude
