/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_auth.h"

#include "logs.h"
#include "owpengram/claude/claude_access_store.h"
#include "owpengram/claude/claude_process.h"

#include <rpl/event_stream.h>
#include <rpl/variable.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QUuid>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QProcess>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>

namespace Owpengram::Claude {
namespace {

constexpr auto kStatusTimeout = 15000;

struct AuthState {
	rpl::variable<AuthStatus> status;
	rpl::variable<QString> systemEmail;
	ProcessEvents::RateLimits limits;
	rpl::event_stream<> limitsChanges;
	QPointer<QProcess> check;
	QPointer<QProcess> systemCheck;
	QPointer<QProcess> login;
	bool systemChecked = false;
	bool quitHooked = false;
};

AuthState &State() {
	// Never destroyed (QProcess children are cleaned up on quit).
	static const auto result = new AuthState();
	return *result;
}

void HookQuit() {
	auto &state = State();
	const auto app = QCoreApplication::instance();
	if (state.quitHooked || !app) {
		return;
	}
	state.quitHooked = true;
	QObject::connect(app, &QCoreApplication::aboutToQuit, [] {
		auto &state = State();
		for (const auto &process : {
				state.check,
				state.systemCheck,
				state.login }) {
			if (process) {
				process->kill();
				process->waitForFinished(1000);
				delete process.data();
			}
		}
	});
}

void SetStatus(Fn<void(AuthStatus&)> change) {
	auto status = State().status.current();
	change(status);
	State().status = status;
}

[[nodiscard]] QString NewProfileId() {
	return QUuid::createUuid().toString(QUuid::Id128);
}

[[nodiscard]] QProcess *Launch(
		const QStringList &args,
		const QString &profileId) {
	const auto binary = FindClaudeBinary(ConfiguredClaudePath());
	if (binary.isEmpty()) {
		return nullptr;
	}
	HookQuit();
	const auto process = new QProcess();
	process->setProcessEnvironment(ClaudeEnvironment(binary, profileId));
	process->setProgram(binary);
	process->setArguments(args);
	return process;
}

struct ParsedStatus {
	bool ok = false;
	bool loggedIn = false;
	QString email;
};

[[nodiscard]] ParsedStatus ParseStatus(const QByteArray &output) {
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(output, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		LOG(("Claude: bad auth status output."));
		return {};
	}
	const auto object = document.object();
	const auto loggedIn = object.value(u"loggedIn"_q).toBool(false);
	return {
		.ok = true,
		.loggedIn = loggedIn,
		.email = loggedIn ? object.value(u"email"_q).toString() : QString(),
	};
}

// `claude auth status --json` of the profile, done() on the main thread.
[[nodiscard]] QProcess *CheckStatus(
		const QString &profileId,
		Fn<void(ParsedStatus)> done) {
	const auto process = Launch(
		{ u"auth"_q, u"status"_q, u"--json"_q },
		profileId);
	if (!process) {
		return nullptr;
	}
	QObject::connect(
		process,
		&QProcess::finished,
		[=](int, QProcess::ExitStatus) {
			// `auth status` exits with 1 when signed out, still with JSON.
			done(ParseStatus(process->readAllStandardOutput()));
			process->deleteLater();
		});
	QObject::connect(process, &QProcess::errorOccurred, [=](
			QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) {
			LOG(("Claude: auth status failed to start."));
			done({});
			process->deleteLater();
		}
	});
	QObject::connect(process, &QProcess::started, [=] {
		// A hung check must not block the next refresh forever.
		QTimer::singleShot(kStatusTimeout, process, [=] {
			process->kill();
		});
	});
	process->start(QIODevice::ReadOnly);
	return process;
}

void RefreshSystemEmail() {
	auto &state = State();
	if (state.systemCheck) {
		return;
	}
	state.systemChecked = true;
	state.systemCheck = CheckStatus(QString(), [](ParsedStatus parsed) {
		if (parsed.ok) {
			State().systemEmail = parsed.email;
		}
	});
}

} // namespace

rpl::producer<AuthStatus> AuthStatusValue() {
	if (!State().status.current().known && !State().check) {
		RefreshAuthStatus();
	}
	return State().status.value();
}

AuthStatus CurrentAuthStatus() {
	return State().status.current();
}

