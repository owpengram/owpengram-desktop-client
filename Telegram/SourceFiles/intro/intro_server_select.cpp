/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.
For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "base/weak_ptr.h"
#include "intro/intro_server_select.h"
#include "intro/intro_qr.h"
#include "intro/intro_widget.h"
#include "boxes/abstract_box.h"
#include "boxes/owpengram_add_server_box.h"
#include "boxes/owpengram_server_details_box.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "owpengram/owpengram_servers.h"
#include "ui/boxes/confirm_box.h"
#include "ui/painter.h"
#include "ui/toast/toast.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"

#include <QRegion>
namespace Intro {
namespace details {
namespace {
[[nodiscard]] QString FormatIpLine(const Owpengram::Server &server) {
	return tr::lng_owpengram_server_ip_line(
		tr::now,
		lt_host,
		server.host);
}
[[nodiscard]] QString FormatPortLine(const Owpengram::Server &server) {
	return tr::lng_owpengram_server_port_line(
		tr::now,
		lt_port,
		server.port > 0 ? QString::number(server.port) : u"—"_q);
}
class ServerCard final : public Ui::RippleButton {
public:
	ServerCard(
		QWidget *parent,
		const Owpengram::Server &server,
		Fn<void(const Owpengram::Server&)> join,
		Fn<void(const Owpengram::Server&)> details);
	void setOnline(bool online, int latencyMs);
protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
private:
	void updateGeometryInner();
	Owpengram::Server _server;
	Fn<void(const Owpengram::Server&)> _join;
	Fn<void(const Owpengram::Server&)> _details;
	object_ptr<Ui::RpWidget> _logo;
	object_ptr<Ui::FlatLabel> _name;
	object_ptr<Ui::FlatLabel> _ip;
	object_ptr<Ui::FlatLabel> _port;
	object_ptr<Ui::FlatLabel> _description;
	object_ptr<Ui::FlatLabel> _status;
	object_ptr<Ui::FlatLabel> _latency;
	object_ptr<Ui::RoundButton> _joinButton;
	std::optional<bool> _online;
	QPoint _statusDotCenter;
};
ServerCard::ServerCard(
	QWidget *parent,
	const Owpengram::Server &server,
	Fn<void(const Owpengram::Server&)> join,
	Fn<void(const Owpengram::Server&)> details)
: RippleButton(parent, st::introServerCardRipple)
, _server(server)
, _join(std::move(join))
, _details(std::move(details))
, _logo(this)
, _name(this, server.name, st::introServerCardName)
, _ip(this, FormatIpLine(server), st::introServerCardEndpoint)
, _port(this, FormatPortLine(server), st::introServerCardEndpoint)
, _description(this, server.description, st::introServerCardDescription)
, _status(this, tr::lng_owpengram_server_checking(tr::now), st::introServerCardStatusOnline)
, _latency(this, QString(), st::introServerCardStatusLatency)
, _joinButton(this, tr::lng_owpengram_server_join(), st::introServerCardJoinButton) {
	setPointerCursor(true);
	_logo->resize(st::introServerCardLogo, st::introServerCardLogo);
	_logo->setAttribute(Qt::WA_TransparentForMouseEvents);
	_logo->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_logo.data());
		PainterHighQualityEnabler hq(p);
		const auto size = st::introServerCardLogo;
		p.setPen(Qt::NoPen);
		p.setBrush(st::boxBg);
		p.drawEllipse(0, 0, size, size);
		const auto image = QPixmap(_server.logoPath).scaled(
			size,
			size,
			Qt::KeepAspectRatioByExpanding,
			Qt::SmoothTransformation);
		const auto left = (size - image.width()) / 2;
		const auto top = (size - image.height()) / 2;
		p.setClipRect(0, 0, size, size);
		p.setClipRegion(QRegion(0, 0, size, size, QRegion::Ellipse));
		p.drawPixmap(left, top, image);
	}, _logo->lifetime());
	for (const auto label : {
		_name.data(),
		_ip.data(),
		_port.data(),
		_description.data(),
		_status.data(),
		_latency.data(),
	}) {
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	setClickedCallback([=] {
		if (_details) {
			_details(_server);
		}
	});
	_joinButton->setClickedCallback([=] {
		if (_join) {
			_join(_server);
		}
	});
	resize(st::introServerCardWidth, st::introServerCardHeight);
	updateGeometryInner();
}
void ServerCard::setOnline(bool online, int latencyMs) {
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
	updateGeometryInner();
	update();
}
void ServerCard::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.setClipRect(e->rect());
	const auto inner = rect().marginsRemoved(st::introServerCardPadding);
	const auto radius = st::introServerCardRadius;
	p.setPen(Qt::NoPen);
	p.setBrush(st::boxBg);
	p.drawRoundedRect(inner, radius, radius);
	p.setPen(st::boxDividerFg);
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(inner, radius, radius);
	if (_online.has_value()) {
		const auto dotSize = st::introServerCardStatusDot;
		p.setPen(Qt::NoPen);
		p.setBrush(*_online ? st::windowActiveTextFg : st::attentionButtonFg);
		p.drawEllipse(
			QRectF(
				_statusDotCenter.x() - dotSize / 2.,
				_statusDotCenter.y() - dotSize / 2.,
				dotSize,
				dotSize));
	}
	paintRipple(p, 0, 0);
}
void ServerCard::resizeEvent(QResizeEvent *e) {
	RippleButton::resizeEvent(e);
	updateGeometryInner();
}
void ServerCard::updateGeometryInner() {
	const auto padding = st::introServerCardPadding;
	const auto inner = rect().marginsRemoved(padding);
	const auto logoSize = st::introServerCardLogo;
	const auto skip = st::introServerCardHeaderSkip;
	const auto joinWidth = st::introServerCardJoinButton.width;
	const auto joinHeight = st::introServerCardJoinButton.height;
	const auto footerHeight = st::introServerCardFooterHeight;
	const auto footerTop = inner.y() + inner.height() - footerHeight;
	const auto nameLineHeight = st::introServerCardName.style.font->height;
	_logo->move(inner.x(), inner.y());
	const auto nameLeft = inner.x() + logoSize + skip;
	const auto nameWidth = std::max(inner.right() - nameLeft + 1, 1);
	_name->resizeToWidth(nameWidth);
	_name->resize(nameWidth, nameLineHeight);
	_name->moveToLeft(
		nameLeft,
		inner.y() + (logoSize - nameLineHeight) / 2);
	const auto headerBottom = inner.y() + logoSize;
	_ip->resizeToWidth(inner.width());
	_ip->moveToLeft(inner.x(), headerBottom + skip);
	_port->resizeToWidth(inner.width());
	_port->moveToLeft(inner.x(), _ip->y() + _ip->height());
	const auto descriptionTop = _port->y() + _port->height() + skip;
	const auto descriptionMaxHeight = st::introServerCardDescriptionMaxHeight;
	const auto descriptionBottom = footerTop - skip;
	const auto descriptionHeight = std::max(
		descriptionBottom - descriptionTop,
		st::introServerCardDescription.style.font->height);
	_description->resizeToWidth(inner.width());
	_description->resize(
		inner.width(),
		std::min(_description->height(), std::min(descriptionHeight, descriptionMaxHeight)));
	_description->moveToLeft(inner.x(), descriptionTop);
	_joinButton->resize(joinWidth, joinHeight);
	_joinButton->moveToLeft(
		inner.right() - joinWidth + 1,
		footerTop + footerHeight - joinHeight);
	_joinButton->raise();
	const auto statusLeft = inner.x()
		+ st::introServerCardStatusDot
		+ st::introServerCardStatusDotSkip;
	const auto statusWidth = std::max(
		inner.width() - joinWidth - skip * 2,
		1);
	_status->resizeToWidth(statusWidth);
	_status->moveToLeft(statusLeft, footerTop);
	_latency->resizeToWidth(joinWidth);
	_latency->moveToLeft(
		inner.right() - joinWidth + 1,
		footerTop);
	const auto dotSize = st::introServerCardStatusDot;
	_statusDotCenter = QPoint(
		inner.x() + st::introServerCardStatusDotSkip + dotSize / 2,
		_status->y() + _status->height() / 2);
}
} // namespace
ServerSelectWidget::ServerSelectWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data, true)
, _addServer(
	this,
	tr::lng_owpengram_server_add(),
	st::introServerAddButton)
