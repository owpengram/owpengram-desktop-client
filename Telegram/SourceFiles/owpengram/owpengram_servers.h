/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"

#include <rpl/event_stream.h>
#include <rpl/producer.h>

#include <optional>
#include <vector>

#include <QtCore/QByteArray>
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
// Updates an existing custom server IN PLACE, keeping its id -- unlike
// deleting and re-AddCustomServer-ing it (which mints a fresh
// QUuid::createUuid() id), this keeps every account already pointed at this
// server (Main::Account's saved OwpengramServerSelection.id) resolving to
// it correctly after the edit. See CurrentServerForAccount /
// ServerFromStoredSelection: a stale id it can no longer FindServer() falls
// back to a synthetic Server with name = host, which is the account-switcher
// "server name disappeared, shows the address instead" bug this fixes.
// Returns std::nullopt if id doesn't match any existing custom server.
// logoSourcePath empty means "keep the current logo", not "clear it" --
// this box has no clear-logo affordance today.
[[nodiscard]] std::optional<Server> UpdateCustomServer(
	const QString &id,
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

// Fires whenever the stored custom-server list changes (Add/Update/Remove
// CustomServer, from ANY code path -- including one triggered from outside
// the currently-visible UI, e.g. an owpg://addserver link opened while the
// server-select screen happens to already be showing). Any UI presenting
// that list should subscribe instead of only refreshing from its own local
// "just saved" callback, or it goes stale until the user navigates away and
// back.
[[nodiscard]] rpl::producer<> CustomServersChanges();

void ApplyServerToAccount(
	not_null<Main::Account*> account,
	const Server &server);

void RestoreServerToConfig(
	not_null<Main::Account*> account,
	not_null<MTP::Config*> config);

void RestoreServerToAccount(not_null<Main::Account*> account);

[[nodiscard]] Server CurrentServerForAccount(
	not_null<Main::Account*> account);

// Every currently-configured account (any state, not just fully signed in)
// whose saved server selection resolves to serverId -- used to warn before
// deleting a custom server that doing so will orphan those accounts (see the
// "Remove server" confirm box), since nothing else keeps an account pointed
// at a server that no longer exists in the list from silently breaking.
[[nodiscard]] std::vector<not_null<Main::Account*>> AccountsUsingServer(
	const QString &serverId);

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
	// Name/description/hasIcon are admin-edited on the server (Server
	// Settings' identity section) and optional -- empty/false means the
	// operator hasn't set them, not that the server failed to answer.
	QString name;
	QString description;
	bool hasIcon = false;
};

// Fetches the server's RSA public key, home DC id, and identity
// (name/description/icon presence) from its well-known same-port HTTP
// endpoint (GET host:port/owpengram/server-info), so "Add Server" can be
// filled in from just host:port instead of manual PEM copy-paste + guessing
// the DC id. Calls done(result) on success, done(std::nullopt) on any
// failure (offline, unsupported server, malformed response) -- always on
// the main thread.
void FetchServerInfo(
	const QString &host,
	int port,
	Fn<void(std::optional<ServerInfoFetchResult> result)> done);

// Fetches the server's icon (GET host:port/owpengram/server-icon) as raw
// image bytes -- only worth calling when a prior FetchServerInfo answered
// with hasIcon=true. done(bytes) on success, done(QByteArray()) (empty) on
// any failure -- always on the main thread.
void FetchServerIcon(
	const QString &host,
	int port,
	Fn<void(QByteArray data)> done);

// Re-reads the admin-edited identity (name/description/icon) of every server
// that serves /owpengram/server-info and stores it, so a logo or title the
// operator changed shows up without the user having to open Edit Server and
// re-fetch by hand. Call it when a server list becomes visible.
//
// Deliberately cosmetic-only: host, port, RSA key, DC id and id are NEVER
// touched by this. That endpoint is plain HTTP, so anything able to MITM it
// could otherwise redirect the connection or swap the key the handshake is
// verified against; a wrong name or icon is merely wrong, not dangerous.
//
// The Telegram server is skipped -- it is not an OwpenGram backend and has
// no such endpoint. Fetches are fire-and-forget, deduplicated per server
// while one is in flight, and any failure silently keeps the stored values.
void RefreshServersInfo();

// Calls done(true) once the account's MTP is connected to the given server,
// or done(false) after a 30s timeout. Returns a cancel handle: call it to
// stop polling and guarantee done() is never called afterwards (e.g. when
// the user dismisses the "Connecting..." box before either outcome).
[[nodiscard]] Fn<void()> WaitForServerConnection(
	not_null<Main::Account*> account,
	const Server &server,
	Fn<void(bool ok)> done);

[[nodiscard]] QString DefaultLogoPath();
[[nodiscard]] QString TelegramLogoPath();
[[nodiscard]] QString FormatEndpoint(const Server &server);

} // namespace Owpengram
