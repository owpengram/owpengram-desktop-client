/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_sessions.h"

#include "base/qt/qt_common_adapters.h"
#include "owpengram/claude/claude_session_format.h"
#include "logs.h"
#include "settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QUuid>

#include <algorithm>

namespace Owpengram::Claude {
namespace {

[[nodiscard]] QString AiPath() {
	return cWorkingDir() + u"ai/"_q;
}

[[nodiscard]] QString SessionsPath() {
	return AiPath() + u"sessions/"_q;
}

[[nodiscard]] QString ScopeToString(SessionScope scope) {
	switch (scope) {
	case SessionScope::Temporary: return u"temporary"_q;
	case SessionScope::Chat: return u"chat"_q;
	case SessionScope::Account: return u"account"_q;
	case SessionScope::All: return u"all"_q;
	}
	return u"temporary"_q;
}

[[nodiscard]] SessionScope ScopeFromString(const QString &value) {
	if (value == u"chat"_q) {
		return SessionScope::Chat;
	} else if (value == u"account"_q) {
		return SessionScope::Account;
	} else if (value == u"all"_q) {
		return SessionScope::All;
	}
	return SessionScope::Temporary;
}

[[nodiscard]] bool WriteJson(const QString &path, const QJsonObject &object) {
	QSaveFile file(path);
	return file.open(QIODevice::WriteOnly)
		&& file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) >= 0
		&& file.commit();
}

[[nodiscard]] QDateTime ParseCreated(const QString &text) {
	return QDateTime::fromString(text, Qt::ISODate);
}

struct Loaded {
	SessionInfo info;
	QString claudeSessionId;
};

[[nodiscard]] std::optional<Loaded> LoadFolder(const QString &folder) {
	QFile file(folder + u"/session.json"_q);
	if (!file.open(QIODevice::ReadOnly)) {
		return std::nullopt;
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	if (!document.isObject()) {
		return std::nullopt;
	}
	const auto object = document.object();
	auto result = Loaded();
	result.info.id = object.value(u"id"_q).toString();
	if (result.info.id.isEmpty()) {
		return std::nullopt;
	}
	result.info.scope = ScopeFromString(object.value(u"scope"_q).toString());
	result.info.accountKey = object.value(u"account"_q).toString();
	const auto peer = object.value(u"peer"_q).toString().toULongLong();
	result.info.peer = peer ? PeerId(PeerIdHelper(peer)) : PeerId();
	result.info.title = object.value(u"title"_q).toString();
	result.info.created = ParseCreated(object.value(u"created"_q).toString());
	result.info.folder = folder;
	result.claudeSessionId = object.value(u"claude_session_id"_q).toString();
	if (result.claudeSessionId.isEmpty()) {
		result.claudeSessionId = result.info.id;
	}
	return result;
}

} // namespace

Session::Session(SessionInfo info, QString claudeSessionId)
: _info(std::move(info))
, _claudeSessionId(std::move(claudeSessionId)) {
}

const SessionInfo &Session::info() const {
	return _info;
}

bool Session::persistent() const {
	return (_info.scope != SessionScope::Temporary)
		&& !_info.folder.isEmpty();
}

QString Session::claudeSessionId() const {
	return _claudeSessionId;
}

void Session::setClaudeSessionId(QString id) {
	if (_claudeSessionId == id) {
		return;
	}
	_claudeSessionId = std::move(id);
	writeMeta();
}

QString Session::transcriptPath() const {
	return persistent() ? (_info.folder + u"/transcript.md"_q) : QString();
}

void Session::appendTranscript(QString role, QString text) {
	if (!persistent()) {
		return;
	}
	const auto now = QDateTime::currentDateTime();
	const auto date = now.date();
	const auto time = now.time();
	const auto entry = TranscriptEntry(
		role.toStdString(),
		date.year(),
		date.month(),
		date.day(),
		time.hour(),
		time.minute(),
		text.toStdString());
	QDir().mkpath(_info.folder);
	QFile file(transcriptPath());
	if (!file.open(QIODevice::WriteOnly | QIODevice::Append)
		|| file.write(entry.data(), qint64(entry.size())) < 0) {
		LOG(("Claude: could not write transcript."));
	}
}

void Session::rename(QString title) {
	if (_info.title == title) {
		return;
	}
	_info.title = std::move(title);
	writeMeta();
}

void Session::writeMeta() const {
	if (!persistent()) {
		return;
	}
	auto object = QJsonObject();
	object.insert(u"id"_q, _info.id);
	object.insert(u"scope"_q, ScopeToString(_info.scope));
	object.insert(u"account"_q, _info.accountKey);
	object.insert(u"peer"_q, QString::number(_info.peer.value));
	object.insert(u"title"_q, _info.title);
	object.insert(u"created"_q, _info.created.toString(Qt::ISODate));
	object.insert(u"claude_session_id"_q, _claudeSessionId);
	QDir().mkpath(_info.folder);
	if (!WriteJson(_info.folder + u"/session.json"_q, object)) {
		LOG(("Claude: could not write session.json."));
	}
}

QString Sessions::aiPath() const {
	return AiPath();
}

std::shared_ptr<Session> Sessions::create(
		SessionScope scope,
		QString accountKey,
		PeerId peer) {
	auto info = SessionInfo{
		.id = QUuid::createUuid().toString(QUuid::WithoutBraces),
		.scope = scope,
		.accountKey = std::move(accountKey),
		.peer = peer,
		.created = QDateTime::currentDateTime(),
	};
	if (scope != SessionScope::Temporary) {
		const auto date = info.created.date();
		const auto time = info.created.time();
		const auto name = QString::fromStdString(SessionFolderName(
			date.year(),
			date.month(),
			date.day(),
			time.hour(),
			time.minute(),
			info.title.toStdString()));
		auto folder = SessionsPath() + name;
		for (auto n = 2; QFileInfo::exists(folder); ++n) {
			folder = SessionsPath() + name + '-' + QString::number(n);
		}
		info.folder = folder;
		// ai/ and ai/memory/ appear at the first write.
		QDir().mkpath(AiPath() + u"memory"_q);
	}
	const auto id = info.id;
	auto result = std::shared_ptr<Session>(new Session(std::move(info), id));
	result->writeMeta();
	_open[id] = result;
	return result;
}

std::vector<SessionInfo> Sessions::history() const {
	auto result = std::vector<SessionInfo>();
	const auto dirs = QDir(SessionsPath()).entryInfoList(
		QDir::Dirs | QDir::NoDotAndDotDot);
	for (const auto &dir : dirs) {
		if (auto loaded = LoadFolder(dir.absoluteFilePath())) {
			result.push_back(std::move(loaded->info));
		}
	}
	std::stable_sort(result.begin(), result.end(), [](
			const SessionInfo &a,
			const SessionInfo &b) {
		return a.created > b.created;
	});
	return result;
}

std::shared_ptr<Session> Sessions::open(const QString &id) {
	if (const auto already = find(id)) {
		return already;
	}
	const auto dirs = QDir(SessionsPath()).entryInfoList(
		QDir::Dirs | QDir::NoDotAndDotDot);
	for (const auto &dir : dirs) {
		auto loaded = LoadFolder(dir.absoluteFilePath());
		if (loaded && loaded->info.id == id) {
			auto result = std::shared_ptr<Session>(new Session(
				std::move(loaded->info),
				std::move(loaded->claudeSessionId)));
			_open[id] = result;
			return result;
		}
	}
	return nullptr;
}

std::shared_ptr<Session> Sessions::find(const QString &id) const {
	const auto i = _open.find(id);
	return (i != _open.end()) ? i->second.lock() : nullptr;
}

std::shared_ptr<Session> Sessions::lastForChat(
		const QString &accountKey,
		PeerId peer) const {
	auto best = std::shared_ptr<Session>();
	for (const auto &[id, weak] : _open) {
		const auto session = weak.lock();
		if (!session) {
			continue;
		}
		const auto &info = session->info();
		if (info.scope == SessionScope::Chat
			&& info.accountKey == accountKey
			&& info.peer == peer
			&& (!best || info.created > best->info().created)) {
			best = session;
		}
	}
	if (best) {
		return best;
	}
	for (const auto &info : history()) {
		if (info.scope == SessionScope::Chat
			&& info.accountKey == accountKey
			&& info.peer == peer) {
			return const_cast<Sessions*>(this)->open(info.id);
		}
	}
	return nullptr;
}

Sessions &AllSessions() {
	static auto instance = Sessions();
	return instance;
}

} // namespace Owpengram::Claude
