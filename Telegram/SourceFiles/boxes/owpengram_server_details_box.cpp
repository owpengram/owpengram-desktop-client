/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/owpengram_server_details_box.h"

#include "base/weak_ptr.h"
#include "lang/lang_keys.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"

#include <QRegion>

namespace {
class DetailRow final : public Ui::RpWidget {
public:
	DetailRow(
		QWidget *parent,
		const QString &label,
		const QString &value);
protected:
	void resizeEvent(QResizeEvent *e) override;
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
	const auto height = std::max(
		_label->height(),
		_value->height());
	resize(parent->width(), height);
}
void DetailRow::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
	const auto labelWidth = st::introServerDetailsLabelWidth;
	_label->resizeToWidth(labelWidth);
	_label->moveToLeft(0, 0);
	const auto valueWidth = std::max(width() - labelWidth - st::introServerDetailsRowSkip, 1);
	_value->resizeToWidth(valueWidth);
	_value->moveToLeft(labelWidth + st::introServerDetailsRowSkip, 0);
	const auto rowHeight = std::max(_label->height(), _value->height());
	if (height() != rowHeight) {
		resize(width(), rowHeight);
	}
}
class HeaderRow final : public Ui::RpWidget {
public:
	HeaderRow(
		QWidget *parent,
		const QString &name,
		const QString &logoPath);
protected:
	void resizeEvent(QResizeEvent *e) override;
private:
	object_ptr<Ui::RpWidget> _logo;
	object_ptr<Ui::FlatLabel> _name;
};
HeaderRow::HeaderRow(
	QWidget *parent,
	const QString &name,
	const QString &logoPath)
: RpWidget(parent)
, _logo(this)
, _name(this, name, st::introServerCardName) {
	const auto logoSize = st::introServerCardLogo;
	_logo->resize(logoSize, logoSize);
	_logo->paintRequest(
	) | rpl::on_next([=, path = logoPath] {
		auto p = QPainter(_logo.data());
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::boxBg);
		p.drawEllipse(0, 0, logoSize, logoSize);
		const auto image = QPixmap(path).scaled(
			logoSize,
			logoSize,
			Qt::KeepAspectRatioByExpanding,
			Qt::SmoothTransformation);
		const auto left = (logoSize - image.width()) / 2;
		const auto top = (logoSize - image.height()) / 2;
		p.setClipRect(0, 0, logoSize, logoSize);
		p.setClipRegion(QRegion(0, 0, logoSize, logoSize, QRegion::Ellipse));
		p.drawPixmap(left, top, image);
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
, _latency(this, QString(), st::introServerCardStatusLatency) {
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
	const auto padding = st::introServerDetailsStatusBlock;
	const auto inner = rect().marginsRemoved(padding);
	p.setPen(st::boxDividerFg);
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(inner, st::introServerCardRadius / 2, st::introServerCardRadius / 2);
	if (_online.has_value()) {
		const auto dotSize = st::introServerCardStatusDot;
		const auto dotTop = padding.top()
			+ (_status->height() - dotSize) / 2;
		p.setPen(Qt::NoPen);
		p.setBrush(*_online ? st::windowActiveTextFg : st::attentionButtonFg);
		p.drawEllipse(
			QRectF(
				padding.left() + st::introServerCardStatusDotSkip,
				dotTop,
				dotSize,
				dotSize));
	}
}
void StatusBlock::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
	const auto padding = st::introServerDetailsStatusBlock;
	const auto statusLeft = padding.left()
		+ st::introServerCardStatusDot
		+ st::introServerCardStatusDotSkip;
	const auto innerWidth = width() - padding.left() - padding.right();
	const auto statusWidth = std::max(
		innerWidth / 2,
		1);
	_status->resizeToWidth(statusWidth);
	_status->moveToLeft(statusLeft, padding.top());
	_latency->resizeToWidth(statusWidth);
	_latency->moveToLeft(
		width() - padding.right() - _latency->width(),
		padding.top());
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
	Fn<void(const Owpengram::Server&)> connect)
: _server(std::move(server))
, _connect(std::move(connect))
, _content(this) {
	_content->add(
		object_ptr<HeaderRow>(
			_content,
			_server.name,
			_server.logoPath),
		st::boxRowPadding);
	_content->add(
		object_ptr<DetailRow>(
			_content,
			tr::lng_owpengram_server_details_host(tr::now),
			_server.host),
		st::boxRowPadding + QMargins(0, st::introServerDetailsRowSkip, 0, 0));
	_content->add(
		object_ptr<DetailRow>(
			_content,
			tr::lng_owpengram_server_details_port(tr::now),
			_server.port > 0 ? QString::number(_server.port) : u"—"_q),
		st::boxRowPadding + QMargins(0, st::introServerDetailsRowSkip, 0, 0));
	if (!_server.description.isEmpty()) {
		_content->add(
			object_ptr<DetailRow>(
				_content,
				tr::lng_owpengram_server_details_description(tr::now),
				_server.description),
			st::boxRowPadding + QMargins(0, st::introServerDetailsRowSkip, 0, 0));
	}
	_statusBlock = _content->add(
		object_ptr<StatusBlock>(_content),
		st::boxRowPadding + QMargins(0, st::introServerDetailsRowSkip, 0, 0));
}
void ServerDetailsBox::prepare() {
	setTitle(rpl::single(_server.name));
	addButton(tr::lng_owpengram_server_join(), [=] {
		if (_connect) {
			_connect(_server);
		}
		closeBox();
	});
	addButton(tr::lng_cancel(), [=] { closeBox(); });
	setDimensionsToContent(st::boxWidth, _content);
	Owpengram::CheckServerOnline(_server, crl::guard(this, [=](
			bool online,
			int latencyMs) {
		if (const auto status = _statusBlock.data()) {
			static_cast<StatusBlock*>(status)->setOnline(online, latencyMs);
		}
	}));
}
