/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/owpengram_add_server_box.h"

#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "ui/effects/ripple_animation.h"
#include "ui/image/image.h"
#include "ui/painter.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"

namespace {

class ServerLogoPicker final : public Ui::RippleButton {
public:
	ServerLogoPicker(
		QWidget *parent,
		not_null<const QImage*> preview,
		Fn<void()> choose);

private:
	void paintEvent(QPaintEvent *e) override;

	const QImage *_preview = nullptr;

};

class ServerLogoRow final : public Ui::RpWidget {
public:
	ServerLogoRow(
		QWidget *parent,
		not_null<const QImage*> preview,
		Fn<void()> choose);

	void refreshPreview();

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	ServerLogoPicker *_picker = nullptr;
	object_ptr<Ui::FlatLabel> _hint;

};

ServerLogoPicker::ServerLogoPicker(
		QWidget *parent,
		not_null<const QImage*> preview,
		Fn<void()> choose)
: RippleButton(parent, st::defaultRippleAnimation)
, _preview(preview) {
	const auto size = st::introServerAddLogoSize;
	resize(size, size);
	setClickedCallback(std::move(choose));
}

void ServerLogoPicker::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	PainterHighQualityEnabler hq(p);
	paintRipple(p, 0, 0);

	const auto size = st::introServerAddLogoSize;
	const auto pixmap = _preview->isNull()
		? QPixmap(Owpengram::DefaultLogoPath()).scaled(
			size,
			size,
			Qt::KeepAspectRatioByExpanding,
			Qt::SmoothTransformation)
		: QPixmap::fromImage(*_preview).scaled(
			size,
			size,
			Qt::KeepAspectRatioByExpanding,
			Qt::SmoothTransformation);
	const auto left = (size - pixmap.width()) / 2;
	const auto top = (size - pixmap.height()) / 2;

	p.setPen(Qt::NoPen);
	p.setBrush(st::boxBg);
	p.drawEllipse(0, 0, size, size);
	p.setClipRect(0, 0, size, size);
	p.setClipRegion(QRegion(0, 0, size, size, QRegion::Ellipse));
	p.drawPixmap(left, top, pixmap);
}

ServerLogoRow::ServerLogoRow(
		QWidget *parent,
		not_null<const QImage*> preview,
		Fn<void()> choose)
: RpWidget(parent)
, _hint(this, tr::lng_owpengram_server_logo_choose(tr::now), st::boxLabel) {
	_picker = Ui::CreateChild<ServerLogoPicker>(this, preview, std::move(choose));
}

void ServerLogoRow::refreshPreview() {
	_picker->update();
}

void ServerLogoRow::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
	const auto logoSize = st::introServerAddLogoSize;
	_picker->moveToLeft(0, (height() - logoSize) / 2);
	const auto hintLeft = logoSize + st::introServerAddLogoHintSkip;
	const auto hintWidth = std::max(width() - hintLeft, 1);
	_hint->resizeToWidth(hintWidth);
	_hint->moveToLeft(hintLeft, (height() - _hint->height()) / 2);
}

} // namespace

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

	addLabel(tr::lng_owpengram_server_logo(tr::now));
	_logoRow = _content->add(
		object_ptr<ServerLogoRow>(
			_content,
			&_logoPreview,
			[=] { chooseLogo(); }),
		st::boxRowPadding);
	_logoRow->resize(_logoRow->width(), st::introServerAddLogoRowHeight);

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

	addLabel(tr::lng_owpengram_server_rsa_key(tr::now));
	_rsaPublicKey = _content->add(
		object_ptr<Ui::InputField>(
			_content,
			st::defaultInputField,
			Ui::InputField::Mode::MultiLine,
			tr::lng_owpengram_server_rsa_key_hint()),
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

	_content->add(
		object_ptr<Ui::FixedHeightWidget>(
			_content,
			st::introServerAddSectionSkip));

	_multiDc = _content->add(
		object_ptr<Ui::Checkbox>(
			_content,
			u"Multi-DC server (Telegram-compatible)"_q,
			false),
		st::boxRowPadding);

	addLabel(u"Main data-center (single-server only)"_q);
	_mainDcField = _content->add(
		object_ptr<Ui::InputField>(
			_content,
			st::defaultInputField,
			tr::lng_owpengram_server_port(),
			u"1"_q),
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

void AddServerBox::chooseLogo() {
	const auto callback = [=](FileDialog::OpenResult &&result) {
		if (result.paths.isEmpty()) {
			return;
		}
		const auto path = result.paths.front();
		auto image = Images::Read({
			.path = path,
			.forceOpaque = true,
		}).image;
		if (image.isNull()) {
			Ui::Toast::Show(tr::lng_owpengram_server_logo_invalid(tr::now));
			return;
		}
		_logoSourcePath = path;
		_logoPreview = std::move(image);
		if (const auto row = _logoRow.data()) {
			static_cast<ServerLogoRow*>(row)->refreshPreview();
		}
	};
	FileDialog::GetOpenPath(
		this,
		tr::lng_owpengram_server_logo_choose(tr::now),
		FileDialog::ImagesFilter(),
		crl::guard(this, callback));
}

void AddServerBox::save() {
	const auto name = _name->getLastText().trimmed();
	auto host = _host->getLastText().trimmed();
	auto port = _portField->getLastText().toInt();
	const auto rsaPublicKey = _rsaPublicKey->getLastText().trimmed();
	const auto description = _description->getLastText().trimmed();
	const auto multiDc = _multiDc && _multiDc->checked();
	const auto mainDcId = (!multiDc && _mainDcField)
		? _mainDcField->getLastText().trimmed().toInt()
		: 0;
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
	if (!Owpengram::IsValidRsaPublicKeyPem(rsaPublicKey)) {
		Ui::Toast::Show(tr::lng_owpengram_server_rsa_key_invalid(tr::now));
		_rsaPublicKey->setFocusFast();
		return;
	}
	if (const auto server = Owpengram::AddCustomServer(
			name,
			host,
			port,
			description,
			rsaPublicKey,
			_logoSourcePath,
			multiDc,
			mainDcId)) {
		if (_done) {
			_done(*server);
		}
		closeBox();
	}
}
