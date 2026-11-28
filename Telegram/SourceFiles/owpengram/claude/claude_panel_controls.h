/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/rp_widget.h"

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Owpengram::Claude {

class ControlButton;
class UsageRing;

// The row under the panel input (like the Claude app): "Model  Effort"
// (menu: models, effort), the permission mode (menu) and the subscription
// usage ring (popup with the 5-hour and weekly limits). Options are stored
// in AccessStore; changed() fires after the user changed one.
class PanelControls final : public Ui::RpWidget {
public:
	explicit PanelControls(QWidget *parent);

	[[nodiscard]] rpl::producer<> changed() const;

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void refresh();
	void showModelMenu();
	void showModeMenu();
	void showUsage();
	void updateGeometry(int width);

	const not_null<ControlButton*> _model;
	const not_null<ControlButton*> _mode;
	const not_null<UsageRing*> _usage;
	base::unique_qptr<Ui::PopupMenu> _menu;
	rpl::event_stream<> _changed;

};

} // namespace Owpengram::Claude
