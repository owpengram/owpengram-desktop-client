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
#include "main/main_domain.h"
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
const auto kOfficialDefaultHost = u"152.89.254.50"_q;
constexpr auto kOfficialDefaultPort = 2398;

// Default RSA public key for the built-in self-hosted OwpenGram server.
const auto kOfficialRsaPublicKey = u"\
-----BEGIN RSA PUBLIC KEY-----\n\
MIIBCgKCAQEAxUi0fsjiop7kTI+c+ATHwIs+XPcircj9WL/tNASH45/phiIvxhPU\n\
z4T6OgUfDVpQEC8SWCuq77aZAhnpZ0rfQ+h6vEv7X970wIAPT/hbyzekyWEFmH5Q\n\
RxSPrOMKF/V0wuTOVJcoHZW5r5cK7xsVe/otdYdOgt67kITS7pqoO1BlstRuOEHL\n\
jhaJ/40dXocrWpDQlJP2TZFwk5JF1Pbx/2mLr/asQapc/qbQP82b+iDLW8QIBT0f\n\
+xi4Js4k7Qo9kZSuUHUCDzmJt6Z0USBxxp/tSZRVjWRaT9ORDrdyfb/mKFSt9BtC\n\
B5VRJ1e0q7P/9/w21T0p9uV3eNXhnPnLFQIDAQAB\n\
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

// Single choke point every AddCustomServer/UpdateCustomServer/
// RemoveCustomServer writes through, so firing the change notification here
// (rather than separately in each of those three) can never drift out of
// sync with a future fourth mutator.
[[nodiscard]] rpl::event_stream<> &CustomServersChangedStream() {
	static auto stream = rpl::event_stream<>();
	return stream;
}

void WriteCustomServersJson(const QJsonArray &array) {
	const auto path = ServersFilePath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return;
	}
	file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
	// Close (flush) explicitly before firing: the change notification's
	// subscribers re-read this same file synchronously and immediately
	// (ServerSelectWidget's list rebuild), and on Windows a second QFile
	// opened for reading against a path this object still has open for
	// writing can see stale buffered content rather than what was just
	// written -- this was the actual cause of the server-select list not
	// updating live for an owpg://addserver-added server until the screen
	// was left and re-entered (by which point this object had long since
	// gone out of scope and closed on its own).
	file.close();
	CustomServersChangedStream().fire({});
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

std::optional<Server> UpdateCustomServer(
		const QString &id,
		const QString &name,
		const QString &host,
		int port,
		const QString &description,
		const QString &rsaPublicKey,
		const QString &logoSourcePath,
		bool multiDc,
		int mainDcId) {
	if (id.isEmpty()
		|| name.trimmed().isEmpty()
		|| host.trimmed().isEmpty()
		|| port <= 0) {
		return std::nullopt;
	}
	auto array = ReadCustomServersJson();
	auto index = -1;
	for (auto i = 0; i != array.size(); ++i) {
		if (array.at(i).isObject()
			&& array.at(i).toObject().value(u"id"_q).toString() == id) {
			index = i;
			break;
		}
	}
	if (index < 0) {
		return std::nullopt;
	}

	auto server = Server();
	server.id = id;
	server.name = name.trimmed();
	server.host = host.trimmed();
	server.port = port;
	server.description = description.trimmed();
	server.rsaPublicKey = rsaPublicKey.trimmed();
	server.isOfficial = false;
	server.multiDc = multiDc;
	server.mainDcId = (mainDcId > 0) ? mainDcId : 0;
	// logoSourcePath empty means "unchanged" -- keep whatever this server
	// already had on disk instead of silently dropping it.
	server.logoPath = array.at(index).toObject().value(u"logoPath"_q).toString();
	if (!logoSourcePath.isEmpty()) {
		if (const auto saved = SaveCustomServerLogo(server.id, logoSourcePath)) {
			server.logoPath = *saved;
		}
	}

	array[index] = ServerToJson(server);
	WriteCustomServersJson(array);
	return server;
}

bool IsRemovableServer(const Server &server) {
	return !server.isOfficial
		&& server.id != QString::fromLatin1(kTelegramServerId)
		&& server.id != QString::fromLatin1(kOfficialServerId);
}

