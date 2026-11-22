/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <initializer_list>
#include <optional>
#include <string_view>

namespace Owpengram::Claude {

enum class Level : int {
	Forbidden = 0,
	Visible = 1,
	Read = 2,
	ActConfirm = 3,
	Auto = 4,
};

enum class Capability {
	SeeChat,
	ReadMessages,
	Act,
};

enum class SessionScope {
	Temporary,
	Chat,
	Account,
	All,
};

[[nodiscard]] constexpr Level Effective(
		Level accountDefault,
		std::optional<Level> chatOverride) {
	return chatOverride.value_or(accountDefault);
}

[[nodiscard]] constexpr bool Allows(Level level, Capability capability) {
	switch (capability) {
	case Capability::SeeChat:
		return level >= Level::Visible;
	case Capability::ReadMessages:
		return level >= Level::Read;
	case Capability::Act:
		return level >= Level::ActConfirm;
	}
	return false;
}

[[nodiscard]] constexpr bool NeedsConfirmation(Level level) {
	return level == Level::ActConfirm;
}

[[nodiscard]] constexpr bool BadgeShown(
		Level accountDefault,
		std::optional<Level> chatOverride) {
	if (Effective(accountDefault, chatOverride) > Level::Forbidden) {
		return true;
	}
	return chatOverride
		&& *chatOverride == Level::Forbidden
		&& accountDefault > Level::Forbidden;
}

[[nodiscard]] constexpr bool CanForward(Level source, Level target) {
	return Allows(source, Capability::ReadMessages)
		&& Allows(target, Capability::Act);
}

[[nodiscard]] constexpr std::string_view Serialize(Level level) {
	switch (level) {
	case Level::Forbidden:
		return "forbidden";
	case Level::Visible:
		return "visible";
	case Level::Read:
		return "read";
	case Level::ActConfirm:
		return "act_confirm";
	case Level::Auto:
		return "auto";
	}
	return "forbidden";
}

[[nodiscard]] constexpr std::optional<Level> ParseLevel(
		std::string_view value) {
	for (const auto level : {
		Level::Forbidden,
		Level::Visible,
		Level::Read,
		Level::ActConfirm,
		Level::Auto,
	}) {
		if (Serialize(level) == value) {
			return level;
		}
	}
	return std::nullopt;
}

} // namespace Owpengram::Claude
