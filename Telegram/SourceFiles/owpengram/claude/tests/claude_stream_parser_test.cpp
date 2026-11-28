#undef NDEBUG
#include "owpengram/claude/claude_stream_parser.h"

#include <cassert>
#include <iostream>
#include <string>

using Owpengram::Claude::LineSplitter;

int main() {
	{ // Line in parts.
		LineSplitter s;
		s.feed("{\"a\":");
		assert(s.take().empty());
		s.feed("1}\n");
		const auto lines = s.take();
		assert(lines.size() == 1 && lines[0] == "{\"a\":1}");
		assert(s.take().empty());
	}
	{ // Several lines at once, partial tail waits.
		LineSplitter s;
		s.feed("one\ntwo\nthr");
		auto lines = s.take();
		assert(lines.size() == 2 && lines[0] == "one" && lines[1] == "two");
		s.feed("ee\n");
		lines = s.take();
		assert(lines.size() == 1 && lines[0] == "three");
	}
	{ // CRLF and empty lines.
		LineSplitter s;
		s.feed("a\r\n\r\nb\r\n");
		const auto lines = s.take();
		assert(lines.size() == 2 && lines[0] == "a" && lines[1] == "b");
	}
	{ // Split CRLF.
		LineSplitter s;
		s.feed("a\r");
		assert(s.take().empty());
		s.feed("\nb\n");
		const auto lines = s.take();
		assert(lines.size() == 2 && lines[0] == "a" && lines[1] == "b");
	}
	{ // Overflow: line dropped, error flagged, next lines fine.
		LineSplitter s(16);
		s.feed(std::string(10, 'x'));
		assert(!s.overflowed());
		s.feed(std::string(10, 'x'));
		s.feed(std::string(5, 'y') + "\nok\n");
		const auto lines = s.take();
		assert(lines.size() == 1 && lines[0] == "ok");
		assert(s.overflowed());
		assert(!s.overflowed()); // Flag resets after being read.
	}
	{ // Complete over-long line in one feed.
		LineSplitter s(8);
		s.feed("0123456789\nok\n");
		const auto lines = s.take();
		assert(lines.size() == 1 && lines[0] == "ok");
		assert(s.overflowed());
	}
	std::cout << "ok\n";
}
