/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_confirm.h"

#include "base/timer.h"
#include "base/weak_qptr.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <memory>

namespace Owpengram::Claude {
namespace {

constexpr auto kConfirmationTimeout = 2 * 60 * crl::time(1000);

struct State {
	Fn<void(ConfirmResult)> done;
	base::Timer timer;
	base::weak_qptr<Ui::GenericBox> box;
	rpl::lifetime lifetime;
};

// Calls `done` exactly once. The state is kept alive by the timer callback
// (a deliberate cycle) until the first finish() breaks it.
void Finish(const std::shared_ptr<State> &state, ConfirmResult result) {
	if (!state->done) {
		return;
	}
	const auto callback = std::move(state->done);
	state->done = nullptr;
	state->timer.cancel();
	state->timer.setCallback(nullptr); // Safe inside the callback (copied).
	state->lifetime.destroy();
	if (const auto box = state->box.get()) {
		state->box = base::weak_qptr<Ui::GenericBox>();
		box->closeBox(); // boxClosing -> Finish is a no-op now.
	}
	callback(result);
}

} // namespace

void RequestConfirmation(
		not_null<Main::Session*> session,
		QString clientName,
		QString summary,
		Fn<void(bool approved)> done) {
	RequestConfirmationWithReason(
		session,
		std::move(clientName),
		std::move(summary),
		[done = std::move(done)](ConfirmResult result) {
			done(result == ConfirmResult::Approved);
		});
}

void RequestConfirmationWithReason(
		not_null<Main::Session*> session,
		QString clientName,
		QString summary,
		Fn<void(ConfirmResult result)> done) {
	// No window of this account (e.g. closed to tray) -> nobody to ask,
	// deny. We deliberately do not use Session::tryResolveWindow(), which
	// would switch the active account when there are no windows.
	const auto &windows = session->windows();
	auto controller = (Window::SessionController*)nullptr;
	for (const auto window : windows) {
		if (window->isPrimary()) {
			controller = window;
			break;
		}
	}
	if (!controller && !windows.empty()) {
		controller = windows.front();
	}
	if (!controller) {
		done(ConfirmResult::Unavailable);
		return;
	}

	const auto state = std::make_shared<State>();
	state->done = std::move(done);
	state->timer.setCallback([=] {
		Finish(state, ConfirmResult::TimedOut);
	});
	state->timer.callOnce(kConfirmationTimeout);

	session->account().sessionChanges(
	) | rpl::filter([](Main::Session *value) {
		return !value;
	}) | rpl::on_next([=](Main::Session*) {
		Finish(state, ConfirmResult::Unavailable);
	}, state->lifetime);

	auto box = Ui::MakeConfirmBox({
		.text = std::move(summary),
		.confirmed = Fn<void()>([=] { Finish(state, ConfirmResult::Approved); }),
		.confirmText = tr::lng_owpengram_claude_confirm_allow(),
		.cancelText = tr::lng_owpengram_claude_confirm_deny(),
		.title = tr::lng_owpengram_claude_confirm_title(
			tr::now,
			lt_client,
			clientName),
	});
	state->box = controller->window().show(std::move(box));
	if (const auto raw = state->box.get()) {
		// Cancel button, Esc, outside click, window closed: all deny.
		raw->boxClosing() | rpl::on_next([=] {
			// Already closing: do not close it again from Finish.
			state->box = base::weak_qptr<Ui::GenericBox>();
			Finish(state, ConfirmResult::Denied);
		}, raw->lifetime());
	}
	controller->window().activate();
}

} // namespace Owpengram::Claude
