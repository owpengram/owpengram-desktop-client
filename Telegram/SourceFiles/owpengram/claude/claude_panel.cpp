/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_panel.h"

#include "dialogs/dialogs_key.h"
#include "lang/lang_keys.h"
#include "owpengram/claude/claude_access_store.h"
#include "owpengram/claude/claude_auth.h"
#include "owpengram/claude/claude_history_box.h"
#include "owpengram/claude/claude_panel_connectors.h"
#include "owpengram/claude/claude_panel_messages.h"
#include "owpengram/claude/claude_panel_model.h"
#include "owpengram/claude/claude_panel_toggle.h"
#include "owpengram/claude/claude_sessions.h"
#include "owpengram/claude/claude_settings_menu.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "window/window_session_controller.h"
#include "styles/style_claude.h"
#include "styles/style_layers.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <QtCore/QDir>
#include <QtCore/QUrl>
#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>
#include <QtGui/QGuiApplication>

namespace Owpengram::Claude {

QString ScopeText(SessionScope scope) {
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
	rpl::single(QString())))
, _send(Ui::CreateChild<Ui::IconButton>(
	_inputWrap.get(),
	st::owpengramClaudePanelSend))
, _auth(Ui::CreateChild<Ui::RpWidget>(this))
, _authText(Ui::CreateChild<Ui::FlatLabel>(
	_auth.get(),
	QString(),
	st::owpengramClaudePanelSubText))
, _authButton(Ui::CreateChild<Ui::LinkButton>(_auth.get(), QString())) {
	setupTop();
	setupInput();
	setupAuth();

	auto &state = PanelStateFor(controller);
	state.widgets = state.widgets.current() + 1;
	lifetime().add([=] {
		if (const auto state = PanelStateIfExists(controller)) {
			state->widgets = state->widgets.current() - 1;
		}
	});
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
			st::owpengramClaudePanelSparkPosition.x(),
			(_top->height() - st::owpengramClaudePanelSpark.height()) / 2,
			_top->width());
	}, _top->lifetime());

	_chip->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	_chip->setClickedCallback([=] { showScopeMenu(); });
	_chip->widthValue() | rpl::on_next([=] {
		updateControlsGeometry();
	}, _chip->lifetime());

	_close->setClickedCallback([=] {
		ClosePanel(controller());
	});
	const auto tooltip = [](not_null<Ui::IconButton*> button, QString text) {
		button->setAccessibleName(text);
		button->setToolTip(text);
	};
	tooltip(_close, tr::lng_owpengram_claude_panel_close(tr::now));
	tooltip(_menuToggle, tr::lng_owpengram_claude_panel_menu(tr::now));
	tooltip(_history, tr::lng_owpengram_claude_panel_history(tr::now));
	_menuToggle->setClickedCallback([=] { showMenu(); });
	_history->setClickedCallback([=] { ShowHistoryBox(controller()); });

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

void PanelWidget::setupAuth() {
	_auth->hide();
	_authText->setAttribute(Qt::WA_TransparentForMouseEvents);
	AuthStatusValue(
	) | rpl::on_next([=](const AuthStatus &status) {
		const auto shown = status.loginRunning
			|| (status.known && !status.loggedIn);
		_auth->setVisible(shown);
		if (status.loginRunning) {
			_authText->setText(
				tr::lng_owpengram_claude_auth_waiting(tr::now));
			_authButton->setText(status.loginUrl.isEmpty()
				? tr::lng_owpengram_claude_auth_cancel(tr::now)
				: tr::lng_owpengram_claude_auth_open_page(tr::now));
		} else {
			_authText->setText(
				tr::lng_owpengram_claude_auth_signed_out(tr::now));
			_authButton->setText(
				tr::lng_owpengram_claude_auth_sign_in(tr::now));
		}
		updateControlsGeometry();
	}, _auth->lifetime());
	_authButton->setClickedCallback([=] {
		const auto status = CurrentAuthStatus();
		if (!status.loginRunning) {
			StartLogin();
		} else if (!status.loginUrl.isEmpty()) {
			QDesktopServices::openUrl(QUrl(status.loginUrl));
		} else {
			CancelLogin();
		}
	});
}

