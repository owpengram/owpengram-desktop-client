/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_tools.h"

#include "owpengram/claude/claude_confirm.h"
#include "owpengram/claude/claude_path_rules.h"
#include "owpengram/claude/claude_sessions.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QStringList>

namespace Owpengram::Claude {
namespace {

using Done = Fn<void(ToolResult)>;

constexpr auto kConnectorClient = "OwpenGram panel";
constexpr auto kOwnPrefix = "mcp__owpengram__";
constexpr auto kSummaryLimit = 300;

[[nodiscard]] ToolResult Verdict(QJsonObject object) {
	// The MCP layer serializes an object as compact JSON text.
	return OkResult(std::move(object));
}

[[nodiscard]] ToolResult Allow(const QJsonObject &input) {
	return Verdict(QJsonObject{
		{ u"behavior"_q, u"allow"_q },
		{ u"updatedInput"_q, input },
	});
}

[[nodiscard]] ToolResult Deny(const QString &message) {
	return Verdict(QJsonObject{
		{ u"behavior"_q, u"deny"_q },
		{ u"message"_q, message },
	});
}

[[nodiscard]] QString OutsideText() {
	return u"Access is limited to the ai folder."_q;
}

// Canonical absolute path of `path` (relative ones are resolved against
// `root`, the working folder of the process). A path that does not exist
// yet gets the canonical form of its nearest existing ancestor plus the
// remaining names. Empty if it cannot be resolved or has "..".
[[nodiscard]] QString CanonicalPath(const QString &root, QString path) {
	if (path.isEmpty()
		|| path.contains(QChar(0))
		|| PathHasDotDot(path.toStdString())) {
		return QString();
	}
	if (path.startsWith(u'~')) {
		return QString();
	}
	if (QDir::isRelativePath(path)) {
		path = root + u'/' + path;
	}
	auto tail = QStringList();
	auto current = QDir::cleanPath(path);
	for (auto guard = 0; guard != 256; ++guard) {
		const auto info = QFileInfo(current);
		if (info.exists() || info.isSymLink()) {
			const auto canonical = info.canonicalFilePath();
			if (canonical.isEmpty()) {
				return QString(); // Dangling symlink.
			}
			auto result = canonical;
			for (auto i = tail.rbegin(); i != tail.rend(); ++i) {
				result += u'/' + *i;
			}
			return result;
		}
		const auto name = info.fileName();
		const auto parent = info.absolutePath();
		if (name.isEmpty() || parent == current) {
			return QString();
		}
		tail.push_back(name);
		current = parent;
	}
	return QString();
}

// Whether the session may use `path` with the file tool (the path is
// resolved to its canonical form first).
[[nodiscard]] bool FileAccessAllowed(
		const QString &root,
		const QString &path,
		bool write,
		const SessionInfo &session) {
	const auto canonical = CanonicalPath(root, path);
	if (canonical.isEmpty()) {
		return false;
	}
	const auto ownFolder = session.folder.isEmpty()
		? QString()
		: QFileInfo(session.folder).fileName();
	const auto folderAllowed = [&](std::string_view name) {
		const auto folder = QString::fromUtf8(name.data(), int(name.size()));
		switch (session.scope) {
		case SessionScope::Temporary:
			return false;
		case SessionScope::Chat:
			return !ownFolder.isEmpty() && (folder == ownFolder);
		case SessionScope::Account:
			for (const auto &info : AllSessions().history()) {
				if (!info.folder.isEmpty()
					&& QFileInfo(info.folder).fileName() == folder) {
					return info.accountKey == session.accountKey;
				}
			}
			return false;
		case SessionScope::All:
			return true;
		}
		return false;
	};
	return AiPathAllowed(
		root.toStdString(),
		canonical.toStdString(),
		write,
		session.scope,
		folderAllowed);
}

[[nodiscard]] ToolResult ApproveFileTool(
		const QString &tool,
		const QJsonObject &input,
		const SessionInfo &session) {
	const auto root = QFileInfo(AllSessions().aiPath()).canonicalFilePath();
	const auto denied = Deny(OutsideText());
	if (root.isEmpty()) {
		return denied;
	}
	const auto stringField = [&](const QString &key, QString &out) {
		const auto value = input.value(key);
		if (value.isUndefined() || value.isNull()) {
			return true;
		} else if (!value.isString()) {
			return false;
		}
		out = value.toString();
		return true;
	};
	if (tool == u"Read"_q || tool == u"Write"_q || tool == u"Edit"_q) {
		auto path = QString();
		const auto write = (tool != u"Read"_q);
		if (!stringField(u"file_path"_q, path)
			|| path.isEmpty()
			|| !FileAccessAllowed(root, path, write, session)) {
			return denied;
		}
		return Allow(input);
	}
	// Glob / Grep: `path` is required (never the ai/ root), follows the
	// read rules; patterns are relative to it.
	auto path = QString();
	if (!stringField(u"path"_q, path)) {
		return denied;
	} else if (path.isEmpty()) {
		return Deny(u"specify path: memory/ or your session folder"_q);
	} else if (!FileAccessAllowed(root, path, false, session)) {
		return denied;
	}
	auto pattern = QString();
	if (tool == u"Glob"_q) {
		if (!stringField(u"pattern"_q, pattern)
			|| !PatternIsSafe(pattern.toStdString())) {
			return denied;
		}
	} else {
		// Grep: `pattern` is a regex, `glob` is a file filter.
		if (!stringField(u"glob"_q, pattern)
			|| (!pattern.isEmpty()
				&& !PatternIsSafe(pattern.toStdString()))) {
			return denied;
		}
	}
	return Allow(input);
}

[[nodiscard]] QString ConnectorSummary(
		const QString &tool,
		const QJsonObject &input) {
	auto text = SanitizeForDisplay(QString::fromUtf8(
		QJsonDocument(input).toJson(QJsonDocument::Compact)));
	if (text.size() > kSummaryLimit) {
		text = text.left(kSummaryLimit) + QChar(0x2026);
	}
	const auto name = SanitizeForDisplay(tool.left(120));
	return u"Claude wants to use the connector tool "_q + name
		+ u"\n\n"_q + text;
}

// Exactly "mcp__owpengram__" + a registered tool (not `approve`).
[[nodiscard]] bool IsOwnTool(const QString &tool) {
	const auto prefix = QString::fromLatin1(kOwnPrefix);
	if (!tool.startsWith(prefix)) {
		return false;
	}
	const auto name = tool.mid(prefix.size());
	if (name == u"approve"_q) {
		return false;
	}
	for (const auto &def : Tools().list()) {
		if (def.name == name) {
			return true;
		}
	}
	return false;
}

void Approve(ToolCall call, Done done) {
	const auto toolValue = call.arguments.value(u"tool_name"_q);
	if (!toolValue.isString() || toolValue.toString().isEmpty()) {
		done(Deny(u"tool_name is required"_q));
		return;
	}
	const auto tool = toolValue.toString();
	const auto inputValue = call.arguments.value(u"input"_q);
	if (!inputValue.isObject()) {
		done(Deny(u"input must be an object"_q));
		return;
	}
	const auto input = inputValue.toObject();

	if (tool == u"Read"_q
		|| tool == u"Write"_q
		|| tool == u"Edit"_q
		|| tool == u"Glob"_q
		|| tool == u"Grep"_q) {
		// Only reachable with a panel session (see ToolRegistry::call).
		done(call.session
			? ApproveFileTool(tool, input, *call.session)
			: Deny(OutsideText()));
		return;
	} else if (IsOwnTool(tool)) {
		// OwpenGram's own tools are limited by access levels and scope.
		done(Allow(input));
		return;
	} else if (!tool.startsWith(u"mcp__"_q)) {
		done(Deny(u"This tool is not available."_q));
		return;
	}
	// Another connector: ask the user.
	auto session = call.session
		? FindSession(call.session->accountKey)
		: nullptr;
	if (!session) {
		done(Deny(u"The account is not available."_q));
		return;
	}
	RequestConfirmationWithReason(
		session,
		QString::fromLatin1(kConnectorClient),
		ConnectorSummary(tool, input),
		[=](ConfirmResult result) {
			if (result == ConfirmResult::Approved) {
				done(Allow(input));
			} else {
				done(Deny(u"The user did not allow this action."_q));
			}
		},
		std::move(call.cancelled));
}

} // namespace

void RegisterApproveTool(ToolRegistry &registry) {
	registry.add({
		.name = u"approve"_q,
		.description = u"Permission prompt for the Claude panel."_q,
		.inputSchema = QJsonObject{
			{ u"type"_q, u"object"_q },
			{ u"properties"_q, QJsonObject{
				{ u"tool_name"_q, QJsonObject{
					{ u"type"_q, u"string"_q },
				} },
				{ u"input"_q, QJsonObject{
					{ u"type"_q, u"object"_q },
				} },
				{ u"tool_use_id"_q, QJsonObject{
					{ u"type"_q, u"string"_q },
				} },
			} },
			{ u"required"_q, QJsonArray{ u"tool_name"_q, u"input"_q } },
		},
		.handler = Approve,
	});
}

} // namespace Owpengram::Claude
