/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_top_bar_button.h"

#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "styles/style_claude.h"
#include "styles/style_info.h"

namespace Owpengram::Claude {

TopBarButton::TopBarButton(QWidget *parent)
: RippleButton(parent, st::owpengramClaudeTopBar.ripple) {
	const auto &st = st::owpengramClaudeTopBar;
	resize(st.width, st.height);
	setCursor(style::cur_pointer);
}

void TopBarButton::setActive(bool active) {
	if (_active != active) {
		_active = active;
		update();
	}
}

QRect TopBarButton::rippleRect() const {
	const auto &st = st::owpengramClaudeTopBar;
	return QRect(st.rippleAreaPosition, QSize(
		st.rippleAreaSize,
		st.rippleAreaSize));
}

void TopBarButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto ripple = rippleRect();
	const auto color = _active ? &st::lightButtonBgOver : nullptr;
	paintRipple(p, ripple.topLeft(), color ? &(*color)->c : nullptr);

	p.setFont(st::owpengramClaudeTopBarFont);
	p.setPen(_active
		? st::windowActiveTextFg
		: isOver()
		? st::menuIconFgOver
		: st::menuIconFg);
	p.drawText(ripple, Qt::AlignCenter, u"AI"_q);
}

QImage TopBarButton::prepareRippleMask() const {
	return Ui::RippleAnimation::EllipseMask(rippleRect().size());
}

QPoint TopBarButton::prepareRippleStartPosition() const {
	const auto result = mapFromGlobal(QCursor::pos())
		- rippleRect().topLeft();
	return QRect(QPoint(), rippleRect().size()).contains(result)
		? result
		: DisabledRippleStartPosition();
}

} // namespace Owpengram::Claude
