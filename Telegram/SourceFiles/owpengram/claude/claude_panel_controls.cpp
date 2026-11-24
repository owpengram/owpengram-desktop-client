/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_panel_controls.h"

#include "lang/lang_keys.h"
#include "owpengram/claude/claude_access_store.h"
#include "owpengram/claude/claude_auth.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_claude.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <QtCore/QUrl>
#include <QtGui/QDesktopServices>

namespace Owpengram::Claude {
namespace {

constexpr auto kUsageUrl = "https://claude.ai/settings/usage";

struct ModelOption {
	QString value; // Passed to --model, empty - claude's default.
	tr::phrase<> name;
};

[[nodiscard]] std::vector<ModelOption> Models() {
	return {
		{ QString(), tr::lng_owpengram_claude_model_default },
		{ u"opus"_q, tr::lng_owpengram_claude_model_opus },
		{ u"sonnet"_q, tr::lng_owpengram_claude_model_sonnet },
		{ u"haiku"_q, tr::lng_owpengram_claude_model_haiku },
		{ u"fable"_q, tr::lng_owpengram_claude_model_fable },
	};
}

[[nodiscard]] std::vector<ModelOption> Efforts() {
	return {
		{ QString(), tr::lng_owpengram_claude_effort_default },
		{ u"low"_q, tr::lng_owpengram_claude_effort_low },
		{ u"medium"_q, tr::lng_owpengram_claude_effort_medium },
		{ u"high"_q, tr::lng_owpengram_claude_effort_high },
		{ u"xhigh"_q, tr::lng_owpengram_claude_effort_xhigh },
		{ u"max"_q, tr::lng_owpengram_claude_effort_max },
	};
}

[[nodiscard]] QString NameOf(
		const std::vector<ModelOption> &options,
		const QString &value) {
	for (const auto &option : options) {
		if (option.value == value) {
			return option.name(tr::now);
		}
	}
	return value; // Set elsewhere to a full model name.
}

[[nodiscard]] QString ResetText(const QDateTime &when) {
	if (!when.isValid()) {
		return QString();
	}
	const auto local = when.toLocalTime();
	const auto today = QDate::currentDate();
	const auto time = local.time().toString(u"HH:mm"_q);
	return (local.date() == today)
		? tr::lng_owpengram_claude_usage_resets(tr::now, lt_when, time)
		: tr::lng_owpengram_claude_usage_resets(
			tr::now,
			lt_when,
			QLocale().dayName(local.date().dayOfWeek(), QLocale::ShortFormat)
				+ ' ' + time);
}

} // namespace

// "Primary  Secondary" flat text button with a rounded ripple.
class ControlButton final : public Ui::RippleButton {
public:
	explicit ControlButton(QWidget *parent);

	void setTexts(QString primary, QString secondary);

protected:
	void paintEvent(QPaintEvent *e) override;
	QImage prepareRippleMask() const override;

private:
	QString _primary;
	QString _secondary;

};

ControlButton::ControlButton(QWidget *parent)
: RippleButton(parent, st::defaultRippleAnimation) {
	setCursor(style::cur_pointer);
}

void ControlButton::setTexts(QString primary, QString secondary) {
	_primary = std::move(primary);
	_secondary = std::move(secondary);
	const auto &font = st::owpengramClaudeControlFont;
	const auto padding = st::owpengramClaudeControlPadding;
	auto width = padding * 2 + font->width(_primary);
	if (!_secondary.isEmpty()) {
		width += font->spacew * 2 + font->width(_secondary);
	}
	resize(width, st::owpengramClaudeControlHeight);
	update();
}

void ControlButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	if (isOver()) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		const auto radius = st::owpengramClaudeControlRadius;
		p.drawRoundedRect(rect(), radius, radius);
	}
	paintRipple(p, 0, 0);

	const auto &font = st::owpengramClaudeControlFont;
	auto left = st::owpengramClaudeControlPadding;
	const auto top = (height() - font->height) / 2 + font->ascent;
	p.setFont(font);
	p.setPen(st::windowFg);
	p.drawText(left, top, _primary);
	if (!_secondary.isEmpty()) {
		left += font->width(_primary) + font->spacew * 2;
		p.setPen(st::windowSubTextFg);
		p.drawText(left, top, _secondary);
	}
}

