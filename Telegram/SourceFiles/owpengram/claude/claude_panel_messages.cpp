/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_panel_messages.h"

#include "lang/lang_keys.h"
#include "owpengram/claude/claude_panel_model.h"
#include "owpengram/claude/claude_sessions.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "styles/style_claude.h"

#include <QtCore/QRegularExpression>

namespace Owpengram::Claude {
namespace {

constexpr auto kInstallCommand = "npm install -g @anthropic-ai/claude-code";
constexpr auto kInstallUrl = "https://claude.com/claude-code";

// Claude writes GitHub-flavored markdown: headings and "*"/"-" bullets are
// turned into bold lines and "•" before the Telegram markdown parser runs.
[[nodiscard]] TextWithEntities PrepareAssistantText(const QString &text) {
	static const auto heading = QRegularExpression(
		u"^#{1,6}\\s+(.+?)\\s*#*\\s*$"_q,
		QRegularExpression::MultilineOption);
	static const auto bullet = QRegularExpression(
		u"^(\\s*)[*\\-+]\\s+"_q,
		QRegularExpression::MultilineOption);
	auto prepared = text;
	prepared.replace(heading, u"**\\1**"_q);
	prepared.replace(bullet, QString::fromUtf8("\\1\xE2\x80\xA2 "));

	// "**bold**" is cut out here: the Telegram parser skips it next to
	// non-separators ("**Як:**", "**Що потрібно:** 3 ...").
	static const auto bold = QRegularExpression(u"\\*\\*([^*\\n]+?)\\*\\*"_q);
	const auto parse = [](const QString &part) {
		return TextUtilities::ParseEntities(
			part,
			TextParseLinks | TextParseMarkdown);
	};
	auto result = TextWithEntities();
	auto from = 0;
	auto matches = bold.globalMatch(prepared);
	while (matches.hasNext()) {
		const auto match = matches.next();
		result.append(parse(prepared.mid(from, match.capturedStart() - from)));
		result.append(Ui::Text::Bold(match.captured(1)));
		from = match.capturedEnd();
	}
	result.append(parse(prepared.mid(from)));
	return result;
}

[[nodiscard]] TextWithEntities NoticeText(const FeedEntry &entry) {
	switch (entry.type) {
	case FeedEntryType::McpDisabled:
		return tr::lng_owpengram_claude_panel_mcp_disabled(
			tr::now,
			tr::marked);
	case FeedEntryType::NotInstalled:
		return tr::lng_owpengram_claude_panel_not_installed(
			tr::now,
			lt_command,
			Ui::Text::Wrapped(
				tr::marked(QString::fromLatin1(kInstallCommand)),
				EntityType::Code),
			lt_link,
			tr::link(
				QString::fromLatin1(kInstallUrl),
				QString::fromLatin1(kInstallUrl)),
			tr::marked);
	case FeedEntryType::Failed:
		return tr::marked(
			tr::lng_owpengram_claude_panel_failed(tr::now)
			+ (entry.text.isEmpty() ? QString() : ('\n' + entry.text)));
	default:
		return tr::marked(entry.text);
	}
}

} // namespace

PanelMessages::PanelMessages(
	QWidget *parent,
	std::shared_ptr<PanelModel> model)
: VerticalLayout(parent)
, _model(std::move(model))
, _entries(add(
	object_ptr<Ui::VerticalLayout>(this),
	style::margins(),
	style::al_justify)) {
	const auto &padding = st::owpengramClaudePanelFeedPadding;
	const auto empty = insert(
		0,
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			this,
			object_ptr<Ui::FlatLabel>(
				this,
				tr::lng_owpengram_claude_panel_empty(),
				st::owpengramClaudePanelEmpty),
			padding),
		style::al_justify);
	empty->toggleOn(_empty.value(), anim::type::instant);

	const auto thinking = add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			this,
			object_ptr<Ui::FlatLabel>(
				this,
				tr::lng_owpengram_claude_panel_thinking(),
				st::owpengramClaudePanelSubText),
			style::margins(
				padding.left(),
				0,
				padding.right(),
				padding.bottom())),
		style::margins(),
		style::al_justify);
	thinking->toggleOn(_model->thinkingValue(), anim::type::instant);

	for (const auto &entry : _model->entries()) {
		addEntry(entry);
	}
	_model->appended(
	) | rpl::on_next([=](int index) {
		addEntry(_model->entries()[index]);
	}, lifetime());
}

