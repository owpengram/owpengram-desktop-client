/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "intro/intro_step.h"
#include "owpengram/owpengram_servers.h"

namespace Ui {
class RpWidget;
class ScrollArea;
class VerticalLayout;
} // namespace Ui

namespace Intro {
namespace details {

class ServerSelectWidget final : public Step {
public:
	ServerSelectWidget(
		QWidget *parent,
		not_null<Main::Account*> account,
		not_null<Data*> data);

	void activate() override;
	void submit() override;
	[[nodiscard]] rpl::producer<QString> nextButtonText() const override;
	[[nodiscard]] bool hasBack() const override {
		return true;
	}

	void resizeEvent(QResizeEvent *e) override;
	void showEvent(QShowEvent *e) override;

private:
	void rebuildList();
	void joinServer(const Owpengram::Server &server);
	void proceedJoin(const Owpengram::Server &server);
	void updateListGeometry();
	[[nodiscard]] int listWidth() const;

	object_ptr<Ui::RpWidget> _panel;
	object_ptr<Ui::ScrollArea> _scroll;
	not_null<Ui::VerticalLayout*> _content;
	rpl::lifetime _rowsLifetime;
	base::Timer _statusTimer;
};

} // namespace details
} // namespace Intro
