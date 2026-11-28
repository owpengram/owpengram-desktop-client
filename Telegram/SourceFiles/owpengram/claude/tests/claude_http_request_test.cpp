/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#undef NDEBUG
#include "owpengram/claude/claude_http_request.h"

#include <cassert>

using namespace Owpengram::Claude;

int main() {
	const std::string raw = "POST /mcp HTTP/1.1\r\nHost: x\r\n"
		"Authorization: Bearer t\r\nContent-Length: 2\r\n\r\n{}";
	HttpRequest r;

	// parses_post_with_body
	assert(ParseHttpRequest(raw, r) == ParseState::Done);
	assert(r.method == "POST" && r.path == "/mcp"
		&& r.headers["authorization"] == "Bearer t" && r.body == "{}");

	// partial_needs_more
	assert(ParseHttpRequest(raw.substr(0, raw.size() - 1), r)
		== ParseState::NeedMore);
	assert(ParseHttpRequest("POST /mcp HTT", r) == ParseState::NeedMore);

	// too_large
	assert(ParseHttpRequest(
		"POST /mcp HTTP/1.1\r\nContent-Length: 2000000\r\n\r\n",
		r) == ParseState::TooLarge);

	// garbage_is_bad
	assert(ParseHttpRequest("NOT HTTP\r\n\r\n", r) == ParseState::Bad);

	// header names are case-insensitive, values are trimmed
	assert(ParseHttpRequest(
		"POST /mcp HTTP/1.1\r\nAUTHORIZATION:   Bearer x  \r\n"
		"content-LENGTH: 1\r\n\r\nA",
		r) == ParseState::Done);
	assert(r.headers["authorization"] == "Bearer x" && r.body == "A");

	// POST without Content-Length has an empty body
	assert(ParseHttpRequest("POST /mcp HTTP/1.1\r\nHost: x\r\n\r\n", r)
		== ParseState::Done);
	assert(r.body.empty());

	// chunked is not supported
	assert(ParseHttpRequest(
		"POST /mcp HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
		r) == ParseState::Bad);

	// bad Content-Length
	assert(ParseHttpRequest(
		"POST /mcp HTTP/1.1\r\nContent-Length: abc\r\n\r\n", r)
		== ParseState::Bad);
	assert(ParseHttpRequest(
		"POST /mcp HTTP/1.1\r\nContent-Length: -1\r\n\r\n", r)
		== ParseState::Bad);
	assert(ParseHttpRequest(
		"POST /mcp HTTP/1.1\r\nContent-Length: 1\r\n"
		"Content-Length: 2\r\n\r\nab", r) == ParseState::Bad);
	assert(ParseHttpRequest(
		"POST /mcp HTTP/1.1\r\nContent-Length: 99999999999999999999\r\n\r\n",
		r) == ParseState::TooLarge);

	// custom maxBody
	assert(ParseHttpRequest(
		"POST /mcp HTTP/1.1\r\nContent-Length: 5\r\n\r\nabcde", r, 4)
		== ParseState::TooLarge);

	// header section cap, with and without terminator
	assert(ParseHttpRequest(
		"GET / HTTP/1.1\r\nX: " + std::string(20000, 'a'), r)
		== ParseState::TooLarge);
	assert(ParseHttpRequest(
		"GET / HTTP/1.1\r\nX: " + std::string(20000, 'a') + "\r\n\r\n", r)
		== ParseState::TooLarge);

	// header line without a colon
	assert(ParseHttpRequest("GET / HTTP/1.1\r\nbroken\r\n\r\n", r)
		== ParseState::Bad);

	// pipelined bytes: consumed covers exactly one request
	{
		const auto two = raw + raw;
		auto consumed = size_t();
		assert(ParseHttpRequest(two, r, 1 << 20, &consumed)
			== ParseState::Done);
		assert(consumed == raw.size() && r.body == "{}");
	}
	return 0;
}
