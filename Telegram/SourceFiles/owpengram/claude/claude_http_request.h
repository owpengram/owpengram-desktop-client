/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// Std-only minimal HTTP/1.x request parser for the local MCP server.
// One request per connection is expected (the server replies with
// "Connection: close"); `consumed` is reported for callers that care about
// pipelined bytes. Transfer-Encoding is not supported (-> Bad).

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace Owpengram::Claude {

struct HttpRequest {
	std::string method;
	std::string path;
	std::map<std::string, std::string> headers; // Lower-case names.
	std::string body;
};

enum class ParseState { NeedMore, Done, Bad, TooLarge };

inline constexpr size_t kMaxHttpHeaderSize = 16 * 1024;

namespace details {

[[nodiscard]] inline std::string_view TrimHttp(std::string_view s) {
	while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
		s.remove_prefix(1);
	}
	while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
		s.remove_suffix(1);
	}
	return s;
}

[[nodiscard]] inline std::string LowerHttp(std::string_view s) {
	auto result = std::string(s);
	for (auto &c : result) {
		if (c >= 'A' && c <= 'Z') {
			c = char(c - 'A' + 'a');
		}
	}
	return result;
}

} // namespace details

// Parses one request from the start of `buffer`.
//  NeedMore  - headers or body incomplete.
//  Done      - `out` filled; `*consumed` (if given) = bytes of this request.
//  Bad       - malformed / unsupported (chunked, bad Content-Length).
//  TooLarge  - header section > 16 KB or Content-Length > maxBody.
// A request without Content-Length has an empty body.
[[nodiscard]] inline ParseState ParseHttpRequest(
		std::string_view buffer,
		HttpRequest &out,
		size_t maxBody = 1 << 20,
		size_t *consumed = nullptr) {
	using namespace details;

	const auto end = buffer.find("\r\n\r\n");
	if (end == std::string_view::npos) {
		return (buffer.size() > kMaxHttpHeaderSize)
			? ParseState::TooLarge
			: ParseState::NeedMore;
	}
	if (end + 4 > kMaxHttpHeaderSize) {
		return ParseState::TooLarge;
	}
	const auto head = buffer.substr(0, end);

	auto line = head.substr(0, head.find("\r\n"));
	const auto sp1 = line.find(' ');
	const auto sp2 = (sp1 == std::string_view::npos)
		? std::string_view::npos
		: line.find(' ', sp1 + 1);
	if (sp1 == std::string_view::npos
		|| sp2 == std::string_view::npos
		|| sp1 == 0
		|| sp2 == sp1 + 1
		|| line.find(' ', sp2 + 1) != std::string_view::npos) {
		return ParseState::Bad;
	}
	const auto version = line.substr(sp2 + 1);
	if (version.size() != 8 || version.substr(0, 7) != "HTTP/1.") {
		return ParseState::Bad;
	}

	auto result = HttpRequest();
	result.method = std::string(line.substr(0, sp1));
	result.path = std::string(line.substr(sp1 + 1, sp2 - sp1 - 1));

	auto pos = (head.size() > line.size()) ? line.size() + 2 : head.size();
	auto hasLength = false;
	auto length = size_t();
	auto tooLarge = false;
	while (pos < head.size()) {
		auto next = head.find("\r\n", pos);
		if (next == std::string_view::npos) {
			next = head.size();
		}
		const auto row = head.substr(pos, next - pos);
		pos = next + 2;

		const auto colon = row.find(':');
		if (colon == std::string_view::npos || colon == 0) {
			return ParseState::Bad;
		}
		const auto name = LowerHttp(row.substr(0, colon));
		const auto value = std::string(TrimHttp(row.substr(colon + 1)));
		if (name == "transfer-encoding") {
			return ParseState::Bad;
		}
		if (name == "content-length") {
			if (hasLength || value.empty()) {
				return ParseState::Bad;
			}
			hasLength = true;
			for (const auto c : value) {
				if (c < '0' || c > '9') {
					return ParseState::Bad;
				}
			}
			// Saturate on overflow instead of wrapping.
			for (const auto c : value) {
				const auto digit = size_t(c - '0');
				if (length > (SIZE_MAX - digit) / 10) {
					tooLarge = true;
					break;
				}
				length = length * 10 + digit;
			}
		}
		result.headers[name] = value;
	}
	if (tooLarge || length > maxBody) {
		return ParseState::TooLarge;
	}

	const auto total = end + 4 + length;
	if (buffer.size() < total) {
		return ParseState::NeedMore;
	}
	result.body = std::string(buffer.substr(end + 4, length));
	out = std::move(result);
	if (consumed) {
		*consumed = total;
	}
	return ParseState::Done;
}

} // namespace Owpengram::Claude
