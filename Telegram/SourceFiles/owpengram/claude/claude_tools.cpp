/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_tools.h"

#include "core/application.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "owpengram/claude/claude_access_store.h"

#include <QtCore/QDateTime>
#include <QtCore/QTimeZone>

#include <algorithm>
#include <memory>

namespace Owpengram::Claude {
namespace {

[[nodiscard]] QJsonObject MediaToJson(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return QJsonObject();
	}
	auto type = QString();
	auto name = QString();
	auto size = qint64(0);
	if (const auto document = media->document()) {
		name = document->filename();
		size = document->size;
		if (document->sticker()) {
			type = u"sticker"_q;
		} else if (document->isVoiceMessage()) {
			type = u"voice"_q;
		} else if (document->isVideoMessage()) {
			type = u"video_message"_q;
		} else if (document->isGifv() || document->isAnimation()) {
			type = u"animation"_q;
		} else if (document->isSong()) {
			type = u"audio"_q;
		} else if (document->isVideoFile()) {
			type = u"video"_q;
		} else {
			type = u"file"_q;
		}
	} else if (media->photo()) {
		type = u"photo"_q;
	} else if (media->sharedContact()) {
		type = u"contact"_q;
	} else if (media->poll()) {
		type = u"poll"_q;
	} else if (media->location()) {
		type = u"location"_q;
	} else if (media->webpage()) {
		type = u"webpage"_q;
	} else {
		type = u"other"_q;
	}
	return QJsonObject{
		{ u"type"_q, type },
		{ u"name"_q, name },
		{ u"size"_q, double(size) },
	};
}

} // namespace

void ToolRegistry::add(ToolDef def) {
	_tools.push_back(std::move(def));
}

bool ToolRegistry::panelOnly(const QString &name) const {
	return name == u"approve"_q;
}

void ToolRegistry::call(ToolCall call, Fn<void(ToolResult)> done) {
	for (const auto &tool : _tools) {
		if (tool.name == call.name) {
			if (panelOnly(tool.name) && !call.session) {
				// Indistinguishable from an unknown tool.
				break;
			}
			tool.handler(std::move(call), std::move(done));
			return;
		}
	}
	done(ErrorResult(u"unknown tool: "_q + call.name));
}

ToolRegistry &Tools() {
	static auto result = [] {
		auto registry = std::make_unique<ToolRegistry>();
		RegisterReadTools(*registry);
		RegisterActTools(*registry);
		RegisterApproveTool(*registry);
		return registry;
	}();
	return *result;
}

QString NotAllowedText() {
	return u"not found or not allowed"_q;
}

ToolResult OkResult(QJsonValue content) {
	auto result = ToolResult();
	result.content = std::move(content);
	return result;
}

ToolResult ErrorResult(QString text) {
	auto result = ToolResult();
	result.isError = true;
	result.errorText = std::move(text);
	return result;
}

ToolResult NotAllowedResult() {
	return ErrorResult(NotAllowedText());
}

std::vector<not_null<Main::Session*>> AuthorizedSessions() {
	auto result = std::vector<not_null<Main::Session*>>();
	for (const auto &entry : Core::App().domain().accounts()) {
		const auto account = entry.account.get();
		if (account->sessionExists()) {
			result.push_back(&account->session());
		}
	}
	return result;
}

QString SessionKey(not_null<Main::Session*> session) {
	return AccountKey(&session->account());
}

Main::Session *FindSession(const QString &accountKey) {
	if (accountKey.isEmpty()) {
		return nullptr;
	}
	for (const auto session : AuthorizedSessions()) {
		if (SessionKey(session) == accountKey) {
			return session;
		}
	}
	return nullptr;
}

Level EffectiveLevel(not_null<PeerData*> peer) {
	auto result = Access().effective(peer);
	// A basic group migrated to a supergroup keeps old messages: the
	// supergroup's (possibly stricter) level applies as well.
	if (const auto channel = peer->migrateTo()) {
		result = std::min(result, Access().effective(channel));
	}
	return result;
}

bool PeerAllows(not_null<PeerData*> peer, Capability capability) {
	return Allows(EffectiveLevel(peer), capability);
}

bool CallAllowsAccount(const ToolCall &call, const QString &accountKey) {
	if (!call.session) {
		return true;
	}
	const auto &info = call.session.value();
	return (info.scope == SessionScope::All)
		|| (accountKey == info.accountKey);
}

bool CallAllowsPeer(const ToolCall &call, not_null<PeerData*> peer) {
	return SessionAllowsPeer(call.session, peer);
}

bool SessionAllowsPeer(
		const std::optional<SessionInfo> &session,
		not_null<PeerData*> peer) {
	if (!session) {
		return true;
	}
	const auto &info = session.value();
	return ScopeAllows(
		info.scope,
		info.accountKey.toStdString(),
		uint64_t(info.peer.value),
		SessionKey(&peer->session()).toStdString(),
		uint64_t(peer->id.value));
}

