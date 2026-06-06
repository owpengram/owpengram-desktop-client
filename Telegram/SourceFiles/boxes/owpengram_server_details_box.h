/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"
#include "owpengram/owpengram_servers.h"

namespace Ui {
class FlatLabel;
class RoundButton;
class RpWidget;
class VerticalLayout;
} // namespace Ui

class ServerDetailsBox : public Ui::BoxContent {
public:
	ServerDetailsBox(
		QWidget*,
		Owpengram::Server server,
		Fn<void(const Owpengram::Server&)> connect);

protected:
	void prepare() override;

private:
	Owpengram::Server _server;
	Fn<void(const Owpengram::Server&)> _connect;
	object_ptr<Ui::VerticalLayout> _content;
	QPointer<Ui::RpWidget> _statusBlock;

};
