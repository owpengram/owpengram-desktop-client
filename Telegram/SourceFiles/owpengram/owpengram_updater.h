/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>

// OwpenGram ships a single self-contained executable per platform, published
// as a plain asset on a GitHub release. Upstream's updater cannot deliver
// that: it expects a signed .tdupdate archive that unpacks into a directory
// tree with a companion Updater binary next to the app, and our releases have
// neither. So this module replaces the transport (where an update comes from)
// and the apply step (how it lands), while everything the user sees -- the
// progress bar in Advanced settings, the "Restart and update" button -- stays
// the upstream UI, driven through the usual Core::UpdateChecker signals.
//
// Apply is a self-replacement rather than a helper process: both Windows and
// Linux let you rename a running executable, because renaming only rewrites a
// directory entry while the running image keeps its open inode. Writing *into*
// the file would fail (ETXTBSY / sharing violation), renaming it does not.

namespace Owpengram::Updater {

// One published release, as far as the updater cares about it.
struct Release {
	int build = 0; // The N in the "ON" tag.
	QString tag;
	QString assetName;
	QString assetUrl;
	qint64 assetSize = 0;

	// Raw 32 bytes, empty when the feed carried no digest for the asset.
	// GitHub returns one ("digest": "sha256:...") for every asset it stores.
	QByteArray sha256;

	QString notes;
};

// The release number compiled into this build, from -DOWPENGRAM_BUILD. Zero
// means the build was made without one, and the updater stays off: without it
// there is nothing to compare a tag against, since AppVersion tracks the
// upstream Telegram version (7.2.2), not ours.
[[nodiscard]] int RunningBuild();

// Where the release feed is read from. Defaults to the GitHub API for the
// desktop client repository; OWPENGRAM_UPDATE_URL replaces it, which is how
// an update is tested end to end without publishing anything -- see
// docs/updates.md.
[[nodiscard]] QString FeedUrl();
[[nodiscard]] bool UsingTestFeed();

// False when this build cannot check for updates at all: no release number
// and no test feed. A .deb install still checks -- see CanReplaceBinary()
// below for what differs once something is found.
[[nodiscard]] bool Enabled();
[[nodiscard]] bool InstalledByPackage();

// True when a verified download can be applied by replacing this executable
// in place (Windows, and the portable Linux binary). False for a .deb
// install: StagePending() then leaves the download for InstallStagedPackage()
// instead of writing the marker HasStaged()/ApplyStaged() look for.
[[nodiscard]] bool CanReplaceBinary();

// The asset this platform downloads: OwpenGram.exe on Windows, the bare
// OwpenGram binary on Linux. Empty where self-update is not supported.
[[nodiscard]] QString WantedAssetName();

// Parses a GitHub "releases/latest" response, or any JSON shaped like one.
// Attacker-reachable bytes, so every field is checked rather than assumed.
[[nodiscard]] std::optional<Release> ParseFeed(
	const QByteArray &json,
	QString *error = nullptr);

// The release the checker decided to fetch, handed to the staging step once
// the download finishes. The Updater state machine carries no per-transport
// payload of its own, so it lives here for the length of one check.
void SetPending(Release release);
[[nodiscard]] bool HasPending();
void ClearPending();

// Verifies size and digest, then moves the download next to a marker naming
// the build it belongs to. Runs off the main thread.
[[nodiscard]] bool StagePending(const QString &downloaded);

// True when a staged update is waiting to be applied on the next restart.
[[nodiscard]] bool HasStaged();

// Swaps the staged binary in for the running one. Called after the app has
// shut down but while the process is still alive, so the executable being
// replaced is our own. Rolls back on failure.
[[nodiscard]] bool ApplyStaged();

void ClearStaged();

// Removes the previous executable left behind by an applied update. Safe to
// call on every launch; does nothing when there is no leftover.
void CleanupAfterUpdate();

// ── .deb installs ───────────────────────────────────────────────────────
//
// A package install can't replace its own binary (see CanReplaceBinary()),
// so instead of staging a restart-time swap it stages a verified .deb and
// installs it live, through the desktop's own polkit authentication agent --
// the same "sudo apt install ./file.deb" a .deb user would run by hand,
// just triggered from a button instead of a terminal.

// Absolute path to a staged, verified .deb waiting to be installed, or empty
// when nothing is staged (or this build can replace its own binary, in which
// case package installs are simply not this build's concern).
[[nodiscard]] QString StagedPackagePath();

using InstallDoneCallback = Fn<void(bool success, QString errorOutput)>;

// Runs `pkexec apt-get install -y <path>` (falling back to `pkexec dpkg -i`
// if apt-get is missing) on the staged package. `done` is called on the main
// thread once it exits, or immediately if nothing is staged or no polkit
// agent is available to elevate through.
void InstallStagedPackage(InstallDoneCallback done);

// The "Update ready" button's click handler, shared by every place in
// Advanced settings that shows it. Package installs run pkexec here and
// restart only once it succeeds; everything else keeps upstream's "quit now,
// apply on the next launch" behavior untouched.
void HandleReadyButtonClick();

} // namespace Owpengram::Updater
