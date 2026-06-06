/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/owpengram_add_server_box.h"

#include "lang/lang_keys.h"
#include "ui/toast/toast.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"

AddServerBox::AddServerBox(
	QWidget*,
	Fn<void(Owpengram::Server)> done)
: _done(std::move(done))
, _content(this) {
	addLabel(tr::lng_owpengram_server_name(tr::now));
	_name = _content->add(
		object_ptr<Ui::InputField>(
			_content,
			st::defaultInputField,
			tr::lng_owpengram_server_name()),
		st::boxRowPadding);

	_content->add(
		object_ptr<Ui::FixedHeightWidget>(
			_content,
			st::introServerAddSectionSkip));

	addLabel(tr::lng_owpengram_server_host(tr::now));
	_host = _content->add(
		object_ptr<Ui::InputField>(
			_content,
			st::defaultInputField,
			tr::lng_owpengram_server_host_hint()),
		st::boxRowPadding);

	addLabel(tr::lng_owpengram_server_port(tr::now));
	_portField = _content->add(
		object_ptr<Ui::InputField>(
			_content,
			st::defaultInputField,
			tr::lng_owpengram_server_port()),
		st::boxRowPadding);

	_content->add(
		object_ptr<Ui::FixedHeightWidget>(
			_content,
			st::introServerAddSectionSkip));

	addLabel(tr::lng_owpengram_server_description(tr::now));
	_description = _content->add(
		object_ptr<Ui::InputField>(
			_content,
			st::defaultInputField,
			Ui::InputField::Mode::MultiLine,
			tr::lng_owpengram_server_description()),
		st::boxRowPadding);
}

void AddServerBox::prepare() {
	setTitle(tr::lng_owpengram_server_add_title());
	addButton(tr::lng_settings_save(), [=] { save(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });
	setDimensionsToContent(st::boxWidth, _content);
}

void AddServerBox::setInnerFocus() {
	if (_name) {
		_name->setFocusFast();
	}
}

void AddServerBox::addLabel(const QString &text) {
	_content->add(
		object_ptr<Ui::FlatLabel>(
			_content,
			text,
			st::boxLabel),
		st::boxRowPadding);
}

void AddServerBox::save() {
	const auto name = _name->getLastText().trimmed();
	auto host = _host->getLastText().trimmed();
	auto port = _portField->getLastText().toInt();
	const auto description = _description->getLastText().trimmed();
	if (host.contains(':')) {
		const auto parts = host.split(':');
		if (parts.size() == 2 && port <= 0) {
			host = parts[0].trimmed();
			port = parts[1].trimmed().toInt();
		}
	}
	if (name.isEmpty()) {
		Ui::Toast::Show(tr::lng_owpengram_server_invalid(tr::now));
		_name->setFocusFast();
		return;
	}
	if (host.isEmpty()) {
		Ui::Toast::Show(tr::lng_owpengram_server_invalid(tr::now));
		_host->setFocusFast();
		return;
	}
	if (port <= 0) {
		Ui::Toast::Show(tr::lng_owpengram_server_invalid(tr::now));
		_portField->setFocusFast();
		return;
	}
	if (const auto server = Owpengram::AddCustomServer(
			name,
			host,
			port,
			description)) {
		if (_done) {
			_done(*server);
		}
		closeBox();
	}
}
