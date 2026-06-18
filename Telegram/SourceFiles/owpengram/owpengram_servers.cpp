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
#include "mtproto/details/mtproto_rsa_public_key.h"
#include "mtproto/mtproto_dc_options.h"
#include "mtproto/facade.h"
#include "mtproto/mtp_instance.h"
#include "base/timer.h"
#include "storage/localstorage.h"
#include "storage/storage_account.h"
#include "base/qt/qt_common_adapters.h"
#include "ui/image/image.h"

#include <QtCore/QDir>
#include <QtGui/QImage>
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
const auto kServerLogosDir = u"owpengram_server_logos"_q;
constexpr auto kCheckTimeoutMs = 3000;
constexpr auto kConnectTimeoutMs = 30000;
const auto kOfficialDefaultHost = u"89.28.58.29"_q;
constexpr auto kOfficialDefaultPort = 10443;
const auto kTeamgramDefaultHost = u"43.155.11.190"_q;
constexpr auto kTeamgramDefaultPort = 10443;

// Default RSA public keys for the built-in self-hosted servers (the binary now
// ships the real Telegram keys, so each self-hosted profile carries its own key).
// Replace kTeamgramRsaPublicKey with Teamgram's real key if it differs.
const auto kOfficialRsaPublicKey = u"\
-----BEGIN RSA PUBLIC KEY-----\n\
MIIBCgKCAQEAvKLEOWTzt9Hn3/9Kdp/RdHcEhzmd8xXeLSpHIIzaXTLJDw8BhJy1\n\
jR/iqeG8Je5yrtVabqMSkA6ltIpgylH///FojMsX1BHu4EPYOXQgB0qOi6kr08iX\n\
ZIH9/iOPQOWDsL+Lt8gDG0xBy+sPe/2ZHdzKMjX6O9B4sOsxjFrk5qDoWDrioJor\n\
AJ7eFAfPpOBf2w73ohXudSrJE0lbQ8pCWNpMY8cB9i8r+WBitcvouLDAvmtnTX7a\n\
khoDzmKgpJBYliAY4qA73v7u5UIepE8QgV0jCOhxJCPubP8dg+/PlLLVKyxU5Cdi\n\
QtZj2EMy4s9xlNKzX8XezE0MHEa6bQpnFwIDAQAB\n\
-----END RSA PUBLIC KEY-----"_q;

// Teamgram currently shares the same default self-hosted key (this is what it
// used before each profile carried its own key). Swap in Teamgram's real public
// key here if its server uses a different one.
const auto kTeamgramRsaPublicKey = u"\
-----BEGIN RSA PUBLIC KEY-----\n\
MIIBCgKCAQEAvKLEOWTzt9Hn3/9Kdp/RdHcEhzmd8xXeLSpHIIzaXTLJDw8BhJy1\n\
jR/iqeG8Je5yrtVabqMSkA6ltIpgylH///FojMsX1BHu4EPYOXQgB0qOi6kr08iX\n\
ZIH9/iOPQOWDsL+Lt8gDG0xBy+sPe/2ZHdzKMjX6O9B4sOsxjFrk5qDoWDrioJor\n\
AJ7eFAfPpOBf2w73ohXudSrJE0lbQ8pCWNpMY8cB9i8r+WBitcvouLDAvmtnTX7a\n\
khoDzmKgpJBYliAY4qA73v7u5UIepE8QgV0jCOhxJCPubP8dg+/PlLLVKyxU5Cdi\n\
QtZj2EMy4s9xlNKzX8XezE0MHEa6bQpnFwIDAQAB\n\
-----END RSA PUBLIC KEY-----"_q;