QImage ControlButton::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(
		size(),
		st::owpengramClaudeControlRadius);
}

// The 5-hour usage as a ring; hidden until claude reported the limits.
class UsageRing final : public Ui::RippleButton {
public:
	explicit UsageRing(QWidget *parent);

protected:
	void paintEvent(QPaintEvent *e) override;
	QImage prepareRippleMask() const override;

};

UsageRing::UsageRing(QWidget *parent)
: RippleButton(parent, st::defaultRippleAnimation) {
	setCursor(style::cur_pointer);
	resize(st::owpengramClaudeControlHeight, st::owpengramClaudeControlHeight);
	UsageLimitsValue(
	) | rpl::on_next([=](const ProcessEvents::RateLimits &limits) {
		setVisible(limits.fiveHour.known || limits.weekly.known);
		update();
	}, lifetime());
}

void UsageRing::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	if (isOver()) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		const auto radius = st::owpengramClaudeControlRadius;
		p.drawRoundedRect(rect(), radius, radius);
	}
	paintRipple(p, 0, 0);

	const auto limits = CurrentUsageLimits();
	const auto used = limits.fiveHour.known
		? limits.fiveHour.utilization
		: limits.weekly.utilization;
	const auto size = st::owpengramClaudeUsageRingSize;
	const auto line = st::owpengramClaudeUsageRingLine;
	const auto rect = QRectF(
		(width() - size) / 2. + line / 2.,
		(height() - size) / 2. + line / 2.,
		size - line,
		size - line);
	auto hq = PainterHighQualityEnabler(p);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(st::windowSubTextFg->c, line));
	p.drawEllipse(rect);
	if (used > 0.) {
		auto pen = QPen((used >= 0.9)
			? st::attentionButtonFg->c
			: st::windowActiveTextFg->c, line);
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);
		p.drawArc(rect, 90 * 16, -int(std::round(used * 360 * 16)));
	}
}

QImage UsageRing::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(
		size(),
		st::owpengramClaudeControlRadius);
}

PanelControls::PanelControls(QWidget *parent)
: RpWidget(parent)
, _model(Ui::CreateChild<ControlButton>(this))
, _mode(Ui::CreateChild<ControlButton>(this))
, _usage(Ui::CreateChild<UsageRing>(this)) {
	_model->setClickedCallback([=] { showModelMenu(); });
	_mode->setClickedCallback([=] { showModeMenu(); });
	_usage->setClickedCallback([=] { showUsage(); });
	_usage->setAccessibleName(tr::lng_owpengram_claude_usage_title(tr::now));
	_usage->shownValue(
	) | rpl::on_next([=] {
		updateGeometry(width());
	}, lifetime());
	refresh();
}

rpl::producer<> PanelControls::changed() const {
	return _changed.events();
}

void PanelControls::refresh() {
	auto &access = Access();
	_model->setTexts(
		NameOf(Models(), access.panelModel()),
		access.panelEffort().isEmpty()
			? QString()
			: NameOf(Efforts(), access.panelEffort()));
	_mode->setTexts((access.panelMode() == u"plan"_q)
		? tr::lng_owpengram_claude_mode_plan(tr::now)
		: tr::lng_owpengram_claude_mode_manual(tr::now), QString());
	updateGeometry(width());
}

int PanelControls::resizeGetHeight(int newWidth) {
	updateGeometry(newWidth);
	return st::owpengramClaudeControlHeight;
}

void PanelControls::updateGeometry(int width) {
	auto right = 0;
	if (!_usage->isHidden()) {
		_usage->moveToRight(right, 0, width);
		right += _usage->width() + st::owpengramClaudeControlSkip;
	}
	_mode->moveToRight(right, 0, width);
	right += _mode->width() + st::owpengramClaudeControlSkip;
	_model->moveToRight(right, 0, width);
}

