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
#include "core/application.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "window/window_controller.h"
#include "window/window_separate_id.h"
#include "window/window_session_controller.h"

#include <memory>

namespace Owpengram::Claude {
namespace {

constexpr auto kConfirmationTimeout = 2 * 60 * crl::time(1000);
// ConfirmBox maps Enter/Return to the confirm button (lib_ui-side
// ui/boxes/confirm_box.cpp:82-91) and the box takes focus when shown, so a
// key press that was meant for something else could approve. Allow is
// ignored for this long after the box is shown.
constexpr auto kIgnoreAllowFor = crl::time(1000);

struct State {
	Fn<void(ConfirmResult)> done;
	base::Timer timer;
	base::weak_qptr<Ui::GenericBox> box;
	crl::time shownAt = 0;
	rpl::lifetime lifetime;
};

// Calls `done` exactly once. The state is kept alive by the timer callback
// (a deliberate cycle) until the first finish() breaks it.
void Finish(std::shared_ptr<State> state, ConfirmResult result) {
	// By value: destroying state->lifetime below may free a lambda that
	// holds the last other reference to the state.
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
		Fn<void(ConfirmResult result)> done,
		std::optional<rpl::producer<>> cancelled) {
	// The window of this account; if it has none (e.g. closed to tray, or
	// the main window shows another account) open one for it. That does not
	// switch the account shown in other windows: a Primary SeparateId with
	// an account is a new window bound to that account
	// (Application::ensureSeparateWindowFor, core/application.cpp:1764).
	// We deliberately do not use Session::tryResolveWindow(), which
	// activates the account in the domain when there are no windows.
	const auto &windows = session->windows();
	auto window = (Window::Controller*)nullptr;
	for (const auto controller : windows) {
		if (controller->isPrimary()) {
			window = &controller->window();
			break;
		}
	}
	if (!window && !windows.empty()) {
		window = &windows.front()->window();
	}
	if (!window) {
		window = Core::App().ensureSeparateWindowFor(
			Window::SeparateId(&session->account()));
	}
	if (!window) {
		done(ConfirmResult::Unavailable);
		return;
	}

	const auto state = std::make_shared<State>();
	state->done = std::move(done);
	state->timer.setCallback([=] {
		// Finish outside of the timer's own timerEvent.
		crl::on_main([weak = std::weak_ptr<State>(state)] {
			if (const auto strong = weak.lock()) {
				Finish(strong, ConfirmResult::TimedOut);
			}
		});
	});
	state->timer.callOnce(kConfirmationTimeout);

	session->account().sessionChanges(
	) | rpl::filter([](Main::Session *value) {
		return !value;
	}) | rpl::on_next([=](Main::Session*) {
		Finish(state, ConfirmResult::SessionClosed);
	}, state->lifetime);

	if (cancelled) {
		std::move(*cancelled) | rpl::on_next([=] {
			Finish(state, ConfirmResult::Cancelled);
		}, state->lifetime);
	}

	auto box = Ui::MakeConfirmBox({
		.text = std::move(summary),
		.confirmed = Fn<void()>([=] {
			if (crl::now() - state->shownAt < kIgnoreAllowFor) {
				return; // Too early: keep the box open, do nothing.
			}
			Finish(state, ConfirmResult::Approved);
		}),
		.confirmText = tr::lng_owpengram_claude_confirm_allow(),
		.cancelText = tr::lng_owpengram_claude_confirm_deny(),
		.title = tr::lng_owpengram_claude_confirm_title(
			tr::now,
			lt_client,
			clientName),
	});
	state->shownAt = crl::now();
	state->box = window->show(std::move(box));
	if (const auto raw = state->box.get()) {
		// Cancel button, Esc, outside click, window closed: all deny.
		raw->boxClosing() | rpl::on_next([=] {
			// Already closing: do not close it again from Finish.
			state->box = base::weak_qptr<Ui::GenericBox>();
			Finish(state, ConfirmResult::Denied);
		}, raw->lifetime());
	}
	window->activate();
}

} // namespace Owpengram::Claude
