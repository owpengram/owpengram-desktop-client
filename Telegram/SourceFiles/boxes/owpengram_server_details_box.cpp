/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/owpengram_server_details_box.h"

#include "base/weak_ptr.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "owpengram/owpengram_servers.h"
#include "ui/boxes/confirm_box.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"

#include <QFontMetrics>
#include <QRegion>

namespace {
class DetailRow final : public Ui::RpWidget {
public:
	DetailRow(
		QWidget *parent,
		const QString &label,
		const QString &value);

	[[nodiscard]] int resizeGetHeight(int newWidth) override;

private:
	object_ptr<Ui::FlatLabel> _label;
	object_ptr<Ui::FlatLabel> _value;
};
DetailRow::DetailRow(
	QWidget *parent,
	const QString &label,
	const QString &value)
: RpWidget(parent)
, _label(this, label, st::introServerDetailsLabel)
, _value(this, value, st::introServerDetailsValue) {
}
int DetailRow::resizeGetHeight(int newWidth) {
	const auto labelWidth = st::introServerDetailsLabelWidth;
	_label->resizeToWidth(labelWidth);
	const auto valueWidth = std::max(newWidth - labelWidth - st::introServerDetailsRowSkip, 1);
	_value->resizeToWidth(valueWidth);
	const auto rowHeight = std::max(_label->height(), _value->height());
	_label->moveToLeft(0, (rowHeight - _label->height()) / 2);
	_value->moveToLeft(
		labelWidth + st::introServerDetailsRowSkip,
		(rowHeight - _value->height()) / 2);
	return rowHeight;
}

class DescriptionText final : public Ui::RpWidget {
public:
	DescriptionText(QWidget *parent, const QString &text);
	[[nodiscard]] int resizeGetHeight(int newWidth) override;
protected:
	void paintEvent(QPaintEvent *e) override;
private:
	QString _text;
};

DescriptionText::DescriptionText(QWidget *parent, const QString &text)
: RpWidget(parent)
, _text(text) {
}

int DescriptionText::resizeGetHeight(int newWidth) {
	if (_text.isEmpty() || newWidth <= 0) {
		return st::introServerDetailsDescription.style.font->height;
	}
	const QFontMetrics fm(st::introServerDetailsDescription.style.font->f);
	const auto bounds = fm.boundingRect(
		QRect(0, 0, newWidth, 99999),
		Qt::TextWordWrap | Qt::TextWrapAnywhere,
		_text);
	return std::max(bounds.height(), 1);
}

void DescriptionText::paintEvent(QPaintEvent *e) {
	if (_text.isEmpty()) return;
	const auto &st = st::introServerDetailsDescription;
	QPainter p(this);
	p.setFont(st.style.font->f);
	p.setPen(st.textFg->c);
	p.drawText(
		rect(),
		Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap | Qt::TextWrapAnywhere,
		_text);
}

class DescriptionSection final : public Ui::RpWidget {
public:
	DescriptionSection(QWidget *parent, const QString &text);
	[[nodiscard]] int resizeGetHeight(int newWidth) override;
private:
	object_ptr<Ui::FlatLabel> _title;
	object_ptr<Ui::RpWidget> _box;
	DescriptionText *_text = nullptr;
};

DescriptionSection::DescriptionSection(QWidget *parent, const QString &text)
: RpWidget(parent)
, _title(
	this,
	tr::lng_owpengram_server_details_description(tr::now),
	st::introServerDetailsSectionTitle)
, _box(this) {
	_text = Ui::CreateChild<DescriptionText>(_box.get(), text);
	_box->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_box);
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		const auto radius = st::introServerDetailsDescriptionRadius;
		p.drawRoundedRect(_box->rect(), radius, radius);
	}, _box->lifetime());
	setNaturalWidth(-1);
}

