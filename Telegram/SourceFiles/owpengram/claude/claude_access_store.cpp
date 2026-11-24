/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_access_store.h"

#include "base/qt/qt_common_adapters.h"
#include "base/random.h"
#include "data/data_peer.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "owpengram/owpengram_servers.h"
#include "logs.h"
#include "settings.h"

#include <array>

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QJsonArray>
#include <QtCore/QSaveFile>

namespace Owpengram::Claude {
namespace {

constexpr auto kFileVersion = 1;
constexpr auto kSaveDelay = crl::time(1000);

[[nodiscard]] QString FilePath() {
	return cWorkingDir() + u"tdata/owpengram_claude.json"_q;
}

[[nodiscard]] QString LevelToString(Level level) {
	const auto view = Serialize(level);
	return QString::fromLatin1(view.data(), int(view.size()));
}

[[nodiscard]] std::optional<Level> LevelFromValue(const QJsonValue &value) {
	if (!value.isString()) {
		return std::nullopt;
	}
	return ParseLevel(value.toString().toStdString());
}

[[nodiscard]] QString NewToken() {
	auto bytes = base::RandomValue<std::array<uchar, 32>>();
	return QString::fromLatin1(QByteArray(
		reinterpret_cast<const char*>(bytes.data()),
		int(bytes.size())).toHex());
}

} // namespace

QString AccountKey(not_null<Main::Account*> account) {
	if (!account->sessionExists()) {
		return QString();
	}
	auto scope = ServerScopeKeyForAccount(account);
	if (scope.isEmpty()) {
		scope = u"telegram"_q;
	}
	return scope
		+ u":"_q
		+ QString::number(account->session().userId().bare);
}

AccessStore::AccessStore()
: _saveTimer([=] { save(); }) {
	load();
}

Level AccessStore::accountDefault(const QString &accountKey) const {
	const auto i = _accounts.find(accountKey);
	return (i != _accounts.end()) ? i->second.defaultLevel : Level::Forbidden;
}

std::optional<Level> AccessStore::chatOverride(
		const QString &accountKey,
		PeerId peer) const {
	const auto i = _accounts.find(accountKey);
	if (i == _accounts.end()) {
		return std::nullopt;
	}
	const auto j = i->second.chats.find(peer);
	return (j != i->second.chats.end())
		? std::make_optional(j->second)
		: std::nullopt;
}

bool AccessStore::hasOverrideAtLeast(
		const QString &accountKey,
		Level level) const {
	const auto i = _accounts.find(accountKey);
	if (i == _accounts.end()) {
		return false;
	}
	for (const auto &[peer, value] : i->second.chats) {
		if (value >= level) {
			return true;
		}
	}
	return false;
}

Level AccessStore::effective(not_null<PeerData*> peer) const {
	const auto key = AccountKey(&peer->session().account());
	if (key.isEmpty()) {
		return Level::Forbidden;
	}
	return Effective(accountDefault(key), chatOverride(key, peer->id));
}

void AccessStore::setAccountDefault(const QString &accountKey, Level level) {
	if (accountKey.isEmpty()) {
		return;
	}
	auto &data = _accounts[accountKey];
	if (data.defaultLevel == level) {
		return;
	}
	data.defaultLevel = level;
	scheduleSave();
	_changes.fire({});
}

void AccessStore::setChatOverride(
		const QString &accountKey,
		PeerId peer,
		std::optional<Level> level) {
	if (accountKey.isEmpty() || !peer) {
		return;
	}
	if (level) {
		auto &chats = _accounts[accountKey].chats;
		const auto i = chats.find(peer);
		if (i != chats.end() && i->second == *level) {
			return;
		}
		chats[peer] = *level;
	} else {
		const auto i = _accounts.find(accountKey);
		if (i == _accounts.end() || !i->second.chats.remove(peer)) {
			return;
		}
	}
	scheduleSave();
	_changes.fire({});
}

rpl::producer<> AccessStore::changes() const {
	return _changes.events();
}

bool AccessStore::mcpEnabled() const {
	return _mcpEnabled;
}

void AccessStore::setMcpEnabled(bool enabled) {
	if (_mcpEnabled == enabled) {
		return;
	}
	_mcpEnabled = enabled;
	scheduleSave();
	_changes.fire({});
}

int AccessStore::mcpPort() const {
	return _mcpPort;
}

QString AccessStore::mcpToken() {
	if (_mcpToken.isEmpty()) {
		_mcpToken = NewToken();
		scheduleSave();
	}
	return _mcpToken;
}

void AccessStore::regenerateMcpToken() {
	_mcpToken = NewToken();
	scheduleSave();
	_changes.fire({});
}

const std::vector<AccessStore::ClaudeProfile> &AccessStore::claudeProfiles(
) const {
	return _claudeProfiles;
}

void AccessStore::setClaudeProfile(const QString &id, const QString &email) {
	if (id.isEmpty()) {
		return;
	}
	for (auto &profile : _claudeProfiles) {
		if (profile.id == id) {
			if (profile.email != email) {
				profile.email = email;
				scheduleSave();
				_changes.fire({});
			}
			return;
		}
	}
	_claudeProfiles.push_back({ .id = id, .email = email });
	scheduleSave();
	_changes.fire({});
}

void AccessStore::removeClaudeProfile(const QString &id) {
	const auto i = ranges::find(_claudeProfiles, id, &ClaudeProfile::id);
	if (i == end(_claudeProfiles)) {
		return;
	}
	_claudeProfiles.erase(i);
	if (_selectedClaudeProfile == id) {
		_selectedClaudeProfile = QString();
	}
	scheduleSave();
	_changes.fire({});
}

QString AccessStore::selectedClaudeProfile() const {
	return _selectedClaudeProfile;
}

void AccessStore::selectClaudeProfile(const QString &id) {
	const auto known = id.isEmpty()
		|| ranges::contains(_claudeProfiles, id, &ClaudeProfile::id);
	if (!known || _selectedClaudeProfile == id) {
		return;
	}
	_selectedClaudeProfile = id;
	scheduleSave();
	_changes.fire({});
}

bool AccessStore::panelOpen() const {
	return _panelOpen;
}

void AccessStore::setPanelOpen(bool open) {
	if (_panelOpen != open) {
		_panelOpen = open;
		scheduleSave();
	}
}

bool AccessStore::panelPinned() const {
	return _panelPinned;
}

void AccessStore::setPanelPinned(bool pinned) {
	if (_panelPinned != pinned) {
		_panelPinned = pinned;
		scheduleSave();
	}
}

void AccessStore::load() {
	QFile file(FilePath());
	if (!file.exists()) {
		return;
	}
	if (!file.open(QIODevice::ReadOnly)) {
		LOG(("Claude: bad access file (cannot open)."));
		return;
	}
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(file.readAll(), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		LOG(("Claude: bad access file (%1).").arg(error.errorString()));
		return;
	}
	const auto root = document.object();

	const auto mcp = root.value(u"mcp"_q).toObject();
	_mcpEnabled = mcp.value(u"enabled"_q).toBool(false);
	const auto port = mcp.value(u"port"_q).toInt(kDefaultMcpPort);
	_mcpPort = (port > 0 && port < 65536) ? port : kDefaultMcpPort;
	_mcpToken = mcp.value(u"token"_q).toString();

	const auto claude = root.value(u"claude"_q).toObject();
	for (const auto &value : claude.value(u"profiles"_q).toArray()) {
		const auto object = value.toObject();
		const auto id = object.value(u"id"_q).toString();
		// The id is a folder name: only what NewProfileId() makes.
		static const auto valid = QRegularExpression(u"^[a-f0-9]{32}$"_q);
		if (valid.match(id).hasMatch()) {
			_claudeProfiles.push_back({
				.id = id,
				.email = object.value(u"email"_q).toString(),
			});
		}
	}
	const auto selected = claude.value(u"selected"_q).toString();
	if (ranges::contains(_claudeProfiles, selected, &ClaudeProfile::id)) {
		_selectedClaudeProfile = selected;
	}

	const auto panel = root.value(u"panel"_q).toObject();
	_panelOpen = panel.value(u"open"_q).toBool(false);
	_panelPinned = panel.value(u"pinned"_q).toBool(false);

	const auto accounts = root.value(u"accounts"_q).toObject();
	for (auto i = accounts.begin(); i != accounts.end(); ++i) {
		const auto object = i.value().toObject();
		auto data = AccountData();
		data.defaultLevel = LevelFromValue(
			object.value(u"default"_q)).value_or(Level::Forbidden);
		const auto chats = object.value(u"chats"_q).toObject();
		for (auto j = chats.begin(); j != chats.end(); ++j) {
			auto ok = false;
			const auto id = j.key().toULongLong(&ok);
			const auto level = LevelFromValue(j.value());
			if (ok && id && level) {
				data.chats.emplace(PeerId(PeerIdHelper(id)), *level);
			}
		}
		_accounts.emplace(i.key(), std::move(data));
	}
}

void AccessStore::hookQuit() {
	if (_quitHooked) {
		return;
	}
	const auto app = QCoreApplication::instance();
	if (!app) {
		return;
	}
	_quitHooked = true;
	QObject::connect(app, &QCoreApplication::aboutToQuit, [=] {
		flush();
	});
}

void AccessStore::scheduleSave() {
	hookQuit();
	_dirty = true;
	_saveTimer.callOnce(kSaveDelay);
}

void AccessStore::flush() {
	if (_dirty) {
		save();
	}
}

void AccessStore::save() {
	_saveTimer.cancel();
	_dirty = false;

	auto accounts = QJsonObject();
	for (const auto &[key, data] : _accounts) {
		auto chats = QJsonObject();
		for (const auto &[peer, level] : data.chats) {
			chats.insert(
				QString::number(peer.value),
				LevelToString(level));
		}
		auto object = QJsonObject();
		object.insert(u"default"_q, LevelToString(data.defaultLevel));
		object.insert(u"chats"_q, chats);
		accounts.insert(key, object);
	}
	auto mcp = QJsonObject();
	mcp.insert(u"enabled"_q, _mcpEnabled);
	mcp.insert(u"port"_q, _mcpPort);
	mcp.insert(u"token"_q, _mcpToken);

	auto root = QJsonObject();
	root.insert(u"version"_q, kFileVersion);
	root.insert(u"mcp"_q, mcp);
	auto profiles = QJsonArray();
	for (const auto &profile : _claudeProfiles) {
		auto object = QJsonObject();
		object.insert(u"id"_q, profile.id);
		object.insert(u"email"_q, profile.email);
		profiles.push_back(object);
	}
	auto claude = QJsonObject();
	claude.insert(u"profiles"_q, profiles);
	claude.insert(u"selected"_q, _selectedClaudeProfile);
	root.insert(u"claude"_q, claude);
	auto panel = QJsonObject();
	panel.insert(u"open"_q, _panelOpen);
	panel.insert(u"pinned"_q, _panelPinned);
	root.insert(u"panel"_q, panel);
	root.insert(u"accounts"_q, accounts);

	const auto path = FilePath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile file(path);
	// The file holds the MCP bearer token: owner-only access.
	if (!file.open(QIODevice::WriteOnly)
		|| !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
		|| file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0
		|| !file.commit()) {
		LOG(("Claude: could not write access file."));
	}
}

QString ClaudeProfilePath(const QString &id) {
	return cWorkingDir() + u"tdata/owpengram_claude_profiles/"_q + id + '/';
}

AccessStore &Access() {
	// Never destroyed: avoids static-destruction-order issues with the
	// QObject-based timer. The aboutToQuit flush is connected lazily
	// (see AccessStore::hookQuit), as QCoreApplication may not exist yet.
	static auto instance = new AccessStore();
	return *instance;
}

} // namespace Owpengram::Claude
