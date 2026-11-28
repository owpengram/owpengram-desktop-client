/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>

#include <memory>

class PeerData;

namespace Window {
class SectionMemento;
class SessionController;
} // namespace Window

namespace Owpengram::Claude {

// Opening and closing the Claude panel. The panel lives in the third
// column (like Info); when the window cannot get a third column it is
// shown as a layer. Main thread only.

// Some panel widget is shown (third column or layer).
[[nodiscard]] bool PanelShown(
	not_null<Window::SessionController*> controller);
[[nodiscard]] rpl::producer<bool> PanelShownValue(
	not_null<Window::SessionController*> controller);

// The panel is the widget in the third column right now.
[[nodiscard]] bool PanelInThirdColumn(
	not_null<Window::SessionController*> controller);

// Third column width while the panel is there: its own stored width, up to
// half of the chat + panel width (Info keeps the usual maximum).
[[nodiscard]] int PanelThirdColumnWidth(int chatAndThirdWidth);

// The top bar button: closes a shown panel, else opens it.
void TogglePanel(not_null<Window::SessionController*> controller);
void OpenPanel(not_null<Window::SessionController*> controller);
// The panel close button: releases the session as HandlePanelClosed and
// closes the third column (or the layer).
void ClosePanel(not_null<Window::SessionController*> controller);

// Info takes the third column from the panel: true if the panel was shown
// there, it is marked closed and the caller shows Info in its place.
bool TakeThirdColumnForInfo(
	not_null<Window::SessionController*> controller);

// MainWidget: the third column is created (layout change) - the panel
// memento if the panel should be shown there, else nullptr.
[[nodiscard]] std::shared_ptr<Window::SectionMemento> ThirdColumnPanel(
	not_null<Window::SessionController*> controller);

// MainWidget, the active chat changed: an unpinned panel is closed, a
// pinned one stays (and comes back if something replaced it). True if the
// third column is handled and the default Info / tabbed logic is skipped.
bool HandleActiveChatChanged(
	not_null<Window::SessionController*> controller,
	PeerData *peer);

} // namespace Owpengram::Claude