int DescriptionSection::resizeGetHeight(int newWidth) {
	const auto titleSkip = st::introServerDetailsRowSkip;
	const auto padding = st::introServerDetailsDescriptionPadding;
	const auto innerWidth = std::max(
		newWidth - padding.left() - padding.right(),
		1);

	_title->resizeToWidth(newWidth);
	_title->moveToLeft(0, 0);

	_text->resizeToWidth(innerWidth);
	_text->moveToLeft(padding.left(), padding.top());

	const auto boxTop = _title->height() + titleSkip;
	const auto contentHeight = _text->height();
	const auto naturalBoxHeight = contentHeight
		+ padding.top()
		+ padding.bottom();
	const auto boxHeight = std::max(
		naturalBoxHeight,
		st::introServerDetailsDescriptionMinHeight);

	_box->setGeometry(0, boxTop, newWidth, boxHeight);

	return boxTop + boxHeight;
}

class HeaderRow final : public Ui::RpWidget {
public:
	HeaderRow(
		QWidget *parent,
		const QString &name,
		const QString &logoPath,
		bool isOfficial);
protected:
	void resizeEvent(QResizeEvent *e) override;
private:
	object_ptr<Ui::RpWidget> _logo;
	object_ptr<Ui::FlatLabel> _name;
};
HeaderRow::HeaderRow(
	QWidget *parent,
	const QString &name,
	const QString &logoPath,
	bool isOfficial)
