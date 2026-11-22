/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "owpengram/claude/claude_access_rules.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Owpengram::Claude {
namespace details {

[[nodiscard]] inline bool IsSeparator(char c) {
	return (c == '/') || (c == '\\');
}

// Splits on both separators; returns false for a relative path.
// Lexically resolves "." and "..". An escape above the root fails.
[[nodiscard]] inline bool NormalizeAbsolute(
		std::string_view path,
		std::vector<std::string> &out) {
	if (path.empty()) {
		return false;
	}
	for (const auto c : path) {
		if (c == '\0') {
			return false;
		}
	}
	// Absolute: "/...", "\...", or a drive "X:/...".
	auto i = size_t(0);
	auto drive = std::string();
	if (IsSeparator(path[0])) {
		i = 1;
	} else if (path.size() >= 3
		&& path[1] == ':'
		&& IsSeparator(path[2])
		&& ((path[0] >= 'a' && path[0] <= 'z')
			|| (path[0] >= 'A' && path[0] <= 'Z'))) {
		drive = std::string(1, char(path[0] | 0x20)) + ":";
		i = 3;
	} else {
		return false;
	}
	out.clear();
	if (!drive.empty()) {
		out.push_back(drive);
	}
	const auto base = out.size();
	while (i <= path.size()) {
		auto j = i;
		while (j < path.size() && !IsSeparator(path[j])) {
			++j;
		}
		const auto part = path.substr(i, j - i);
		if (part.empty() || part == ".") {
			// Skip.
		} else if (part == "..") {
			if (out.size() <= base) {
				return false;
			}
			out.pop_back();
		} else {
			out.emplace_back(part);
		}
		i = j + 1;
	}
	return true;
}

} // namespace details

// Lexical check: both paths absolute, `path` is `root` or below it after
// resolving "." and "..". Callers canonicalize symlinks first.
[[nodiscard]] inline bool PathInside(
		std::string_view root,
		std::string_view path) {
	auto a = std::vector<std::string>();
	auto b = std::vector<std::string>();
	if (!details::NormalizeAbsolute(root, a)
		|| !details::NormalizeAbsolute(path, b)) {
		return false;
	}
	if (b.size() < a.size()) {
		return false;
	}
	for (auto i = size_t(0); i != a.size(); ++i) {
		if (a[i] != b[i]) {
			return false;
		}
	}
	return true;
}

// True if some path component is exactly "..".
[[nodiscard]] inline bool PathHasDotDot(std::string_view path) {
	auto i = size_t(0);
	while (i <= path.size()) {
		auto j = i;
		while (j < path.size() && !details::IsSeparator(path[j])) {
			++j;
		}
		if (path.substr(i, j - i) == "..") {
			return true;
		}
		i = j + 1;
	}
	return false;
}

// Glob pattern (Glob `pattern`, Grep `glob`): always relative to the
// checked search folder. No absolute patterns, "..", "~", braces, and no
// component starting with ".".
[[nodiscard]] inline bool PatternIsSafe(std::string_view pattern) {
	if (pattern.empty() || PathHasDotDot(pattern)) {
		return false;
	}
	if (details::IsSeparator(pattern[0])
		|| pattern[0] == '~'
		|| (pattern.size() >= 2 && pattern[1] == ':')) {
		return false;
	}
	if (pattern.find_first_of("~{}") != std::string_view::npos) {
		return false;
	}
	auto i = size_t(0);
	while (i <= pattern.size()) {
		auto j = i;
		while (j < pattern.size() && !details::IsSeparator(pattern[j])) {
			++j;
		}
		if (j > i && pattern[i] == '.') {
			return false;
		}
		i = j + 1;
	}
	return true;
}

namespace details {

[[nodiscard]] inline bool IsClaudeMd(std::string_view name) {
	const auto lower = [](std::string_view value) {
		auto result = std::string(value);
		for (auto &c : result) {
			if (c >= 'A' && c <= 'Z') {
				c = char(c + 32);
			}
		}
		return result;
	};
	const auto value = lower(name);
	return (value == "claude.md") || (value == "claude.local.md");
}

} // namespace details

// Layout inside the ai/ folder (`root`, canonical). `path` is canonical and
// absolute. Write access: only memory/**. Read access: memory/**, root files,
// and sessions/<folder>/** where `folderAllowed(folder)` says so (the
// sessions folder itself and the root folder are listable only with the All
// scope). Any component starting with "." and any CLAUDE.md /
// CLAUDE.local.md is denied everywhere.
[[nodiscard]] inline bool AiPathAllowed(
		std::string_view root,
		std::string_view path,
		bool write,
		SessionScope scope,
		const std::function<bool(std::string_view)> &folderAllowed) {
	auto a = std::vector<std::string>();
	auto b = std::vector<std::string>();
	if (!details::NormalizeAbsolute(root, a)
		|| !details::NormalizeAbsolute(path, b)
		|| b.size() < a.size()) {
		return false;
	}
	for (auto i = size_t(0); i != a.size(); ++i) {
		if (a[i] != b[i]) {
			return false;
		}
	}
	const auto rel = std::vector<std::string>(b.begin() + a.size(), b.end());
	for (const auto &part : rel) {
		if (part[0] == '.' || details::IsClaudeMd(part)) {
			return false;
		}
	}
	if (rel.empty()) {
		return !write && (scope == SessionScope::All);
	} else if (rel[0] == "memory") {
		return true;
	} else if (write) {
		return false;
	} else if (rel[0] == "sessions") {
		if (scope == SessionScope::All) {
			return true;
		} else if (rel.size() == 1) {
			return false;
		}
		return folderAllowed && folderAllowed(rel[1]);
	}
	return true; // Other files in the root of ai/.
}

} // namespace Owpengram::Claude
