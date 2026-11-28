/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_history_box.h"

#include "data/data_peer.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "owpengram/claude/claude_access_store.h"
#include "owpengram/claude/claude_panel.h"
#include "owpengram/claude/claude_panel_model.h"
#include "owpengram/claude/claude_panel_toggle.h"
#include "owpengram/claude/claude_sessions.h"
#include "owpengram/claude/claude_tools.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/multi_select.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_claude.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace Owpengram::Claude {
namespace {

class HistoryRow final : public Ui::RippleButton {
public:
	HistoryRow(QWidget *parent, QString title, QString date, QString about);

	[[nodiscard]] rpl::producer<> deleteRequests() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	const QString _title;
	const QString _date;
	const QString _about;
	const not_null<Ui::IconButton*> _delete;

};

HistoryRow::HistoryRow(
	QWidget *parent,
	QString title,
	QString date,
	QString about)
: RippleButton(parent, st::defaultRippleAnimation)
, _title(std::move(title))
, _date(std::move(date))
, _about(std::move(about))
, _delete(Ui::CreateChild<Ui::IconButton>(
	this,
	st::stickersRemove)) {
	const auto text = tr::lng_owpengram_claude_history_delete(tr::now);
	_delete->setToolTip(text);
	_delete->setAccessibleName(text);
	resize(width(), st::owpengramClaudeHistoryRowHeight);
}

rpl::producer<> HistoryRow::deleteRequests() const {
	return _delete->clicks() | rpl::to_empty;
}

int HistoryRow::resizeGetHeight(int newWidth) {
	return st::owpengramClaudeHistoryRowHeight;
}

void HistoryRow::resizeEvent(QResizeEvent *e) {
	RippleButton::resizeEvent(e);
	_delete->moveToRight(
		st::boxRowPadding.right() - (_delete->width() / 4),
		(height() - _delete->height()) / 2);
}

void HistoryRow::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	p.fillRect(rect(), isOver() ? st::windowBgOver : st::windowBg);
	paintRipple(p, 0, 0);

	const auto left = st::boxRowPadding.left();
	const auto right = st::boxRowPadding.right() + _delete->width();
	const auto available = width() - left - right;
	if (available <= 0) {
		return;
	}
	const auto &dateFont = st::normalFont;
	const auto dateWidth = dateFont->width(_date);
	const auto &titleFont = st::semiboldFont;
	const auto titleWidth = std::max(
		available - dateWidth - st::normalFont->spacew * 2,
		0);

	p.setFont(titleFont);
	p.setPen(st::windowFg);
	p.drawTextLeft(
		left,
		st::owpengramClaudeHistoryRowTitleTop,
		width(),
		titleFont->elided(_title, titleWidth));

	p.setFont(dateFont);
	p.setPen(st::windowSubTextFg);
	p.drawTextLeft(
		left + available - dateWidth,
		st::owpengramClaudeHistoryRowTitleTop,
		width(),
		_date);
	p.drawTextLeft(
		left,
		st::owpengramClaudeHistoryRowAboutTop,
		width(),
		dateFont->elided(_about, available));
}

[[nodiscard]] QString ChatText(
		not_null<Window::SessionController*> controller,
		const SessionInfo &info) {
	if (!info.peer) {
		return QString();
	}
	const auto own = AccountKey(&controller->session().account());
	const auto session = (info.accountKey == own)
		? &controller->session()
		: FindSession(info.accountKey);
	if (!session) {
		return tr::lng_owpengram_claude_history_other_account(tr::now);
	}
	const auto peer = session->data().peerLoaded(info.peer);
	return (peer && Allows(Access().effective(peer), Capability::SeeChat))
		? peer->name()
		: tr::lng_owpengram_claude_history_hidden_chat(tr::now);
}

[[nodiscard]] QString AboutText(
		not_null<Window::SessionController*> controller,
		const SessionInfo &info) {
	const auto separator = QString::fromUtf8(" \xC2\xB7 ");
	auto result = ScopeText(info.scope);
	if (const auto chat = ChatText(controller, info); !chat.isEmpty()) {
		result += separator + chat;
	}
	return result;
}

[[nodiscard]] QString DateText(const SessionInfo &info) {
	return info.created.toLocalTime().toString(u"dd.MM.yyyy HH:mm"_q);
}

