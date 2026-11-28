/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "owpengram/claude/claude_http_request.h"

#include <rpl/event_stream.h>
#include <rpl/lifetime.h>
#include <rpl/producer.h>
#include <rpl/variable.h>

#include <QtCore/QString>

#include <map>
#include <memory>
#include <vector>

class QTcpServer;
class QTcpSocket;

namespace Owpengram::Claude {

// Local MCP server: Streamable HTTP, JSON-RPC 2.0, plain JSON responses.
// 127.0.0.1 only, `Authorization: Bearer <token>`, `POST /mcp` only.
// JSON-RPC batches are not supported (removed from MCP in 2025-06-18):
// they get -32600. Main thread only.
class McpServer final {
public:
	McpServer();
	~McpServer();

	// Names (clientInfo.name) of clients that sent a request in the last
	// 5 minutes, sorted and unique.
	[[nodiscard]] rpl::producer<std::vector<QString>> clientsValue() const;

	// Listening on the configured port (false when enabled but the port is
	// busy or invalid).
	[[nodiscard]] bool isListening() const;

private:
	struct Connection;
	struct Session {
		QString clientName;
		crl::time lastSeen = 0;
	};

	void applyAccessState();
	void start(int port);
	void stop();
	void dropConnections();

	void accept();
	void rejectEarly(
		not_null<QTcpSocket*> socket,
		int code,
		const char *reason);
	void readyRead(not_null<QTcpSocket*> socket);
	void handleRequest(
		not_null<QTcpSocket*> socket,
		const HttpRequest &request);
	void closeConnection(not_null<QTcpSocket*> socket);

	void touchSession(const QString &id);
	QString newSession(const QString &clientName);
	void pruneSessions();
	void pushClients();

	std::unique_ptr<QTcpServer> _server;
	int _port = 0;
	uint64 _serial = 0;
	QString _token; // Token the current connections were accepted under.
	std::map<QTcpSocket*, std::unique_ptr<Connection>> _connections;
	std::map<QString, Session> _sessions;
	rpl::variable<std::vector<QString>> _clients;
	// Fired when the server stops or the token is regenerated: pending
	// confirmations of calls received before that are denied.
	rpl::event_stream<> _cancelled;
	base::Timer _sessionsTimer;
	rpl::lifetime _lifetime;

};

// Lazily created, lives until process exit. Created from Application::run,
// then follows Access() changes on its own.
McpServer &Mcp();

} // namespace Owpengram::Claude
