/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_mcp_server.h"

#include "core/version.h"
#include "logs.h"
#include "owpengram/claude/claude_access_store.h"
#include "owpengram/claude/claude_tools.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QUuid>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

#include <algorithm>
#include <optional>

namespace Owpengram::Claude {
namespace {

constexpr auto kLatestProtocol = "2025-06-18";
constexpr auto kOldProtocol = "2025-03-26";
constexpr auto kReadTimeout = 30 * crl::time(1000);
constexpr auto kDrainTimeout = 2 * crl::time(1000);
constexpr auto kClientsWindow = 5 * 60 * crl::time(1000);
constexpr auto kSessionLifetime = 60 * 60 * crl::time(1000);
constexpr auto kSessionsTick = 30 * crl::time(1000);
constexpr auto kMaxConnections = 32;
constexpr auto kMaxSessions = 64;

constexpr auto kParseError = -32700;
constexpr auto kInvalidRequest = -32600;
constexpr auto kMethodNotFound = -32601;
constexpr auto kInvalidParams = -32602;

[[nodiscard]] bool ConstantTimeEqual(
		const std::string &a,
		const std::string &b) {
	const auto n = std::max(a.size(), b.size());
	auto diff = unsigned(a.size() != b.size());
	for (auto i = size_t(); i != n; ++i) {
		const auto x = (i < a.size()) ? static_cast<unsigned char>(a[i]) : 0U;
		const auto y = (i < b.size()) ? static_cast<unsigned char>(b[i]) : 0U;
		diff |= (x ^ y);
	}
	return (diff == 0);
}

[[nodiscard]] bool TokenMatches(const HttpRequest &request) {
	const auto expected = Access().mcpToken().toUtf8().toStdString();
	auto given = std::string();
	const auto i = request.headers.find("authorization");
	if (i != request.headers.end()) {
		const auto &value = i->second;
		if (value.size() >= 7
			&& details::LowerHttp(value.substr(0, 7)) == "bearer ") {
			given = std::string(details::TrimHttp(value.substr(7)));
		}
	}
	// Compared even when empty, so that timing does not tell the cases apart.
	return ConstantTimeEqual(given, expected) && !expected.empty();
}

// Browsers always send Origin on cross-site requests; refuse anything that
// is not a local page (DNS rebinding protection, as the MCP spec asks).
[[nodiscard]] bool OriginAllowed(const HttpRequest &request) {
	const auto i = request.headers.find("origin");
	if (i == request.headers.end()) {
		return true;
	}
	const auto host = QUrl(QString::fromStdString(i->second)).host();
	return (host == u"localhost"_q)
		|| (host == u"127.0.0.1"_q)
		|| (host == u"::1"_q)
		|| (host == u"[::1]"_q);
}

[[nodiscard]] QString HeaderValue(
		const HttpRequest &request,
		const std::string &name) {
	const auto i = request.headers.find(name);
	return (i != request.headers.end())
		? QString::fromStdString(i->second)
		: QString();
}

[[nodiscard]] QByteArray ContentToText(const QJsonValue &value) {
	if (value.isObject()) {
		return QJsonDocument(value.toObject()).toJson(
			QJsonDocument::Compact);
	} else if (value.isArray()) {
		return QJsonDocument(value.toArray()).toJson(
			QJsonDocument::Compact);
	} else if (value.isUndefined()) {
		return "null";
	}
	// Scalars: serialize as a one-element array and strip the brackets.
	auto wrapped = QJsonDocument(QJsonArray{ value }).toJson(
		QJsonDocument::Compact);
	return wrapped.mid(1, wrapped.size() - 2);
}

[[nodiscard]] QJsonObject ToolResultToJson(const ToolResult &result) {
	const auto text = result.isError
		? result.errorText.toUtf8()
		: ContentToText(result.content);
	return QJsonObject{
		{ u"content"_q, QJsonArray{ QJsonObject{
			{ u"type"_q, u"text"_q },
			{ u"text"_q, QString::fromUtf8(text) },
		} } },
		{ u"isError"_q, result.isError },
	};
}

[[nodiscard]] QJsonObject ErrorObject(
		const QJsonValue &id,
		int code,
		const QString &message) {
	return QJsonObject{
		{ u"jsonrpc"_q, u"2.0"_q },
		{ u"id"_q, id },
		{ u"error"_q, QJsonObject{
			{ u"code"_q, code },
			{ u"message"_q, message },
		} },
	};
}

[[nodiscard]] QJsonObject ResultObject(
		const QJsonValue &id,
		const QJsonObject &result) {
	return QJsonObject{
		{ u"jsonrpc"_q, u"2.0"_q },
		{ u"id"_q, id },
		{ u"result"_q, result },
	};
}

void Respond(
		not_null<QTcpSocket*> socket,
		int code,
		const char *reason,
		const QByteArray &body = {},
		const QByteArray &extraHeaders = {}) {
	auto head = QByteArray("HTTP/1.1 ") + QByteArray::number(code) + ' '
		+ reason + "\r\n";
	if (!body.isEmpty()) {
		head += "Content-Type: application/json\r\n";
	}
	head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
	head += "Cache-Control: no-store\r\n";
	head += "Connection: close\r\n";
	head += extraHeaders;
	head += "\r\n";
	socket->write(head);
	if (!body.isEmpty()) {
		socket->write(body);
	}
}

} // namespace

struct McpServer::Connection {
	std::string buffer;
	QTimer *timer = nullptr; // Child of the socket.
	bool dispatched = false; // Request parsed, waiting for the handler.
	bool draining = false; // Response sent, discarding the rest of input.
};

McpServer::McpServer()
: _sessionsTimer([=] {
	pruneSessions();
	pushClients();
}) {
	Access().changes() | rpl::on_next([=] {
		applyAccessState();
	}, _lifetime);
	applyAccessState();
}

McpServer::~McpServer() {
	stop();
}

rpl::producer<std::vector<QString>> McpServer::clientsValue() const {
	return _clients.value();
}

void McpServer::applyAccessState() {
	if (!Access().mcpEnabled()) {
		stop();
		return;
	}
	const auto port = Access().mcpPort();
	if (!_server || !_server->isListening() || port != _port) {
		start(port);
		return;
	}
	if (Access().mcpToken() != _token) {
		// Token regenerated: nothing accepted under the old one lives on.
		_token = Access().mcpToken();
		dropConnections();
		_sessions.clear();
		pushClients();
	}
}

void McpServer::start(int port) {
	stop();
	auto server = std::make_unique<QTcpServer>();
	if (port < 1 || port > 65535
		|| !server->listen(QHostAddress::LocalHost, quint16(port))) {
		LOG(("Claude MCP: could not listen on 127.0.0.1:%1 (%2)."
			).arg(port
			).arg(server->errorString()));
		return;
	}
	_port = port;
	_token = Access().mcpToken();
	_server = std::move(server);
	QObject::connect(_server.get(), &QTcpServer::newConnection, [=] {
		accept();
	});
}

void McpServer::stop() {
	dropConnections();
	if (_server) {
		_server->close();
		_server.reset();
	}
	_port = 0;
	_token = QString();
	_sessions.clear();
	_sessionsTimer.cancel();
	pushClients();
}

void McpServer::dropConnections() {
	auto sockets = std::vector<QTcpSocket*>();
	for (const auto &[socket, connection] : _connections) {
		sockets.push_back(socket);
	}
	_connections.clear();
	for (const auto socket : sockets) {
		socket->abort();
		socket->deleteLater();
	}
}

void McpServer::closeConnection(not_null<QTcpSocket*> socket) {
	_connections.erase(socket);
	socket->abort();
	socket->deleteLater();
}

void McpServer::accept() {
	while (_server && _server->hasPendingConnections()) {
		const auto socket = _server->nextPendingConnection();
		if (!socket) {
			break;
		}
		if (!socket->peerAddress().isLoopback()) {
			Respond(socket, 403, "Forbidden");
			socket->disconnectFromHost();
			socket->deleteLater();
			continue;
		} else if (_connections.size() >= kMaxConnections) {
			Respond(socket, 503, "Service Unavailable");
			socket->disconnectFromHost();
			socket->deleteLater();
			continue;
		}
		auto connection = std::make_unique<Connection>();
		connection->timer = new QTimer(socket);
		connection->timer->setSingleShot(true);
		connection->timer->setInterval(int(kReadTimeout));
		QObject::connect(connection->timer, &QTimer::timeout, [=] {
			closeConnection(socket);
		});
		connection->timer->start();
		_connections.emplace(socket, std::move(connection));

		QObject::connect(socket, &QTcpSocket::readyRead, socket, [=] {
			readyRead(socket);
		});
		QObject::connect(socket, &QTcpSocket::disconnected, socket, [=] {
			// Handlers that finish later see the socket is gone.
			_connections.erase(socket);
			socket->deleteLater();
		});
	}
}

void McpServer::readyRead(not_null<QTcpSocket*> socket) {
	const auto i = _connections.find(socket);
	if (i == _connections.end()) {
		return;
	}
	auto &connection = *i->second;
	const auto data = socket->readAll();
	if (connection.draining || connection.dispatched) {
		return; // Discard.
	}
	connection.buffer.append(data.constData(), size_t(data.size()));

	auto request = HttpRequest();
	const auto state = ParseHttpRequest(connection.buffer, request);
	if (state == ParseState::NeedMore) {
		return;
	}
	connection.buffer.clear();
	connection.timer->stop();

	// Error replies leave unread input behind: keep draining for a moment so
	// that closing does not reset the connection before the client reads.
	const auto reject = [&](int code, const char *reason) {
		Respond(socket, code, reason);
		connection.draining = true;
		connection.timer->start(int(kDrainTimeout));
	};
	if (state == ParseState::TooLarge) {
		reject(413, "Payload Too Large");
		return;
	} else if (state == ParseState::Bad) {
		reject(400, "Bad Request");
		return;
	} else if (!socket->peerAddress().isLoopback()
		|| !OriginAllowed(request)) {
		reject(403, "Forbidden");
		return;
	} else if (!TokenMatches(request)) {
		Respond(
			socket,
			401,
			"Unauthorized",
			{},
			"WWW-Authenticate: Bearer\r\n");
		socket->disconnectFromHost();
		return;
	}
	connection.dispatched = true;
	handleRequest(socket, request);
}

void McpServer::handleRequest(
		not_null<QTcpSocket*> socket,
		const HttpRequest &request) {
	const auto finish = [=](
			int code,
			const char *reason,
			const QByteArray &body = {},
			const QByteArray &extra = {}) {
		if (!_connections.contains(socket)) {
			return; // Client is gone, drop the result.
		}
		Respond(socket, code, reason, body, extra);
		socket->disconnectFromHost();
	};
	if (request.path != "/mcp") {
		finish(404, "Not Found");
		return;
	} else if (request.method != "POST") {
		finish(405, "Method Not Allowed", {}, "Allow: POST\r\n");
		return;
	}

	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(
		QByteArray::fromStdString(request.body),
		&error);
	const auto reply = [=](const QJsonObject &object) {
		finish(
			200,
			"OK",
			QJsonDocument(object).toJson(QJsonDocument::Compact));
	};
	if (error.error != QJsonParseError::NoError
		|| (!document.isObject() && !document.isArray())) {
		reply(ErrorObject(QJsonValue::Null, kParseError, u"Parse error"_q));
		return;
	} else if (document.isArray()) {
		reply(ErrorObject(
			QJsonValue::Null,
			kInvalidRequest,
			u"Batch requests are not supported"_q));
		return;
	}
	const auto message = document.object();
	const auto hasId = message.contains(u"id"_q)
		&& !message.value(u"id"_q).isNull();
	const auto id = hasId ? message.value(u"id"_q) : QJsonValue::Null;
	const auto method = message.value(u"method"_q);
	if (message.value(u"jsonrpc"_q).toString() != u"2.0"_q) {
		reply(ErrorObject(id, kInvalidRequest, u"Invalid Request"_q));
		return;
	} else if (!method.isString()) {
		// A client's response to something, or garbage.
		if (!hasId
			|| message.contains(u"result"_q)
			|| message.contains(u"error"_q)) {
			finish(202, "Accepted");
		} else {
			reply(ErrorObject(id, kInvalidRequest, u"Invalid Request"_q));
		}
		return;
	}
	const auto name = method.toString();
	const auto paramsValue = message.value(u"params"_q);
	if (!paramsValue.isUndefined()
		&& !paramsValue.isObject()
		&& !paramsValue.isNull()) {
		if (hasId) {
			reply(ErrorObject(id, kInvalidParams, u"Invalid params"_q));
		} else {
			finish(202, "Accepted");
		}
		return;
	}
	const auto params = paramsValue.toObject();
	const auto sessionId = HeaderValue(request, "mcp-session-id");

	if (!hasId) {
		// Notification (notifications/initialized and friends).
		touchSession(sessionId);
		finish(202, "Accepted");
		return;
	}

	if (name == u"initialize"_q) {
		const auto requested = params.value(u"protocolVersion"_q).toString();
		const auto version = (requested == QString::fromLatin1(kOldProtocol))
			? requested
			: QString::fromLatin1(kLatestProtocol);
		const auto client = params.value(u"clientInfo"_q).toObject().value(
			u"name"_q).toString().left(100);
		const auto session = newSession(client);
		auto result = QJsonObject{
			{ u"protocolVersion"_q, version },
			{ u"capabilities"_q, QJsonObject{
				{ u"tools"_q, QJsonObject() },
			} },
			{ u"serverInfo"_q, QJsonObject{
				{ u"name"_q, u"owpengram"_q },
				{ u"version"_q, QString::fromLatin1(AppVersionStr) },
			} },
		};
		finish(
			200,
			"OK",
			QJsonDocument(ResultObject(id, result)).toJson(
				QJsonDocument::Compact),
			"Mcp-Session-Id: " + session.toLatin1() + "\r\n");
		return;
	}

	touchSession(sessionId);
	if (name == u"ping"_q) {
		reply(ResultObject(id, QJsonObject()));
	} else if (name == u"tools/list"_q) {
		auto list = QJsonArray();
		for (const auto &tool : Tools().list()) {
			list.push_back(QJsonObject{
				{ u"name"_q, tool.name },
				{ u"description"_q, tool.description },
				{ u"inputSchema"_q, tool.inputSchema },
			});
		}
		reply(ResultObject(id, QJsonObject{ { u"tools"_q, list } }));
	} else if (name == u"tools/call"_q) {
		const auto tool = params.value(u"name"_q);
		const auto arguments = params.value(u"arguments"_q);
		if (!tool.isString()
			|| (!arguments.isUndefined()
				&& !arguments.isNull()
				&& !arguments.isObject())) {
			reply(ErrorObject(id, kInvalidParams, u"Invalid params"_q));
			return;
		}
		const auto i = _sessions.find(sessionId);
		auto call = ToolCall{
			.name = tool.toString(),
			.arguments = arguments.toObject(),
			.clientName = (i != _sessions.end())
				? i->second.clientName
				: QString(),
		};
		// The connection stays open until the handler is done (a
		// confirmation may take minutes). If the socket is gone by then
		// `finish` drops the result; `once` guards a second call.
		const auto once = std::make_shared<bool>(false);
		Tools().call(std::move(call), [=](ToolResult result) {
			if (std::exchange(*once, true)) {
				return;
			}
			reply(ResultObject(id, ToolResultToJson(result)));
		});
	} else {
		reply(ErrorObject(id, kMethodNotFound, u"Method not found"_q));
	}
}

QString McpServer::newSession(const QString &clientName) {
	const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	if (_sessions.size() >= kMaxSessions) {
		const auto oldest = std::min_element(
			_sessions.begin(),
			_sessions.end(),
			[](const auto &a, const auto &b) {
				return a.second.lastSeen < b.second.lastSeen;
			});
		_sessions.erase(oldest);
	}
	_sessions[id] = Session{ clientName, crl::now() };
	if (!_sessionsTimer.isActive()) {
		_sessionsTimer.callEach(kSessionsTick);
	}
	pushClients();
	return id;
}

void McpServer::touchSession(const QString &id) {
	const auto i = _sessions.find(id);
	if (i != _sessions.end()) {
		const auto was = i->second.lastSeen;
		i->second.lastSeen = crl::now();
		if (crl::now() - was > kClientsWindow) {
			pushClients(); // It is listed again.
		}
	}
}

void McpServer::pruneSessions() {
	const auto now = crl::now();
	for (auto i = _sessions.begin(); i != _sessions.end();) {
		if (now - i->second.lastSeen > kSessionLifetime) {
			i = _sessions.erase(i);
		} else {
			++i;
		}
	}
	if (_sessions.empty()) {
		_sessionsTimer.cancel();
	}
}

void McpServer::pushClients() {
	auto names = std::vector<QString>();
	const auto now = crl::now();
	for (const auto &[id, session] : _sessions) {
		if (now - session.lastSeen <= kClientsWindow
			&& !session.clientName.isEmpty()) {
			names.push_back(session.clientName);
		}
	}
	std::sort(names.begin(), names.end());
	names.erase(std::unique(names.begin(), names.end()), names.end());
	if (names != _clients.current()) {
		_clients = std::move(names);
	}
}

McpServer &Mcp() {
	// Intentionally leaked: sockets must not be torn down after QApplication.
	static const auto instance = new McpServer();
	return *instance;
}

} // namespace Owpengram::Claude