void PanelControls::showModelMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	const auto menu = _menu.get();
	const auto current = Access().panelModel();
	for (const auto &option : Models()) {
		const auto value = option.value;
		menu->addAction(option.name(tr::now), crl::guard(this, [=] {
			Access().setPanelModel(value);
			refresh();
			_changed.fire({});
		}), (value == current) ? &st::menuIconSelect : nullptr);
	}
	menu->addSeparator();

	auto efforts = std::make_unique<Ui::PopupMenu>(
		menu,
		st::popupMenuWithIcons);
	const auto effort = Access().panelEffort();
	for (const auto &option : Efforts()) {
		const auto value = option.value;
		efforts->addAction(option.name(tr::now), crl::guard(this, [=] {
			Access().setPanelEffort(value);
			refresh();
			_changed.fire({});
		}), (value == effort) ? &st::menuIconSelect : nullptr);
	}
	menu->addAction(
		tr::lng_owpengram_claude_effort(tr::now)
			+ u" · "_q
			+ NameOf(Efforts(), effort),
		std::move(efforts),
		&st::menuIconSettings);
	menu->setForcedOrigin(Ui::PanelAnimation::Origin::BottomLeft);
	menu->popup(mapToGlobal(QPoint(_model->x(), _model->y())));
}

void PanelControls::showModeMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	const auto menu = _menu.get();
	menu->addAction(
		tr::lng_owpengram_claude_mode_title(tr::now),
		[] {})->setEnabled(false);
	const auto current = Access().panelMode();
	const auto add = [&](const QString &value, QString text) {
		menu->addAction(text, crl::guard(this, [=] {
			Access().setPanelMode(value);
			refresh();
			_changed.fire({});
		}), (value == current) ? &st::menuIconSelect : nullptr);
	};
	add(u"manual"_q, tr::lng_owpengram_claude_mode_manual_about(tr::now));
	add(u"plan"_q, tr::lng_owpengram_claude_mode_plan_about(tr::now));
	menu->addSeparator();
	// They skip the approve checks (ai/ folder, connectors): not offered.
	for (const auto &text : {
		tr::lng_owpengram_claude_mode_auto_about(tr::now),
		tr::lng_owpengram_claude_mode_edits_about(tr::now),
	}) {
		menu->addAction(text, [] {})->setEnabled(false);
	}
	menu->setForcedOrigin(Ui::PanelAnimation::Origin::BottomRight);
	menu->popup(mapToGlobal(QPoint(_mode->x() + _mode->width(), _mode->y())));
}

void PanelControls::showUsage() {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	const auto menu = _menu.get();
	const auto limits = CurrentUsageLimits();
	menu->addAction(
		tr::lng_owpengram_claude_usage_title(tr::now),
		[] {})->setEnabled(false);
	const auto line = [&](
			const ProcessEvents::RateLimits::Window &window,
			QString name) {
		if (!window.known) {
			return;
		}
		auto text = name
			+ u" · "_q
			+ QString::number(int(std::round(window.utilization * 100)))
			+ '%';
		if (const auto reset = ResetText(window.resetsAt); !reset.isEmpty()) {
			text += u" · "_q + reset;
		}
		menu->addAction(text, [] {})->setEnabled(false);
	};
	line(limits.fiveHour, tr::lng_owpengram_claude_usage_five_hour(tr::now));
	line(limits.weekly, tr::lng_owpengram_claude_usage_weekly(tr::now));
	menu->addSeparator();
	menu->addAction(tr::lng_owpengram_claude_usage_see_all(tr::now), [] {
		QDesktopServices::openUrl(QUrl(QString::fromLatin1(kUsageUrl)));
	}, &st::menuIconStats);
	menu->setForcedOrigin(Ui::PanelAnimation::Origin::BottomRight);
	menu->popup(mapToGlobal(QPoint(
		_usage->x() + _usage->width(),
		_usage->y())));
}

} // namespace Owpengram::Claude
