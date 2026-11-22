/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/wrap/vertical_layout.h"

#include <memory>

namespace Owpengram::Claude {

class PanelModel;
struct FeedEntry;

// The conversation feed: user bubbles on the right, Claude's answers with
// markdown on the left, compact tool call lines, notices with actions and
// the "thinking" line. Follows PanelModel::appended().
class PanelMessages final : public Ui::VerticalLayout {
public:
	PanelMessages(QWidget *parent, std::shared_ptr<PanelModel> model);

private:
	void addEntry(const FeedEntry &entry);
	void addUser(const QString &text);
	void addAssistant(const QString &text);
	void addTool(const QString &text);
	void addNotice(const FeedEntry &entry);

	const std::shared_ptr<PanelModel> _model;
	const not_null<Ui::VerticalLayout*> _entries;
	rpl::variable<bool> _empty = true;

};

} // namespace Owpengram::Claude