, _scroll(this, st::boxScroll)
, _grid(_scroll->setOwnedWidget(object_ptr<Ui::RpWidget>(_scroll))) {
	setTitleText(tr::lng_owpengram_server_selection_title());
	setDescriptionText(tr::lng_owpengram_server_selection_subtitle());
	_addServer->setClickedCallback([=] {
		const auto weak = base::make_weak(this);
		Ui::show(Box<AddServerBox>([=](Owpengram::Server server) {
			Ui::PostponeCall(weak, [=] {
				if (weak) {
					rebuildCards();
				}
			});
		}));
	});
	_statusTimer.setCallback([=] { rebuildCards(); });
	show();
}
void ServerSelectWidget::activate() {
	Step::activate();
	_scroll->show();
	_addServer->show();
	_statusTimer.cancel();
	_statusTimer.callEach(30000);
	Ui::PostponeCall(this, [=] {
		updateScrollGeometry();
		rebuildCards();
	});
}
void ServerSelectWidget::showEvent(QShowEvent *e) {
	Step::showEvent(e);
	Ui::PostponeCall(this, [=] {
		updateScrollGeometry();
		updateCardsGeometry();
	});
}
void ServerSelectWidget::submit() {
}
rpl::producer<QString> ServerSelectWidget::nextButtonText() const {
	return rpl::single(QString());
}
int ServerSelectWidget::scrollWidth() const {
	return std::min(
		width() - 2 * st::introSettingsSkip,
		st::introServerGridWidth);
}
int ServerSelectWidget::effectiveScrollWidth() const {
	return std::max(_scroll->width(), scrollWidth());
}
int ServerSelectWidget::columnCount() const {
	const auto width = effectiveScrollWidth();
	const auto spacing = st::introServerGridSpacing;
	const auto maxColumns = st::introServerGridMaxColumns;
	const auto cardWidth = st::introServerCardWidth;
	const auto rowWidth = cardWidth * maxColumns + spacing * (maxColumns - 1);
	if (width >= rowWidth) {
		return maxColumns;
	}
	return std::clamp(
		std::max(1, (width + spacing) / (cardWidth + spacing)),
		1,
		maxColumns);
}
void ServerSelectWidget::rebuildCards() {
	for (const auto card : _cards) {
		delete card;
	}
	_cards.clear();
	const auto join = [=](const Owpengram::Server &server) {
		joinServer(server);
	};
	const auto details = [=](const Owpengram::Server &server) {
		Ui::show(Box<ServerDetailsBox>(server, join));
	};
	for (const auto &server : Owpengram::ListServers()) {
		if (!server.valid()) {
			continue;
		}
		const auto card = Ui::CreateChild<ServerCard>(
			_grid,
			server,
			join,
			details);
		card->show();
		_cards.push_back(card);
		Owpengram::CheckServerOnline(server, crl::guard(card, [=](
				bool online,
				int latencyMs) {
			if (card) {
				card->setOnline(online, latencyMs);
			}
		}));
	}
	updateCardsGeometry();
}
void ServerSelectWidget::joinServer(const Owpengram::Server &server) {
	if (!server.valid()) {
		Ui::Toast::Show(tr::lng_owpengram_server_invalid(tr::now));
		return;
	}
	const auto weak = base::make_weak(this);
	Owpengram::CheckServerOnline(server, crl::guard(weak, [=](
			bool online,
			int latencyMs) {
		if (!weak) {
			return;
		}
		if (!online) {
			Ui::show(Ui::MakeConfirmBox({
				.text = tr::lng_owpengram_server_offline_confirm(),
				.confirmed = crl::guard(weak, [=] {
					proceedJoin(server);
				}),
				.confirmText = tr::lng_owpengram_server_join(),
			}));
			return;
		}
		proceedJoin(server);
	}));
}
void ServerSelectWidget::proceedJoin(const Owpengram::Server &server) {
	if (!server.valid()) {
		Ui::Toast::Show(tr::lng_owpengram_server_invalid(tr::now));
		return;
	}
	const auto weak = base::make_weak(this);
	Owpengram::ApplyServerToAccount(&account(), server);
	Owpengram::WaitForServerConnection(&account(), server, crl::guard(weak, [=] {
		if (weak) {
			goNext<QrWidget>();
		}
	}));
}
void ServerSelectWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	updateAddButtonGeometry();
	updateScrollGeometry();
}
void ServerSelectWidget::updateScrollGeometry() {
	const auto scrollTop = coverDescriptionBottom() + st::introServerGridTop;
	const auto scrollHeight = height() - scrollTop - st::introSettingsSkip;
	const auto width = scrollWidth();
	const auto cardSize = st::introServerCardHeight;
	_scroll->setGeometry(
		(this->width() - width) / 2,
		scrollTop,
		width,
		std::max(
			scrollHeight,
			cardSize + st::introServerGridSpacing));
	updateCardsGeometry();
}
void ServerSelectWidget::updateAddButtonGeometry() {
	const auto skip = st::introSettingsSkip;
	_addServer->moveToRight(
		skip,
		st::introCoverHeight - _addServer->height() - skip);
}
void ServerSelectWidget::updateCardsGeometry() {
	if (_cards.empty()) {
		return;
	}
	const auto columns = columnCount();
	const auto spacing = st::introServerGridSpacing;
	const auto scrollAreaWidth = effectiveScrollWidth();
	const auto cardWidth = st::introServerCardWidth;
	const auto cardHeight = st::introServerCardHeight;
	const auto rows = (_cards.size() + columns - 1) / columns;
	for (auto i = 0; i != _cards.size(); ++i) {
		const auto card = _cards[i];
		const auto row = i / columns;
		const auto column = i % columns;
		const auto cardsInRow = std::min<int>(
			columns,
			int(_cards.size()) - row * columns);
		const auto rowWidth = cardsInRow * cardWidth
			+ (cardsInRow - 1) * spacing;
		const auto rowOffset = (scrollAreaWidth - rowWidth) / 2;
		card->resize(cardWidth, cardHeight);
		card->moveToLeft(
			rowOffset + column * (cardWidth + spacing),
			row * (cardHeight + spacing));
	}
	const auto maxHeight = rows * cardHeight + (rows - 1) * spacing;
	_grid->resize(scrollAreaWidth, maxHeight);
}
} // namespace details
} // namespace Intro
