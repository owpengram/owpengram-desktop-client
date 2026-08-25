/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/owpengram_add_server_box.h"

#include "core/file_utilities.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h"
#include "lang/lang_keys.h"
#include "ui/effects/ripple_animation.h"
#include "ui/image/image.h"
#include "ui/painter.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtCore/QDir>
#include <QtCore/QTemporaryFile>

namespace {

constexpr auto kBoxWidth = 700;
constexpr auto kAvatarGap = 14;
constexpr auto kAddressSpinnerSize = 20;
constexpr auto kFetchDebounceMs = crl::time(500);

// ── Avatar circle picker ──────────────────────────────────────────────────

class ServerLogoPicker final : public Ui::RippleButton {
public:
	ServerLogoPicker(
		QWidget *parent,
		not_null<const QImage*> preview,
		Fn<void()> choose);

	void refresh() { update(); }

private:
	void paintEvent(QPaintEvent *e) override;

	const QImage *_preview = nullptr;
};

ServerLogoPicker::ServerLogoPicker(
		QWidget *parent,
		not_null<const QImage*> preview,
		Fn<void()> choose)
: RippleButton(parent, st::defaultRippleAnimation)
, _preview(preview) {
	const auto sz = st::introServerAddLogoSize;
	resize(sz, sz);
	setClickedCallback(std::move(choose));
}

void ServerLogoPicker::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	PainterHighQualityEnabler hq(p);
	paintRipple(p, 0, 0);

	const auto sz = st::introServerAddLogoSize;
	p.setPen(Qt::NoPen);