int PanelWidget::updateAuthGeometry(int width) {
	if (_auth->isHidden()) {
		return 0;
	}
	const auto &padding = st::owpengramClaudePanelFeedPadding;
	const auto inner = std::max(width - padding.left() - padding.right(), 1);
	_authText->resizeToWidth(inner);
	_authText->moveToLeft(padding.left(), padding.top(), width);
	const auto buttonTop = _authText->y()
		+ _authText->height()
		+ st::owpengramClaudePanelNoticeButtonSkip;
	_authButton->moveToLeft(padding.left(), buttonTop, width);
	const auto height = buttonTop + _authButton->height();
	_auth->resize(width, height);
	return height;
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

void PanelWidget::switchAccount(const QString &profileId) {
	SelectAccount(profileId);
	// claude keeps conversations per account: continue in a new session
	// of the same type under the chosen account.
	if (_model) {
		StartNewSession(controller(), _model->scope());
	}
}

void PanelWidget::showConnectors() {
	const auto model = _model;
	if (!model) {
		return;
	}
	controller()->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setWidth(st::boxWideWidth);
		box->setMaxHeight(st::boxMaxListHeight);
		box->addRow(
			object_ptr<PanelConnectors>(box, model),
			style::margins());
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
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
		crl::guard(this, [=] { ShowHistoryBox(controller()); }),
		&st::menuIconSchedule);
	menu->addAction(
		tr::lng_owpengram_claude_panel_connectors(tr::now),
		crl::guard(this, [=] { showConnectors(); }),
		&st::menuIconManage);
	menu->addSeparator();

	FillClaudeSettingsMenu(menu, controller());
	menu->addSeparator();

	// Claude accounts: the system sign-in and the ones added here.
	const auto auth = CurrentAuthStatus();
	const auto selected = Access().selectedClaudeProfile();
	const auto addAccount = [&](const QString &id, const QString &email) {
		const auto chosen = (id == selected);
		const auto text = !email.isEmpty()
			? tr::lng_owpengram_claude_account(tr::now, lt_email, email)
			: tr::lng_owpengram_claude_account_signed_out(tr::now);
		menu->addAction(text, crl::guard(this, [=] {
			if (!chosen) {
				switchAccount(id);
			}
		}), chosen ? &st::menuIconSelect : nullptr);
	};
	const auto systemEmail = (selected.isEmpty() && auth.known)
		? auth.email
		: SystemEmail();
	if (!systemEmail.isEmpty() || selected.isEmpty()) {
		addAccount(QString(), systemEmail);
	}
	for (const auto &profile : Access().claudeProfiles()) {
		addAccount(profile.id, profile.email);
	}
	if (!auth.loginRunning) {
		if (auth.known && !auth.loggedIn) {
			menu->addAction(
				tr::lng_owpengram_claude_auth_sign_in(tr::now),
				[] { StartLogin(); },
				&st::menuIconProfile);
		}
		menu->addAction(
			tr::lng_owpengram_claude_account_add(tr::now),
			[] { AddAccount(); },
			&st::menuIconAddToFolder);
	}
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
	// Same height as the chat top bar next to the panel.
	const auto topHeight = st::topBarHeight;
	_top->setGeometry(0, 0, width, topHeight);
	auto right = st::owpengramClaudePanelButtonsRight;
	for (const auto button : { _close, _menuToggle, _pin, _history }) {
		button->moveToRight(
			right,
			(topHeight - button->height()) / 2,
			width);
		right += button->width();
	}
	// The title goes first when there is no room for both: the session
	// type chip must never run under the buttons.
	const auto left = st::owpengramClaudePanelTitlePosition.x();
	const auto skip = st::owpengramClaudePanelChipSkip;
	const auto buttonsLeft = width - right;
	const auto withTitle = left + _title->width() + skip + _chip->width();
	const auto showTitle = (withTitle + skip <= buttonsLeft);
	_title->setVisible(showTitle);
	_title->moveToLeft(left, (topHeight - _title->height()) / 2, width);
	_chip->setVisible(left + _chip->width() + skip <= buttonsLeft);
	_chip->moveToLeft(
		showTitle ? (left + _title->width() + skip) : left,
		(topHeight - _chip->height()) / 2,
		width);
	_topShadow->setGeometry(0, topHeight, width, st::lineWidth);

	auto bottom = height();

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

	const auto authHeight = updateAuthGeometry(width);
	bottom -= authHeight;
	_auth->moveToLeft(0, bottom, width);

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


} // namespace Owpengram::Claude