void RefreshAuthStatus() {
	auto &state = State();
	if (state.check) {
		return;
	}
	const auto profile = Access().selectedClaudeProfile();
	state.check = CheckStatus(profile, [=](ParsedStatus parsed) {
		if (!parsed.ok || Access().selectedClaudeProfile() != profile) {
			return;
		}
		SetStatus([&](AuthStatus &status) {
			status.known = true;
			status.loggedIn = parsed.loggedIn;
			status.email = parsed.email;
		});
		if (profile.isEmpty()) {
			State().systemEmail = parsed.email;
		} else if (parsed.loggedIn) {
			Access().setClaudeProfile(profile, parsed.email);
		}
	});
	if (!state.check) {
		SetStatus([](AuthStatus &status) {
			status = AuthStatus();
		});
	}
}

void Login(const QString &profileId, bool added) {
	auto &state = State();
	if (state.login) {
		return;
	}
	const auto process = Launch({
		u"auth"_q,
		u"login"_q,
		u"--claudeai"_q,
	}, profileId);
	if (!process) {
		return;
	}
	state.login = process;
	process->setProcessChannelMode(QProcess::MergedChannels);
	SetStatus([](AuthStatus &status) {
		status.loginRunning = true;
		status.loginUrl = QString();
	});
	QObject::connect(process, &QProcess::readyReadStandardOutput, [=] {
		static const auto url = QRegularExpression(
			u"https://[^\\s\"'<>]+"_q);
		const auto text = QString::fromUtf8(process->readAll());
		const auto match = url.match(text);
		if (match.hasMatch()) {
			SetStatus([&](AuthStatus &status) {
				status.loginUrl = match.captured(0);
			});
		}
	});
	const auto done = [=] {
		process->deleteLater();
		if (!added) {
			SetStatus([](AuthStatus &status) {
				status.loginRunning = false;
				status.loginUrl = QString();
			});
			RefreshAuthStatus();
			return;
		}
		// A new profile: keep it only if the sign-in went through.
		const auto checked = CheckStatus(profileId, [=](ParsedStatus parsed) {
			SetStatus([](AuthStatus &status) {
				status.loginRunning = false;
				status.loginUrl = QString();
			});
			if (parsed.ok && parsed.loggedIn) {
				Access().setClaudeProfile(profileId, parsed.email);
				SelectAccount(profileId);
			} else {
				QDir(ClaudeProfilePath(profileId)).removeRecursively();
			}
		});
		if (!checked) {
			SetStatus([](AuthStatus &status) {
				status.loginRunning = false;
				status.loginUrl = QString();
			});
		}
	};
	QObject::connect(
		process,
		&QProcess::finished,
		[=](int, QProcess::ExitStatus) { done(); });
	QObject::connect(process, &QProcess::errorOccurred, [=](
			QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) {
			LOG(("Claude: auth login failed to start."));
			done();
		}
	});
	// No stdin: a prompt waiting for input would hang, claude completes the
	// sign-in through its local callback after the browser step.
	process->start(QIODevice::ReadOnly);
}

void StartLogin() {
	Login(Access().selectedClaudeProfile(), false);
}

void AddAccount() {
	Login(NewProfileId(), true);
}

void SelectAccount(const QString &profileId) {
	auto &access = Access();
	if (access.selectedClaudeProfile() == profileId) {
		return;
	}
	access.selectClaudeProfile(profileId);
	auto &state = State();
	SetUsageLimits({});
	if (state.check) {
		// Its answer is about the previous profile.
		state.check->kill();
		state.check = nullptr;
	}
	SetStatus([&](AuthStatus &status) {
		const auto loginRunning = status.loginRunning;
		const auto loginUrl = status.loginUrl;
		status = AuthStatus();
		status.loginRunning = loginRunning;
		status.loginUrl = loginUrl;
	});
	RefreshAuthStatus();
}

QString SystemEmail() {
	if (!State().systemChecked) {
		RefreshSystemEmail();
	}
	return State().systemEmail.current();
}

rpl::producer<ProcessEvents::RateLimits> UsageLimitsValue() {
	return rpl::single(
		rpl::empty
	) | rpl::then(
		State().limitsChanges.events()
	) | rpl::map([] {
		return State().limits;
	});
}

ProcessEvents::RateLimits CurrentUsageLimits() {
	return State().limits;
}

void SetUsageLimits(const ProcessEvents::RateLimits &limits) {
	State().limits = limits;
	State().limitsChanges.fire({});
}

rpl::producer<QString> SystemEmailValue() {
	if (!State().systemChecked) {
		RefreshSystemEmail();
	}
	return State().systemEmail.value();
}

void CancelLogin() {
	if (const auto process = State().login.data()) {
		process->kill();
	}
}

} // namespace Owpengram::Claude
