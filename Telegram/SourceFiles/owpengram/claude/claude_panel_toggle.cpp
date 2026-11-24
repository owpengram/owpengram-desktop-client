/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_panel_toggle.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "dialogs/dialogs_key.h"
#include "owpengram/claude/claude_panel.h"
#include "owpengram/claude/claude_panel_model.h"
#include "ui/layers/layer_widget.h"
#include "window/section_widget.h"
#include "window/window_adaptive.h"
#include "window/window_session_controller.h"
#include "styles/style_claude.h"

namespace Owpengram::Claude {
namespace {

// The panel in a narrow window: a centered layer of the panel width.
class PanelLayer final : public Ui::LayerWidget {
public:
	explicit PanelLayer(not_null<Window::SessionController*> controller);

	void parentResized() override;

protected:
	void doSetInnerFocus() override;

private:
	void closeHook() override;

	const not_null<Window::SessionController*> _controller;
	const not_null<PanelWidget*> _panel;

};

PanelLayer::PanelLayer(not_null<Window::SessionController*> controller)
: _controller(controller)
, _panel(Ui::CreateChild<PanelWidget>(this, controller)) {
	_panel->show();
}

void PanelLayer::parentResized() {
	const auto parent = parentWidget();
	if (!parent) {
		return;
	}
	const auto margin = st::owpengramClaudePanelLayerMargin;
	const auto width = std::min(
		st::owpengramClaudePanelLayerWidth,
		parent->width() - 2 * margin);
	const auto height = parent->height() - 2 * margin;
	setGeometry(
		(parent->width() - width) / 2,
		margin,
		std::max(width, 0),
		std::max(height, 0));
	_panel->setGeometry(rect());
}

void PanelLayer::doSetInnerFocus() {
	_panel->setInnerFocus();
}

void PanelLayer::closeHook() {
	// Closed by an outside click / Escape (ClosePanel resets inLayer
	// before hiding the layer, so the session is released once).
	if (const auto state = PanelStateIfExists(_controller)) {
		if (state->inLayer) {
			state->inLayer = false;
			state->open = false;
			HandlePanelClosed(_controller);
		}
	}
}

[[nodiscard]] Window::SectionShow ThirdColumnShow() {
	return Window::SectionShow(
		Window::SectionShow::Way::ClearStack,
		anim::type::instant,
		anim::activation::background).withThirdColumn();
}

// The current conversation, not ChooseModelForChat: a pinned panel keeps
// its conversation when it comes back in another chat.
[[nodiscard]] std::shared_ptr<Window::SectionMemento> CurrentPanelMemento(
		not_null<Window::SessionController*> controller) {
	const auto &state = PanelStateFor(controller);
	if (const auto current = state.current.current()) {
		return std::make_shared<PanelMemento>(current);
	}
	return MakePanelMemento(controller);
}

[[nodiscard]] bool ShownInThirdColumn(const PanelState &state) {
	return !state.inLayer && (state.widgets.current() > 0);
}

} // namespace

bool PanelShown(not_null<Window::SessionController*> controller) {
	return PanelStateFor(controller).widgets.current() > 0;
}

rpl::producer<bool> PanelShownValue(
		not_null<Window::SessionController*> controller) {
	return PanelStateFor(controller).widgets.value(
	) | rpl::map([](int count) {
		return count > 0;
	}) | rpl::distinct_until_changed();
}

void TogglePanel(not_null<Window::SessionController*> controller) {
	if (PanelShown(controller)) {
		ClosePanel(controller);
	} else {
		OpenPanel(controller);
	}
}

void OpenPanel(not_null<Window::SessionController*> controller) {
	auto &state = PanelStateFor(controller);
	if (state.widgets.current() > 0) {
		return;
	}
	const auto memento = MakePanelMemento(controller);
	state.chat = controller->activeChatCurrent().peer();
	state.open = true;
	if (controller->adaptive().isThreeColumn()) {
		// Replaces Info / the tabbed selector in the third column.
		controller->showSection(memento, ThirdColumnShow());
	} else if (controller->canShowThirdSection()) {
		// Like Info: the three column layout needs this setting, the
		// panel is created by MainWidget (ThirdColumnPanel) on relayout.
		Core::App().settings().setThirdSectionInfoEnabled(true);
		Core::App().saveSettingsDelayed();
		controller->resizeForThirdSection();
		controller->updateColumnLayout();
	} else {
		state.inLayer = true;
		controller->showSpecialLayer(
			object_ptr<PanelLayer>(controller),
			anim::type::normal);
	}
}

void ClosePanel(not_null<Window::SessionController*> controller) {
	auto &state = PanelStateFor(controller);
	const auto inThirdColumn = ShownInThirdColumn(state);
	const auto inLayer = state.inLayer;
	state.inLayer = false;
	state.open = false;
	HandlePanelClosed(controller);
	if (inLayer) {
		controller->hideSpecialLayer();
	} else if (inThirdColumn) {
		controller->closeThirdSection();
	}
}

bool TakeThirdColumnForInfo(
		not_null<Window::SessionController*> controller) {
	const auto state = PanelStateIfExists(controller);
	if (!state || !ShownInThirdColumn(*state)) {
		return false;
	}
	state->open = false;
	HandlePanelClosed(controller);
	return true;
}

std::shared_ptr<Window::SectionMemento> ThirdColumnPanel(
		not_null<Window::SessionController*> controller) {
	const auto &state = PanelStateFor(controller);
	if (!state.open.current() || state.inLayer) {
		return nullptr;
	}
	return CurrentPanelMemento(controller);
}

bool HandleActiveChatChanged(
		not_null<Window::SessionController*> controller,
		PeerData *peer) {
	auto &state = PanelStateFor(controller);
	const auto sameChat = (state.chat == peer);
	state.chat = peer;
	if (!state.open.current()) {
		return false;
	} else if (sameChat) {
		// Keep the panel instead of Info / the tabbed selector.
		return ShownInThirdColumn(state);
	} else if (!state.pinned.current()) {
		const auto inThirdColumn = ShownInThirdColumn(state);
		if (state.inLayer) {
			ClosePanel(controller);
			return false;
		}
		state.open = false;
		HandlePanelClosed(controller);
		if (inThirdColumn) {
			controller->closeThirdSection();
			return true;
		}
		return false;
	} else if (state.inLayer
		|| !peer
		|| !controller->adaptive().isThreeColumn()) {
		// No chat: the third column goes away, the pinned panel comes
		// back with the next chat.
		return false;
	}
	if (!ShownInThirdColumn(state)) {
		controller->showSection(
			CurrentPanelMemento(controller),
			ThirdColumnShow());
	}
	return true;
}

} // namespace Owpengram::Claude
