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
#include "mtproto/mtp_instance.h"
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
constexpr auto kCheckTimeoutMs = 3000;

[[nodiscard]] QString ServersFilePath() {
	return cWorkingDir() + u"tdata/"_q + kServersFile;
}

[[nodiscard]] std::optional<std::pair<QString, int>> ReadOfficialEndpoint() {
	const auto &options = Core::App().fallbackProductionConfig().dcOptions();
	const auto variants = options.lookup(
		MTP::Instance::Fields::kDefaultMainDc,
		MTP::DcType::Regular,
		false);
	for (auto address = 0; address != MTP::DcOptions::Variants::AddressTypeCount; ++address) {
		for (auto protocol = 0; protocol != MTP::DcOptions::Variants::ProtocolCount; ++protocol) {
			for (const auto &endpoint : variants.data[address][protocol]) {
				return std::make_pair(
					QString::fromStdString(endpoint.ip),
					endpoint.port);
			}
		}
	}
	return std::nullopt;
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

} // namespace

QString DefaultLogoPath() {
	return u":/gui/art/logo_256.png"_q;
}

Server OfficialServer() {
	auto result = Server();
	result.id = QString::fromLatin1(kOfficialServerId);
	result.name = tr::lng_owpengram_server_official_name(tr::now);
	result.description = tr::lng_owpengram_server_official_description(tr::now);
	result.logoPath = DefaultLogoPath();
	result.isOfficial = true;
	if (const auto endpoint = ReadOfficialEndpoint()) {
		result.host = endpoint->first;
		result.port = endpoint->second;
	} else {
		result.host = u"192.168.100.10"_q;
		result.port = 10443;
	}
	return result;
}

std::vector<Server> ListServers() {
	auto result = std::vector<Server>();
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

bool RemoveCustomServer(const QString &id) {
	if (id == QString::fromLatin1(kOfficialServerId)) {
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

void ApplyServerToAccount(
		not_null<Main::Account*> account,
		const Server &server) {
	Expects(server.valid());

	const auto dcId = MTP::Instance::Fields::kDefaultMainDc;
	const auto flags = MTPDdcOption::Flag::f_static;
	auto &mtp = account->mtp();
	auto &dcOptions = mtp.dcOptions();

	dcOptions.setOptionsLocked(false);
	dcOptions.setFromList(MTP_vector<MTPDcOption>(1, MTP_dcOption(
		MTP_flags(flags),
		MTP_int(dcId),
		MTP_string(server.host),
		MTP_int(server.port),
		MTPbytes())));
	dcOptions.setOptionsLocked(true);

	mtp.setMainDcId(dcId);
	account->local().writeOwpengramServer(
		server.id,
		server.host,
		server.port);
	account->local().writeMtpConfig();
	mtp.restart();
	mtp.reInitConnection(dcId);
}

void CheckServerOnline(const Server &server, Fn<void(bool online)> done) {
	if (!server.valid()) {
		done(false);
		return;
	}
	const auto host = server.host;
	const auto port = server.port;
	crl::async([=, done = std::move(done)]() mutable {
		QTcpSocket socket;
		socket.connectToHost(host, port);
		const auto connected = socket.waitForConnected(kCheckTimeoutMs);
		if (connected) {
			socket.disconnectFromHost();
			if (socket.state() != QAbstractSocket::UnconnectedState) {
				socket.waitForDisconnected(1000);
			}
		}
		crl::on_main([=, done = std::move(done)]() mutable {
			done(connected);
		});
	});
}

} // namespace Owpengram
