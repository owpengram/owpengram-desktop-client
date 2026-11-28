/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Owpengram::Claude {

inline constexpr auto kMaxStreamLine = std::size_t(8 * 1024 * 1024);

// Splits an incrementally received byte stream into lines. "\n" or "\r\n"
// terminates a line, empty lines are skipped, an incomplete tail waits for
// the next feed(). A line longer than the limit is dropped (up to its
// terminator) and flagged, see overflowed().
class LineSplitter final {
public:
	explicit LineSplitter(std::size_t maxLine = kMaxStreamLine)
	: _max(maxLine) {
	}

	void feed(std::string_view data) {
		for (const auto ch : data) {
			if (ch == '\n') {
				finishLine();
			} else if (_skipping) {
				continue;
			} else {
				_current.push_back(ch);
				if (_current.size() > _max + 1) { // +1: a pending '\r'.
					_current.clear();
					_skipping = true;
					_overflowed = true;
				}
			}
		}
	}

	[[nodiscard]] std::vector<std::string> take() {
		return std::exchange(_lines, {});
	}

	// True if a line was dropped since the last call; resets the flag.
	[[nodiscard]] bool overflowed() {
		return std::exchange(_overflowed, false);
	}

private:
	void finishLine() {
		if (_skipping) {
			_skipping = false;
			return;
		}
		if (!_current.empty() && _current.back() == '\r') {
			_current.pop_back();
		}
		if (_current.size() > _max) {
			_overflowed = true;
		} else if (!_current.empty()) {
			_lines.push_back(std::move(_current));
		}
		_current.clear();
	}

	std::size_t _max = 0;
	std::string _current;
	std::vector<std::string> _lines;
	bool _skipping = false;
	bool _overflowed = false;

};

} // namespace Owpengram::Claude
