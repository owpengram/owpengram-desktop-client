/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"

#include <optional>
#include <vector>

#include <QtCore/QString>

namespace Main {
class Account;
} // namespace Main

namespace MTP {
class Config;
} // namespace MTP

namespace Owpengram {

inline constexpr auto kOfficialServerId = "official";
inline constexpr auto kTelegramServerId = "telegram";
inline constexpr auto kTeamgramServerId = "teamgram";

struct Server {
	QString id;
	QString name;
	QString host;
	int port = 0;
	QString description;
	QString rsaPublicKey;
	QString logoPath;
	bool isOfficial = false;
	bool isTelegram = false;
	// Multi-DC servers (Telegram) trust help.getConfig / the special loader to
	// discover real alternate data-center addresses. Single-server backends
	// (owpengram/Teamgram/MyTelegram/custom) are one physical machine, so every
	// dc_id is mapped onto the single host:port instead.
	bool multiDc = false;
	// Home data-center id. 0 = auto (multiDc -> 2, single-server -> 1).
	int mainDcId = 0;

	[[nodiscard]] bool valid() const {
		return !id.isEmpty() && !host.isEmpty() && port > 0;
	}
};

[[nodiscard]] Server TelegramServer();
[[nodiscard]] Server TeamgramServer();
[[nodiscard]] Server OfficialServer();
[[nodiscard]] std::vector<Server> ListServers();
[[nodiscard]] std::optional<Server> FindServer(const QString &id);
[[nodiscard]] std::optional<Server> AddCustomServer(
	const QString &name,
	const QString &host,
	int port,
	const QString &description,
	const QString &rsaPublicKey = QString(),
	const QString &logoSourcePath = QString(),
	bool multiDc = false,
	int mainDcId = 0);
[[nodiscard]] bool IsValidRsaPublicKeyPem(const QString &pem);
[[nodiscard]] QString ResolveServerLogoPath(const QString &logoPath);
[[nodiscard]] bool RemoveCustomServer(const QString &id);
[[nodiscard]] bool IsRemovableServer(const Server &server);

void ApplyServerToAccount(
	not_null<Main::Account*> account,
	const Server &server);

void RestoreServerToConfig(
	not_null<Main::Account*> account,
	not_null<MTP::Config*> config);

void RestoreServerToAccount(not_null<Main::Account*> account);

[[nodiscard]] Server CurrentServerForAccount(
	not_null<Main::Account*> account);

[[nodiscard]] bool ShouldUseCloudLangPack();

void CheckServerOnline(
	const Server &server,
	Fn<void(bool online, int latencyMs)> done);

void WaitForServerConnection(
	not_null<Main::Account*> account,
	const Server &server,
	Fn<void()> done);

[[nodiscard]] QString DefaultLogoPath();
[[nodiscard]] QString TelegramLogoPath();
[[nodiscard]] QString TeamgramLogoPath();
[[nodiscard]] QString FormatEndpoint(const Server &server);

} // namespace Owpengram
