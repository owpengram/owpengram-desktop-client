/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/wrap/vertical_layout.h"

#include <memory>

namespace Ui {
class LinkButton;
template <typename Widget>
class SlideWrap;
} // namespace Ui

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
	// Tool lines of one reply go to a group, collapsed into "Used N tools"
	// once the reply (or anything else) follows.
	void finishToolGroup();
	// VerticalLayout does not resize a justified row it adds: fit them.
	void fitEntries();

	const std::shared_ptr<PanelModel> _model;
	const not_null<Ui::VerticalLayout*> _entries;
	rpl::variable<bool> _empty = true;
	Ui::VerticalLayout *_toolDetails = nullptr;
	Ui::SlideWrap<Ui::VerticalLayout> *_toolDetailsWrap = nullptr;
	Ui::SlideWrap<Ui::LinkButton> *_toolHeader = nullptr;
	int _toolCount = 0;

};

} // namespace Owpengram::Claude
