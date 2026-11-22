/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_panel.h"

#include "dialogs/dialogs_key.h"
#include "lang/lang_keys.h"
#include "owpengram/claude/claude_panel_connectors.h"
#include "owpengram/claude/claude_panel_messages.h"
#include "owpengram/claude/claude_panel_model.h"
#include "owpengram/claude/claude_sessions.h"
#include "owpengram/claude/claude_settings_menu.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "window/window_session_controller.h"
#include "styles/style_claude.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <QtCore/QDir>
#include <QtCore/QUrl>
#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>
#include <QtGui/QGuiApplication>

namespace Owpengram::Claude {
namespace {

Fn<void(not_null<Window::SessionController*>)> &HistoryHandler() {
	static auto result = Fn<void(not_null<Window::SessionController*>)>();
	return result;
}

[[nodiscard]] QString ScopeText(SessionScope scope) {
	switch (scope) {
	case SessionScope::Temporary:
		return tr::lng_owpengram_claude_panel_scope_temporary(tr::now);
	case SessionScope::Chat:
		return tr::lng_owpengram_claude_panel_scope_chat(tr::now);
	case SessionScope::Account:
		return tr::lng_owpengram_claude_panel_scope_account(tr::now);
	case SessionScope::All:
		return tr::lng_owpengram_claude_panel_scope_all(tr::now);
	}
	Unexpected("Scope in ScopeText.");
}

void ShowHistory(not_null<Window::SessionController*> controller) {
	if (const auto &handler = HistoryHandler()) {
		handler(controller);
	} else {
		controller->showToast(
			tr::lng_owpengram_claude_panel_history_soon(tr::now));
	}
}

} // namespace

PanelMemento::PanelMemento(std::weak_ptr<PanelModel> model)
: _model(std::move(model)) {
}

object_ptr<Window::SectionWidget> PanelMemento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	if (const auto model = _model.lock()) {
		PanelStateFor(controller).current = model;
	} else {
		ChooseModelForChat(
			controller,
			controller->activeChatCurrent().peer());
	}
	auto result = object_ptr<PanelWidget>(parent, controller);
	result->setGeometry(geometry);
	return result;
}

std::shared_ptr<PanelModel> PanelMemento::model() const {
	return _model.lock();
}

PanelWidget::PanelWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: SectionWidget(parent, controller)
, _top(Ui::CreateChild<Ui::RpWidget>(this))
, _title(Ui::CreateChild<Ui::FlatLabel>(
	_top.get(),
	tr::lng_owpengram_claude_panel_title(),
	st::owpengramClaudePanelTitle))
, _chip(Ui::CreateChild<Ui::RoundButton>(
	_top.get(),
	rpl::single(QString()),
	st::owpengramClaudePanelChip))
, _close(Ui::CreateChild<Ui::IconButton>(
	_top.get(),
	st::owpengramClaudePanelClose))
, _menuToggle(Ui::CreateChild<Ui::IconButton>(
	_top.get(),
	st::owpengramClaudePanelMenu))
, _pin(Ui::CreateChild<Ui::IconButton>(
	_top.get(),
	st::owpengramClaudePanelPin))
, _history(Ui::CreateChild<Ui::IconButton>(
	_top.get(),
	st::owpengramClaudePanelHistory))
, _topShadow(Ui::CreateChild<Ui::PlainShadow>(this))
, _scroll(Ui::CreateChild<Ui::ScrollArea>(this))
, _inputWrap(Ui::CreateChild<Ui::RpWidget>(this))
, _field(Ui::CreateChild<Ui::InputField>(
	_inputWrap.get(),
	st::owpengramClaudePanelField,
	Ui::InputField::Mode::MultiLine,
	tr::lng_owpengram_claude_panel_placeholder()))
, _send(Ui::CreateChild<Ui::IconButton>(
	_inputWrap.get(),
	st::owpengramClaudePanelSend)) {
	setupTop();
	setupInput();

	auto &state = PanelStateFor(controller);
	if (!state.current.current()) {
		ChooseModelForChat(
			controller,
			controller->activeChatCurrent().peer());
	}
	state.current.value(
	) | rpl::filter([](const std::shared_ptr<PanelModel> &model) {
		return model != nullptr;
	}) | rpl::on_next([=](std::shared_ptr<PanelModel> model) {
		setModel(std::move(model));
	}, lifetime());
}

