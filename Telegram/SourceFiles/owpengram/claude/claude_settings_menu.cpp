/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_settings_menu.h"

#include "base/assertion.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "owpengram/claude/claude_access_store.h"
#include "owpengram/claude/claude_mcp_server.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

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
	Unexpected("Level in LevelText.");
}

[[nodiscard]] QString CommandText() {
	auto &store = Access();
	return QStringLiteral(
		"claude mcp add --transport http owpengram "
		"http://127.0.0.1:%1/mcp "
		"--header \"Authorization: Bearer %2\"").arg(
			QString::number(store.mcpPort()),
			store.mcpToken());
}

} // namespace

void FillClaudeSettingsMenu(
		not_null<Ui::PopupMenu*> menu,
		not_null<Window::SessionController*> controller) {
	const auto key = AccountKey(&controller->session().account());
	if (!key.isEmpty()) {
		auto submenu = std::make_unique<Ui::PopupMenu>(
			menu,
			st::popupMenuWithIcons);
		const auto current = Access().accountDefault(key);
		for (const auto level : {
			Level::Forbidden,
			Level::Visible,
			Level::Read,
			Level::ActConfirm,
			Level::Auto,
		}) {
			submenu->addAction(LevelText(level), [=] {
				Access().setAccountDefault(key, level);
			}, (current == level) ? &st::menuIconSelect : nullptr);
		}
		menu->addAction(
			tr::lng_owpengram_claude_menu_account(tr::now),
			std::move(submenu),
			&st::menuIconPermissions);
		menu->addSeparator();
	}

	const auto enabled = Access().mcpEnabled();
	menu->addAction(
		(!enabled
			? tr::lng_owpengram_claude_menu_mcp_off(tr::now)
			: Mcp().isListening()
			? tr::lng_owpengram_claude_menu_mcp_on(tr::now)
			: tr::lng_owpengram_claude_menu_mcp_error(tr::now)),
		[=] { Access().setMcpEnabled(!enabled); },
		enabled ? &st::menuIconSelect : nullptr);
	menu->addAction(
		tr::lng_owpengram_claude_menu_copy_command(tr::now),
		[=] {
			QGuiApplication::clipboard()->setText(CommandText());
			controller->showToast(
				tr::lng_owpengram_claude_menu_command_copied(tr::now));
		},
		&st::menuIconCopy);
	menu->addAction(
		tr::lng_owpengram_claude_menu_regenerate(tr::now),
		[=] {
			controller->show(Ui::MakeConfirmBox({
				.text = tr::lng_owpengram_claude_regenerate_text(),
				.confirmed = Fn<void(Fn<void()>)>([=](Fn<void()> close) {
					Access().regenerateMcpToken();
					close();
					controller->showToast(
						tr::lng_owpengram_claude_token_regenerated(
							tr::now));
				}),
				.confirmText = tr::lng_owpengram_claude_regenerate_confirm(),
				.title = tr::lng_owpengram_claude_regenerate_title(),
			}));
		},
		&st::menuIconRestore);
}

} // namespace Owpengram::Claude
