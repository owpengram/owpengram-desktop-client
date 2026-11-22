/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_path_rules.h"

#undef NDEBUG
#include <cassert>

using namespace Owpengram::Claude;

int main() {
	// path_escape_denied
	assert(!PathInside("/w/ai", "/w/ai/x/../../tdata/k"));
	assert(!PathInside("/w/ai", "/w/ai/.."));
	assert(!PathInside("/w/ai", "/w/tdata"));
	// path_inside_ok
	assert(PathInside("/w/ai", "/w/ai/sessions/s/transcript.md"));
	assert(PathInside("/w/ai", "/w/ai/./a/../b"));
	assert(PathInside("/w/ai/", "/w/ai/memory"));
	assert(PathInside("/w/ai", "/w/ai"));
	// prefix_trap
	assert(!PathInside("/w/ai", "/w/ai2/x"));
	// relative, empty and mixed separators
	assert(!PathInside("/w/ai", "ai/x"));
	assert(!PathInside("/w/ai", ""));
	assert(!PathInside("", "/w/ai/x"));
	assert(PathInside("C:\\w\\ai", "C:/w/ai/x"));
	assert(!PathInside("C:\\w\\ai", "C:\\w\\ai\\..\\tdata"));

	assert(!PathHasDotDot("/w/ai/a"));
	assert(PathHasDotDot("/w/ai/../a"));
	assert(PathHasDotDot("..\\a"));
	assert(!PathHasDotDot("/w/ai/a..b"));
	assert(PatternIsSafe("**/*.md"));
	assert(PatternIsSafe("/w/ai/**/*.md", "/w/ai"));
	assert(!PatternIsSafe("/etc/*", "/w/ai"));
	assert(!PatternIsSafe("../*"));
	assert(!PatternIsSafe("a/../../*"));
	assert(!PatternIsSafe("C:\\x\\*"));
	assert(!PatternIsSafe("~/x"));
	return 0;
}