void PanelWidget::setupTop() {
	_top->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_top);
		p.fillRect(_top->rect(), st::windowBg);
		st::owpengramClaudePanelSpark.paint(
			p,
			st::owpengramClaudePanelSparkPosition,
			_top->width());
	}, _top->lifetime());

	_chip->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	_chip->setClickedCallback([=] { showScopeMenu(); });
	_chip->widthValue() | rpl::on_next([=] {
		updateControlsGeometry();
	}, _chip->lifetime());

	_close->setClickedCallback([=] {
		HandlePanelClosed(controller());
		_removeRequests.fire({});
	});
	const auto tooltip = [](not_null<Ui::IconButton*> button, QString text) {
		button->setAccessibleName(text);
		button->setToolTip(text);
	};
	tooltip(_close, tr::lng_owpengram_claude_panel_close(tr::now));
	tooltip(_menuToggle, tr::lng_owpengram_claude_panel_menu(tr::now));
	tooltip(_history, tr::lng_owpengram_claude_panel_history(tr::now));
	_menuToggle->setClickedCallback([=] { showMenu(); });
	_history->setClickedCallback([=] { ShowHistory(controller()); });

	auto &state = PanelStateFor(controller());
	_pin->setClickedCallback([=] {
		auto &pinned = PanelStateFor(controller()).pinned;
		pinned = !pinned.current();
	});
	state.pinned.value() | rpl::on_next([=](bool pinned) {
		tooltip(_pin, pinned
			? tr::lng_owpengram_claude_panel_unpin(tr::now)
			: tr::lng_owpengram_claude_panel_pin(tr::now));
		_pin->setIconOverride(
			pinned ? &st::owpengramClaudePanelPinActive : nullptr,
			pinned ? &st::owpengramClaudePanelPinActive : nullptr);
	}, _pin->lifetime());
}

void PanelWidget::setupInput() {
	_inputWrap->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_inputWrap);
		auto hq = PainterHighQualityEnabler(p);
		const auto border = st::owpengramClaudePanelInputBorder;
		const auto half = border / 2.;
		p.setPen(QPen(st::owpengramClaudePanelInputBorderFg, border));
		p.setBrush(st::windowBg);
		const auto radius = st::owpengramClaudePanelInputRadius;
		p.drawRoundedRect(
			QRectF(_inputWrap->rect()).marginsRemoved(
				{ half, half, half, half }),
			radius,
			radius);
	}, _inputWrap->lifetime());

	_field->setSubmitSettings(Ui::InputField::SubmitSettings::Enter);
	_field->submits(
	) | rpl::on_next([=] {
		submit();
	}, _field->lifetime());
	_field->heightChanges(
	) | rpl::on_next([=] {
		updateControlsGeometry();
	}, _field->lifetime());
	_send->setClickedCallback([=] { submit(); });
}

void PanelWidget::setModel(std::shared_ptr<PanelModel> model) {
	if (_model == model) {
		return;
	}
	_model = std::move(model);
	_model->cancelStopWhenIdle();
	_chip->setText(rpl::single(ScopeText(_model->scope())));

	const auto messages = _scroll->setOwnedWidget(
		object_ptr<PanelMessages>(this, _model));
	_messages = messages.data();
	Ui::SetStickyBottomScroll(_scroll.get(), messages.data());

	_connectors = object_ptr<PanelConnectors>(this, _model);
	_connectors->show();
	_connectors->heightValue(
	) | rpl::on_next([=] {
		updateControlsGeometry();
	}, _connectors->lifetime());

	updateControlsGeometry();
	_scroll->scrollToY(_scroll->scrollTopMax());
}

void PanelWidget::submit() {
	const auto text = _field->getLastText();
	if (text.trimmed().isEmpty() || !_model) {
		return;
	}
	_field->clear();
	_model->send(text);
}

void PanelWidget::fillNewSessionMenu(not_null<Ui::PopupMenu*> menu) {
	const auto hasChat = (controller()->activeChatCurrent().peer() != nullptr);
	for (const auto scope : {
		SessionScope::Temporary,
		SessionScope::Chat,
		SessionScope::Account,
		SessionScope::All,
	}) {
		const auto needsChat = (scope == SessionScope::Temporary)
			|| (scope == SessionScope::Chat);
		if (needsChat && !hasChat) {
			continue;
		}
		const auto selected = _model && (_model->scope() == scope);
		menu->addAction(ScopeText(scope), crl::guard(this, [=] {
			StartNewSession(controller(), scope);
		}), selected ? &st::menuIconSelect : nullptr);
	}
}

void PanelWidget::showScopeMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	fillNewSessionMenu(_menu.get());
	_menu->popup(QCursor::pos());
}

