/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Owpengram::Claude {

// Account access levels, MCP server switch, copy command, new token.
void FillClaudeSettingsMenu(
	not_null<Ui::PopupMenu*> menu,
	not_null<Window::SessionController*> controller);

} // namespace Owpengram::Claude
