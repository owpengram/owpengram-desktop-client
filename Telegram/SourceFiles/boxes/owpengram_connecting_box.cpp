/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/owpengram_connecting_box.h"

#include "lang/lang_keys.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"

namespace {

constexpr auto kDotsIntervalMs = crl::time(400);
constexpr auto kMaxDots = 3;

} // namespace

ConnectingBox::ConnectingBox(QWidget*, Fn<void()> onCancel)
: _onCancel(std::move(onCancel))
, _content(this) {
}

void ConnectingBox::prepare() {
	// Escape and outside click stay off even with the button present, so a
	// stray click elsewhere can never drop an in-flight connection attempt
	// by accident -- cancelling is only ever a deliberate button press.
	setCloseByEscape(false);
	setCloseByOutsideClick(false);

	_content->add(
		object_ptr<Ui::FixedHeightWidget>(
			_content,
			st::boxLittleSkip));
	_label = _content->add(
		object_ptr<Ui::FlatLabel>(
			_content,
			QString(),
			st::introErrorCentered),
		st::boxRowPadding);
	_content->add(
		object_ptr<Ui::FixedHeightWidget>(
			_content,
			st::boxLittleSkip));

	updateText();

	_dotsTimer.setCallback([=] {
		_dots = (_dots + 1) % (kMaxDots + 1);
		updateText();
	});
	_dotsTimer.callEach(kDotsIntervalMs);

	// Present from the very first frame, not only after a timeout: a dead
	// server must never force the user to sit through the full 30s wait.
	_button = addButton(tr::lng_cancel(), [=] {
		if (_onCancel) {
			_onCancel();
		}
		closeBox();
	});

	setDimensionsToContent(st::boxWidth, _content);
}

void ConnectingBox::updateText() {
	if (!_label) {
		return;
	}
	if (_failed) {
		_label->setText(u"Connection failed"_q);
	} else {
		_label->setText(u"Connecting to server"_q + QString(_dots, '.'));
	}
}

void ConnectingBox::showFailed() {
	if (_failed) {
		return;
	}
	_failed = true;
	_dotsTimer.cancel();
	updateText();

	// The connection attempt is already over, so relabel the same button
	// rather than layering a second one on top of it.
	if (_button) {
		_button->setText(tr::lng_close());
	}
}