	if (_preview->isNull()) {
		// Placeholder: neutral circle + "+" to invite the user to pick a photo.
		p.setBrush(st::windowBgOver);
		p.drawEllipse(0, 0, sz, sz);
		const auto arm = sz / 4;
		const auto cx = sz / 2;
		const auto cy = sz / 2;
		p.setPen(QPen(st::windowSubTextFg, 2, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(cx - arm, cy, cx + arm, cy);
		p.drawLine(cx, cy - arm, cx, cy + arm);
	} else {
		p.setBrush(st::boxBg);
		p.drawEllipse(0, 0, sz, sz);
		p.setClipRegion(QRegion(0, 0, sz, sz, QRegion::Ellipse));
		const auto pixmap = QPixmap::fromImage(*_preview).scaled(
			sz, sz,
			Qt::KeepAspectRatioByExpanding,
			Qt::SmoothTransformation);
		p.drawPixmap((sz - pixmap.width()) / 2, (sz - pixmap.height()) / 2, pixmap);
	}
}

// ── Top row: [avatar] [name / description] ────────────────────────────────
// Floating placeholders inside InputField act as labels — no separate FlatLabel.

class AvatarNameDescRow final : public Ui::RpWidget {
public:
	AvatarNameDescRow(
		QWidget *parent,
		not_null<const QImage*> preview,
		Fn<void()> choose);

	[[nodiscard]] Ui::InputField *name() const { return _name; }
	[[nodiscard]] Ui::InputField *desc() const { return _desc; }
	void refreshPreview() { _picker->refresh(); }

protected:
	int resizeGetHeight(int newWidth) override;

private:
	ServerLogoPicker *_picker  = nullptr;
	Ui::FlatLabel    *_iconLbl = nullptr;
	Ui::InputField   *_name    = nullptr;
	Ui::InputField   *_desc    = nullptr;
};

AvatarNameDescRow::AvatarNameDescRow(
		QWidget *parent,
		not_null<const QImage*> preview,
		Fn<void()> choose)
: RpWidget(parent) {
	_picker = Ui::CreateChild<ServerLogoPicker>(this, preview, std::move(choose));
	_iconLbl = Ui::CreateChild<Ui::FlatLabel>(
		this,
		u"Icon"_q,
		st::introServerAddIconCaption);
	_name = Ui::CreateChild<Ui::InputField>(
		this,
		st::defaultInputField,
		tr::lng_owpengram_server_name());
	_desc = Ui::CreateChild<Ui::InputField>(
		this,
		st::defaultInputField,
		tr::lng_owpengram_server_description());
}

int AvatarNameDescRow::resizeGetHeight(int newWidth) {
	const auto sz = st::introServerAddLogoSize;
	const auto rightX = sz + kAvatarGap;
	const auto rightW = std::max(newWidth - rightX, 1);

	_name->resizeToWidth(rightW);
	_name->moveToLeft(rightX, 0);
	const auto descY = _name->height() + 8;
	_desc->resizeToWidth(rightW);
	_desc->moveToLeft(rightX, descY);
	const auto rightH = descY + _desc->height();

	_iconLbl->resizeToWidth(sz);
	const auto lblGap = 4;
	const auto lblH = _iconLbl->height();
	const auto leftH = sz + lblGap + lblH;
	const auto totalH = std::max(rightH, leftH);

	const auto leftTopY = (totalH - leftH) / 2;
	_picker->resize(sz, sz);
	_picker->moveToLeft(0, leftTopY);
	_iconLbl->moveToLeft(0, leftTopY + sz + lblGap);

	return totalH;
}

// ── Side-by-side radio buttons ────────────────────────────────────────────

class RadioTypeRow final : public Ui::RpWidget {
public:
	RadioTypeRow(
		QWidget *parent,
		std::shared_ptr<Ui::RadiobuttonGroup> group);

protected:
	int resizeGetHeight(int newWidth) override;

private:
	Ui::Radiobutton *_single = nullptr;
	Ui::Radiobutton *_multi  = nullptr;
};

RadioTypeRow::RadioTypeRow(
		QWidget *parent,
		std::shared_ptr<Ui::RadiobuttonGroup> group)
: RpWidget(parent) {
	_single = Ui::CreateChild<Ui::Radiobutton>(
		this, group, 0, u"Single server"_q);
	_multi = Ui::CreateChild<Ui::Radiobutton>(
		this, group, 2, u"Multi-DC (Telegram)"_q);
}

int RadioTypeRow::resizeGetHeight(int newWidth) {
	const auto gap = 12;
	const auto half = std::max((newWidth - gap) / 2, 1);
	_single->resizeToWidth(half);
	_single->moveToLeft(0, 0);
	_multi->resizeToWidth(half);
	_multi->moveToLeft(half + gap, 0);
	return std::max(_single->height(), _multi->height());
}

// Splits "host:port" on the last colon (IPv6-literal-safe enough for our
// purposes -- this UI only ever targets IPv4/hostname backends). Returns
// port = 0 when the address has no ':' or a non-numeric tail.
[[nodiscard]] std::pair<QString, int> ParseAddress(const QString &address) {
	const auto colon = address.lastIndexOf(':');
	if (colon <= 0) {
		return { address, 0 };
	}
	return { address.left(colon).trimmed(), address.mid(colon + 1).trimmed().toInt() };
}

} // namespace

// ── AddServerBox ──────────────────────────────────────────────────────────

AddServerBox::AddServerBox(
	QWidget*,
	Fn<void(Owpengram::Server)> done,
	Owpengram::Server existing)
: _done(std::move(done))
, _content(this)
, _typeGroup(std::make_shared<Ui::RadiobuttonGroup>(0)) {

	_content->add(object_ptr<Ui::FixedHeightWidget>(
		_content,
		st::introServerAddSectionSkip));

	// ── avatar + name + description ──────────────────────────────────────
	const auto topRow = _content->add(
		object_ptr<AvatarNameDescRow>(
			_content,
			&_logoPreview,
			[=] { chooseLogo(); }),
		st::boxRowPadding);
	_refreshAvatar = [topRow] { topRow->refreshPreview(); };
	_name        = topRow->name();
	_description = topRow->desc();

	_content->add(object_ptr<Ui::FixedHeightWidget>(
		_content,
		st::introServerAddSectionSkip));

	// ── address (host:port) ──────────────────────────────────────────────
	_address = _content->add(
		object_ptr<Ui::InputField>(
			_content,
			st::defaultInputField,
			tr::lng_owpengram_server_host_hint()),
		st::boxRowPadding);
	_addressSpinner = Info::Statistics::InfiniteRadialAnimationWidget(
		_address,
		kAddressSpinnerSize,
		&st::defaultInfiniteRadialAnimation);
	_addressSpinner->hide();
	_address->sizeValue(
	) | rpl::on_next([=](QSize size) {
		_addressSpinner->moveToRight(
			0,
			(size.height() - kAddressSpinnerSize) / 2,
			size.width());
	}, _address->lifetime());

	_content->add(object_ptr<Ui::FixedHeightWidget>(
		_content,
		st::introServerAddSectionSkip));

	// ── advanced (collapsed by default -- everything below only matters
	// for servers that don't serve their key/DC over /owpengram/server-info,
	// or to override what auto-fetch filled in) ───────────────────────────
	_advancedToggle = _content->add(
		object_ptr<Ui::LinkButton>(
			_content,
			u"Advanced"_q,
			st::defaultLinkButton),
		st::boxRowPadding);
	_advancedToggle->setClickedCallback([=] { toggleAdvanced(); });

	_content->add(object_ptr<Ui::FixedHeightWidget>(_content, 6));

	_advancedWrap = _content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_content,
			object_ptr<Ui::VerticalLayout>(_content)));
	{
		const auto advanced = _advancedWrap->entity();

		// ── server type ─────────────────────────────────────────────────
		advanced->add(
			object_ptr<Ui::FlatLabel>(
				advanced,
				u"Server type"_q,
				st::boxLabel),
			st::boxRowPadding);
		advanced->add(object_ptr<Ui::FixedHeightWidget>(advanced, 6));
		advanced->add(
			object_ptr<RadioTypeRow>(advanced, _typeGroup),
			st::boxRowPadding);

		advanced->add(object_ptr<Ui::FixedHeightWidget>(
			advanced,
			st::introServerAddSectionSkip / 2));

		// ── main DC (single-server only) ────────────────────────────────
		_mainDcWrap = advanced->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				advanced,
				object_ptr<Ui::VerticalLayout>(advanced)));
		{
			const auto inner = _mainDcWrap->entity();
			inner->add(
				object_ptr<Ui::FlatLabel>(
					inner,
					u"Main data center"_q,
					st::boxLabel),
				st::boxRowPadding);
			_mainDcField = inner->add(
				object_ptr<Ui::InputField>(
					inner,
					st::defaultInputField,
					rpl::single(u"1..5"_q),
					u"2"_q),
				st::boxRowPadding);
			inner->add(object_ptr<Ui::FixedHeightWidget>(
				inner,
				st::introServerAddSectionSkip / 2));
		}

		// ── RSA key ──────────────────────────────────────────────────────
		advanced->add(
			object_ptr<Ui::FlatLabel>(
				advanced,
				tr::lng_owpengram_server_rsa_key(tr::now),
				st::boxLabel),
			st::boxRowPadding);
		_rsaPublicKey = advanced->add(
			object_ptr<Ui::InputField>(
				advanced,
				st::defaultInputField,
				Ui::InputField::Mode::MultiLine,
				tr::lng_owpengram_server_rsa_key_hint()),
			st::boxRowPadding);
	}
	_advancedWrap->toggle(false, anim::type::instant);

	_content->add(object_ptr<Ui::FixedHeightWidget>(
		_content,
		st::introServerAddSectionSkip));

	// ── toggle main DC visibility ─────────────────────────────────────────
	_typeGroup->setChangedCallback([=](int value) {
		_mainDcWrap->toggle(value == 0, anim::type::normal);
	});

	// ── auto-fetch the key + DC as the user types the address (debounced),
	// and immediately on losing focus (covers paste-then-tab-away) ────────
	_address->changes(
	) | rpl::on_next([=] {
		_fetchDebounce.callOnce(kFetchDebounceMs);
	}, _address->lifetime());
	_fetchDebounce.setCallback([=] { fetchPublicKeyForAddress(); });
	_address->focusedChanges(
	) | rpl::filter([](bool focused) {
		return !focused;
	}) | rpl::on_next([=] {
		_fetchDebounce.cancel();
		fetchPublicKeyForAddress();
	}, lifetime());

	// ── pre-fill when editing an existing server ──────────────────────────
	if (existing.valid()) {
		_editingId = existing.id;
		_name->setText(existing.name);
		_description->setText(existing.description);
		_address->setText(existing.port > 0
			? existing.host + ':' + QString::number(existing.port)
			: existing.host);
		_typeGroup->setValue(existing.multiDc ? 2 : 0);
		if (existing.mainDcId > 0) {
			_mainDcField->setText(QString::number(existing.mainDcId));
		}
		_rsaPublicKey->setText(existing.rsaPublicKey);
		_mainDcWrap->toggle(!existing.multiDc, anim::type::instant);
		// Show what's already configured instead of hiding it behind a
		// click -- Advanced only collapses by default for the new-server,
		// auto-fetch-does-everything case.
		toggleAdvanced();
	}
}