: RpWidget(parent)
, _logo(this)
, _name(this, name, st::introServerCardName) {
	const auto logoSize = st::introServerCardLogo;
	_logo->resize(logoSize, logoSize);
	_logo->paintRequest(
	) | rpl::on_next([=, path = Owpengram::ResolveServerLogoPath(logoPath)] {
		auto p = QPainter(_logo.data());
		PainterHighQualityEnabler hq(p);
		if (path.isEmpty()) {
			static const QColor kColors[7] = {
				QColor(0xE5, 0x39, 0x35),
				QColor(0xE6, 0x4A, 0x19),
				QColor(0x43, 0xA0, 0x47),
				QColor(0x00, 0x88, 0x8D),
				QColor(0x19, 0x76, 0xD2),
				QColor(0x51, 0x2D, 0xA8),
				QColor(0xF5, 0x7F, 0x17),
			};
			const int colorIdx = int(qHash(name) % 7u);
			p.setPen(Qt::NoPen);
			p.setBrush(kColors[colorIdx]);
			p.drawEllipse(0, 0, logoSize, logoSize);
			const auto ch = name.isEmpty()
				? QChar('?')
				: name.at(0).toUpper();
			p.setPen(Qt::white);
			auto font = p.font();
			font.setPixelSize(logoSize / 2);
			font.setBold(true);
			p.setFont(font);
			p.drawText(QRect(0, 0, logoSize, logoSize), Qt::AlignCenter, ch);
		} else {
			// Keyed on "is this bundled art" rather than on isOfficial:
			// ":/gui/art/..." logos ship pre-shaped and carry transparency
			// outside the circle, so masking them would clip the design.
			// Everything else is an arbitrary rectangular image -- picked by
			// the user, or fetched from the server by RefreshServersInfo --
			// and has to be cropped to the circle. The official server now
			// gets a fetched logo too, so isOfficial no longer implies
			// bundled art, which is what left it square here.
			const auto circleMask = !path.startsWith(u":/"_q);
			const auto image = QPixmap(path).scaled(
				logoSize,
				logoSize,
				circleMask
					? Qt::KeepAspectRatioByExpanding
					: Qt::KeepAspectRatio,
				Qt::SmoothTransformation);
			const auto left = (logoSize - image.width()) / 2;
			const auto top = (logoSize - image.height()) / 2;
			if (circleMask) {
				p.setPen(Qt::NoPen);
				p.setBrush(st::boxBg);
				p.drawEllipse(0, 0, logoSize, logoSize);
				// QRegion-based clipping is always rasterized with hard,
				// aliased edges in Qt no matter what PainterHighQualityEnabler
				// sets -- a QPainterPath clip is a genuinely different path
				// that does respect antialiasing. Same fix as ServerRow in
				// intro_server_select.cpp, which this had drifted out of sync
				// with (visibly jagged circle here, smooth one there).
				auto clipPath = QPainterPath();
				clipPath.addEllipse(0, 0, logoSize, logoSize);
				p.setClipPath(clipPath);
			}
			p.drawPixmap(left, top, image);
		}
	}, _logo->lifetime());
	resize(parent->width(), logoSize);
}
void HeaderRow::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
	const auto logoSize = st::introServerCardLogo;
	const auto skip = st::introServerDetailsHeaderSkip;
	_logo->moveToLeft(0, 0);
	const auto nameLeft = logoSize + skip;
	const auto nameWidth = std::max(width() - nameLeft, 1);
	_name->resizeToWidth(nameWidth);
	_name->moveToLeft(nameLeft, (height() - _name->height()) / 2);
}
class StatusBlock final : public Ui::RpWidget {
public:
	StatusBlock(QWidget *parent);
	void setOnline(bool online, int latencyMs);
protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
private:
	object_ptr<Ui::FlatLabel> _status;
	object_ptr<Ui::FlatLabel> _latency;
	std::optional<bool> _online;
};
StatusBlock::StatusBlock(QWidget *parent)
: RpWidget(parent)
, _status(this, tr::lng_owpengram_server_checking(tr::now), st::introServerCardStatusOnline)
, _latency(this, QString(), st::introServerDetailsStatusLatency) {
	const auto padding = st::introServerDetailsStatusBlock;
	resize(parent->width(), padding.top() + _status->height() + padding.bottom());
}
void StatusBlock::setOnline(bool online, int latencyMs) {
	_online = online;
	_status->setText(online
		? tr::lng_owpengram_server_online(tr::now)
		: tr::lng_owpengram_server_offline(tr::now));
	_status->setTextColorOverride(online
		? st::windowActiveTextFg->c
		: st::attentionButtonFg->c);
	_latency->setText(online && latencyMs >= 0
		? tr::lng_owpengram_server_latency(
			tr::now,
			lt_latency,
			QString::number(latencyMs))
		: QString());
	const auto padding = st::introServerDetailsStatusBlock;
	resize(
		width(),
		padding.top() + _status->height() + padding.bottom());
	update();
}
void StatusBlock::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.setClipRect(e->rect());
	PainterHighQualityEnabler hq(p);

	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBgOver);
	const auto radius = st::introServerDetailsStatusRadius;
	p.drawRoundedRect(rect(), radius, radius);

	if (_online.has_value()) {
		const auto padding = st::introServerDetailsStatusBlock;
		const auto dotSize = st::introServerCardStatusDot;
		const auto dotLeft = padding.left();
		const auto dotTop = padding.top()
			+ (_status->height() - dotSize) / 2;
		p.setPen(Qt::NoPen);
		p.setBrush(*_online ? st::windowActiveTextFg : st::attentionButtonFg);
		p.drawEllipse(QRectF(dotLeft, dotTop, dotSize, dotSize));
	}
}
void StatusBlock::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
	const auto padding = st::introServerDetailsStatusBlock;
	const auto innerLeft = padding.left();
	const auto innerWidth = width() - padding.left() - padding.right();
	const auto statusLeft = innerLeft
		+ st::introServerCardStatusDot
		+ st::introServerDetailsStatusDotSkip;
	const auto statusWidth = std::max(
		innerWidth / 2,
		1);
	_status->resizeToWidth(statusWidth);
	_status->moveToLeft(statusLeft, padding.top());
	_latency->resizeToWidth(innerWidth);
	_latency->moveToLeft(innerLeft, padding.top());
	const auto blockHeight = padding.top()
		+ _status->height()
		+ padding.bottom();
	if (height() != blockHeight) {
		resize(width(), blockHeight);
	}
}
} // namespace
ServerDetailsBox::ServerDetailsBox(
	QWidget*,
	Owpengram::Server server,
	Fn<void(const Owpengram::Server&)> connect,
	Fn<void()> removed,
	Fn<void()> edit)
