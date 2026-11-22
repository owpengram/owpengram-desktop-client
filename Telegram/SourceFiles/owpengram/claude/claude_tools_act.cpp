/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_tools.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "core/application.h"
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
const auto kInvisibleText = QString::fromLatin1(
	"text contains invisible or direction-control characters");

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

// Invisible and direction-changing characters: zero-width, bidi marks and
// overrides / isolates, line / paragraph separators, word joiner and
// invisible operators, Mongolian vowel separator, BOM.
[[nodiscard]] bool IsInvisibleOrDirection(QChar ch) {
	const auto code = ch.unicode();
	return (code >= 0x200B && code <= 0x200F)
		|| (code >= 0x202A && code <= 0x202E)
		|| (code >= 0x2060 && code <= 0x2064)
		|| (code >= 0x2066 && code <= 0x2069)
		|| code == 0x061C
		|| code == 0x180E
		|| code == 0x2028
		|| code == 0x2029
		|| code == 0xFEFF;
}

[[nodiscard]] bool HasInvisibleOrDirection(const QString &text) {
	return std::any_of(text.begin(), text.end(), IsInvisibleOrDirection);
}

// Text Claude wants to SEND: it is sent exactly as it is shown to the user
// (line breaks normalized to \n first), so anything that could look
// different to the recipient is refused instead of cleaned.
[[nodiscard]] QString NormalizedNewlines(QString text) {
	return text.replace(u"\r\n"_q, u"\n"_q).replace(QChar('\r'), QChar('\n'));
}

// Everything else Claude (or other chat participants) controls is shown to
// the user only after this: invisible / direction-changing characters removed.
[[nodiscard]] QString Sanitized(const QString &text) {
	auto result = QString();
	result.reserve(text.size());
	for (const auto ch : text) {
		if (IsInvisibleOrDirection(ch) || ch == QChar('\r')) {
			continue;
		}
		result += ch;
	}
	// No more than two consecutive blank lines (three newlines).
	while (result.contains(u"\n\n\n"_q)) {
		result.replace(u"\n\n\n"_q, u"\n\n"_q);
	}
	return result;
}

[[nodiscard]] QString Truncated(QString text, int limit) {
	if (text.size() > limit) {
		text.truncate(limit);
		text += QChar(0x2026);
	}
	return text;
}

// Untrusted text as an indented block, so that a line like "In reply to:"
// inside it can not be mistaken for the application's own lines.
[[nodiscard]] QString Quoted(const QString &text) {
	auto lines = Sanitized(text).split(QChar('\n'));
	for (auto &line : lines) {
		line = QChar(0x2502) + u" "_q + line;
	}
	return lines.join(QChar('\n'));
}

// Exact text (already checked by HasInvisibleOrDirection), as an indented
// block, nothing removed or collapsed.
[[nodiscard]] QString QuotedExact(const QString &text) {
	auto lines = text.split(QChar('\n'));
	for (auto &line : lines) {
		line = QChar(0x2502) + u" "_q + line;
	}
	return lines.join(QChar('\n'));
}

[[nodiscard]] QString SingleLine(QString text) {
	return Sanitized(text).replace(QChar('\n'), QChar(' '));
}

[[nodiscard]] QString Preview(not_null<HistoryItem*> item) {
	const auto text = item->originalText().text;
	if (!text.isEmpty()) {
		return Truncated(SingleLine(text), kPreviewLimit);
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
	const auto text = NormalizedNewlines(ArgString(args, u"text"_q));
	if (text.trimmed().isEmpty()) {
		return Failed(u"invalid arguments: text is required"_q);
	} else if (HasInvisibleOrDirection(text)) {
		return Failed(kInvisibleText);
	}
	const auto peer = FindAllowedPeer(
		call,
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
		SingleLine(peer->name()));
	return Prepared{ .plan = Plan{
		.session = session,
		.level = EffectiveLevel(peer),
		.summary = header + u"\n"_q + QuotedExact(text),
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
	const auto text = NormalizedNewlines(ArgString(args, u"text"_q));
	const auto msgId = ArgMsgId(args.value(u"msg_id"_q));
	if (text.trimmed().isEmpty() || !msgId) {
		return Failed(u"invalid arguments: msg_id and text are required"_q);
	} else if (HasInvisibleOrDirection(text)) {
		return Failed(kInvisibleText);
	}
	const auto peer = FindAllowedPeer(
		call,
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
		SingleLine(peer->name()));
	const auto quoted = tr::lng_owpengram_claude_confirm_reply_to(tr::now);
	const auto replyTo = item->id;
	return Prepared{ .plan = Plan{
		.session = session,
		.level = EffectiveLevel(peer),
		.summary = header
			+ u"\n"_q + QuotedExact(text)
			+ u"\n"_q + quoted
			+ u"\n"_q + Quoted(Preview(item)),
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
		call,
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
		SingleLine(peer->name()),
		lt_emoji,
		Sanitized(emoji));
	const auto target = tr::lng_owpengram_claude_confirm_react_to(tr::now);
	return Prepared{ .plan = Plan{
		.session = session,
		.level = EffectiveLevel(peer),
		.summary = header + u"\n"_q + target + u"\n"_q + Quoted(Preview(item)),
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
		call,
		ArgString(args, u"from_account"_q),
		ArgString(args, u"from_chat"_q),
		Capability::ReadMessages);
	const auto target = FindAllowedPeer(
		call,
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
		SingleLine(source->name()),
		lt_chat,
		SingleLine(target->name()));
	return Prepared{ .plan = Plan{
		.session = session,
		.level = targetLevel,
		.summary = header + u"\n"_q + Quoted(previews.join(QChar('\n'))),
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
	// The client name is chosen by the client: sanitize, cap, and mark it.
	auto name = Truncated(SingleLine(call.clientName).trimmed(), 64);
	if (name.isEmpty()) {
		name = u"unknown"_q;
	}
	call.clientName = u"MCP client: "_q + name;

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
	const auto seen = plan.summary;
	RequestConfirmationWithReason(
		plan.session,
		call.clientName,
		seen,
		[=](ConfirmResult result) {
			switch (result) {
			case ConfirmResult::Approved: {
				if (Core::App().passcodeLocked()) {
					guarded(ErrorResult(u"locked"_q));
					break;
				}
				// The level (or the chat) could have changed while the box
				// was open: everything is checked again from the raw
				// arguments, nothing from before the box is reused.
				auto again = prepare(call);
				if (!again.plan) {
					guarded(std::move(again.result));
				} else if (again.plan->summary != seen) {
					// What would be done is not what the user saw.
					guarded(ErrorResult(
						u"changed while waiting for confirmation; retry"_q));
				} else {
					guarded(again.plan->perform());
				}
			} break;
			case ConfirmResult::TimedOut:
				guarded(ErrorResult(u"denied: timeout"_q));
				break;
			case ConfirmResult::Unavailable:
				guarded(ErrorResult(u"denied: no window for this account"_q));
				break;
			case ConfirmResult::SessionClosed:
				guarded(ErrorResult(u"denied: account session closed"_q));
				break;
			case ConfirmResult::Cancelled:
				guarded(ErrorResult(u"denied: client disconnected"_q));
				break;
			case ConfirmResult::Denied:
				guarded(ErrorResult(u"denied by user"_q));
				break;
			}
		},
		call.cancelled);
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
		u"Send a text message as the user. A message that is just one dice "
		"emoji is sent as an animated dice. Forum chats are not supported "
		"yet."_q + note,
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
		"one account is supported. Forum chats are not supported "
		"yet. status \"forwarded\" means the request was sent, not that "
		"delivery is confirmed."_q + note,
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
