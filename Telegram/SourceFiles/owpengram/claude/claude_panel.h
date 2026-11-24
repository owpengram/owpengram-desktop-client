/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "base/unique_qptr.h"
#include "owpengram/claude/claude_access_rules.h"
#include "window/section_widget.h"
#include "window/section_memento.h"

#include <memory>

namespace Ui {
class FlatLabel;
class IconButton;
class LinkButton;
class InputField;
class PlainShadow;
class PopupMenu;
class RoundButton;
class ScrollArea;
} // namespace Ui

namespace Owpengram::Claude {

class PanelModel;
class PanelConnectors;
class PanelMessages;

class PanelMemento final : public Window::SectionMemento {
public:
	// Holds the model weakly: PanelState owns it, a memento left in the
	// navigation stack must not keep an old claude process alive.
	explicit PanelMemento(std::weak_ptr<PanelModel> model);

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

	// Null if the model is gone.
	[[nodiscard]] std::shared_ptr<PanelModel> model() const;

private:
	std::weak_ptr<PanelModel> _model;

};

// Claude side panel: header (spark, "Claude", session type chip, history,
// pin, menu, close), the conversation feed, the input field and the
// connectors. Shows PanelStateFor(controller).current and follows it.
class PanelWidget final : public Window::SectionWidget {
public:
	PanelWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	bool sameTypeAs(not_null<Window::SectionMemento*> memento) override;
	std::shared_ptr<Window::SectionMemento> createMemento() override;
	rpl::producer<> removeRequests() const override;

	bool floatPlayerHandleWheelEvent(QEvent *e) override;
	QRect floatPlayerAvailableRect() override;

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void doSetInnerFocus() override;

private:
	void setupTop();
	void setupInput();
	void setModel(std::shared_ptr<PanelModel> model);
	void submit();
	void showMenu();
	void showScopeMenu();
	void fillNewSessionMenu(not_null<Ui::PopupMenu*> menu);
	void updateControlsGeometry();
	void setupAuth();
	// Height of the sign-in banner above the input (0 when hidden).
	int updateAuthGeometry(int width);

	const not_null<Ui::RpWidget*> _top;
	const not_null<Ui::FlatLabel*> _title;
	const not_null<Ui::RoundButton*> _chip;
	const not_null<Ui::IconButton*> _close;
	const not_null<Ui::IconButton*> _menuToggle;
	const not_null<Ui::IconButton*> _pin;
	const not_null<Ui::IconButton*> _history;
	const not_null<Ui::PlainShadow*> _topShadow;
	const not_null<Ui::ScrollArea*> _scroll;
	const not_null<Ui::RpWidget*> _inputWrap;
	const not_null<Ui::InputField*> _field;
	const not_null<Ui::IconButton*> _send;
	const not_null<Ui::RpWidget*> _auth;
	const not_null<Ui::FlatLabel*> _authText;
	const not_null<Ui::LinkButton*> _authButton;

	std::shared_ptr<PanelModel> _model;
	QPointer<PanelMessages> _messages;
	object_ptr<PanelConnectors> _connectors = { nullptr };
	base::unique_qptr<Ui::PopupMenu> _menu;
	rpl::event_stream<> _removeRequests;

};

[[nodiscard]] std::shared_ptr<PanelMemento> MakePanelMemento(
	not_null<Window::SessionController*> controller);

// "Temporary", "This chat", "This account", "Everything".
[[nodiscard]] QString ScopeText(SessionScope scope);

} // namespace Owpengram::Claude
