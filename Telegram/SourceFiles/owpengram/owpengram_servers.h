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
	// (owpengram/MyTelegram/custom) are one physical machine, so every
	// dc_id is mapped onto the single host:port instead.
	bool multiDc = false;
	// Home data-center id. 0 = auto (multiDc -> 2, single-server -> 1).
	int mainDcId = 0;

	[[nodiscard]] bool valid() const {
		return !id.isEmpty() && !host.isEmpty() && port > 0;
	}
};

[[nodiscard]] Server TelegramServer();
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

// Stable key for grouping per-server local device state (currently: recent/frequently-used
// custom emoji, see Core::Settings::recentEmojiForScope) by backend rather than by device. Two
// different custom servers do NOT share a document-id namespace — a custom-emoji document id
// cached while using one never resolves on another (or on official Telegram), so state shared
// across every account on the device would leak cross-server ids into every account's UI,
// unresolvable there forever. Official Telegram, and anything we can't identify a server for,
// get an empty QString (the account is treated as unscoped/legacy), so existing behaviour for
// real Telegram accounts is unaffected. Host, not server id, is the identity that matters here:
// the same physical backend added twice under different local ids should still share one scope.
[[nodiscard]] QString ServerScopeKeyForAccount(not_null<Main::Account*> account);

[[nodiscard]] bool ShouldUseCloudLangPack();

void CheckServerOnline(
	const Server &server,
	Fn<void(bool online, int latencyMs)> done);

struct ServerInfoFetchResult {
	QString rsaPublicKeyPem;
	int dcId = 0;
};

// Fetches the server's RSA public key and home DC id from its well-known
// same-port HTTP endpoint (GET host:port/owpengram/server-info), so "Add
// Server" can be filled in from just host:port instead of manual PEM
// copy-paste + guessing the DC id. Calls done(result) on success,
// done(std::nullopt) on any failure (offline, unsupported server, malformed
// response) -- always on the main thread.
void FetchServerInfo(
	const QString &host,
	int port,
	Fn<void(std::optional<ServerInfoFetchResult> result)> done);

// Calls done(true) once the account's MTP is connected to the given server,
// or done(false) after a 30s timeout.
void WaitForServerConnection(
	not_null<Main::Account*> account,
	const Server &server,
	Fn<void(bool ok)> done);

[[nodiscard]] QString DefaultLogoPath();
[[nodiscard]] QString TelegramLogoPath();
[[nodiscard]] QString FormatEndpoint(const Server &server);

} // namespace Owpengram
