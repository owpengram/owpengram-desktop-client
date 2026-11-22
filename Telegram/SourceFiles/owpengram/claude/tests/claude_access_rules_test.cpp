/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_access_rules.h"

#include <cassert>

using namespace Owpengram::Claude;

int main() {
	assert(Effective(Level::Forbidden, std::nullopt) == Level::Forbidden);
	assert(!BadgeShown(Level::Forbidden, std::nullopt));

	assert(Effective(Level::Read, Level::Auto) == Level::Auto);
	assert(Effective(Level::Auto, Level::Visible) == Level::Visible);

	assert(!Allows(Effective(Level::Auto, Level::Forbidden), Capability::SeeChat));
	assert(BadgeShown(Level::Auto, Level::Forbidden));

	assert(Allows(Level::Visible, Capability::SeeChat)
		&& !Allows(Level::Visible, Capability::ReadMessages));

	assert(NeedsConfirmation(Level::ActConfirm) && !NeedsConfirmation(Level::Auto));

	assert(CanForward(Level::Read, Level::ActConfirm)
		&& !CanForward(Level::Read, Level::Visible)
		&& !CanForward(Level::Visible, Level::Auto));

	for (const auto level : {
		Level::Forbidden,
		Level::Visible,
		Level::Read,
		Level::ActConfirm,
		Level::Auto,
	}) {
		assert(ParseLevel(Serialize(level)) == level);
	}
	assert(!ParseLevel("bogus"));

	return 0;
}
