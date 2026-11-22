/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_access_badge.h"

#include "base/assertion.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "owpengram/claude/claude_access_store.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_claude.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

namespace Owpengram::Claude {
namespace {

[[nodiscard]] QString LevelText(Level level) {
	switch (level) {
	case Level::Forbidden:
		return tr::lng_owpengram_claude_level_forbidden(tr::now);
	case Level::Visible:
		return tr::lng_owpengram_claude_level_visible(tr::now);
	case Level::Read:
		return tr::lng_owpengram_claude_level_read(tr::now);
	case Level::ActConfirm:
		return tr::lng_owpengram_claude_level_act_confirm(tr::now);
	case Level::Auto:
		return tr::lng_owpengram_claude_level_auto(tr::now);
	}
	Unexpected("Level in Owpengram::Claude::LevelText.");
}

[[nodiscard]] const style::color &LevelBg(Level level) {
	switch (level) {
	case Level::Forbidden: return st::owpengramClaudeBadgeForbiddenBg;
	case Level::Visible: return st::owpengramClaudeBadgeVisibleBg;
	case Level::Read: return st::owpengramClaudeBadgeReadBg;
	case Level::ActConfirm: return st::owpengramClaudeBadgeActBg;
	case Level::Auto: return st::owpengramClaudeBadgeAutoBg;
	}
	Unexpected("Level in Owpengram::Claude::LevelBg.");
}

[[nodiscard]] const style::icon &LevelIcon(Level level) {
	switch (level) {
	case Level::Forbidden: return st::owpengramClaudeEyeOff;
	case Level::Visible: return st::owpengramClaudeList;
	case Level::Read: return st::owpengramClaudeEye;
	case Level::ActConfirm: return st::owpengramClaudeHand;
	case Level::Auto: return st::owpengramClaudeBolt;
	}
	Unexpected("Level in Owpengram::Claude::LevelIcon.");
}

} // namespace

AccessBadge::AccessBadge(QWidget *parent, not_null<PeerData*> peer)
: RippleButton(parent, st::owpengramClaudeBadge.ripple)
, _st(st::owpengramClaudeBadge)
, _peer(peer) {
	resize(_st.width, _st.height);
	setCursor(style::cur_pointer);
	setClickedCallback([=] { showMenu(); });
	hide();

	const auto updateName = [=] {
		setAccessibleName(LevelText(Access().effective(_peer)));
	};
	updateName();
	Access().changes() | rpl::on_next([=] {
		updateName();
		update();
	}, lifetime());
}

AccessBadge::~AccessBadge() = default;

bool AccessBadge::shouldShow() const {
	const auto key = AccountKey(&_peer->account());
	if (key.isEmpty()) {
		return false;
	}
	const auto &access = Access();
	return BadgeShown(
		access.accountDefault(key),
		access.chatOverride(key, _peer->id));
}

QImage AccessBadge::prepareRippleMask() const {
	return Ui::RippleAnimation::EllipseMask(
		QSize(_st.rippleAreaSize, _st.rippleAreaSize));
}

QPoint AccessBadge::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos()) - _st.rippleAreaPosition;
}

void AccessBadge::paintEvent(QPaintEvent *e) {
	const auto &access = Access();
	const auto level = access.effective(_peer);

	auto p = QPainter(this);
	st::owpengramClaudeBadgeShadow.paint(p, _st.iconPosition, width());
	const auto circle = QRect(
		_st.rippleAreaPosition,
		QSize(_st.rippleAreaSize, _st.rippleAreaSize));
	{
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(LevelBg(level));
		p.drawEllipse(circle);
	}
	paintRipple(p, _st.rippleAreaPosition.x(), _st.rippleAreaPosition.y());

	LevelIcon(level).paintInCenter(p, circle);
}

void AccessBadge::showMenu() {
	if (_menu) {
		return;
	}
	const auto key = AccountKey(&_peer->account());
	if (key.isEmpty()) {
		return;
	}
	const auto peerId = _peer->id;
	const auto accountLevel = Access().accountDefault(key);
	const auto chosen = Access().chatOverride(key, peerId);

	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	const auto add = [&](
			const QString &text,
			std::optional<Level> choice) {
		_menu->addAction(text, [=] {
			Access().setChatOverride(key, peerId, choice);
		}, (chosen == choice) ? &st::menuIconSelect : nullptr);
	};
	add(
		tr::lng_owpengram_claude_level_as_account(
			tr::now,
			lt_level,
			LevelText(accountLevel)),
		std::nullopt);
	_menu->addSeparator();
	for (const auto level : {
		Level::Forbidden,
		Level::Visible,
		Level::Read,
		Level::ActConfirm,
		Level::Auto,
	}) {
		add(LevelText(level), level);
	}
	_menu->popup(QCursor::pos());
}

} // namespace Owpengram::Claude