void ConfirmDelete(
		not_null<Window::SessionController*> controller,
		const QString &id,
		Fn<void()> removed) {
	if (AllSessions().find(id)) {
		controller->showToast(
			tr::lng_owpengram_claude_history_delete_open(tr::now));
		return;
	}
	controller->show(Ui::MakeConfirmBox({
		.text = tr::lng_owpengram_claude_history_delete_text(),
		.confirmed = Fn<void(Fn<void()>)>([=](Fn<void()> close) {
			// The session may have been opened while the box was shown.
			if (!AllSessions().remove(id)) {
				controller->showToast(
					tr::lng_owpengram_claude_history_delete_open(tr::now));
			}
			close();
			removed();
		}),
		.confirmText = tr::lng_owpengram_claude_history_delete_confirm(),
		.confirmStyle = &st::attentionBoxButton,
		.title = tr::lng_owpengram_claude_history_delete_title(),
	}));
}

} // namespace

object_ptr<Ui::BoxContent> HistoryBox(
		not_null<Window::SessionController*> controller,
		Fn<void(const SessionInfo &info)> open) {
	return Box([=](not_null<Ui::GenericBox*> box) {
		struct State {
			std::vector<SessionInfo> sessions;
			Fn<void()> rebuild;
		};
		const auto state = box->lifetime().make_state<State>();
		state->sessions = AllSessions().history();

		box->setTitle(tr::lng_owpengram_claude_panel_history());
		box->setWidth(st::boxWideWidth);
		box->setMaxHeight(st::boxMaxListHeight);

		const auto select = box->setPinnedToTopContent(
			object_ptr<Ui::MultiSelect>(
				box,
				st::defaultMultiSelect,
				tr::lng_owpengram_claude_history_search()));
		const auto list = box->verticalLayout();

		state->rebuild = [=] {
			list->clear();
			const auto query = select->getQuery().trimmed();
			auto shown = 0;
			for (const auto &info : state->sessions) {
				const auto title = info.title.isEmpty()
					? tr::lng_owpengram_claude_history_untitled(tr::now)
					: info.title;
				if (!query.isEmpty()
					&& !title.contains(query, Qt::CaseInsensitive)) {
					continue;
				}
				++shown;
				const auto row = list->add(object_ptr<HistoryRow>(
					list,
					title,
					DateText(info),
					AboutText(controller, info)));
				row->setClickedCallback([=] {
					box->closeBox();
					open(info);
				});
				const auto id = info.id;
				row->deleteRequests(
				) | rpl::on_next([=] {
					ConfirmDelete(controller, id, crl::guard(box, [=] {
						state->sessions = AllSessions().history();
						state->rebuild();
					}));
				}, row->lifetime());
			}
			if (!shown) {
				list->add(
					object_ptr<Ui::FlatLabel>(
						list,
						(state->sessions.empty()
							? tr::lng_owpengram_claude_history_empty()
							: tr::lng_owpengram_claude_history_not_found()),
						st::defaultFlatLabel),
					st::owpengramClaudeHistoryEmptyPadding);
			}
			list->resizeToWidth(st::boxWideWidth);
		};
		select->setQueryChangedCallback([=](const QString &) {
			state->rebuild();
		});
		box->setFocusCallback([=] {
			select->setInnerFocus();
		});
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
		state->rebuild();
	});
}

void ShowHistoryBox(not_null<Window::SessionController*> controller) {
	controller->show(HistoryBox(controller, [=](const SessionInfo &info) {
		const auto own = AccountKey(&controller->session().account());
		if (info.scope != SessionScope::All && info.accountKey != own) {
			controller->showToast(
				tr::lng_owpengram_claude_history_switch_account(tr::now));
			return;
		}
		const auto peer = info.peer
			? controller->session().data().peerLoaded(info.peer)
			: nullptr;
		ShowSavedSession(controller, info.id);
		if (peer && controller->activeChatCurrent().peer() != peer) {
			// An unpinned panel closes on the chat switch, the saved
			// session stays the current one and OpenPanel shows it.
			controller->showPeerHistory(peer);
		}
		OpenPanel(controller);
	}));
}

} // namespace Owpengram::Claude