PeerData *FindAllowedPeer(
		const ToolCall &call,
		const QString &accountKey,
		const QString &chatId,
		Capability capability) {
	const auto session = FindSession(accountKey);
	if (!session) {
		return nullptr;
	}
	auto ok = false;
	const auto value = chatId.toULongLong(&ok);
	if (!ok || !value) {
		return nullptr;
	}
	const auto peer = session->data().peerLoaded(PeerId(value));
	if (!peer
		|| !CallAllowsPeer(call, peer)
		|| !PeerAllows(peer, capability)) {
		return nullptr;
	}
	return peer;
}

std::vector<not_null<PeerData*>> VisibleChats(
		not_null<Main::Session*> session) {
	auto result = std::vector<not_null<PeerData*>>();
	const auto list = session->data().chatsList()->indexed();
	for (const auto row : list->all()) {
		const auto history = row->history();
		if (!history) {
			continue;
		}
		const auto peer = history->peer;
		if (PeerAllows(peer, Capability::SeeChat)) {
			result.push_back(peer);
		}
	}
	return result;
}

bool AccountVisible(not_null<Main::Session*> session) {
	const auto key = SessionKey(session);
	if (key.isEmpty()) {
		return false;
	}
	if (Allows(Access().accountDefault(key), Capability::SeeChat)) {
		return true;
	}
	return Access().hasOverrideAtLeast(key, Level::Visible);
}

QString ChatIdString(not_null<PeerData*> peer) {
	return QString::number(qulonglong(peer->id.value));
}

QString PeerTypeName(not_null<PeerData*> peer) {
	if (peer->isUser()) {
		return peer->isBot() ? u"bot"_q : u"user"_q;
	} else if (peer->isChat()) {
		return u"group"_q;
	} else if (peer->isMegagroup()) {
		return u"supergroup"_q;
	}
	return u"channel"_q;
}

QString LevelString(Level level) {
	const auto view = Serialize(level);
	return QString::fromLatin1(view.data(), int(view.size()));
}

QString IsoDate(TimeId date) {
	return QDateTime::fromSecsSinceEpoch(date, QTimeZone::utc()).toString(Qt::ISODate);
}

QString ArgString(const QJsonObject &args, const QString &key) {
	const auto value = args.value(key);
	if (value.isString()) {
		return value.toString();
	} else if (value.isDouble()) {
		return QString::number(qlonglong(value.toDouble()));
	}
	return QString();
}

int ArgInt(
		const QJsonObject &args,
		const QString &key,
		int fallback,
		int min,
		int max) {
	const auto value = args.value(key);
	const auto result = value.isDouble()
		? int(std::clamp(value.toDouble(), double(min), double(max)))
		: value.isString()
		? value.toString().toInt()
		: fallback;
	return std::clamp(result, min, max);
}

QJsonObject MessageToJson(not_null<HistoryItem*> item) {
	auto result = QJsonObject{
		{ u"msg_id"_q, double(item->id.bare) },
		{ u"date"_q, IsoDate(item->date()) },
		{ u"from"_q, item->from()->name() },
		{ u"text"_q, item->originalText().text },
	};
	if (const auto reply = item->replyToId()) {
		result.insert(u"reply_to"_q, double(reply.bare));
	}
	const auto media = MediaToJson(item);
	if (!media.isEmpty()) {
		result.insert(u"media"_q, media);
	}
	return result;
}

Fn<void(ToolResult)> GuardedBySession(
		not_null<Main::Session*> session,
		Fn<void(ToolResult)> done) {
	struct State {
		Fn<void(ToolResult)> done;
		rpl::lifetime lifetime; // Session-destroyed subscription.
	};
	const auto state = std::make_shared<State>();
	state->done = std::move(done);
	const auto finish = [](
			const std::shared_ptr<State> &state,
			ToolResult result) {
		state->lifetime.destroy();
		if (!state->done) {
			return;
		}
		const auto callback = std::move(state->done);
		state->done = nullptr;
		callback(std::move(result));
	};
	// Per-call subscription, released in finish (no growth on the
	// long-lived session lifetime).
	session->account().sessionChanges(
	) | rpl::filter([](Main::Session *value) {
		return !value;
	}) | rpl::on_next([weak = std::weak_ptr<State>(state), finish](
			Main::Session*) {
		if (const auto strong = weak.lock()) {
			finish(strong, ErrorResult(u"account session closed"_q));
		}
	}, state->lifetime);
	return [=](ToolResult result) {
		finish(state, std::move(result));
	};
}

} // namespace Owpengram::Claude