void AddServerBox::prepare() {
	setTitle(_editingId.isEmpty()
		? tr::lng_owpengram_server_add_title()
		: tr::lng_owpengram_server_edit_title());
	addButton(tr::lng_settings_save(), [=] { save(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });
	setDimensionsToContent(kBoxWidth, _content);
}

void AddServerBox::setInnerFocus() {
	if (_name) {
		_name->setFocusFast();
	}
}

void AddServerBox::chooseLogo() {
	const auto callback = [=](FileDialog::OpenResult &&result) {
		if (result.paths.isEmpty()) {
			return;
		}
		auto image = Images::Read({
			.path = result.paths.front(),
			.forceOpaque = true,
		}).image;
		if (image.isNull()) {
			Ui::Toast::Show(tr::lng_owpengram_server_logo_invalid(tr::now));
			return;
		}
		_logoSourcePath = result.paths.front();
		_logoPreview = std::move(image);
		if (_refreshAvatar) {
			_refreshAvatar();
		}
	};
	FileDialog::GetOpenPath(
		this,
		tr::lng_owpengram_server_logo_choose(tr::now),
		FileDialog::ImagesFilter(),
		crl::guard(this, callback));
}

void AddServerBox::toggleAdvanced() {
	_advancedExpanded = !_advancedExpanded;
	_advancedWrap->toggle(_advancedExpanded, anim::type::normal);
	_advancedToggle->setText(_advancedExpanded
		? u"Hide advanced"_q
		: u"Advanced"_q);
}

void AddServerBox::fetchPublicKeyForAddress() {
	const auto address = _address->getLastText().trimmed();
	if (address.isEmpty() || address == _lastFetchedAddress) {
		return;
	}
	const auto [host, port] = ParseAddress(address);
	if (host.isEmpty() || port <= 0) {
		return;
	}
	_lastFetchedAddress = address;
	_addressSpinner->show();
	Owpengram::FetchServerInfo(host, port, crl::guard(this, [=](
			std::optional<Owpengram::ServerInfoFetchResult> result) {
		_addressSpinner->hide();
		if (!result || _lastFetchedAddress != address) {
			return;
		}
		// Always overwrite -- the server is the source of truth once it
		// answers, even if the user had typed/pasted something already.
		if (!result->rsaPublicKeyPem.isEmpty()) {
			_rsaPublicKey->setText(result->rsaPublicKeyPem);
		}
		if (result->dcId > 0 && _mainDcField) {
			_mainDcField->setText(QString::number(result->dcId));
		}
		if (!result->name.isEmpty()) {
			_name->setText(result->name);
		}
		if (!result->description.isEmpty()) {
			_description->setText(result->description);
		}
		if (result->hasIcon) {
			Owpengram::FetchServerIcon(host, port, crl::guard(this, [=](
					QByteArray data) {
				if (_lastFetchedAddress != address) {
					return;
				}
				applyFetchedIcon(data);
			}));
		}
	}));
}

void AddServerBox::applyFetchedIcon(const QByteArray &data) {
	if (data.isEmpty()) {
		return;
	}
	auto file = QTemporaryFile(
		QDir::tempPath() + u"/owpengram-server-icon-XXXXXX"_q);
	file.setAutoRemove(false);
	if (!file.open() || file.write(data) < 0) {
		return;
	}
	file.close();
	const auto path = file.fileName();
	auto image = Images::Read({ .path = path, .forceOpaque = true }).image;
	if (image.isNull()) {
		return;
	}
	// Always overwrite, same as the RSA key/DC/name/description above -- the
	// server answering is the source of truth even if the user already
	// picked a local file via chooseLogo().
	_logoSourcePath = path;
	_logoPreview = std::move(image);
	if (_refreshAvatar) {
		_refreshAvatar();
	}
}

void AddServerBox::save() {
	const auto name = _name->getLastText().trimmed();
	const auto address = _address->getLastText().trimmed();
	const auto [host, port] = ParseAddress(address);
	const auto rsaPublicKey = _rsaPublicKey->getLastText().trimmed();
	const auto description = _description->getLastText().trimmed();
	const auto multiDc = (_typeGroup->current() == 2);
	const auto mainDcId = (!multiDc && _mainDcField)
		? _mainDcField->getLastText().trimmed().toInt()
		: 0;

	if (name.isEmpty()) {
		Ui::Toast::Show(tr::lng_owpengram_server_invalid(tr::now));
		_name->setFocusFast();
		return;
	}
	if (host.isEmpty() || port <= 0) {
		Ui::Toast::Show(tr::lng_owpengram_server_invalid(tr::now));
		_address->setFocusFast();
		return;
	}
	if (!Owpengram::IsValidRsaPublicKeyPem(rsaPublicKey)) {
		Ui::Toast::Show(tr::lng_owpengram_server_rsa_key_invalid(tr::now));
		_rsaPublicKey->setFocusFast();
		return;
	}

	// Editing updates the existing row in place -- NOT delete + re-add, which
	// would mint a fresh id and silently break every account already
	// pointed at this server (see UpdateCustomServer's doc comment).
	const auto server = _editingId.isEmpty()
		? Owpengram::AddCustomServer(
			name,
			host,
			port,
			description,
			rsaPublicKey,
			_logoSourcePath,
			multiDc,
			mainDcId)
		: Owpengram::UpdateCustomServer(
			_editingId,
			name,
			host,
			port,
			description,
			rsaPublicKey,
			_logoSourcePath,
			multiDc,
			mainDcId);
	if (server) {
		if (_done) {
			_done(*server);
		}
		closeBox();
	}
}
