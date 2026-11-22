/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_tools.h"

#include "api/api_chat_participants.h"
#include "apiwrap.h"
#include "core/application.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "owpengram/claude/claude_access_store.h"
#include "owpengram/owpengram_servers.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <algorithm>
#include <memory>

namespace Owpengram::Claude {
namespace {

using Done = Fn<void(ToolResult)>;

[[nodiscard]] QJsonObject Prop(const QString &type, const QString &text) {
	return QJsonObject{
		{ u"type"_q, type },
		{ u"description"_q, text },
	};
}

[[nodiscard]] QJsonObject Schema(
		QJsonObject properties,
		QStringList required = {}) {
	auto result = QJsonObject{
		{ u"type"_q, u"object"_q },
		{ u"properties"_q, std::move(properties) },
	};
	if (!required.isEmpty()) {
		result.insert(u"required"_q, QJsonArray::fromStringList(required));
	}
	return result;
}

[[nodiscard]] QJsonObject AccountProp() {
	return Prop(u"string"_q, u"Account key from list_accounts"_q);
}

[[nodiscard]] QJsonObject ChatProp() {
	return Prop(u"string"_q, u"Chat id from list_chats"_q);
}

// Messages from any messages.Messages reply, as history items. Peers must be
// loaded (processUsers / processChats first), like Api::MessagesSearch does.
[[nodiscard]] std::vector<not_null<HistoryItem*>> ItemsFromReply(
		not_null<Main::Session*> session,
		const MTPmessages_Messages &result) {
	auto &owner = session->data();
	const auto convert = [&](const QVector<MTPMessage> &messages) {
		auto items = std::vector<not_null<HistoryItem*>>();
		for (const auto &message : messages) {
			if (!owner.peerLoaded(PeerFromMessage(message))
				|| !DateFromMessage(message)) {
				continue;
			}
			const auto item = owner.addNewMessage(
				message,
				MessageFlags(),
				NewMessageType::Existing);
			if (item) {
				items.push_back(item);
			}
		}
		return items;
	};
	return result.match([&](const MTPDmessages_messages &data) {
		owner.processUsers(data.vusers());
		owner.processChats(data.vchats());
		return convert(data.vmessages().v);
	}, [&](const MTPDmessages_messagesSlice &data) {
		owner.processUsers(data.vusers());
		owner.processChats(data.vchats());
		return convert(data.vmessages().v);
	}, [&](const MTPDmessages_channelMessages &data) {
		owner.processUsers(data.vusers());
		owner.processChats(data.vchats());
		return convert(data.vmessages().v);
	}, [](const MTPDmessages_messagesNotModified &) {
		return std::vector<not_null<HistoryItem*>>();
	});
}

void ListAccounts(ToolCall call, Done done) {
	auto list = QJsonArray();
	for (const auto session : AuthorizedSessions()) {
		if (!AccountVisible(session)) {
			continue;
		}
		list.push_back(QJsonObject{
			{ u"account"_q, SessionKey(session) },
			{ u"server"_q, CurrentServerForAccount(&session->account()).name },
			{ u"name"_q, session->user()->name() },
		});
	}
	done(OkResult(list));
}

void CurrentChat(ToolCall call, Done done) {
	const auto window = Core::App().activeWindow();
	const auto controller = window ? window->sessionController() : nullptr;
	const auto peer = controller
		? controller->activeChatCurrent().peer()
		: nullptr;
	if (!peer || !PeerAllows(peer, Capability::SeeChat)) {
		done(OkResult(QJsonValue(QJsonValue::Null)));
		return;
	}
	done(OkResult(QJsonObject{
		{ u"account"_q, SessionKey(&peer->session()) },
		{ u"chat"_q, ChatIdString(peer) },
		{ u"title"_q, peer->name() },
	}));
}

[[nodiscard]] QJsonValue MembersCount(not_null<PeerData*> peer) {
	if (const auto channel = peer->asChannel()) {
		if (channel->membersCountKnown()) {
			return channel->membersCount();
		}
	} else if (const auto chat = peer->asChat()) {
		return chat->count;
	}
	return QJsonValue(QJsonValue::Null);
}

void ListChats(ToolCall call, Done done) {
	const auto filter = ArgString(call.arguments, u"account"_q);
	const auto limit = ArgInt(call.arguments, u"limit"_q, 50, 1, 500);
	const auto offset = ArgInt(call.arguments, u"offset"_q, 0, 0, 1000000);
	auto sessions = std::vector<not_null<Main::Session*>>();
	if (filter.isEmpty()) {
		sessions = AuthorizedSessions();
	} else {
		const auto session = FindSession(filter);
		if (!session || !AccountVisible(session)) {
			done(NotAllowedResult());
			return;
		}
		sessions.push_back(session);
	}
	auto list = QJsonArray();
	auto index = 0;
	for (const auto session : sessions) {
		const auto key = SessionKey(session);
		for (const auto peer : VisibleChats(session)) {
			if (index++ < offset) {
				continue;
			} else if (list.size() >= limit) {
				break;
			}
			list.push_back(QJsonObject{
				{ u"account"_q, key },
				{ u"chat"_q, ChatIdString(peer) },
				{ u"title"_q, peer->name() },
				{ u"type"_q, PeerTypeName(peer) },
				{ u"members_count"_q, MembersCount(peer) },
				{ u"unread"_q, peer->owner().history(peer)->unreadCount() },
				{ u"level"_q, LevelString(Access().effective(peer)) },
			});
		}
	}
	done(OkResult(list));
}

void ChatInfo(ToolCall call, Done done) {
	const auto peer = FindAllowedPeer(
		ArgString(call.arguments, u"account"_q),
		ArgString(call.arguments, u"chat"_q),
		Capability::SeeChat);
	if (!peer) {
		done(NotAllowedResult());
		return;
	}
	auto result = QJsonObject{
		{ u"title"_q, peer->name() },
		{ u"type"_q, PeerTypeName(peer) },
		{ u"username"_q, peer->username() },
		{ u"members_count"_q, MembersCount(peer) },
	};
	if (PeerAllows(peer, Capability::ReadMessages)) {
		result.insert(u"about"_q, peer->about());
	}
	done(OkResult(result));
}

[[nodiscard]] QJsonObject MemberToJson(not_null<UserData*> user) {
	return QJsonObject{
		{ u"user"_q, ChatIdString(user) },
		{ u"name"_q, user->name() },
		{ u"username"_q, user->username() },
	};
}

void ChatMembers(ToolCall call, Done done) {
	const auto peer = FindAllowedPeer(
		ArgString(call.arguments, u"account"_q),
		ArgString(call.arguments, u"chat"_q),
		Capability::ReadMessages);
	if (!peer) {
		done(NotAllowedResult());
		return;
	}
	const auto limit = ArgInt(call.arguments, u"limit"_q, 200, 1, 200);
	auto list = QJsonArray();
	if (const auto user = peer->asUser()) {
		list.push_back(MemberToJson(peer->session().user()));
		if (!user->isSelf()) {
			list.push_back(MemberToJson(user));
		}
		done(OkResult(list));
	} else if (const auto chat = peer->asChat()) {
		for (const auto member : chat->participants) {
			if (list.size() >= limit) {
				break;
			}
			list.push_back(MemberToJson(member));
		}
		done(OkResult(list));
	} else if (const auto channel = peer->asChannel()) {
		if (!channel->isMegagroup() || !channel->canViewMembers()) {
			done(ErrorResult(u"members are not available"_q));
			return;
		}
		const auto session = &channel->session();
		const auto guarded = GuardedBySession(session, std::move(done));
		session->api().request(MTPchannels_GetParticipants(
			channel->inputChannel(),
			MTP_channelParticipantsRecent(),
			MTP_int(0), // offset
			MTP_int(limit),
			MTP_long(0) // hash
		)).done([=](const MTPchannels_ChannelParticipants &result) {
			result.match([&](const MTPDchannels_channelParticipants &data) {
				const auto &parsed = Api::ChatParticipants::Parse(
					channel,
					data);
				auto list = QJsonArray();
				for (const auto &participant : parsed.list) {
					if (!participant.isUser()) {
						continue;
					}
					const auto user = session->data().userLoaded(
						participant.userId());
					if (user) {
						list.push_back(MemberToJson(user));
					}
				}
				guarded(OkResult(list));
			}, [&](const MTPDchannels_channelParticipantsNotModified &) {
				guarded(ErrorResult(u"members are not available"_q));
			});
		}).fail([=](const MTP::Error &error) {
			guarded(ErrorResult(u"request failed: "_q + error.type()));
		}).send();
	} else {
		done(NotAllowedResult());
	}
}

void ReadHistory(ToolCall call, Done done) {
	const auto peer = FindAllowedPeer(
		ArgString(call.arguments, u"account"_q),
		ArgString(call.arguments, u"chat"_q),
		Capability::ReadMessages);
	if (!peer) {
		done(NotAllowedResult());
		return;
	}
	const auto limit = ArgInt(call.arguments, u"limit"_q, 50, 1, 200);
	const auto before = ArgInt(
		call.arguments,
		u"before_msg_id"_q,
		0,
		0,
		0x3FFFFFFF);
	const auto session = &peer->session();
	const auto guarded = GuardedBySession(session, std::move(done));
	session->api().request(MTPmessages_GetHistory(
		peer->input(),
		MTP_int(before), // offset_id
		MTP_int(0), // offset_date
		MTP_int(0), // add_offset
		MTP_int(limit),
		MTP_int(0), // max_id
		MTP_int(0), // min_id
		MTP_long(0) // hash
	)).done([=](const MTPmessages_Messages &result) {
		auto list = QJsonArray();
		for (const auto item : ItemsFromReply(session, result)) {
			list.push_back(MessageToJson(item));
		}
		guarded(OkResult(list));
	}).fail([=](const MTP::Error &error) {
		guarded(ErrorResult(u"request failed: "_q + error.type()));
	}).send();
}

// Results are accumulated across accounts; `finished` is called when all
// accounts answered (each account's answer is already session-guarded).
struct SearchState {
	int pending = 0;
	std::vector<std::pair<TimeId, QJsonObject>> found;
	int limit = 50;
	Done done;
};

void SearchFinishOne(
		const std::shared_ptr<SearchState> &state,
		const QJsonValue &part) {
	for (const auto &value : part.toArray()) {
		const auto object = value.toObject();
		state->found.emplace_back(
			QDateTime::fromString(
				object.value(u"date"_q).toString(),
				Qt::ISODate).toSecsSinceEpoch(),
			object);
	}
	if (--state->pending > 0) {
		return;
	}
	std::stable_sort(
		state->found.begin(),
		state->found.end(),
		[](const auto &a, const auto &b) { return a.first > b.first; });
	auto list = QJsonArray();
	for (const auto &[date, object] : state->found) {
		if (list.size() >= state->limit) {
			break;
		}
		list.push_back(object);
	}
	state->done(OkResult(list));
}

[[nodiscard]] QJsonObject WithChat(
		not_null<HistoryItem*> item,
		const QString &account) {
	auto result = MessageToJson(item);
	result.insert(u"account"_q, account);
	result.insert(u"chat"_q, ChatIdString(item->history()->peer));
	return result;
}

// Server search, then filter by ReadMessages AFTER the reply.
void SearchAccount(
		not_null<Main::Session*> session,
		const QString &query,
		int limit,
		Fn<void(ToolResult)> finished) {
	const auto guarded = GuardedBySession(session, std::move(finished));
	const auto key = SessionKey(session);
	session->api().request(MTPmessages_SearchGlobal(
		MTP_flags(MTPmessages_SearchGlobal::Flag::f_folder_id),
		MTP_int(0), // folder_id
		MTPInputChannel(),
		MTP_string(query),
		MTP_inputMessagesFilterEmpty(),
		MTP_int(0), // min_date
		MTP_int(0), // max_date
		MTP_int(0), // offset_rate
		MTP_inputPeerEmpty(),
		MTP_int(0), // offset_id
		MTP_int(limit)
	)).done([=](const MTPmessages_Messages &result) {
		auto list = QJsonArray();
		for (const auto item : ItemsFromReply(session, result)) {
			if (PeerAllows(item->history()->peer, Capability::ReadMessages)) {
				list.push_back(WithChat(item, key));
			}
		}
		guarded(OkResult(list));
	}).fail([=](const MTP::Error &error) {
		// A failing account must not hide results of the others.
		guarded(OkResult(QJsonArray()));
	}).send();
}

void SearchChat(
		not_null<PeerData*> peer,
		const QString &query,
		int limit,
		Done done) {
	const auto session = &peer->session();
	const auto guarded = GuardedBySession(session, std::move(done));
	const auto key = SessionKey(session);
	session->api().request(MTPmessages_Search(
		MTP_flags(MTPmessages_Search::Flag()),
		peer->input(),
		MTP_string(query),
		MTP_inputPeerEmpty(), // from_id
		MTPInputPeer(), // saved_peer_id
		MTPVector<MTPReaction>(), // saved_reaction
		MTPint(), // top_msg_id
		MTP_inputMessagesFilterEmpty(),
		MTP_int(0), // min_date
		MTP_int(0), // max_date
		MTP_int(0), // offset_id
		MTP_int(0), // add_offset
		MTP_int(limit),
		MTP_int(0), // max_id
		MTP_int(0), // min_id
		MTP_long(0) // hash
	)).done([=](const MTPmessages_Messages &result) {
		auto list = QJsonArray();
		for (const auto item : ItemsFromReply(session, result)) {
			list.push_back(WithChat(item, key));
		}
		guarded(OkResult(list));
	}).fail([=](const MTP::Error &error) {
		guarded(ErrorResult(u"request failed: "_q + error.type()));
	}).send();
}

void SearchMessages(ToolCall call, Done done) {
	const auto query = ArgString(call.arguments, u"query"_q);
	const auto scope = ArgString(call.arguments, u"scope"_q);
	const auto limit = ArgInt(call.arguments, u"limit"_q, 50, 1, 100);
	if (query.isEmpty()) {
		done(ErrorResult(u"query is required"_q));
		return;
	}
	if (scope == u"chat"_q) {
		const auto peer = FindAllowedPeer(
			ArgString(call.arguments, u"account"_q),
			ArgString(call.arguments, u"chat"_q),
			Capability::ReadMessages);
		if (!peer) {
			done(NotAllowedResult());
			return;
		}
		SearchChat(peer, query, limit, std::move(done));
		return;
	}
	auto sessions = std::vector<not_null<Main::Session*>>();
	if (scope == u"account"_q) {
		const auto session = FindSession(
			ArgString(call.arguments, u"account"_q));
		if (!session || !AccountVisible(session)) {
			done(NotAllowedResult());
			return;
		}
		sessions.push_back(session);
	} else if (scope == u"all"_q) {
		for (const auto session : AuthorizedSessions()) {
			if (AccountVisible(session)) {
				sessions.push_back(session);
			}
		}
	} else {
		done(ErrorResult(u"scope must be chat, account or all"_q));
		return;
	}
	if (sessions.empty()) {
		done(OkResult(QJsonArray()));
		return;
	}
	const auto state = std::make_shared<SearchState>();
	state->pending = int(sessions.size());
	state->limit = limit;
	state->done = std::move(done);
	for (const auto session : sessions) {
		SearchAccount(session, query, limit, [=](ToolResult result) {
			SearchFinishOne(
				state,
				result.isError ? QJsonValue(QJsonArray()) : result.content);
		});
	}
}

} // namespace

void RegisterReadTools(ToolRegistry &registry) {
	registry.add({
		.name = u"list_accounts"_q,
		.description = u"List the accounts Claude may see."_q,
		.inputSchema = Schema({}),
		.handler = ListAccounts,
	});
	registry.add({
		.name = u"current_chat"_q,
		.description = u"The chat open in the focused window, or null."_q,
		.inputSchema = Schema({}),
		.handler = CurrentChat,
	});
	registry.add({
		.name = u"list_chats"_q,
		.description = u"List visible chats (paged by offset)."_q,
		.inputSchema = Schema({
			{ u"account"_q, AccountProp() },
			{ u"limit"_q, Prop(u"integer"_q, u"Default 50"_q) },
			{ u"offset"_q, Prop(u"integer"_q, u"Default 0"_q) },
		}),
		.handler = ListChats,
	});
	registry.add({
		.name = u"chat_info"_q,
		.description = u"Title, type, username, members count, about."_q,
		.inputSchema = Schema({
			{ u"account"_q, AccountProp() },
			{ u"chat"_q, ChatProp() },
		}, { u"account"_q, u"chat"_q }),
		.handler = ChatInfo,
	});
	registry.add({
		.name = u"chat_members"_q,
		.description = u"List chat members (needs read access)."_q,
		.inputSchema = Schema({
			{ u"account"_q, AccountProp() },
			{ u"chat"_q, ChatProp() },
			{ u"limit"_q, Prop(u"integer"_q, u"Default 200"_q) },
		}, { u"account"_q, u"chat"_q }),
		.handler = ChatMembers,
	});
	registry.add({
		.name = u"read_history"_q,
		.description = u"Read messages, newest first (needs read access)."_q,
		.inputSchema = Schema({
			{ u"account"_q, AccountProp() },
			{ u"chat"_q, ChatProp() },
			{ u"before_msg_id"_q, Prop(u"integer"_q, u"Page offset"_q) },
			{ u"limit"_q, Prop(u"integer"_q, u"Default 50, max 200"_q) },
		}, { u"account"_q, u"chat"_q }),
		.handler = ReadHistory,
	});
	registry.add({
		.name = u"search_messages"_q,
		.description = u"Search messages in a chat, an account or all."_q,
		.inputSchema = Schema({
			{ u"query"_q, Prop(u"string"_q, u"Search text"_q) },
			{ u"scope"_q, QJsonObject{
				{ u"type"_q, u"string"_q },
				{ u"enum"_q, QJsonArray{
					u"chat"_q, u"account"_q, u"all"_q } },
			} },
			{ u"account"_q, AccountProp() },
			{ u"chat"_q, ChatProp() },
			{ u"limit"_q, Prop(u"integer"_q, u"Default 50"_q) },
		}, { u"query"_q, u"scope"_q }),
		.handler = SearchMessages,
	});
}

} // namespace Owpengram::Claude
