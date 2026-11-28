/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/buttons.h"

namespace Owpengram::Claude {

// The chat top bar button that toggles the Claude panel: an "AI" label
// sized and rippled like the other top bar icon buttons.
class TopBarButton final : public Ui::RippleButton {
public:
	explicit TopBarButton(QWidget *parent);

	// The panel is shown: the label takes the active color.
	void setActive(bool active);

protected:
	void paintEvent(QPaintEvent *e) override;
	QImage prepareRippleMask() const override;
	QPoint prepareRippleStartPosition() const override;

private:
	[[nodiscard]] QRect rippleRect() const;

	bool _active = false;

};

} // namespace Owpengram::Claude
