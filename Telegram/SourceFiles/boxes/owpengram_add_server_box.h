/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "ui/layers/box_content.h"
#include "ui/wrap/slide_wrap.h"
#include "owpengram/owpengram_servers.h"

#include <QtGui/QImage>
#include <memory>

namespace Ui {
class InputField;
class VerticalLayout;
class RadiobuttonGroup;
class LinkButton;
class RpWidget;
} // namespace Ui

class AddServerBox : public Ui::BoxContent {
public:
	AddServerBox(
		QWidget*,
		Fn<void(Owpengram::Server)> done,
		Owpengram::Server existing = {});
	~AddServerBox();

	// Add/edit server and the owpg://addserver deep link each reach this
	// box through a different show() -- a window-scoped one
	// (Core::Application's deep-link handler) or the app-global one
	// (ServerSelectWidget's own Add/Edit buttons), neither aware of the
	// other's layer stack. Without this, an owpg://addserver link opened
	// while the Add Server box from a button click is already showing (or
	// a second click on that same button before the box finishes
	// animating in) stacks a second instance on top instead of being a
	// no-op. Every call site must check this before calling show().
	[[nodiscard]] static bool IsOpen();

protected:
	void prepare() override;
	void setInnerFocus() override;

private:
	void save();
	void chooseLogo();

	Fn<void(Owpengram::Server)> _done;
	object_ptr<Ui::VerticalLayout> _content;

	QImage _logoPreview;
	QString _logoSourcePath;
	Fn<void()> _refreshAvatar;

	void fetchPublicKeyForAddress();
	void applyFetchedIcon(const QByteArray &data);
	void toggleAdvanced();

	Ui::InputField *_name = nullptr;
	Ui::InputField *_description = nullptr;
	Ui::InputField *_address = nullptr;
	Ui::InputField *_rsaPublicKey = nullptr;
	Ui::InputField *_mainDcField = nullptr;
	Ui::RpWidget *_addressSpinner = nullptr;

	// Suppresses re-fetching for an address we already have a result for.
	QString _lastFetchedAddress;
	base::Timer _fetchDebounce;

	std::shared_ptr<Ui::RadiobuttonGroup> _typeGroup;
	Ui::SlideWrap<Ui::VerticalLayout> *_mainDcWrap = nullptr;
	Ui::SlideWrap<Ui::VerticalLayout> *_advancedWrap = nullptr;
	Ui::LinkButton *_advancedToggle = nullptr;
	bool _advancedExpanded = false;

	QString _editingId;
};
