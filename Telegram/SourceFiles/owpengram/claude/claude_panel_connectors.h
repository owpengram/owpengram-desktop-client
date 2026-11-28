/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/wrap/vertical_layout.h"

#include <memory>
#include <vector>

namespace Owpengram::Claude {

class PanelModel;
struct Connector;

// "Connectors" under the input field: MCP servers reported by claude's init
// ("name · status", "Reconnect" for failed ones) and the MCP clients that
// talk to the OwpenGram server right now.
class PanelConnectors final : public Ui::VerticalLayout {
public:
	PanelConnectors(QWidget *parent, std::shared_ptr<PanelModel> model);

private:
	void fill(const std::vector<Connector> &connectors);

	const std::shared_ptr<PanelModel> _model;
	const not_null<Ui::VerticalLayout*> _list;

};

} // namespace Owpengram::Claude
