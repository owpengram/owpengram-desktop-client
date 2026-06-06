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
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "owpengram/owpengram_servers.h"
#include "ui/boxes/confirm_box.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"
namespace Intro {
namespace details {
namespace {
class ServerCard final : public Ui::RippleButton {
public:
	ServerCard(
		QWidget *parent,
		const Owpengram::Server &server,
		Fn<void(const Owpengram::Server&)> join);
	void setOnline(bool online);
protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
private:
	void updateGeometryInner();
	Owpengram::Server _server;
	Fn<void(const Owpengram::Server&)> _join;
	object_ptr<Ui::RpWidget> _logo;
	object_ptr<Ui::FlatLabel> _name;
	object_ptr<Ui::FlatLabel> _endpoint;
	object_ptr<Ui::FlatLabel> _description;
	object_ptr<Ui::FlatLabel> _status;
	object_ptr<Ui::RoundButton> _joinButton;
	std::optional<bool> _online;
};
ServerCard::ServerCard(
	QWidget *parent,
	const Owpengram::Server &server,
	Fn<void(const Owpengram::Server&)> join)
: RippleButton(parent, st::introServerCardRipple)
, _server(server)
, _join(std::move(join))
, _logo(this)
, _name(this, server.name, st::introServerCardName)
, _endpoint(this, tr::lng_owpengram_server_endpoint(
	tr::now,
	lt_host,
	server.host,
	lt_port,
	QString::number(server.port)), st::introServerCardEndpoint)
, _description(this, server.description, st::introServerCardDescription)
, _status(this, QString(), st::introServerCardStatusOnline)
, _joinButton(this, tr::lng_owpengram_server_join(), st::introServerCardJoinButton) {
	setPointerCursor(false);
	_logo->resize(st::introServerCardLogo, st::introServerCardLogo);
	_logo->setAttribute(Qt::WA_TransparentForMouseEvents);
	_logo->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_logo.data());
		PainterHighQualityEnabler hq(p);
		p.setRenderHint(QPainter::Antialiasing);
		const auto image = QPixmap(_server.logoPath).scaled(
			st::introServerCardLogo,
			st::introServerCardLogo,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
		const auto left = (st::introServerCardLogo - image.width()) / 2;
		const auto top = (st::introServerCardLogo - image.height()) / 2;
		p.drawPixmap(left, top, image);
	}, _logo->lifetime());
	_name->setAttribute(Qt::WA_TransparentForMouseEvents);
	_endpoint->setAttribute(Qt::WA_TransparentForMouseEvents);
	_description->setAttribute(Qt::WA_TransparentForMouseEvents);
	_status->setAttribute(Qt::WA_TransparentForMouseEvents);
	_status->hide();
	_joinButton->setClickedCallback([=] {
		if (_join) {
			_join(_server);
		}
	});
	resize(st::introServerCardMinWidth, st::introServerCardHeight);
	updateGeometryInner();
}
void ServerCard::setOnline(bool online) {
	_online = online;
	_status->setText(online
		? tr::lng_owpengram_server_online(tr::now)
		: tr::lng_owpengram_server_offline(tr::now));
	_status->setTextColorOverride(online
		? st::windowActiveTextFg->c
		: st::attentionButtonFg->c);
	_status->show();
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
		const auto dotLeft = inner.x();
		const auto dotTop = inner.y() + inner.height()
			- _joinButton->height()
			- st::introServerCardPadding.bottom()
			- _status->height() / 2
			- dotSize / 2;
		p.setPen(Qt::NoPen);
		p.setBrush(*_online ? st::windowActiveTextFg : st::attentionButtonFg);
		p.drawEllipse(
			QRectF(dotLeft, dotTop, dotSize, dotSize));
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
	const auto joinHeight = _joinButton->height();
	const auto statusDotSkip = st::introServerCardStatusDot + padding.left() / 2;
	_logo->move(inner.x(), inner.y());
	const auto textLeft = inner.x() + logoSize + padding.left();
	const auto textWidth = std::max(
		inner.width() - logoSize - padding.left(),
		1);
	_name->resizeToWidth(textWidth);
	_name->moveToLeft(textLeft, inner.y());
	_endpoint->resizeToWidth(textWidth);
	_endpoint->moveToLeft(
		textLeft,
		_name->y() + _name->height() + padding.top() / 3);
	const auto contentTop = std::max(
		_logo->y() + logoSize,
		_endpoint->y() + _endpoint->height())
		+ padding.top();
	_description->resizeToWidth(inner.width());
	_description->moveToLeft(inner.x(), contentTop);
	const auto footerTop = inner.y() + inner.height() - joinHeight
		- padding.bottom() / 2
		- _status->height();
	_status->resizeToWidth(inner.width() - statusDotSkip);
	_status->moveToLeft(inner.x() + statusDotSkip, footerTop);
	_joinButton->resize(inner.width(), joinHeight);
	_joinButton->moveToLeft(
		inner.x(),
		inner.y() + inner.height() - joinHeight);
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
	const auto minWidth = st::introServerCardMinWidth;
	const auto spacing = st::introServerGridSpacing;
	const auto maxColumns = st::introServerGridMaxColumns;
	if (width <= minWidth) {
		return 1;
	}
	const auto columns = (width + spacing) / (minWidth + spacing);
	return std::clamp(columns, 1, maxColumns);
}
void ServerSelectWidget::rebuildCards() {
	for (const auto card : _cards) {
		delete card;
	}
	_cards.clear();
	const auto join = [=](const Owpengram::Server &server) {
		joinServer(server);
	};
	for (const auto &server : Owpengram::ListServers()) {
		if (!server.valid() && !server.isOfficial) {
			continue;
		}
		const auto card = Ui::CreateChild<ServerCard>(_grid, server, join);
		card->show();
		_cards.push_back(card);
		Owpengram::CheckServerOnline(server, crl::guard(card, [=](bool online) {
			if (card) {
				card->setOnline(online);
			}
		}));
	}
	updateCardsGeometry();
}
void ServerSelectWidget::joinServer(const Owpengram::Server &server) {
	const auto weak = base::make_weak(this);
	Owpengram::CheckServerOnline(server, crl::guard(weak, [=](bool online) {
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
	Owpengram::ApplyServerToAccount(&account(), server);
	goNext<QrWidget>();
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
	_scroll->setGeometry(
		(this->width() - width) / 2,
		scrollTop,
		width,
		std::max(
			scrollHeight,
			st::introServerCardHeight + st::introServerGridSpacing));
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
	const auto cardWidth = (scrollAreaWidth - spacing * (columns - 1))
		/ columns;
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
