/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/claude/claude_panel_connectors.h"

#include "lang/lang_keys.h"
#include "owpengram/claude/claude_mcp_server.h"
#include "owpengram/claude/claude_panel_model.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "styles/style_claude.h"

#include <QtCore/QStringList>

namespace Owpengram::Claude {
namespace {

[[nodiscard]] bool NeedsReconnect(const QString &status) {
	return (status == u"failed"_q) || (status == u"needs-auth"_q);
}

} // namespace

PanelConnectors::PanelConnectors(
	QWidget *parent,
	std::shared_ptr<PanelModel> model)
: VerticalLayout(parent)
, _model(std::move(model))
, _list([&] {
	const auto &padding = st::owpengramClaudePanelConnectorsPadding;
	add(
		object_ptr<Ui::FlatLabel>(
			this,
			tr::lng_owpengram_claude_panel_connectors(),
			st::owpengramClaudePanelConnectorsTitle),
		style::margins(padding.left(), padding.top(), padding.right(), 0),
		style::al_justify);
	return add(
		object_ptr<Ui::VerticalLayout>(this),
		style::margins(padding.left(), 0, padding.right(), 0),
		style::al_justify);
}()) {
	const auto &padding = st::owpengramClaudePanelConnectorsPadding;
	add(
		object_ptr<Ui::FlatLabel>(
			this,
			Mcp().clientsValue(
			) | rpl::map([](const std::vector<QString> &clients) {
				auto list = QStringList();
				for (const auto &client : clients) {
					list.push_back(client);
				}
				return list.isEmpty()
					? tr::lng_owpengram_claude_panel_clients_none(tr::now)
					: tr::lng_owpengram_claude_panel_clients(
						tr::now,
						lt_clients,
						list.join(u", "_q));
			}),
			st::owpengramClaudePanelConnectorLabel),
		style::margins(
			padding.left(),
			st::owpengramClaudePanelConnectorSkip,
			padding.right(),
			padding.bottom()),
		style::al_justify);

	_model->connectorsValue(
	) | rpl::on_next([=](const std::vector<Connector> &connectors) {
		fill(connectors);
	}, lifetime());
}

void PanelConnectors::fill(const std::vector<Connector> &connectors) {
	_list->clear();
	const auto skip = st::owpengramClaudePanelConnectorSkip;
	if (connectors.empty()) {
		_list->add(
			object_ptr<Ui::FlatLabel>(
				_list,
				tr::lng_owpengram_claude_panel_connectors_none(tr::now),
				st::owpengramClaudePanelConnectorLabel),
			style::margins(0, skip, 0, 0),
			style::al_justify);
	}
	const auto weak = std::weak_ptr<PanelModel>(_model);
	for (const auto &connector : connectors) {
		const auto text = connector.status.isEmpty()
			? connector.name
			: (connector.name
				+ QString::fromUtf8(" \xC2\xB7 ")
				+ connector.status);
		_list->add(
			object_ptr<Ui::FlatLabel>(
				_list,
				text,
				st::owpengramClaudePanelConnectorLabel),
			style::margins(0, skip, 0, 0),
			style::al_justify);
		if (NeedsReconnect(connector.status)) {
			const auto button = _list->add(
				object_ptr<Ui::LinkButton>(
					_list,
					tr::lng_owpengram_claude_panel_reconnect(tr::now),
					st::owpengramClaudePanelReconnect),
				style::margins(),
				style::al_left);
			button->setClickedCallback([=] {
				if (const auto model = weak.lock()) {
					model->reconnect();
				}
			});
		}
	}
	_list->resizeToWidth(_list->width());
}

} // namespace Owpengram::Claude