void PanelWidget::showMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	const auto menu = _menu.get();

	auto scopes = std::make_unique<Ui::PopupMenu>(
		menu,
		st::popupMenuWithIcons);
	fillNewSessionMenu(scopes.get());
	menu->addAction(
		tr::lng_owpengram_claude_panel_new_session(tr::now),
		std::move(scopes),
		&st::menuIconAddToFolder);
	menu->addAction(
		tr::lng_owpengram_claude_panel_history(tr::now),
		crl::guard(this, [=] { ShowHistory(controller()); }),
		&st::menuIconSchedule);
	menu->addSeparator();

	FillClaudeSettingsMenu(menu, controller());
	menu->addSeparator();

	const auto ai = AllSessions().aiPath();
	menu->addAction(
		tr::lng_owpengram_claude_panel_open_folder(tr::now),
		[=] {
			QDir().mkpath(ai);
			QDesktopServices::openUrl(QUrl::fromLocalFile(ai));
		},
		&st::menuIconShowInFolder);
	menu->addAction(
		tr::lng_owpengram_claude_panel_copy_folder(tr::now),
		crl::guard(this, [=] {
			QGuiApplication::clipboard()->setText(
				QDir::toNativeSeparators(ai));
			controller()->showToast(
				tr::lng_owpengram_claude_panel_folder_copied(tr::now));
		}),
		&st::menuIconCopy);
	menu->popup(QCursor::pos());
}

void PanelWidget::updateControlsGeometry() {
	const auto width = this->width();
	if (width <= 0) {
		return;
	}
	const auto topHeight = st::owpengramClaudePanelTopHeight;
	_top->setGeometry(0, 0, width, topHeight);
	auto right = st::owpengramClaudePanelButtonsRight;
	for (const auto button : { _close, _menuToggle, _pin, _history }) {
		button->moveToRight(right, 0, width);
		right += button->width();
	}
	_title->moveToLeft(
		st::owpengramClaudePanelTitlePosition.x(),
		st::owpengramClaudePanelTitlePosition.y(),
		width);
	_chip->moveToLeft(
		_title->x() + _title->width() + st::owpengramClaudePanelChipSkip,
		st::owpengramClaudePanelChipTop,
		width);
	_topShadow->setGeometry(0, topHeight, width, st::lineWidth);

	auto bottom = height();
	if (_connectors) {
		_connectors->resizeToWidth(width);
		bottom -= _connectors->height();
		_connectors->moveToLeft(0, bottom, width);
	}

	const auto &margin = st::owpengramClaudePanelInputMargin;
	const auto &padding = st::owpengramClaudePanelInputPadding;
	const auto inputWidth = width - margin.left() - margin.right();
	_field->resizeToWidth(std::max(
		inputWidth - padding.left() - padding.right() - _send->width(),
		1));
	const auto inputHeight = padding.top()
		+ std::max(_field->height(), _send->height())
		+ padding.bottom();
	bottom -= margin.bottom() + inputHeight;
	_inputWrap->setGeometry(margin.left(), bottom, inputWidth, inputHeight);
	_field->moveToLeft(padding.left(), padding.top(), inputWidth);
	_send->moveToRight(
		padding.right(),
		inputHeight - padding.bottom() - _send->height(),
		inputWidth);
	bottom -= margin.top();

	const auto scrollTop = topHeight + st::lineWidth;
	const auto scrollHeight = std::max(bottom - scrollTop, 0);
	const auto wasAtBottom = (_scroll->scrollTop() >= _scroll->scrollTopMax());
	_scroll->setGeometry(0, scrollTop, width, scrollHeight);
	if (_messages) {
		_messages->resizeToWidth(width);
	}
	if (wasAtBottom) {
		_scroll->scrollToY(_scroll->scrollTopMax());
	}
}

void PanelWidget::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void PanelWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.fillRect(e->rect(), st::windowBg);
}

void PanelWidget::doSetInnerFocus() {
	_field->setFocusFast();
}

bool PanelWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (const auto panel = dynamic_cast<PanelMemento*>(memento.get())) {
		if (const auto model = panel->model()) {
			PanelStateFor(controller()).current = model;
		}
		return true;
	}
	return false;
}

bool PanelWidget::sameTypeAs(not_null<Window::SectionMemento*> memento) {
	return dynamic_cast<PanelMemento*>(memento.get()) != nullptr;
}

std::shared_ptr<Window::SectionMemento> PanelWidget::createMemento() {
	return std::make_shared<PanelMemento>(_model);
}

rpl::producer<> PanelWidget::removeRequests() const {
	return _removeRequests.events();
}

bool PanelWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect PanelWidget::floatPlayerAvailableRect() {
	return mapToGlobal(_scroll->geometry());
}

std::shared_ptr<PanelMemento> MakePanelMemento(
		not_null<Window::SessionController*> controller) {
	return std::make_shared<PanelMemento>(ChooseModelForChat(
		controller,
		controller->activeChatCurrent().peer()));
}

void SetPanelHistoryHandler(
		Fn<void(not_null<Window::SessionController*>)> handler) {
	HistoryHandler() = std::move(handler);
}

} // namespace Owpengram::Claude
