/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <cstdio>
#include <string>
#include <string_view>

namespace Owpengram::Claude {

inline constexpr auto kSessionSlugMaxLength = 40; // In code points.

namespace details {

[[nodiscard]] inline int Utf8Length(unsigned char lead) {
	if (lead < 0x80) {
		return 1;
	} else if ((lead >> 5) == 0x6) {
		return 2;
	} else if ((lead >> 4) == 0xE) {
		return 3;
	} else if ((lead >> 3) == 0x1E) {
		return 4;
	}
	return 1;
}

[[nodiscard]] inline bool IsSlugCodePoint(char32_t cp) {
	return (cp >= U'0' && cp <= U'9')
		|| (cp >= U'a' && cp <= U'z')
		|| (cp >= U'A' && cp <= U'Z')
		|| (cp >= 0x0400 && cp <= 0x04FF);
}

[[nodiscard]] inline std::string TwoDigits(int value) {
	char buffer[16];
	std::snprintf(buffer, sizeof(buffer), "%02d", value);
	return buffer;
}

[[nodiscard]] inline std::string DateTimeText(
		int y,
		int mo,
		int d,
		int h,
		int mi,
		char dateTimeSeparator,
		char timeSeparator) {
	char buffer[16];
	std::snprintf(buffer, sizeof(buffer), "%04d", y);
	return std::string(buffer)
		+ '-' + TwoDigits(mo)
		+ '-' + TwoDigits(d)
		+ dateTimeSeparator + TwoDigits(h)
		+ timeSeparator + TwoDigits(mi);
}

} // namespace details

// Latin/digits/Cyrillic are kept, everything else becomes '-', repeated
// dashes collapse, at most 40 code points, empty -> "session".
[[nodiscard]] inline std::string SessionSlug(std::string_view title) {
	auto result = std::string();
	auto count = 0;
	auto pendingDash = false;
	auto i = size_t(0);
	while (i < title.size() && count < kSessionSlugMaxLength) {
		const auto len = details::Utf8Length((unsigned char)title[i]);
		const auto size = (i + len <= title.size())
			? size_t(len)
			: title.size() - i;
		auto cp = char32_t(0);
		if (size == 1) {
			cp = (unsigned char)title[i];
		} else {
			cp = char32_t((unsigned char)title[i] & (0xFF >> (size + 1)));
			for (auto k = size_t(1); k != size; ++k) {
				cp = (cp << 6) | ((unsigned char)title[i + k] & 0x3F);
			}
		}
		if (details::IsSlugCodePoint(cp)) {
			if (pendingDash && !result.empty()) {
				result += '-';
				++count;
				pendingDash = false;
				if (count >= kSessionSlugMaxLength) {
					break;
				}
			}
			pendingDash = false;
			result.append(title.substr(i, size));
			++count;
		} else {
			pendingDash = true;
		}
		i += size;
	}
	while (!result.empty() && result.back() == '-') {
		result.pop_back();
	}
	return result.empty() ? std::string("session") : result;
}

// "2026-11-22_14-05_<slug>"
[[nodiscard]] inline std::string SessionFolderName(
		int y,
		int mo,
		int d,
		int h,
		int mi,
		std::string_view title) {
	return details::DateTimeText(y, mo, d, h, mi, '_', '-')
		+ '_'
		+ SessionSlug(title);
}

// "### <role> · 2026-11-22 14:05\n\n<text>\n\n"
[[nodiscard]] inline std::string TranscriptEntry(
		std::string_view role,
		int y,
		int mo,
		int d,
		int h,
		int mi,
		std::string_view text) {
	auto result = std::string("### ");
	result.append(role);
	result += " \xC2\xB7 ";
	result += details::DateTimeText(y, mo, d, h, mi, ' ', ':');
	result += "\n\n";
	result.append(text);
	result += "\n\n";
	return result;
}

} // namespace Owpengram::Claude
