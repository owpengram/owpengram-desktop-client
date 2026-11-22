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
	assert(PatternIsSafe("memory/*.md"));
	assert(!PatternIsSafe(""));
	assert(!PatternIsSafe("/etc/*"));
	assert(!PatternIsSafe("/w/ai/**"));
	assert(!PatternIsSafe("\\x\\*"));
	assert(!PatternIsSafe("../*"));
	assert(!PatternIsSafe("a/../../*"));
	assert(!PatternIsSafe("C:\\x\\*"));
	assert(!PatternIsSafe("C:/x/*"));
	assert(!PatternIsSafe("~/x"));
	assert(!PatternIsSafe("a~b"));
	assert(!PatternIsSafe("{a,../b}"));
	assert(!PatternIsSafe("*.{md,txt}"));
	assert(!PatternIsSafe(".claude/*"));
	assert(!PatternIsSafe("**/.mcp.json"));
	assert(PatternIsSafe("a..b/*"));

	const auto R = std::string("/w/ai");
	const auto none = [](std::string_view) { return false; };
	const auto only = [](std::string_view f) { return f == "mine"; };
	const auto all = SessionScope::All;
	const auto acc = SessionScope::Account;
	const auto chat = SessionScope::Chat;
	const auto tmp = SessionScope::Temporary;
	// Write/Edit only under memory/.
	assert(AiPathAllowed(R, "/w/ai/memory/a.md", true, chat, none));
	assert(AiPathAllowed(R, "/w/ai/memory/x/y.md", true, tmp, none));
	assert(!AiPathAllowed(R, "/w/ai/sessions/mine/transcript.md", true, all, only));
	assert(!AiPathAllowed(R, "/w/ai/notes.md", true, all, only));
	assert(!AiPathAllowed(R, "/w/ai", true, all, only));
	// Read: memory and root files for everyone.
	assert(AiPathAllowed(R, "/w/ai/memory/a.md", false, tmp, none));
	assert(AiPathAllowed(R, "/w/ai/readme.md", false, chat, none));
	// Sessions by scope.
	assert(AiPathAllowed(R, "/w/ai/sessions/other/t.md", false, all, none));
	assert(AiPathAllowed(R, "/w/ai/sessions", false, all, none));
	assert(!AiPathAllowed(R, "/w/ai/sessions", false, acc, only));
	assert(!AiPathAllowed(R, "/w/ai/sessions", false, chat, only));
	assert(AiPathAllowed(R, "/w/ai/sessions/mine/t.md", false, chat, only));
	assert(!AiPathAllowed(R, "/w/ai/sessions/other/t.md", false, chat, only));
	assert(AiPathAllowed(R, "/w/ai/sessions/mine/t.md", false, acc, only));
	assert(!AiPathAllowed(R, "/w/ai/sessions/other/t.md", false, acc, only));
	assert(!AiPathAllowed(R, "/w/ai/sessions/mine/t.md", false, tmp, none));
	// Root listing only with All.
	assert(AiPathAllowed(R, "/w/ai", false, all, none));
	assert(!AiPathAllowed(R, "/w/ai", false, acc, only));
	// Dot paths and CLAUDE.md anywhere.
	assert(!AiPathAllowed(R, "/w/ai/.claude/settings.json", false, all, only));
	assert(!AiPathAllowed(R, "/w/ai/.mcp.json", false, all, only));
	assert(!AiPathAllowed(R, "/w/ai/memory/.hidden", true, all, only));
	assert(!AiPathAllowed(R, "/w/ai/CLAUDE.md", false, all, only));
	assert(!AiPathAllowed(R, "/w/ai/memory/claude.LOCAL.md", true, all, only));
	assert(!AiPathAllowed(R, "/w/ai/sessions/mine/CLAUDE.md", false, all, only));
	assert(AiPathAllowed(R, "/w/ai/memory/claude.md.txt", true, all, only));
	// Outside / traps.
	assert(!AiPathAllowed(R, "/w/ai2/memory/a", false, all, only));
	assert(!AiPathAllowed(R, "/w/tdata/k", false, all, only));
	assert(!AiPathAllowed(R, "/w/ai/memory/../../tdata/k", false, all, only));
	return 0;
}
