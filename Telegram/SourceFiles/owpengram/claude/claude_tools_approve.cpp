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

[[nodiscard]] bool InsideAi(const QString &root, const QString &path) {
	const auto canonical = CanonicalPath(root, path);
	return !canonical.isEmpty()
		&& PathInside(root.toStdString(), canonical.toStdString());
}

[[nodiscard]] ToolResult ApproveFileTool(
		const QString &tool,
		const QJsonObject &input) {
	const auto aiPath = AllSessions().aiPath();
	const auto root = QFileInfo(aiPath).canonicalFilePath();
	if (root.isEmpty()) {
		return Deny(OutsideText());
	}
	const auto denied = Deny(OutsideText());
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
		if (!stringField(u"file_path"_q, path)
			|| path.isEmpty()
			|| !InsideAi(root, path)) {
			return denied;
		}
		return Allow(input);
	}
	// Glob / Grep: the optional `path` defaults to the working folder.
	auto path = QString();
	if (!stringField(u"path"_q, path)) {
		return denied;
	} else if (!path.isEmpty() && !InsideAi(root, path)) {
		return denied;
	}
	if (tool == u"Glob"_q) {
		auto pattern = QString();
		if (!stringField(u"pattern"_q, pattern)
			|| !PatternIsSafe(pattern.toStdString(), root.toStdString())) {
			return denied;
		}
	} else {
		// Grep: `pattern` is a regex, `glob` is a file filter.
		auto glob = QString();
		if (!stringField(u"glob"_q, glob)
			|| (!glob.isEmpty()
				&& !PatternIsSafe(glob.toStdString(), root.toStdString()))) {
			return denied;
		}
	}
	return Allow(input);
}

[[nodiscard]] QString ConnectorSummary(
		const QString &tool,
		const QJsonObject &input) {
	auto text = QString::fromUtf8(QJsonDocument(input).toJson(
		QJsonDocument::Compact));
	for (auto &ch : text) {
		if (ch.unicode() < 0x20) {
			ch = u' ';
		}
	}
	if (text.size() > kSummaryLimit) {
		text = text.left(kSummaryLimit) + QChar(0x2026);
	}
	auto name = tool.left(120);
	for (auto &ch : name) {
		if (ch.unicode() < 0x20) {
			ch = u' ';
		}
	}
	return u"Claude wants to use the connector tool "_q + name
		+ u"\n\n"_q + text;
}

void Approve(ToolCall call, Done done) {
	const auto toolValue = call.arguments.value(u"tool_name"_q);
	if (!toolValue.isString() || toolValue.toString().isEmpty()) {
		done(Deny(u"tool_name is required"_q));
		return;
	}
	const auto tool = toolValue.toString();
	const auto inputValue = call.arguments.value(u"input"_q);
	if (!inputValue.isUndefined()
		&& !inputValue.isNull()
		&& !inputValue.isObject()) {
		done(Deny(u"input must be an object"_q));
		return;
	}
	const auto input = inputValue.toObject();

	if (tool == u"Read"_q
		|| tool == u"Write"_q
		|| tool == u"Edit"_q
		|| tool == u"Glob"_q
		|| tool == u"Grep"_q) {
		done(ApproveFileTool(tool, input));
		return;
	} else if (tool.startsWith(QString::fromLatin1(kOwnPrefix))) {
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