bool RemoveCustomServer(const QString &id) {
	if (id == QString::fromLatin1(kOfficialServerId)
		|| id == QString::fromLatin1(kTelegramServerId)) {
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

rpl::producer<> CustomServersChanges() {
	return CustomServersChangedStream().events();
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
		// A locked config means the account was previously on a single-server
		// backend (owpengram/custom), which force-maps every dc_id
		// 1..5 onto that one non-Telegram host (see the single-server branch
		// of ApplyServerToDcOptions below). In that case DC1/3/4/5 are
		// poisoned with the wrong address and must be fully reset, not
		// preserved — otherwise the client silently keeps talking to the old
		// server on those DCs while believing it's connected to Telegram
		// (this was the actual cause of "new account can't connect" after a
		// prior owpengram login: AuthKey/RSA and empty dc_options errors).
		//
		// When the config was NOT locked (a genuine prior Telegram session),
		// keep the existing optimization: restore RSA keys and unlock, but
		// don't overwrite the full saved DC option list (DC1-DC5 from the
		// previous session). Using setFromList (overwrite) would replace them
		// with only DC2, causing download failures for media on DC3/DC4/DC5
		// until help.getConfig responds (~1-3 seconds). Instead use
		// addFromList so DC2 is present but no saved DCs are discarded.
		const auto wasLocked = config->dcOptions().optionsLocked();
		config->dcOptions().setBuiltInPublicKeys(true);
		config->dcOptions().setOptionsLocked(false);
		if (wasLocked) {
			config->dcOptions().constructFromBuiltIn();
		} else {
			config->dcOptions().addFromList(MTP_vector<MTPDcOption>(1, MTP_dcOption(
				MTP_flags(MTPDdcOption::Flag::f_static),
				MTP_int(MTP::DcId(2)),
				MTP_string(server.host),
				MTP_int(server.port),
				MTPbytes())));
		}
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
		// Same reasoning as RestoreServerToConfig above: a locked instance is
		// coming from a single-server backend, so DC1/3/4/5 may be poisoned
		// even if DC2 happens to already match (e.g. RestoreServerToConfig
		// just added it) — never take the "already matches" shortcut in that
		// case, always force the full built-in-table reset.
		const auto wasLocked = mtp.dcOptions().optionsLocked();
		mtp.dcOptions().setOptionsLocked(false);
		if (!wasLocked && EndpointMatchesServer(&mtp, server)) {
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

std::vector<not_null<Main::Account*>> AccountsUsingServer(
		const QString &serverId) {
	auto result = std::vector<not_null<Main::Account*>>();
	if (serverId.isEmpty()) {
		return result;
	}
	for (const auto &account : Core::App().domain().orderedAccounts()) {
		if (CurrentServerForAccount(account).id == serverId) {
			result.push_back(account);
		}
	}
	return result;
}

QString ServerScopeKeyForAccount(not_null<Main::Account*> account) {
	const auto server = CurrentServerForAccount(account);
	if (server.isTelegram || server.host.isEmpty()) {
		return QString();
	}
	return server.host.toLower();
}

bool ShouldUseCloudLangPack() {
	return true;
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

Fn<void()> WaitForServerConnection(
		not_null<Main::Account*> account,
		const Server &server,
		Fn<void(bool ok)> done) {
	const auto timer = std::make_shared<base::Timer>();
	const auto started = crl::now();
	const auto cancelled = std::make_shared<bool>(false);
	timer->setCallback([=]() {
		if (*cancelled) {
			return;
		}
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
	return [=] {
		*cancelled = true;
		timer->cancel();
	};
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

// RawHttpGetBody performs a blocking plain-HTTP GET and returns the response
// body on a 200 status, or std::nullopt on any failure (connect/write/read
// timeout, non-200, malformed response). Must run off the main thread (see
// callers, always inside crl::async) -- waitForConnected/waitForReadyRead
// block the calling thread. Shared by FetchServerInfo and FetchServerIcon
// so the same-port HTTP endpoints they hit only need one socket dance.
[[nodiscard]] std::optional<QByteArray> RawHttpGetBody(
		const QString &host,
		int port,
		const QByteArray &path) {
	QTcpSocket socket;
	socket.connectToHost(host, port);
	if (!socket.waitForConnected(kCheckTimeoutMs)) {
		return std::nullopt;
	}
	const auto request = "GET " + path + " HTTP/1.1\r\n"
		"Host: " + host.toUtf8() + "\r\n"
		"Connection: close\r\n"
		"\r\n";
	socket.write(request);
	if (!socket.waitForBytesWritten(kCheckTimeoutMs)) {
		return std::nullopt;
	}

	QByteArray raw;
	while (socket.waitForReadyRead(kCheckTimeoutMs)) {
		raw += socket.readAll();
	}
	raw += socket.readAll();
	socket.disconnectFromHost();

	const auto headerEnd = raw.indexOf("\r\n\r\n");
	if (headerEnd < 0) {
		return std::nullopt;
	}
	const auto statusLine = raw.left(raw.indexOf("\r\n"));
	if (!statusLine.contains(" 200 ")) {
		return std::nullopt;
	}
	return raw.mid(headerEnd + 4);
}

void FetchServerInfo(
		const QString &host,
		int port,
		Fn<void(std::optional<ServerInfoFetchResult> result)> done) {
	if (host.isEmpty() || port <= 0) {
		done(std::nullopt);
		return;
	}
	crl::async([=, done = std::move(done)]() mutable {
		const auto body = RawHttpGetBody(host, port, "/owpengram/server-info");
		if (!body) {
			crl::on_main([=]() mutable { done(std::nullopt); });
			return;
		}
		const auto document = QJsonDocument::fromJson(*body);
		if (!document.isObject()) {
			crl::on_main([=]() mutable { done(std::nullopt); });
			return;
		}
		const auto object = document.object();
		const auto pem = object.value("rsa_public_key_pem").toString();
		if (pem.isEmpty()) {
			crl::on_main([=]() mutable { done(std::nullopt); });
			return;
		}
		const auto result = ServerInfoFetchResult{
			.rsaPublicKeyPem = pem,
			.dcId = object.value("dc_id").toInt(),
			.name = object.value("name").toString(),
			.description = object.value("description").toString(),
			.hasIcon = object.value("has_icon").toBool(),
		};
		crl::on_main([=]() mutable { done(result); });
	});
}

void FetchServerIcon(
		const QString &host,
		int port,
		Fn<void(QByteArray data)> done) {
	if (host.isEmpty() || port <= 0) {
		done(QByteArray());
		return;
	}
	crl::async([=, done = std::move(done)]() mutable {
		const auto body = RawHttpGetBody(host, port, "/owpengram/server-icon");
		crl::on_main([=]() mutable { done(body.value_or(QByteArray())); });
	});
}

} // namespace Owpengram
