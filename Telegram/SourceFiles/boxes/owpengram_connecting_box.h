/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"
#include "base/timer.h"

namespace Ui {
class FlatLabel;
class VerticalLayout;
class RoundButton;
} // namespace Ui

// A modal shown while the client connects to a server. Escape and outside
// click are deliberately disabled throughout (a stray click must never drop
// an in-flight connection attempt by accident), but an explicit Cancel
// button is always present so the user is never stuck waiting out the full
// timeout against a server that's simply not answering. Pressing it invokes
// onCancel() (expected to stop the in-flight connection attempt, e.g. via
// the cancel handle returned by WaitForServerConnection) and closes the box.
// On timeout call showFailed(): the button relabels to "Close".
class ConnectingBox : public Ui::BoxContent {
public:
	ConnectingBox(QWidget*, Fn<void()> onCancel);

	void showFailed();

protected:
	void prepare() override;

private:
	void updateText();

	Fn<void()> _onCancel;
	object_ptr<Ui::VerticalLayout> _content;
	QPointer<Ui::FlatLabel> _label;
	QPointer<Ui::RoundButton> _button;
	base::Timer _dotsTimer;
	int _dots = 0;
	bool _failed = false;

};
