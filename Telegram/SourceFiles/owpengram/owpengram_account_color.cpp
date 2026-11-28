/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/owpengram_account_color.h"

#include "core/application.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "storage/storage_account.h"

#include <rpl/event_stream.h>

namespace Owpengram {
namespace {

constexpr auto kAccountColorPrefKey = "owpengram_account_color";
constexpr auto kAccountColorOff = "off";

const auto kAutoPalette = std::array<QColor, 8>{
	QColor(0xFB, 0x8C, 0x00),
	QColor(0x43, 0xA0, 0x47),
	QColor(0x8E, 0x24, 0xAA),
	QColor(0xE5, 0x39, 0x35),
	QColor(0x00, 0xAC, 0xC1),
	QColor(0xD8, 0x1B, 0x60),
	QColor(0xFD, 0xD8, 0x35),
	QColor(0x1E, 0x88, 0xE5),
};

[[nodiscard]] rpl::event_stream<> &ChangesStream() {
	static auto result = rpl::event_stream<>();
	return result;
}

} // namespace

AccountColorSetting ReadAccountColor(not_null<Main::Account*> account) {
	const auto value = account->local().readPref<QByteArray>(
		kAccountColorPrefKey);
	if (value.isEmpty()) {
		return {};
	} else if (value == kAccountColorOff) {
		return { .mode = AccountColorMode::Off };
	}
	const auto color = QColor(QString::fromLatin1(value));
	return color.isValid()
		? AccountColorSetting{
			.mode = AccountColorMode::Custom,
			.custom = color,
		}
		: AccountColorSetting();
}

void WriteAccountColor(
		not_null<Main::Account*> account,
		const AccountColorSetting &setting) {
	switch (setting.mode) {
	case AccountColorMode::Auto:
		account->local().clearPref(kAccountColorPrefKey);
		break;
	case AccountColorMode::Off:
		account->local().writePref<QByteArray>(
			kAccountColorPrefKey,
			QByteArray(kAccountColorOff));
		break;
	case AccountColorMode::Custom:
		account->local().writePref<QByteArray>(
			kAccountColorPrefKey,
			setting.custom.name(QColor::HexRgb).toLatin1());
		break;
	}
	ChangesStream().fire({});
}

QColor AutoAccountColor(not_null<Main::Account*> account) {
	const auto &list = Core::App().domain().accounts();
	auto position = 0;
	for (const auto &[index, existing] : list) {
		if (existing.get() == account) {
			break;
		}
		++position;
	}
	return kAutoPalette[position % kAutoPalette.size()];
}

std::optional<QColor> ResolveAccountColor(not_null<Main::Account*> account) {
	const auto setting = ReadAccountColor(account);
	switch (setting.mode) {
	case AccountColorMode::Off:
		return std::nullopt;
	case AccountColorMode::Custom:
		return setting.custom;
	case AccountColorMode::Auto:
		break;
	}
	if (Core::App().domain().accountsAuthedCount() < 2) {
		return std::nullopt;
	}
	return AutoAccountColor(account);
}

rpl::producer<> AccountColorChanges() {
	return ChangesStream().events();
}

} // namespace Owpengram
