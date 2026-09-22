/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/owpengram_updater.h"

#include "base/platform/base_platform_info.h"
#include "core/application.h"
#include "core/update_checker.h"
#include "logs.h"
#include "settings.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>

#include <memory>

#ifndef OWPENGRAM_BUILD
#define OWPENGRAM_BUILD 0
#endif // OWPENGRAM_BUILD

namespace Owpengram::Updater {
namespace {

constexpr auto kDefaultFeedUrl = "https://api.github.com/repos/owpengram/"
	"owpengram-desktop-client/releases/latest";

// Names the folder rather than reusing tupdates/: upstream's ClearAll() wipes
// that whole tree whenever its own flow fails, which would take a staged
// OwpenGram update with it.
constexpr auto kStagedFolder = "owpengram-update";
constexpr auto kMarkerName = "staged";
constexpr auto kBackupSuffix = ".old";

// Sized for the JSON feed, not for the payload. A GitHub release response is
// a few kilobytes; anything near this is not a feed.
constexpr auto kMaxFeedSize = 512 * 1024;

std::optional<Release> PendingRelease;

[[nodiscard]] QString StagedFolder() {
	return cWorkingDir() + QString::fromLatin1(kStagedFolder);
}

[[nodiscard]] QString MarkerPath() {
	return StagedFolder() + '/' + QString::fromLatin1(kMarkerName);
}

[[nodiscard]] QString RunningExecutable() {
	return cExeDir() + cExeName();
}

// The marker is written last and read first, so a half-finished staging never
// looks ready: build number, then the file name it belongs to.
[[nodiscard]] std::optional<QString> ReadMarker(int *build) {
	auto file = QFile(MarkerPath());
	if (!file.open(QIODevice::ReadOnly)) {
		return std::nullopt;
	}
	const auto lines = QString::fromUtf8(
		file.read(4096)).split('\n', Qt::SkipEmptyParts);
	if (lines.size() < 2) {
		return std::nullopt;
	}
	auto ok = false;
	const auto number = lines[0].trimmed().toInt(&ok);
	if (!ok || number <= 0) {
		return std::nullopt;
	}
	const auto name = lines[1].trimmed();
	if (name.isEmpty() || name.contains('/') || name.contains('\\')) {
		return std::nullopt;
	}
	if (build) {
		*build = number;
	}
	return name;
}

} // namespace

int RunningBuild() {
	return int(OWPENGRAM_BUILD);
}

QString FeedUrl() {
	const auto custom = qEnvironmentVariable("OWPENGRAM_UPDATE_URL");
	return custom.isEmpty()
		? QString::fromLatin1(kDefaultFeedUrl)
		: custom;
}

bool UsingTestFeed() {
	return !qEnvironmentVariableIsEmpty("OWPENGRAM_UPDATE_URL");
}

QString WantedAssetName() {
	if constexpr (Platform::IsWindows()) {
		return u"OwpenGram.exe"_q;
	} else if constexpr (Platform::IsLinux()) {
		// A .deb install fetches the package, not the portable binary: it
		// cannot replace itself, but pkexec + apt can install a .deb.
		return InstalledByPackage() ? u"OwpenGram.deb"_q : u"OwpenGram"_q;
	} else {
		return QString();
	}
}

bool InstalledByPackage() {
	if constexpr (Platform::IsWindows()) {
		return false;
	}
	// A .deb puts the binary in /usr/bin and dpkg owns it from then on;
	// replacing it behind the package manager's back leaves its database
	// describing a file that is no longer there. The path check catches the
	// packaged install, the write check catches every other read-only one
	// (a shared /opt install, a binary owned by another user).
	const auto dir = cExeDir();
	if (dir.startsWith(u"/usr/"_q) || dir.startsWith(u"/opt/"_q)) {
		return true;
	}
	return !QFileInfo(dir).isWritable()
		|| !QFileInfo(RunningExecutable()).isWritable();
}

bool Enabled() {
	if (WantedAssetName().isEmpty()) {
		return false;
	} else if (RunningBuild() <= 0 && !UsingTestFeed()) {
		// Without a release number there is nothing to compare a tag
		// against. A test feed may still drive the flow end to end.
		return false;
	}
	// A package install still checks and still downloads -- it just cannot
	// replace itself, which is what CanReplaceBinary() is for.
	return true;
}

bool CanReplaceBinary() {
	return !InstalledByPackage();
}

std::optional<Release> ParseFeed(const QByteArray &json, QString *error) {
	const auto fail = [&](const QString &text) {
		if (error) {
			*error = text;
		}
		return std::optional<Release>();
	};
	if (json.size() > kMaxFeedSize) {
		return fail(u"feed too large: %1"_q.arg(json.size()));
	}
	auto parseError = QJsonParseError();
	const auto document = QJsonDocument::fromJson(json, &parseError);
	if (parseError.error != QJsonParseError::NoError) {
		return fail(u"bad json: %1"_q.arg(parseError.errorString()));
	} else if (!document.isObject()) {
		return fail(u"feed is not an object"_q);
	}
	const auto root = document.object();
	if (root.value(u"draft"_q).toBool()) {
		return fail(u"release is a draft"_q);
	} else if (root.value(u"prerelease"_q).toBool()) {
		// Only stable releases for now. A channel setting would gate this.
		return fail(u"release is a prerelease"_q);
	}

	const auto tag = root.value(u"tag_name"_q).toString();
	// Tags are O1, O2, ... -- the release number is the whole version story
	// on this side, since AppVersion tracks upstream Telegram instead.
	const auto match = QRegularExpression(
		u"^[Oo](\\d{1,6})$"_q).match(tag);
	if (!match.hasMatch()) {
		return fail(u"unexpected tag '%1'"_q.arg(tag));
	}

	auto result = Release();
	result.build = match.captured(1).toInt();
	result.tag = tag;
	result.notes = root.value(u"body"_q).toString();

	const auto wanted = WantedAssetName();
	const auto assets = root.value(u"assets"_q).toArray();
	for (const auto &entry : assets) {
		const auto asset = entry.toObject();
		if (asset.value(u"name"_q).toString() != wanted) {
			continue;
		}
		result.assetName = wanted;
		result.assetUrl = asset.value(u"browser_download_url"_q).toString();
		result.assetSize = qint64(asset.value(u"size"_q).toDouble());

		const auto digest = asset.value(u"digest"_q).toString();
		if (digest.startsWith(u"sha256:"_q)) {
			const auto hex = digest.mid(7).toLatin1();
			if (hex.size() == 64) {
				result.sha256 = QByteArray::fromHex(hex);
			}
		}
		break;
	}
	if (result.assetUrl.isEmpty()) {
		return fail(u"no '%1' asset in release %2"_q.arg(wanted, tag));
	} else if (!result.assetUrl.startsWith(u"http://"_q)
		&& !result.assetUrl.startsWith(u"https://"_q)) {
		return fail(u"bad asset url"_q);
	} else if (result.assetSize <= 0) {
		return fail(u"bad asset size"_q);
	}
	return result;
}

void SetPending(Release release) {
	PendingRelease = std::move(release);
}

bool HasPending() {
	return PendingRelease.has_value();
}

void ClearPending() {
	PendingRelease = std::nullopt;
}

bool StagePending(const QString &downloaded) {
	if (!PendingRelease) {
		return false;
	}
	const auto release = *PendingRelease;
	ClearPending();

	auto input = QFile(downloaded);
	if (!input.open(QIODevice::ReadOnly)) {
		LOG(("Update Error: cant read downloaded '%1'").arg(downloaded));
		return false;
	} else if (input.size() != release.assetSize) {
		LOG(("Update Error: size mismatch, got %1 expected %2"
			).arg(input.size()).arg(release.assetSize));
		return false;
	}

	// Streamed rather than read whole: these binaries are a quarter of a
	// gigabyte, and this runs on a worker thread on every update.
	auto hash = QCryptographicHash(QCryptographicHash::Sha256);
	if (!hash.addData(&input)) {
		LOG(("Update Error: cant hash downloaded '%1'").arg(downloaded));
		return false;
	}
	const auto digest = hash.result();
	input.close();

	if (!release.sha256.isEmpty() && digest != release.sha256) {
		LOG(("Update Error: sha256 mismatch for '%1'").arg(release.assetName));
		return false;
	} else if (release.sha256.isEmpty()) {
		// A hand-made test feed may omit it. A published GitHub release
		// never does, so this only ever fires in local testing.
		LOG(("Update Warning: feed carried no digest for '%1'."
			).arg(release.assetName));
	}

	ClearStaged();
	const auto folder = StagedFolder();
	if (!QDir().mkpath(folder)) {
		LOG(("Update Error: cant create '%1'").arg(folder));
		return false;
	}
	const auto target = folder + '/' + release.assetName;
	if (!QFile::rename(downloaded, target)
		&& !QFile::copy(downloaded, target)) {
		LOG(("Update Error: cant move update to '%1'").arg(target));
		return false;
	}

	if (!CanReplaceBinary()) {
		// Applied live by InstallStagedPackage(), not at the next restart --
		// no marker written, so HasStaged()/ApplyStaged() (the binary-swap
		// path below) never mistake this folder for their job.
		LOG(("Update Info: staged %1 (build %2) for package install."
			).arg(release.assetName).arg(release.build));
		return true;
	}

	// Written last: until the marker exists, the folder is not a staged
	// update and a crash mid-copy leaves nothing to apply.
	auto marker = QSaveFile(MarkerPath());
	if (!marker.open(QIODevice::WriteOnly)) {
		LOG(("Update Error: cant write staged marker"));
		ClearStaged();
		return false;
	}
	marker.write(u"%1\n%2\n"_q.arg(release.build).arg(
		release.assetName).toUtf8());
	if (!marker.commit()) {
		LOG(("Update Error: cant commit staged marker"));
		ClearStaged();
		return false;
	}
	LOG(("Update Info: staged %1 (build %2) for install."
		).arg(release.assetName).arg(release.build));
	return true;
}

bool HasStaged() {
	auto build = 0;
	const auto name = ReadMarker(&build);
	if (!name) {
		return false;
	} else if (build <= RunningBuild() && !UsingTestFeed()) {
		// Left over from an update that already went in, or from a
		// downgrade attempt. Either way it must not be applied again.
		ClearStaged();
		return false;
	}
	return QFile::exists(StagedFolder() + '/' + *name);
}

bool ApplyStaged() {
	auto build = 0;
	const auto name = ReadMarker(&build);
	if (!name) {
		return false;
	}
	const auto source = StagedFolder() + '/' + *name;
	const auto target = RunningExecutable();
	const auto backup = target + QString::fromLatin1(kBackupSuffix);
	if (!QFile::exists(source)) {
		ClearStaged();
		return false;
	}

	QFile::remove(backup);
	if (!QFile::rename(target, backup)) {
		LOG(("Update Error: cant move '%1' aside").arg(target));
		return false;
	}
	if (!QFile::rename(source, target) && !QFile::copy(source, target)) {
		LOG(("Update Error: cant install '%1', rolling back").arg(target));
		QFile::rename(backup, target);
		return false;
	}
	if constexpr (!Platform::IsWindows()) {
		QFile::setPermissions(target, QFile::permissions(target)
			| QFile::ExeOwner
			| QFile::ExeGroup
			| QFile::ExeOther);
	}
	ClearStaged();
	LOG(("Update Info: installed build %1 over '%2'."
		).arg(build).arg(target));
	return true;
}

void ClearStaged() {
	auto folder = QDir(StagedFolder());
	if (folder.exists()) {
		folder.removeRecursively();
	}
}

void CleanupAfterUpdate() {
	const auto backup = RunningExecutable()
		+ QString::fromLatin1(kBackupSuffix);
	if (QFile::exists(backup) && !QFile::remove(backup)) {
		// Windows can still hold a lock on the image the previous process
		// ran from; the next launch clears it.
		LOG(("Update Info: leftover '%1' still in use.").arg(backup));
	}
}

QString StagedPackagePath() {
	if (CanReplaceBinary()) {
		return QString();
	}
	const auto path = StagedFolder() + '/' + WantedAssetName();
	return QFile::exists(path) ? path : QString();
}

void InstallStagedPackage(InstallDoneCallback done) {
	if (!done) {
		return;
	}
	const auto path = StagedPackagePath();
	if (path.isEmpty()) {
		done(false, u"No update is staged."_q);
		return;
	}

	const auto pkexec = QStandardPaths::findExecutable(u"pkexec"_q);
	if (pkexec.isEmpty()) {
		done(false, u"pkexec was not found -- install by hand: "
			"sudo apt install %1"_q.arg(path));
		return;
	}

	// apt resolves and installs the .deb's own dependencies from a local
	// file path; dpkg alone does not, and would leave a half-configured
	// package behind if one is missing. Same command our README already
	// tells a .deb user to run themselves.
	const auto aptGet = QStandardPaths::findExecutable(u"apt-get"_q);
	const auto dpkg = QStandardPaths::findExecutable(u"dpkg"_q);
	auto args = QStringList();
	if (!aptGet.isEmpty()) {
		args = QStringList{ aptGet, u"install"_q, u"-y"_q, u"--"_q, path };
	} else if (!dpkg.isEmpty()) {
		args = QStringList{ dpkg, u"-i"_q, path };
	} else {
		done(false, u"Neither apt-get nor dpkg was found."_q);
		return;
	}

	const auto process = new QProcess();
	// A shared "already reported" flag: Crashed fires both errorOccurred()
	// and finished(), FailedToStart fires only errorOccurred(). Either path
	// must call `done` exactly once.
	const auto reported = std::make_shared<bool>(false);
	const auto report = [=](bool ok, QString error) {
		if (*reported) {
			return;
		}
		*reported = true;
		if (ok) {
			QFile::remove(path);
		}
		process->deleteLater();
		done(ok, error);
	};

	process->setProgram(pkexec);
	process->setArguments(args);
	QObject::connect(process, &QProcess::errorOccurred, [=](
			QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) {
			report(false, u"Could not start pkexec."_q);
		}
	});
	QObject::connect(process, qOverload<int, QProcess::ExitStatus>(
			&QProcess::finished), [=](int code, QProcess::ExitStatus status) {
		const auto ok = (status == QProcess::NormalExit) && (code == 0);
		auto error = ok
			? QString()
			: QString::fromUtf8(process->readAllStandardError()).trimmed();
		if (!ok && error.isEmpty()) {
			// The most common case by far: the user closed the polkit
			// prompt, or declined it. pkexec exits non-zero with nothing on
			// stderr for that, so this is the only signal available.
			error = u"Cancelled, or not authorized."_q;
		}
		report(ok, error);
	});
	LOG(("Update Info: installing %1 via pkexec.").arg(path));
	process->start();
}

void HandleReadyButtonClick() {
	if (CanReplaceBinary()) {
		// Unchanged from upstream: queue a restart-time swap of this
		// executable -- see ApplyStaged() and the launcher.cpp hook.
		if (!Core::UpdaterDisabled()) {
			Core::checkReadyUpdate();
		}
		Core::Restart();
		return;
	}

	// A package install is applied live, not at the next restart: dpkg/apt
	// replace the binary on disk directly while this process keeps running,
	// the same way running `sudo apt install ./file.deb` in a terminal would
	// while OwpenGram sat open behind it.
	InstallStagedPackage([](bool success, QString error) {
		if (!success) {
			LOG(("Update Error: package install failed: %1").arg(error));
			return;
		}
		// The checker still reports State::Ready from the download that
		// finished earlier; reset it so Restart() below takes its plain
		// "just relaunch" path instead of trying to apply a binary-replace
		// job that was never staged for a package install.
		Core::UpdateChecker().stop();
		Core::Restart();
	});
}

} // namespace Owpengram::Updater