: _server(std::move(server))
, _connect(std::move(connect))
, _removed(std::move(removed))
, _edit(std::move(edit))
, _content(this) {
	const auto rowMargin = st::boxRowPadding + QMargins(
		0,
		st::introServerDetailsRowSkip,
		0,
		0);
	const auto sectionMargin = st::boxRowPadding + QMargins(
		0,
		st::introServerDetailsSectionSkip,
		0,
		0);

	_content->add(
		object_ptr<HeaderRow>(
			_content,
			_server.name,
			_server.logoPath,
			_server.isOfficial),
		st::boxRowPadding);
	_content->add(
		object_ptr<DetailRow>(
			_content,
			tr::lng_owpengram_server_details_host(tr::now),
			_server.host),
		rowMargin);
	_content->add(
		object_ptr<DetailRow>(
			_content,
			tr::lng_owpengram_server_details_port(tr::now),
			_server.port > 0 ? QString::number(_server.port) : u"—"_q),
		rowMargin);
	_content->add(
		object_ptr<DescriptionSection>(
			_content,
			_server.description.isEmpty() ? u"—"_q : _server.description),
		sectionMargin);
	_content->add(
		object_ptr<Ui::FlatLabel>(
			_content,
			tr::lng_owpengram_server_details_status(tr::now),
			st::introServerDetailsSectionTitle),
		sectionMargin);
	_statusBlock = _content->add(
		object_ptr<StatusBlock>(_content),
		st::boxRowPadding + QMargins(0, st::introServerDetailsRowSkip, 0, 0));
}
void ServerDetailsBox::prepare() {
	setTitle(tr::lng_owpengram_server_details_title());
	if (_connect) {
		if (Owpengram::IsRemovableServer(_server)) {
			const auto weak = base::make_weak(this);
			addLeftButton(tr::lng_owpengram_server_delete(), [=] {
				// Nothing else keeps an account pointed at a server that no
				// longer exists in the list from silently breaking (see
				// UpdateCustomServer's doc comment on the "server name
				// disappeared" symptom) -- so rather than leave that account
				// around in a broken state, warn up front and actually log
				// it out as part of this same confirmation, instead of
				// letting a stale account for a server nobody's going to use
				// again linger in the switcher.
				const auto affected = Owpengram::AccountsUsingServer(
					_server.id);
				const auto text = affected.empty()
					? tr::lng_owpengram_server_delete_confirm(
						tr::now,
						lt_name,
						_server.name)
					: tr::lng_owpengram_server_delete_confirm_with_accounts(
						tr::now,
						lt_count,
						float64(affected.size()),
						lt_name,
						_server.name);
				getDelegate()->show(Ui::MakeConfirmBox({
					.text = text,
					.confirmed = crl::guard(weak, [=](Fn<void()> close) {
						if (!weak) {
							close();
							return;
						}
						for (const auto &account : affected) {
							account->logOut();
						}
						if (Owpengram::RemoveCustomServer(_server.id)) {
							if (_removed) {
								_removed();
							}
							closeBox();
						}
						close();
					}),
					.confirmText = tr::lng_owpengram_server_delete(),
					.confirmStyle = &st::attentionBoxButton,
				}));
			}, st::attentionBoxButton);
		}
		addButton(tr::lng_owpengram_server_join(), [=] {
			_connect(_server);
			closeBox();
		});
		addButton(tr::lng_cancel(), [=] { closeBox(); });
		if (_edit && Owpengram::IsRemovableServer(_server)) {
			addButton(tr::lng_proxy_menu_edit(), [=] {
				const auto edit = _edit;
				closeBox();
				edit();
			});
		}
	} else {
		addButton(tr::lng_close(), [=] { closeBox(); });
	}
	setDimensionsToContent(st::boxWidth, _content);
	Owpengram::CheckServerOnline(_server, crl::guard(this, [=](
			bool online,
			int latencyMs) {
		if (const auto status = _statusBlock.data()) {
			static_cast<StatusBlock*>(status)->setOnline(online, latencyMs);
		}
	}));
}
