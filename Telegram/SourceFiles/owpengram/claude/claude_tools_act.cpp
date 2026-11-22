/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_tools.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "data/data_chat_participant_status.h"
#include "data/data_forum_topic.h"
#include "data/data_message_reaction_id.h"
#include "data/data_message_reactions.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_types.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "owpengram/claude/claude_confirm.h"

#include <algorithm>
#include <limits>
#include <optional>

namespace Owpengram::Claude {
namespace {

using Done = Fn<void(ToolResult)>;

constexpr auto kPreviewLimit = 300;
constexpr auto kMaxForwardCount = 100;

// What a tool call is going to do, computed WITHOUT doing it.
struct Plan {
	not_null<Main::Session*> session;
	// Effective level of the chat the action is performed in (the forward
	// target): decides between "ask the user" and "just do it".
	Level level = Level::Forbidden;
	// Shown to the user, includes the client name and the full payload.
	QString summary;
	// Executes the action synchronously. Valid only right after Prepare.
	Fn<ToolResult()> perform;
};

// Either a plan, or an immediate result (error, or "nothing to do").
struct Prepared {
	std::optional<Plan> plan;
	ToolResult result;
};

using PrepareFn = Prepared (*)(const ToolCall &call);

[[nodiscard]] Prepared Failed(ToolResult result) {
	return Prepared{ .result = std::move(result) };
}

[[nodiscard]] Prepared Failed(QString text) {
	return Failed(ErrorResult(std::move(text)));
}

[[nodiscard]] QString Truncated(QString text) {
	if (text.size() > kPreviewLimit) {
		text.truncate(kPreviewLimit);
		text += QChar(0x2026);
	}
	return text;
}

[[nodiscard]] QString Preview(not_null<HistoryItem*> item) {
	const auto text = item->originalText().text;
	if (!text.isEmpty()) {
		return Truncated(text);
	}
	const auto media = MessageToJson(item).value(u"media"_q).toObject();
	const auto type = media.value(u"type"_q).toString();
	return type.isEmpty() ? u"[...]"_q : (u"["_q + type + u"]"_q);
}

[[nodiscard]] int ArgMsgId(const QJsonValue &value) {
	if (!value.isDouble()) {
		return 0;
	}
	const auto number = value.toDouble();
	return (number >= 1 && number <= std::numeric_limits<int>::max())
		? int(number)
		: 0;
}

[[nodiscard]] HistoryItem *FindMessage(
		not_null<PeerData*> peer,
		int msgId) {
	// Only messages already loaded in the client: unknown ids are "not
	// found", just like for the read tools.
	const auto item = peer->owner().message(peer, MsgId(msgId));
	return (item && item->isRegular()) ? item : nullptr;
}

[[nodiscard]] bool CanSendTextsInReplyTo(
		not_null<PeerData*> peer,
		not_null<HistoryItem*> item) {
	// Same rule as ApiWrap::sendMessage uses before sending.
	const auto topic = peer->forumTopicFor(item->topicRootId());
	return topic ? Data::CanSendTexts(topic) : Data::CanSendTexts(peer);
}

// ---- R1: cross-account forward. ----
//
// owpengram_cross_forward is not part of this branch, so there is nothing to
// call. When it lands, this is the ONE place to wire it in (prepare the
// forward in the target account's window and return
// {status: "prepared_for_user"}).
[[nodiscard]] Prepared ForwardAcrossAccounts() {
	return Failed(u"cross-account forward is not available in this build"_q);
}

// ---- send_message ----

[[nodiscard]] Prepared PrepareSend(const ToolCall &call) {
	const auto &args = call.arguments;
	const auto text = ArgString(args, u"text"_q);
	if (text.trimmed().isEmpty()) {
		return Failed(u"invalid arguments: text is required"_q);
	}
	const auto peer = FindAllowedPeer(
		ArgString(args, u"account"_q),
		ArgString(args, u"chat"_q),
		Capability::Act);
	if (!peer) {
		return Failed(NotAllowedResult());
	} else if (!Data::CanSendTexts(peer)) {
		return Failed(u"cannot send messages to this chat"_q);
	}
	const auto session = &peer->session();
	const auto header = tr::lng_owpengram_claude_confirm_send(
		tr::now,
		lt_client,
		call.clientName,
		lt_chat,
		peer->name());
	return Prepared{ .plan = Plan{
		.session = session,
		.level = EffectiveLevel(peer),
		.summary = header + u"\n\n"_q + text,
		.perform = [=] {
			auto message = Api::MessageToSend(
				Api::SendAction(session->data().history(peer)));
			message.textWithTags.text = text;
			// Do not wipe the user's own draft in that chat.
			message.action.clearDraft = false;
			session->api().sendMessage(std::move(message));
			// ApiWrap::sendMessage returns nothing, the server id is not
			// known yet: no msg_id in the result.
			return OkResult(QJsonObject{ { u"status"_q, u"sent"_q } });
		},
	} };
}

// ---- reply ----

[[nodiscard]] Prepared PrepareReply(const ToolCall &call) {
	const auto &args = call.arguments;
	const auto text = ArgString(args, u"text"_q);
	const auto msgId = ArgMsgId(args.value(u"msg_id"_q));
	if (text.trimmed().isEmpty() || !msgId) {
		return Failed(u"invalid arguments: msg_id and text are required"_q);
	}
	const auto peer = FindAllowedPeer(
		ArgString(args, u"account"_q),
		ArgString(args, u"chat"_q),
		Capability::Act);
	const auto item = peer ? FindMessage(peer, msgId) : nullptr;
	if (!item) {
		return Failed(NotAllowedResult());
	} else if (!CanSendTextsInReplyTo(peer, item)) {
		return Failed(u"cannot send messages to this chat"_q);
	}
	const auto session = &peer->session();
	const auto header = tr::lng_owpengram_claude_confirm_reply(
		tr::now,
		lt_client,
		call.clientName,
		lt_chat,
		peer->name());
	const auto quoted = tr::lng_owpengram_claude_confirm_reply_to(
		tr::now,
		lt_text,
		Preview(item));
	const auto replyTo = item->id;
	return Prepared{ .plan = Plan{
		.session = session,
		.level = EffectiveLevel(peer),
		.summary = header + u"\n\n"_q + text + u"\n\n"_q + quoted,
		.perform = [=] {
			auto message = Api::MessageToSend(
				Api::SendAction(session->data().history(peer)));
			message.textWithTags.text = text;
			message.action.clearDraft = false;
			// The topic (for forums) is derived from the replied message.
			message.action.replyTo = {
				.messageId = { peer->id, replyTo },
			};
			session->api().sendMessage(std::move(message));
			return OkResult(QJsonObject{ { u"status"_q, u"sent"_q } });
		},
	} };
}

// ---- react ----

[[nodiscard]] Prepared PrepareReact(const ToolCall &call) {
	const auto &args = call.arguments;
	const auto emoji = ArgString(args, u"emoji"_q).trimmed();
	const auto msgId = ArgMsgId(args.value(u"msg_id"_q));
	const auto reaction = Data::ReactionId{ emoji };
	if (emoji.isEmpty() || reaction.paid() || !msgId) {
		return Failed(u"invalid arguments: msg_id and emoji are required"_q);
	}
	const auto peer = FindAllowedPeer(
		ArgString(args, u"account"_q),
		ArgString(args, u"chat"_q),
		Capability::Act);
	const auto item = peer ? FindMessage(peer, msgId) : nullptr;
	if (!item) {
		return Failed(NotAllowedResult());
	} else if (!item->canReact()) {
		return Failed(u"cannot react to this message"_q);
	}
	const auto session = &peer->session();
	const auto &available = session->data().reactions().list(
		Data::Reactions::Type::Active);
	if (!available.empty()
		&& std::none_of(available.begin(), available.end(), [&](
				const Data::Reaction &entry) {
			return entry.id == reaction;
		})) {
		return Failed(u"emoji is not an available reaction"_q);
	}
	const auto chosen = item->chosenReactions();
	if (std::find(chosen.begin(), chosen.end(), reaction) != chosen.end()) {
		// toggleReaction would REMOVE it: nothing to do instead.
		return Prepared{ .result = OkResult(QJsonObject{
			{ u"status"_q, u"already_reacted"_q },
		}) };
	}
	const auto header = tr::lng_owpengram_claude_confirm_react(
		tr::now,
		lt_client,
		call.clientName,
		lt_chat,
		peer->name(),
		lt_emoji,
		emoji);
	const auto target = tr::lng_owpengram_claude_confirm_react_to(
		tr::now,
		lt_text,
		Preview(item));
	return Prepared{ .plan = Plan{
		.session = session,
		.level = EffectiveLevel(peer),
		.summary = header + u"\n\n"_q + target,
		.perform = [=] {
			item->toggleReaction(reaction, HistoryReactionSource::Selector);
			return OkResult(QJsonObject{ { u"status"_q, u"reacted"_q } });
		},
	} };
}

// ---- forward ----

[[nodiscard]] Prepared PrepareForward(const ToolCall &call) {
	const auto &args = call.arguments;
	const auto idsValue = args.value(u"msg_ids"_q);
	if (!idsValue.isArray()) {
		return Failed(u"invalid arguments: msg_ids is required"_q);
	}
	auto ids = std::vector<int>();
	for (const auto &value : idsValue.toArray()) {
		const auto id = ArgMsgId(value);
		if (!id) {
			return Failed(u"invalid arguments: bad msg_ids"_q);
		}
		ids.push_back(id);
	}
	std::sort(ids.begin(), ids.end());
	ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
	if (ids.empty() || int(ids.size()) > kMaxForwardCount) {
		return Failed(u"invalid arguments: msg_ids must have 1 to 100 ids"_q);
	}

	// Both ends are checked on their own level; neither gives hints.
	const auto source = FindAllowedPeer(
		ArgString(args, u"from_account"_q),
		ArgString(args, u"from_chat"_q),
		Capability::ReadMessages);
	const auto target = FindAllowedPeer(
		ArgString(args, u"to_account"_q),
		ArgString(args, u"to_chat"_q),
		Capability::Act);
	if (!source || !target) {
		return Failed(NotAllowedResult());
	}
	const auto targetLevel = EffectiveLevel(target);
	if (!CanForward(EffectiveLevel(source), targetLevel)) {
		return Failed(NotAllowedResult());
	}
	if (&source->session() != &target->session()) {
		return ForwardAcrossAccounts();
	}
	const auto session = &target->session();

	auto items = HistoryItemsList();
	auto previews = QStringList();
	for (const auto id : ids) {
		const auto item = FindMessage(source, id);
		if (!item) {
			return Failed(NotAllowedResult());
		} else if (!item->allowsForward()) {
			return Failed(u"message cannot be forwarded"_q);
		}
		items.push_back(item);
		previews.push_back(u"• "_q + Preview(item));
	}
	if (!Data::CanSendTexts(target)) {
		return Failed(u"cannot send messages to this chat"_q);
	}
	const auto header = tr::lng_owpengram_claude_confirm_forward(
		tr::now,
		lt_client,
		call.clientName,
		lt_number,
		QString::number(items.size()),
		lt_from,
		source->name(),
		lt_chat,
		target->name());
	return Prepared{ .plan = Plan{
		.session = session,
		.level = targetLevel,
		.summary = header + u"\n\n"_q + previews.join(QChar('\n')),
		.perform = [=] {
			auto action = Api::SendAction(session->data().history(target));
			action.clearDraft = false;
			session->api().forwardMessages(
				Data::ResolvedForwardDraft{ .items = items },
				action);
			return OkResult(QJsonObject{
				{ u"status"_q, u"forwarded"_q },
				{ u"count"_q, int(items.size()) },
			});
		},
	} };
}

// ---- The common gate: prepare, confirm if needed, re-check, perform. ----

void Run(ToolCall call, Done done, PrepareFn prepare) {
	auto prepared = prepare(call);
	if (!prepared.plan) {
		done(std::move(prepared.result));
		return;
	}
	auto &plan = *prepared.plan;
	if (!NeedsConfirmation(plan.level)) {
		// Auto (anything lower was already rejected by Prepare).
		done(plan.perform());
		return;
	}
	const auto guarded = GuardedBySession(plan.session, std::move(done));
	RequestConfirmationWithReason(
		plan.session,
		call.clientName,
		plan.summary,
		[=](ConfirmResult result) {
			switch (result) {
			case ConfirmResult::Approved: {
				// The level (or the chat) could have changed while the box
				// was open: everything is checked again from the raw
				// arguments, nothing from before the box is reused.
				auto again = prepare(call);
				guarded(again.plan
					? again.plan->perform()
					: std::move(again.result));
			} break;
			case ConfirmResult::TimedOut:
				guarded(ErrorResult(u"denied: timeout"_q));
				break;
			case ConfirmResult::Unavailable:
				guarded(ErrorResult(u"denied: no window for this account"_q));
				break;
			case ConfirmResult::Denied:
				guarded(ErrorResult(u"denied by user"_q));
				break;
			}
		});
}

[[nodiscard]] QJsonObject Prop(const QString &type, const QString &text) {
	return QJsonObject{
		{ u"type"_q, type },
		{ u"description"_q, text },
	};
}

[[nodiscard]] QJsonObject Schema(
		QJsonObject properties,
		QStringList required) {
	return QJsonObject{
		{ u"type"_q, u"object"_q },
		{ u"properties"_q, std::move(properties) },
		{ u"required"_q, QJsonArray::fromStringList(required) },
	};
}

void AddAct(
		ToolRegistry &registry,
		QString name,
		QString description,
		QJsonObject schema,
		PrepareFn prepare) {
	registry.add({
		.name = std::move(name),
		.description = std::move(description),
		.inputSchema = std::move(schema),
		.handler = [=](ToolCall call, Done done) {
			Run(std::move(call), std::move(done), prepare);
		},
	});
}

} // namespace

void RegisterActTools(ToolRegistry &registry) {
	const auto account = Prop(u"string"_q, u"Account key from list_accounts"_q);
	const auto chat = Prop(u"string"_q, u"Chat id from list_chats"_q);
	const auto note = u" Requires the Act permission; may ask the user to "
		"confirm in the OwpenGram window and wait up to 2 minutes."_q;

	AddAct(
		registry,
		u"send_message"_q,
		u"Send a text message as the user."_q + note,
		Schema({
			{ u"account"_q, account },
			{ u"chat"_q, chat },
			{ u"text"_q, Prop(u"string"_q, u"Message text"_q) },
		}, { u"account"_q, u"chat"_q, u"text"_q }),
		PrepareSend);
	AddAct(
		registry,
		u"reply"_q,
		u"Reply to a message as the user."_q + note,
		Schema({
			{ u"account"_q, account },
			{ u"chat"_q, chat },
			{ u"msg_id"_q, Prop(u"integer"_q, u"Message to reply to"_q) },
			{ u"text"_q, Prop(u"string"_q, u"Reply text"_q) },
		}, { u"account"_q, u"chat"_q, u"msg_id"_q, u"text"_q }),
		PrepareReply);
	AddAct(
		registry,
		u"react"_q,
		u"Add an emoji reaction to a message as the user."_q + note,
		Schema({
			{ u"account"_q, account },
			{ u"chat"_q, chat },
			{ u"msg_id"_q, Prop(u"integer"_q, u"Message to react to"_q) },
			{ u"emoji"_q, Prop(u"string"_q, u"Reaction emoji"_q) },
		}, { u"account"_q, u"chat"_q, u"msg_id"_q, u"emoji"_q }),
		PrepareReact);
	AddAct(
		registry,
		u"forward"_q,
		u"Forward messages from one chat to another, as the user. The "
		"source chat needs Read, the target Act. Only forwarding within "
		"one account is supported."_q + note,
		Schema({
			{ u"from_account"_q, account },
			{ u"from_chat"_q, chat },
			{ u"msg_ids"_q, QJsonObject{
				{ u"type"_q, u"array"_q },
				{ u"items"_q, QJsonObject{ { u"type"_q, u"integer"_q } } },
				{ u"description"_q, u"Messages to forward (1 to 100)"_q },
			} },
			{ u"to_account"_q, account },
			{ u"to_chat"_q, chat },
		}, {
			u"from_account"_q,
			u"from_chat"_q,
			u"msg_ids"_q,
			u"to_account"_q,
			u"to_chat"_q,
		}),
		PrepareForward);
}

} // namespace Owpengram::Claude