[[nodiscard]] QString ServersFilePath() {
	return cWorkingDir() + u"tdata/"_q + kServersFile;
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

[[nodiscard]] std::optional<Storage::OwpengramServerSelection>
ReadSavedServerSelection(not_null<Main::Account*> account) {
	return account->local().readOwpengramServer();
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
	result.rsaPublicKey = object.value(u"rsaPublicKey"_q).toString();
	result.logoPath = object.value(u"logoPath"_q).toString();
	result.isOfficial = false;
	result.multiDc = object.value(u"multiDc"_q).toBool(false);
	result.mainDcId = object.value(u"mainDcId"_q).toInt(0);
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
	if (!server.rsaPublicKey.isEmpty()) {
		object.insert(u"rsaPublicKey"_q, server.rsaPublicKey);
	}
	if (server.multiDc) {
		object.insert(u"multiDc"_q, true);
	}
	if (server.mainDcId > 0) {
		object.insert(u"mainDcId"_q, server.mainDcId);
	}
	return object;
}

[[nodiscard]] MTP::DcId MainDcIdForServer(const Server &server) {
	if (server.mainDcId > 0) {
		return MTP::DcId(server.mainDcId);
	}
	// Auto: multi-DC servers (Telegram) default to DC 2, single-server backends
	// default to DC 1.
	return MTP::DcId(server.multiDc ? 2 : 1);
}

// For single-server backends every dc_id (files, migrations) must resolve to the
// one physical address, so we publish the same host:port for DC 1..kSingleServerDcs.
constexpr auto kSingleServerDcs = 5;

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

[[nodiscard]] QString CustomServerLogosDirectory() {
	return cWorkingDir() + u"tdata/"_q + kServerLogosDir;
}

[[nodiscard]] bool IsCustomServerLogoPath(const QString &logoPath) {
	return logoPath.startsWith(kServerLogosDir);
}

[[nodiscard]] std::optional<QString> SaveCustomServerLogo(
		const QString &serverId,
		const QString &sourcePath) {
	auto image = Images::Read({
		.path = sourcePath,
		.forceOpaque = true,
	}).image;
	if (image.isNull()) {
		return std::nullopt;
	}
	const auto side = std::min(image.width(), image.height());
	if (side <= 0) {
		return std::nullopt;
	}
	image = image.copy(
		(image.width() - side) / 2,
		(image.height() - side) / 2,
		side,
		side);
	image = image.scaled(
		256,
		256,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	const auto dir = CustomServerLogosDirectory();
	QDir().mkpath(dir);
	const auto relative = kServerLogosDir + u"/"_q + serverId + u".png"_q;
	const auto fullPath = cWorkingDir() + u"tdata/"_q + relative;
	if (!image.save(fullPath, "PNG")) {
		return std::nullopt;
	}
	return relative;
}

void RemoveCustomServerLogoFile(const QString &logoPath) {
	if (!IsCustomServerLogoPath(logoPath)) {
		return;
	}
	QFile::remove(cWorkingDir() + u"tdata/"_q + logoPath);
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
	dcOptions->setOptionsLocked(false);

	if (server.multiDc) {
		// Multi-DC server: leave options unlocked so help.getConfig / the special
		// loader can discover the real alternate data-center addresses.
		if (server.isTelegram) {
			// Official Telegram: the built-in table already holds all 5 DCs.
			dcOptions->constructFromBuiltIn();
		} else {
			// Custom Telegram-compatible server: start from its main DC address;
			// help.getConfig will fill in the rest of its data centers.
			const auto dcId = MainDcIdForServer(server);
			dcOptions->setFromList(MTP_vector<MTPDcOption>(1, MTP_dcOption(
				MTP_flags(MTPDdcOption::Flag::f_static),
				MTP_int(dcId),
				MTP_string(server.host),
				MTP_int(server.port),
				MTPbytes())));
		}
		if (!server.rsaPublicKey.isEmpty()) {
			dcOptions->setPublicKeysFromPem(server.rsaPublicKey);
		} else {
			dcOptions->setBuiltInPublicKeys(true);
		}
		return;
	}

	// Single-server backend: publish the same host:port for DC 1..kSingleServerDcs
	// so any file/migration that references dc_id 2..5 still resolves to the one
	// physical server, then lock the options so nothing overwrites them.
	const auto flags = MTPDdcOption::Flag::f_static
		| MTPDdcOption::Flag::f_tcpo_only;
	auto list = QVector<MTPDcOption>();
	list.reserve(kSingleServerDcs);
	for (auto dcId = 1; dcId <= kSingleServerDcs; ++dcId) {
		list.push_back(MTP_dcOption(
			MTP_flags(flags),
			MTP_int(dcId),
			MTP_string(server.host),
			MTP_int(server.port),
			MTPbytes()));
	}
	if (!server.rsaPublicKey.isEmpty()) {
		dcOptions->setPublicKeysFromPem(server.rsaPublicKey);
	} else {
		// Single-server backends are NOT Telegram, so never fall back to the
		// built-in Telegram RSA keys (the server's fingerprint would never match).
		// Use the shared self-hosted default key instead.
		dcOptions->setPublicKeysFromPem(kOfficialRsaPublicKey);
	}
	dcOptions->setFromList(MTP_vector<MTPDcOption>(std::move(list)));
	dcOptions->setOptionsLocked(true);
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
	// Fallback for built-in ids that aren't in the live list for some reason:
	// start from the full known profile (kind, keys, dc) and override address.
	auto result = Server();
	if (selection.id == QString::fromLatin1(kTelegramServerId)) {
		result = TelegramServer();
	} else if (selection.id == QString::fromLatin1(kTeamgramServerId)) {
		result = TeamgramServer();
	} else if (selection.id == QString::fromLatin1(kOfficialServerId)) {
		result = OfficialServer();
	} else {
		// Unknown custom server: a single-server backend on dc 1 by default.
		result.name = selection.host;
		result.isOfficial = false;
	}
	result.id = selection.id;
	result.host = selection.host;
	result.port = selection.port;
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
	result.multiDc = true;
	result.mainDcId = 2;
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
	result.rsaPublicKey = kTeamgramRsaPublicKey;
	result.multiDc = false;
	result.mainDcId = 2;
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
	result.rsaPublicKey = kOfficialRsaPublicKey;
	result.multiDc = false;
	result.mainDcId = 1;
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
		const QString &description,
		const QString &rsaPublicKey,
		const QString &logoSourcePath,
		bool multiDc,
		int mainDcId) {
	if (name.trimmed().isEmpty() || host.trimmed().isEmpty() || port <= 0) {
		return std::nullopt;
	}
	auto server = Server();
	server.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	server.name = name.trimmed();
	server.host = host.trimmed();
	server.port = port;
	server.description = description.trimmed();
	server.rsaPublicKey = rsaPublicKey.trimmed();
	server.isOfficial = false;
	server.multiDc = multiDc;
	server.mainDcId = (mainDcId > 0) ? mainDcId : 0;
	if (!logoSourcePath.isEmpty()) {
		if (const auto saved = SaveCustomServerLogo(server.id, logoSourcePath)) {
			server.logoPath = *saved;
		}
	}

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
			const auto logo = array.at(i).toObject().value(u"logoPath"_q).toString();
			RemoveCustomServerLogoFile(logo);
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

QString ResolveServerLogoPath(const QString &logoPath) {
	if (logoPath.isEmpty()) {
		return QString();
	}
	if (logoPath.startsWith(u":/"_q) || QFileInfo(logoPath).isAbsolute()) {
		return logoPath;
	}
	return cWorkingDir() + u"tdata/"_q + logoPath;
}

bool IsValidRsaPublicKeyPem(const QString &pem) {
	const auto trimmed = pem.trimmed();
	if (trimmed.isEmpty()) {
		return true;
	}
	const auto utf8 = trimmed.toUtf8();
	return MTP::details::RSAPublicKey(bytes::make_span(utf8)).valid();
}

void ApplyServerToAccount(
		not_null<Main::Account*> account,
		const Server &server) {
	Expects(server.valid());

	const auto previous = ReadSavedServerSelection(account);
	if (previous && SelectionEqualsServer(*previous, server)) {
		return;
	}
	account->local().writeOwpengramServer(
		server.id,
		server.host,
		server.port);
	account->resetAuthorizationKeysForServerSwitch();
	if (account->local().peekLegacyLocalKey()) {
		account->local().writeMtpConfig();
	}
}

void WaitForServerConnection(
		not_null<Main::Account*> account,
		const Server &server,
		Fn<void(bool ok)> done) {
	const auto timer = std::make_shared<base::Timer>();
	const auto started = crl::now();
	timer->setCallback([=]() {
		auto &mtp = account->mtp();
		const auto dcId = mtp.mainDcId();
		const auto connected = (mtp.dcstate(dcId) == MTP::ConnectedState)
			&& EndpointMatchesServer(&mtp, server);
		if (connected) {
			done(true);
			return;
		} else if (crl::now() - started > kConnectTimeoutMs) {
			done(false);
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
