/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/widgets/buttons.h"

class PeerData;

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Owpengram::Claude {

// Round button next to "jump down" showing the level of Claude access
// to the chat. Click opens a menu to change the level for this chat.
class AccessBadge final : public Ui::RippleButton {
public:
	AccessBadge(QWidget *parent, not_null<PeerData*> peer);
	~AccessBadge();

	[[nodiscard]] bool shouldShow() const;

protected:
	void paintEvent(QPaintEvent *e) override;

	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	void showMenu();

	const style::TwoIconButton &_st;
	const not_null<PeerData*> _peer;

	base::unique_qptr<Ui::PopupMenu> _menu;

};

} // namespace Owpengram::Claude
