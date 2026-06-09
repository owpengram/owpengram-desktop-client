/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/owpengram_servers.h"

#include "core/application.h"
#include "crl/crl_on_main.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "mtproto/facade.h"
#include "mtproto/mtproto_config.h"
#include "mtproto/mtproto_dc_options.h"
#include "mtproto/facade.h"
#include "mtproto/mtp_instance.h"
#include "base/timer.h"
#include "storage/localstorage.h"
#include "storage/storage_account.h"
#include "base/qt/qt_common_adapters.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUuid>
#include <QtNetwork/QTcpSocket>

namespace Owpengram {
namespace {

const auto kServersFile = u"owpengram_servers.json"_q;
const auto kServerSelectionFallbackFile = u"owpengram_server_selection.json"_q;
constexpr auto kCheckTimeoutMs = 3000;
const auto kOfficialDefaultHost = u"127.0.0.1"_q;
constexpr auto kOfficialDefaultPort = 10443;
const auto kTeamgramDefaultHost = u"43.155.11.190"_q;
constexpr auto kTeamgramDefaultPort = 10443;

[[nodiscard]] QString ServersFilePath() {
	return cWorkingDir() + u"tdata/"_q + kServersFile;
}

[[nodiscard]] QString ServerSelectionFallbackPath() {
	return cWorkingDir() + u"tdata/"_q + kServerSelectionFallbackFile;
}

[[nodiscard]] QJsonArray ReadCustomServersJson() {
	const auto path = ServersFilePath();
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	if (!document.isArray()) {
		return {};
	}
	return document.array();
}

void WriteCustomServersJson(const QJsonArray &array) {
	const auto path = ServersFilePath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return;
	}
	file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

void WriteServerSelectionFallback(const Server &server) {
	const auto path = ServerSelectionFallbackPath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return;
	}
	auto object = QJsonObject();
	object.insert(u"id"_q, server.id);
	object.insert(u"host"_q, server.host);
	object.insert(u"port"_q, server.port);
	file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

[[nodiscard]] std::optional<Storage::OwpengramServerSelection>
ReadServerSelectionFallback() {
	const auto path = ServerSelectionFallbackPath();
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return std::nullopt;
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	if (!document.isObject()) {
		return std::nullopt;
	}
	const auto object = document.object();
	const auto id = object.value(u"id"_q).toString();
	const auto host = object.value(u"host"_q).toString();
	const auto port = object.value(u"port"_q).toInt();
	if (id.isEmpty() || host.isEmpty() || port <= 0) {
		return std::nullopt;
	}
	return Storage::OwpengramServerSelection{
		.id = id,
		.host = host,
		.port = port,
	};
}

[[nodiscard]] std::optional<Storage::OwpengramServerSelection>
ReadSavedServerSelection(not_null<Main::Account*> account) {
	if (const auto encrypted = account->local().readOwpengramServer()) {
		return encrypted;
	}
	return ReadServerSelectionFallback();
}

[[nodiscard]] bool SelectionEqualsServer(
		const Storage::OwpengramServerSelection &selection,
		const Server &server) {
	return (selection.id == server.id)
		&& (selection.host == server.host)
		&& (selection.port == server.port);
}

[[nodiscard]] Server ServerFromJson(const QJsonObject &object) {
	auto result = Server();
	result.id = object.value(u"id"_q).toString();
	result.name = object.value(u"name"_q).toString();
	result.host = object.value(u"host"_q).toString();
	result.port = object.value(u"port"_q).toInt();
	result.description = object.value(u"description"_q).toString();
	result.logoPath = object.value(u"logoPath"_q).toString();
	if (result.logoPath.isEmpty()) {
		result.logoPath = DefaultLogoPath();
	}
	result.isOfficial = false;
	return result;
}

[[nodiscard]] QJsonObject ServerToJson(const Server &server) {
	auto object = QJsonObject();
	object.insert(u"id"_q, server.id);
	object.insert(u"name"_q, server.name);
	object.insert(u"host"_q, server.host);
	object.insert(u"port"_q, server.port);
	object.insert(u"description"_q, server.description);
	if (!server.logoPath.isEmpty()) {
		object.insert(u"logoPath"_q, server.logoPath);
	}
	return object;
}

[[nodiscard]] MTP::DcId MainDcIdForServer(const Server &server) {
	return server.isTelegram
		? MTP::DcId(2)
		: MTP::Instance::Fields::kDefaultMainDc;
}

[[nodiscard]] bool EndpointMatchesServer(
		not_null<const MTP::Instance*> mtp,
		const Server &server) {
	const auto &options = mtp->dcOptions();
	const auto variants = options.lookup(
		MainDcIdForServer(server),
		MTP::DcType::Regular,
		false);
	for (auto address = 0; address != MTP::DcOptions::Variants::AddressTypeCount; ++address) {
		for (auto protocol = 0; protocol != MTP::DcOptions::Variants::ProtocolCount; ++protocol) {
			for (const auto &endpoint : variants.data[address][protocol]) {
				if (endpoint.ip == server.host.toStdString()
					&& endpoint.port == server.port) {
					return true;
				}
			}
		}
	}
	return false;
}

[[nodiscard]] std::vector<Server> ReadCustomServers() {
	auto result = std::vector<Server>();
	for (const auto &value : ReadCustomServersJson()) {
		if (!value.isObject()) {
			continue;
		}
		const auto server = ServerFromJson(value.toObject());
		if (server.valid()) {
			result.push_back(server);
		}
	}
	return result;
}

void ApplyServerToDcOptions(
		not_null<MTP::DcOptions*> dcOptions,
		const Server &server) {
	const auto dcId = MainDcIdForServer(server);
	const auto flags = server.isTelegram
		? MTPDdcOption::Flag::f_static
		: (MTPDdcOption::Flag::f_static | MTPDdcOption::Flag::f_tcpo_only);
	dcOptions->setBuiltInPublicKeys(server.isTelegram);
	dcOptions->setOptionsLocked(false);
	dcOptions->setFromList(MTP_vector<MTPDcOption>(1, MTP_dcOption(
		MTP_flags(flags),
		MTP_int(dcId),
		MTP_string(server.host),
		MTP_int(server.port),
		MTPbytes())));
	if (!server.isTelegram) {
		dcOptions->setOptionsLocked(true);
	}
}

} // namespace

[[nodiscard]] Server ServerFromStoredSelection(
		const Storage::OwpengramServerSelection &selection) {
	if (const auto known = FindServer(selection.id)) {
		auto result = *known;
		result.host = selection.host;
		result.port = selection.port;
		return result;
	}
	auto result = Server();
	result.id = selection.id;
	result.host = selection.host;
	result.port = selection.port;
	result.logoPath = DefaultLogoPath();
	if (selection.id == QString::fromLatin1(kTelegramServerId)) {
		const auto telegram = TelegramServer();
		result.name = telegram.name;
		result.description = telegram.description;
		result.isOfficial = true;
		result.isTelegram = true;
	} else if (selection.id == QString::fromLatin1(kTeamgramServerId)) {
		const auto teamgram = TeamgramServer();
		result.name = teamgram.name;
		result.description = teamgram.description;
		result.logoPath = teamgram.logoPath;
	} else if (selection.id == QString::fromLatin1(kOfficialServerId)) {
		const auto official = OfficialServer();
		result.name = official.name;
		result.description = official.description;
		result.isOfficial = true;
	} else {
		result.name = selection.host;
		result.isOfficial = false;
	}
	return result;
}

QString DefaultLogoPath() {
	return u":/gui/art/logo_256.png"_q;
}

QString TelegramLogoPath() {
	return u":/gui/art/telegram_logo_256.png"_q;
}

QString TeamgramLogoPath() {
	return u":/gui/art/teamgram_logo_256.png"_q;
}

Server TelegramServer() {
	auto result = Server();
	result.id = QString::fromLatin1(kTelegramServerId);
	result.name = tr::lng_owpengram_server_telegram_name(tr::now);
	result.description = tr::lng_owpengram_server_telegram_description(tr::now);
	result.host = u"149.154.167.51"_q;
	result.port = 443;
	result.logoPath = TelegramLogoPath();
	result.isOfficial = true;
	result.isTelegram = true;
	return result;
}

Server TeamgramServer() {
	auto result = Server();
	result.id = QString::fromLatin1(kTeamgramServerId);
	result.name = tr::lng_owpengram_server_teamgram_name(tr::now);
	result.description = tr::lng_owpengram_server_teamgram_description(tr::now);
	result.host = kTeamgramDefaultHost;
	result.port = kTeamgramDefaultPort;
	result.logoPath = TeamgramLogoPath();
	result.isOfficial = true;
	return result;
}

Server OfficialServer() {
	auto result = Server();
	result.id = QString::fromLatin1(kOfficialServerId);
	result.name = tr::lng_owpengram_server_official_name(tr::now);
	result.description = tr::lng_owpengram_server_official_description(tr::now);
	result.logoPath = DefaultLogoPath();
	result.isOfficial = true;
	result.host = kOfficialDefaultHost;
	result.port = kOfficialDefaultPort;
	return result;
}

QString FormatEndpoint(const Server &server) {
	return u"IP: %1\nPort: %2"_q.arg(
		server.host,
		server.port > 0 ? QString::number(server.port) : u"—"_q);
}

std::vector<Server> ListServers() {
	auto result = std::vector<Server>();
	result.push_back(TelegramServer());
	result.push_back(TeamgramServer());
	result.push_back(OfficialServer());
	for (const auto &custom : ReadCustomServers()) {
		result.push_back(custom);
	}
	return result;
}

std::optional<Server> FindServer(const QString &id) {
	for (const auto &server : ListServers()) {
		if (server.id == id) {
			return server;
		}
	}
	return std::nullopt;
}

std::optional<Server> AddCustomServer(
		const QString &name,
		const QString &host,
		int port,
		const QString &description) {
	if (name.trimmed().isEmpty() || host.trimmed().isEmpty() || port <= 0) {
		return std::nullopt;
	}
	auto server = Server();
	server.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	server.name = name.trimmed();
	server.host = host.trimmed();
	server.port = port;
	server.description = description.trimmed();
	server.logoPath = DefaultLogoPath();
	server.isOfficial = false;

	auto array = ReadCustomServersJson();
	array.push_back(ServerToJson(server));
	WriteCustomServersJson(array);
	return server;
}

bool IsRemovableServer(const Server &server) {
	return !server.isOfficial
		&& server.id != QString::fromLatin1(kTeamgramServerId)
		&& server.id != QString::fromLatin1(kTelegramServerId)
		&& server.id != QString::fromLatin1(kOfficialServerId);
}

bool RemoveCustomServer(const QString &id) {
	if (id == QString::fromLatin1(kOfficialServerId)
		|| id == QString::fromLatin1(kTelegramServerId)
		|| id == QString::fromLatin1(kTeamgramServerId)) {
		return false;
	}
	auto array = ReadCustomServersJson();
	auto changed = false;
	for (auto i = 0; i != array.size(); ++i) {
		if (!array.at(i).isObject()) {
			continue;
		}
		if (array.at(i).toObject().value(u"id"_q).toString() == id) {
			array.removeAt(i);
			changed = true;
			break;
		}
	}
	if (changed) {
		WriteCustomServersJson(array);
	}
	return changed;
}

void RestoreServerToConfig(
		not_null<Main::Account*> account,
		not_null<MTP::Config*> config) {
	const auto selection = ReadSavedServerSelection(account);
	if (!selection) {
		// No saved server means server selection hasn't happened yet.
		// If the config somehow has locked options (inherited from an owpengram
		// fallback config), unlock it so the new account doesn't silently talk
		// to owpengram before the user picks a server.
		if (config->dcOptions().optionsLocked()) {
			config->dcOptions().setOptionsLocked(false);
		}
		return;
	}
	const auto server = ServerFromStoredSelection(*selection);
	if (!server.valid()) {
		return;
	}
	if (server.isTelegram) {
		// For Telegram accounts, restore RSA keys and lock state but do NOT
		// overwrite the full saved DC option list (DC1-DC5 from the previous
		// session). Using setFromList (overwrite) would replace them with only
		// DC2, causing download failures for media on DC3/DC4/DC5 until
		// help.getConfig responds (~1-3 seconds). Instead use addFromList so
		// DC2 is present but no saved DCs are discarded.
		config->dcOptions().setBuiltInPublicKeys(true);
		config->dcOptions().setOptionsLocked(false);
		config->dcOptions().addFromList(MTP_vector<MTPDcOption>(1, MTP_dcOption(
			MTP_flags(MTPDdcOption::Flag::f_static),
			MTP_int(MTP::DcId(2)),
			MTP_string(server.host),
			MTP_int(server.port),
			MTPbytes())));
	} else {
		ApplyServerToDcOptions(&config->dcOptions(), server);
	}
}

void RestoreServerToAccount(not_null<Main::Account*> account) {
	const auto selection = ReadSavedServerSelection(account);
	if (!selection) {
		// No saved server: ensure the live MTP instance isn't locked either.
		auto &mtp = account->mtp();
		if (mtp.dcOptions().optionsLocked()) {
			mtp.dcOptions().setOptionsLocked(false);
		}
		return;
	}
	const auto server = ServerFromStoredSelection(*selection);
	if (!server.valid()) {
		return;
	}
	auto &mtp = account->mtp();
	if (server.isTelegram) {
		mtp.dcOptions().setOptionsLocked(false);
		if (EndpointMatchesServer(&mtp, server)) {
			return;
		}
		const auto dcId = mtp.mainDcId();
		ApplyServerToDcOptions(&mtp.dcOptions(), server);
		mtp.reInitConnection(dcId);
		return;
	}
	const auto dcId = MainDcIdForServer(server);
	mtp.dcOptions().setBuiltInPublicKeys(false);
	ApplyServerToDcOptions(&mtp.dcOptions(), server);
	mtp.setMainDcId(dcId);
	mtp.reInitConnection(dcId);
}

Server CurrentServerForAccount(not_null<Main::Account*> account) {
	if (const auto selection = ReadSavedServerSelection(account)) {
		const auto server = ServerFromStoredSelection(*selection);
		if (server.valid()) {
			return server;
		}
	}
	return OfficialServer();
}

bool ShouldUseCloudLangPack() {
	return false;
}

void ApplyServerToAccount(
		not_null<Main::Account*> account,
		const Server &server) {
	Expects(server.valid());

	const auto previous = ReadSavedServerSelection(account);
	if (previous && SelectionEqualsServer(*previous, server)) {
		return;
	}
	WriteServerSelectionFallback(server);
	if (account->local().peekLegacyLocalKey()) {
		account->local().writeOwpengramServer(
			server.id,
			server.host,
			server.port);
	}
	account->resetAuthorizationKeysForServerSwitch();
	account->local().writeMtpConfig();
}

void WaitForServerConnection(
		not_null<Main::Account*> account,
		const Server &server,
		Fn<void()> done) {
	const auto timer = std::make_shared<base::Timer>();
	const auto started = crl::now();
	timer->setCallback([=]() {
		auto &mtp = account->mtp();
		const auto dcId = mtp.mainDcId();
		const auto connected = (mtp.dcstate(dcId) == MTP::ConnectedState)
			&& EndpointMatchesServer(&mtp, server);
		if (connected || crl::now() - started > 10000) {
			done();
			return;
		}
		timer->callOnce(100);
	});
	timer->callOnce(100);
}

void CheckServerOnline(
		const Server &server,
		Fn<void(bool online, int latencyMs)> done) {
	if (!server.valid()) {
		done(false, -1);
		return;
	}
	const auto host = server.host;
	const auto port = server.port;
	crl::async([=, done = std::move(done)]() mutable {
		QTcpSocket socket;
		const auto begin = crl::now();
		socket.connectToHost(host, port);
		const auto connected = socket.waitForConnected(kCheckTimeoutMs);
		const auto latency = int(crl::now() - begin);
		if (connected) {
			socket.disconnectFromHost();
			if (socket.state() != QAbstractSocket::UnconnectedState) {
				socket.waitForDisconnected(1000);
			}
		}
		crl::on_main([=, done = std::move(done)]() mutable {
			done(connected, connected ? latency : -1);
		});
	});
}

} // namespace Owpengram
