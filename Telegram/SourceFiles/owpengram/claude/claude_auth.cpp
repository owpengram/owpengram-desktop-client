/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_auth.h"

#include "logs.h"
#include "owpengram/claude/claude_process.h"

#include <rpl/variable.h>

#include <QtCore/QCoreApplication>
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
	QPointer<QProcess> check;
	QPointer<QProcess> login;
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
		for (const auto &process : { state.check, state.login }) {
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

[[nodiscard]] QProcess *Launch(const QStringList &args) {
	const auto binary = FindClaudeBinary(ConfiguredClaudePath());
	if (binary.isEmpty()) {
		return nullptr;
	}
	HookQuit();
	const auto process = new QProcess();
	process->setProcessEnvironment(ClaudeEnvironment(binary));
	process->setProgram(binary);
	process->setArguments(args);
	return process;
}

void ParseStatus(const QByteArray &output) {
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(output, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		LOG(("Claude: bad auth status output."));
		return;
	}
	const auto object = document.object();
	SetStatus([&](AuthStatus &status) {
		status.known = true;
		status.loggedIn = object.value(u"loggedIn"_q).toBool(false);
		status.email = status.loggedIn
			? object.value(u"email"_q).toString()
			: QString();
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
	const auto process = Launch({ u"auth"_q, u"status"_q, u"--json"_q });
	if (!process) {
		SetStatus([](AuthStatus &status) {
			status = AuthStatus();
		});
		return;
	}
	state.check = process;
	QObject::connect(
		process,
		&QProcess::finished,
		[=](int code, QProcess::ExitStatus) {
			// `auth status` exits with 1 when signed out, still with JSON.
			ParseStatus(process->readAllStandardOutput());
			process->deleteLater();
		});
	QObject::connect(process, &QProcess::errorOccurred, [=](
			QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) {
			LOG(("Claude: auth status failed to start."));
			process->deleteLater();
		}
	});
	process->start(QIODevice::ReadOnly);
	QObject::connect(process, &QProcess::started, [=] {
		// A hung check must not block the next refresh forever.
		QTimer::singleShot(kStatusTimeout, process, [=] {
			process->kill();
		});
	});
}

void StartLogin() {
	auto &state = State();
	if (state.login) {
		return;
	}
	const auto process = Launch({
		u"auth"_q,
		u"login"_q,
		u"--claudeai"_q,
	});
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
		SetStatus([](AuthStatus &status) {
			status.loginRunning = false;
			status.loginUrl = QString();
		});
		process->deleteLater();
		RefreshAuthStatus();
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

void CancelLogin() {
	if (const auto process = State().login.data()) {
		process->kill();
	}
}

} // namespace Owpengram::Claude
