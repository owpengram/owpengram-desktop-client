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
class QProcessEnvironment;

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
// MCP server is off: claude is not started (the panel offers to enable it).
struct McpDisabled {
};
// The process ended normally (or was terminated from outside).
struct Exited {
};

} // namespace ProcessEvents

using ProcessEvent = std::variant<
	ProcessEvents::AssistantText,
	ProcessEvents::ToolUse,
	ProcessEvents::Result,
	ProcessEvents::Init,
	ProcessEvents::Failed,
	ProcessEvents::NotInstalled,
	ProcessEvents::McpDisabled,
	ProcessEvents::Exited>;

// Path from the settings (empty = search the default locations).
void SetConfiguredClaudePath(const QString &path);
// Configured path, ~/.local/bin/claude, ~/.claude/local/claude,
// /opt/homebrew/bin/claude, /usr/local/bin/claude. Empty if none.
[[nodiscard]] QString FindClaudeBinary(const QString &configured);
[[nodiscard]] QString ConfiguredClaudePath();
// The system environment with PATH extended for a GUI-launched claude and
// CLAUDE_CONFIG_DIR of the Claude account profile (empty id - the system
// sign-in, the environment is left as is).
[[nodiscard]] QProcessEnvironment ClaudeEnvironment(
	const QString &binary,
	const QString &profileId);
// The same for the selected profile (AccessStore).
[[nodiscard]] QProcessEnvironment ClaudeEnvironment(const QString &binary);

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
	// Fires the event; false if this object was destroyed by a handler.
	bool fire(ProcessEvent event);
	void launch();
	[[nodiscard]] QString stderrTail() const;
	void removeConfig();

	const std::shared_ptr<Session> _session;
	QPointer<QProcess> _process;
	LineSplitter _splitter;
	QString _configPath;
	QString _settingsPath;
	// Lines written since the last start and not yet answered by init:
	// re-sent if the --resume attempt falls back to --session-id.
	std::vector<QByteArray> _unacked;
	bool _resumeAttempt = false;
	bool _gotInit = false;
	bool _fellBack = false;
	QByteArray _stderr;
	// Continue the conversation (--resume) instead of creating it.
	bool _resume = false;
	rpl::event_stream<ProcessEvent> _events;

};

} // namespace Owpengram::Claude
