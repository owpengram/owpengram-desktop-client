/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "owpengram/claude/claude_stream_parser.h"

#include <rpl/event_stream.h>
#include <rpl/producer.h>

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>

#include <memory>
#include <variant>
#include <vector>

class QProcess;

namespace Owpengram::Claude {

class Session;

struct Connector {
	QString name;
	QString status;
};

namespace ProcessEvents {

struct AssistantText {
	QString text;
};
struct ToolUse {
	QString name;
	QString summary;
};
struct Result {
	bool isError = false;
	QString text;
};
struct Init {
	QString model;
	std::vector<Connector> connectors;
};
struct Failed {
	QString message;
};
struct NotInstalled {
};

} // namespace ProcessEvents

using ProcessEvent = std::variant<
	ProcessEvents::AssistantText,
	ProcessEvents::ToolUse,
	ProcessEvents::Result,
	ProcessEvents::Init,
	ProcessEvents::Failed,
	ProcessEvents::NotInstalled>;

// Path from the settings (empty = search the default locations).
void SetConfiguredClaudePath(const QString &path);
// Configured path, ~/.local/bin/claude, ~/.claude/local/claude,
// /opt/homebrew/bin/claude, /usr/local/bin/claude. Empty if none.
[[nodiscard]] QString FindClaudeBinary(const QString &configured);

// One `claude -p` process per open panel session. Main thread only.
// Every send() and every assistant text goes to Session::appendTranscript.
class ClaudeProcess final : public QObject {
public:
	explicit ClaudeProcess(std::shared_ptr<Session> session);
	~ClaudeProcess();

	[[nodiscard]] bool running() const;

	// Does nothing if already running. Lazily called by send().
	void start();
	void send(const QString &text);
	void stop();
	// Restarts the process (resuming the same conversation), e.g. after
	// the MCP server was enabled or the token was regenerated.
	void reconnect();

	[[nodiscard]] rpl::producer<ProcessEvent> events() const;

private:
	void handleStdout();
	void handleStderr();
	void handleLine(const std::string &line);
	void handleFinished(int code, bool crashed);
	void emitFailed(QString message);
	[[nodiscard]] QString stderrTail() const;
	void removeConfig();

	const std::shared_ptr<Session> _session;
	QPointer<QProcess> _process;
	LineSplitter _splitter;
	QString _configPath;
	QByteArray _stderr;
	// Continue the conversation (--resume) instead of creating it.
	bool _resume = false;
	rpl::event_stream<ProcessEvent> _events;

};

} // namespace Owpengram::Claude
