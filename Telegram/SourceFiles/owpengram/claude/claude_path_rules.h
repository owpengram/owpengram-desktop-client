/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

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

// Glob pattern for the Glob tool: no ".." component, no "~", and if it is
// absolute, its literal prefix (before the first glob char) must be inside
// `root`. Relative patterns are resolved against the process cwd (ai/).
[[nodiscard]] inline bool PatternIsSafe(
		std::string_view pattern,
		std::string_view root = {}) {
	if (pattern.empty() || PathHasDotDot(pattern)) {
		return false;
	}
	if (pattern[0] == '~') {
		return false;
	}
	const auto absolute = details::IsSeparator(pattern[0])
		|| (pattern.size() >= 2 && pattern[1] == ':');
	if (!absolute) {
		return true;
	}
	if (root.empty()) {
		return false;
	}
	const auto glob = pattern.find_first_of("*?[{");
	const auto literal = (glob == std::string_view::npos)
		? pattern
		: pattern.substr(0, glob);
	return PathInside(root, literal);
}

} // namespace Owpengram::Claude
