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
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/cached_round_corners.h"
#include "ui/painter.h"
#include "ui/toast/toast.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_intro.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QRegion>

namespace Intro {
namespace details {
namespace {

[[nodiscard]] QString FormatEndpointLine(const Owpengram::Server &server) {
	return tr::lng_owpengram_server_endpoint(
		tr::now,
		lt_host,
		server.host,
		lt_port,
		server.port > 0 ? QString::number(server.port) : u"—"_q);
}

[[nodiscard]] QString FormatStatusText(bool online, int latencyMs) {
	if (online && latencyMs >= 0) {
		return tr::lng_owpengram_server_online(tr::now)
			+ u" · "_q
			+ tr::lng_owpengram_server_latency(
				tr::now,
				lt_latency,
				QString::number(latencyMs));
	} else if (online) {
		return tr::lng_owpengram_server_online(tr::now);
	}
	return tr::lng_owpengram_server_offline(tr::now);
}

void AddServerLogo(
		not_null<Ui::SettingsButton*> button,
		const QString &logoPath) {
	const auto icon = Ui::CreateChild<Ui::RpWidget>(button.get());
	const auto size = st::introServerRowLogo;
	icon->resize(size, size);
	icon->setAttribute(Qt::WA_TransparentForMouseEvents);
	button->sizeValue(
	) | rpl::on_next([=](const QSize &widgetSize) {
		icon->moveToLeft(
			st::settingsButton.iconLeft,
			(widgetSize.height() - size) / 2,
			widgetSize.width());
	}, icon->lifetime());
	icon->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(icon);
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::boxBg);
		p.drawEllipse(0, 0, size, size);
		const auto image = QPixmap(logoPath).scaled(
			size,
			size,
			Qt::KeepAspectRatioByExpanding,
			Qt::SmoothTransformation);
		const auto left = (size - image.width()) / 2;
		const auto top = (size - image.height()) / 2;
		p.setClipRect(0, 0, size, size);
		p.setClipRegion(QRegion(0, 0, size, size, QRegion::Ellipse));
		p.drawPixmap(left, top, image);
	}, icon->lifetime());
	icon->show();
}

} // namespace

ServerSelectWidget::ServerSelectWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data, false)
, _panel(this)
, _scroll(Ui::CreateChild<Ui::ScrollArea>(_panel.get()))
, _content(_scroll.get()->setOwnedWidget(
	object_ptr<Ui::VerticalLayout>(_scroll.data()))) {
	setTitleText(tr::lng_owpengram_server_selection_title());
	setDescriptionText(tr::lng_owpengram_server_selection_subtitle());

	_panel->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_panel.data());
		Ui::FillRoundRect(
			p,
			_panel->rect(),
			st::boxBg,
			Ui::BoxCorners);
	}, _panel->lifetime());

	widthValue(
	) | rpl::on_next([=] {
		updateListGeometry();
	}, lifetime());

	_statusTimer.setCallback([=] { rebuildList(); });
	show();
}

void ServerSelectWidget::activate() {
	Step::activate();
	_panel->show();
	_scroll.get()->show();
	_statusTimer.cancel();
	_statusTimer.callEach(30000);
	Ui::PostponeCall(this, [=] {
		updateListGeometry();
		rebuildList();
	});
}

void ServerSelectWidget::showEvent(QShowEvent *e) {
	Step::showEvent(e);
	Ui::PostponeCall(this, [=] {
		updateListGeometry();
	});
}

void ServerSelectWidget::submit() {
}

rpl::producer<QString> ServerSelectWidget::nextButtonText() const {
	return rpl::single(QString());
}

int ServerSelectWidget::listWidth() const {
	return st::introNextButton.width;
}

void ServerSelectWidget::rebuildList() {
	_rowsLifetime.destroy();
	_content->clear();

	const auto join = [=](const Owpengram::Server &server) {
		joinServer(server);
	};
	const auto details = [=](const Owpengram::Server &server) {
		Ui::show(Box<ServerDetailsBox>(server, join));
	};
	const auto showAddServer = [=] {
		const auto weak = base::make_weak(this);
		Ui::show(Box<AddServerBox>([=](Owpengram::Server server) {
			Ui::PostponeCall(weak, [=] {
				if (weak) {
					rebuildList();
				}
			});
		}));
	};

	auto servers = std::vector<Owpengram::Server>();
	for (const auto &server : Owpengram::ListServers()) {
		if (server.valid()) {
			servers.push_back(server);
		}
	}

	Ui::AddDivider(_content);
	for (auto i = 0; i != int(servers.size()); ++i) {
		const auto &server = servers[i];
		const auto status = _rowsLifetime.make_state<rpl::variable<QString>>(
			tr::lng_owpengram_server_checking(tr::now));
		const auto button = Settings::AddButtonWithLabel(
			_content,
			rpl::single(server.name),
			status->value(),
			st::settingsButton);
		AddServerLogo(button, server.logoPath);
		button->addClickHandler([=] {
			details(server);
		});

		_content->add(
			object_ptr<Ui::FlatLabel>(
				_content,
				FormatEndpointLine(server),
				st::introServerRowSubtext),
			st::introServerRowSubtextMargin);
		if (!server.description.isEmpty()) {
			_content->add(
				object_ptr<Ui::FlatLabel>(
					_content,
					server.description,
					st::introServerRowDescription),
				st::introServerRowDescriptionMargin);
		}

		const auto weak = base::make_weak(button);
		Owpengram::CheckServerOnline(server, crl::guard(button, [=](
				bool online,
				int latencyMs) {
			if (weak) {
				*status = FormatStatusText(online, latencyMs);
			}
		}));

		if (i + 1 != int(servers.size())) {
			Ui::AddDivider(_content);
		}
	}

	Ui::AddDivider(_content);
	Settings::AddButtonWithIcon(
		_content,
		tr::lng_owpengram_server_add(),
		st::settingsButtonLightNoIcon,
		{ &st::menuIconAdd }
	)->addClickHandler(showAddServer);
	Ui::AddDivider(_content);

	_content->resizeToWidth(listWidth());
	updateListGeometry();
}

void ServerSelectWidget::joinServer(const Owpengram::Server &server) {
	if (!server.valid()) {
		Ui::Toast::Show(tr::lng_owpengram_server_invalid(tr::now));
		return;
	}
	const auto weak = base::make_weak(this);
	Owpengram::CheckServerOnline(server, crl::guard(weak, [=](bool online, int) {
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
	updateListGeometry();
}

void ServerSelectWidget::updateListGeometry() {
	const auto w = listWidth();
	const auto left = contentLeft();
	const auto top = descriptionBottom() + st::introServerListTop;
	const auto height = std::max(
		0,
		this->height() - top - st::introServerListBottom);
	_panel->setGeometry(left, top, w, height);
	_scroll.get()->setGeometry(_panel->rect());
	_content->resizeToWidth(w);
}

} // namespace details
} // namespace Intro