void PanelMessages::addEntry(const FeedEntry &entry) {
	_empty = false;
	switch (entry.type) {
	case FeedEntryType::User: addUser(entry.text); break;
	case FeedEntryType::Assistant: addAssistant(entry.text); break;
	case FeedEntryType::Tool: addTool(entry.text); break;
	case FeedEntryType::McpDisabled:
	case FeedEntryType::NotInstalled:
	case FeedEntryType::Failed:
	case FeedEntryType::Error: addNotice(entry); break;
	}
}

void PanelMessages::addUser(const QString &text) {
	const auto &padding = st::owpengramClaudePanelFeedPadding;
	const auto bubble = _entries->add(
		object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
			_entries,
			object_ptr<Ui::FlatLabel>(
				_entries,
				text,
				st::owpengramClaudePanelText),
			st::owpengramClaudePanelBubblePadding),
		style::margins(
			padding.left() + st::owpengramClaudePanelBubbleLeftSkip,
			padding.top(),
			padding.right(),
			0),
		// Not al_right: VerticalLayout tests al_left first and al_right
		// shares its AlignVCenter bit, so al_right lays out on the left.
		style::al_topright);
	bubble->entity()->setSelectable(true);
	bubble->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(bubble);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::owpengramClaudePanelBubbleBg);
		const auto radius = st::owpengramClaudePanelBubbleRadius;
		p.drawRoundedRect(bubble->rect(), radius, radius);
	}, bubble->lifetime());
}

void PanelMessages::addAssistant(const QString &text) {
	const auto &padding = st::owpengramClaudePanelFeedPadding;
	// The text is set before adding: a label resized to the feed width and
	// then given new text takes its natural (one line) width again.
	auto owned = object_ptr<Ui::FlatLabel>(
		_entries,
		st::owpengramClaudePanelText);
	owned->setMarkedText(PrepareAssistantText(text));
	owned->setSelectable(true);
	_entries->add(
		std::move(owned),
		style::margins(padding.left(), padding.top(), padding.right(), 0),
		style::al_justify);
}

void PanelMessages::addTool(const QString &text) {
	const auto &padding = st::owpengramClaudePanelFeedPadding;
	const auto label = _entries->add(
		object_ptr<Ui::FlatLabel>(
			_entries,
			QString::fromUtf8("\xF0\x9F\x94\xA7 ") + text,
			st::owpengramClaudePanelSubText),
		style::margins(
			padding.left(),
			st::owpengramClaudePanelEntrySkip,
			padding.right(),
			0),
		style::al_justify);
	label->setSelectable(true);
}

void PanelMessages::addNotice(const FeedEntry &entry) {
	const auto &padding = st::owpengramClaudePanelFeedPadding;
	const auto error = (entry.type == FeedEntryType::Failed)
		|| (entry.type == FeedEntryType::Error);
	auto owned = object_ptr<Ui::FlatLabel>(
		_entries,
		(error
			? st::owpengramClaudePanelErrorText
			: st::owpengramClaudePanelSubText));
	owned->setMarkedText(NoticeText(entry));
	owned->setSelectable(true);
	_entries->add(
		std::move(owned),
		style::margins(padding.left(), padding.top(), padding.right(), 0),
		style::al_justify);

	const auto addButton = [&](const QString &text, Fn<void()> callback) {
		const auto button = _entries->add(
			object_ptr<Ui::LinkButton>(_entries, text),
			style::margins(
				padding.left(),
				st::owpengramClaudePanelNoticeButtonSkip,
				padding.right(),
				0),
			style::al_left);
		button->setClickedCallback(std::move(callback));
	};
	const auto weak = std::weak_ptr<PanelModel>(_model);
	if (entry.type == FeedEntryType::McpDisabled) {
		addButton(tr::lng_owpengram_claude_panel_enable_mcp(tr::now), [=] {
			// Not from inside the process event handler: from a click.
			if (const auto model = weak.lock()) {
				model->enableMcpAndRetry();
			}
		});
	} else if (entry.type == FeedEntryType::Failed) {
		if (_model->scope() == SessionScope::Temporary) {
			_entries->add(
				object_ptr<Ui::FlatLabel>(
					_entries,
					tr::lng_owpengram_claude_panel_reconnect_temporary(
						tr::now),
					st::owpengramClaudePanelSubText),
				style::margins(
					padding.left(),
					st::owpengramClaudePanelNoticeButtonSkip,
					padding.right(),
					0),
				style::al_justify);
		}
		addButton(tr::lng_owpengram_claude_panel_reconnect(tr::now), [=] {
			if (const auto model = weak.lock()) {
				model->reconnect();
			}
		});
	}
}

} // namespace Owpengram::Claude
