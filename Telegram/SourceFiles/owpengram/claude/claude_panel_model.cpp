/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_panel_model.h"

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

rpl::producer<QString> PanelModel::modelNameValue() const {
	return _modelName.value();
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

void PanelModel::send(const QString &text) {
	const auto trimmed = text.trimmed();
	if (trimmed.isEmpty()) {
		return;
	}
	add(FeedEntryType::User, trimmed);
	_undelivered = trimmed;
	_thinking = true;
	ensureProcess();
	_process->send(trimmed);
	if (_process->running()) {
		_undelivered = QString();
	}
}

void PanelModel::reconnect() {
	ensureProcess();
	_process->reconnect();
}

void PanelModel::enableMcpAndRetry() {
	Access().setMcpEnabled(true);
	if (_undelivered.isEmpty()) {
		return;
	}
	// The text is already in the feed, send it again without adding.
	_thinking = true;
	ensureProcess();
	_process->send(_undelivered);
	if (_process->running()) {
		_undelivered = QString();
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
		_thinking = false;
		if (data.isError) {
			add(FeedEntryType::Error, data.text);
		}
	}, [&](const Init &data) {
		_modelName = data.model;
		_connectors = data.connectors;
		_connectorsChanges.fire({});
	}, [&](const Failed &data) {
		_thinking = false;
		add(FeedEntryType::Failed, data.message);
	}, [&](const NotInstalled &) {
		_thinking = false;
		add(FeedEntryType::NotInstalled, QString());
	}, [&](const McpDisabled &) {
		_thinking = false;
		add(FeedEntryType::McpDisabled, QString());
	}, [&](const Exited &) {
		// Silent: the next send() starts the process again.
		_thinking = false;
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
		if (current->accountKey() == key
			&& (!peerId || current->peer() == peerId)) {
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
