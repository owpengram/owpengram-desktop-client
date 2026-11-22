/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#undef NDEBUG
#include "owpengram/claude/claude_session_format.h"

#include <cassert>

using namespace Owpengram::Claude;

int main() {
	assert(SessionFolderName(2026, 11, 22, 14, 5, "Привіт, світ!")
		== "2026-11-22_14-05_Привіт-світ");
	assert(SessionFolderName(2026, 11, 22, 14, 5, "")
		== "2026-11-22_14-05_session");
	assert(SessionFolderName(2026, 11, 22, 14, 5, "?!  ..")
		== "2026-11-22_14-05_session");
	assert(SessionFolderName(2026, 1, 2, 3, 4, "Hello  World_1")
		== "2026-01-02_03-04_Hello-World-1");

	// 40 code points (not bytes) max.
	const auto longCyr = SessionFolderName(2026, 11, 22, 14, 5,
		std::string(50, 'a') + "я");
	assert(longCyr == "2026-11-22_14-05_" + std::string(40, 'a'));
	std::string cyr;
	for (auto i = 0; i != 45; ++i) {
		cyr += "ж";
	}
	std::string expected;
	for (auto i = 0; i != 40; ++i) {
		expected += "ж";
	}
	assert(SessionFolderName(2026, 11, 22, 14, 5, cyr)
		== "2026-11-22_14-05_" + expected);
	// Trailing dash after truncation is trimmed.
	assert(SessionFolderName(2026, 11, 22, 14, 5,
		std::string(39, 'a') + " bbb")
		== "2026-11-22_14-05_" + std::string(39, 'a'));

	assert(TranscriptEntry("user", 2026, 11, 22, 9, 3, "hi")
		== "### user · 2026-11-22 09:03\n\nhi\n\n");
	return 0;
}
