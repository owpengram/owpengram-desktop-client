/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/layers/box_content.h"
#include "owpengram/owpengram_servers.h"

#include <QtGui/QImage>

namespace Ui {
class InputField;
class VerticalLayout;
class Checkbox;
} // namespace Ui

class AddServerBox : public Ui::BoxContent {
public:
	AddServerBox(
		QWidget*,
		Fn<void(Owpengram::Server)> done);

protected:
	void prepare() override;
	void setInnerFocus() override;

private:
	void save();
	void chooseLogo();
	void addLabel(const QString &text);

	Fn<void(Owpengram::Server)> _done;
	object_ptr<Ui::VerticalLayout> _content;
	QPointer<Ui::RpWidget> _logoRow;
	QString _logoSourcePath;
	QImage _logoPreview;
	QPointer<Ui::InputField> _name;
	QPointer<Ui::InputField> _host;
	QPointer<Ui::InputField> _portField;
	QPointer<Ui::InputField> _rsaPublicKey;
	QPointer<Ui::InputField> _description;
	QPointer<Ui::Checkbox> _multiDc;
	QPointer<Ui::InputField> _mainDcField;

};
