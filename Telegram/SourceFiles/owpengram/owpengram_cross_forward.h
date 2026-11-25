/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <memory>

class HistoryItem;

namespace Main {
class Account;
} // namespace Main

namespace Ui {
class PopupMenu;
class Show;
} // namespace Ui

namespace Owpengram {

// "Forward To >" in the message context menu: a copy of a message into a
// chat of ANOTHER signed-in account, usually on another server, where a
// real forward is impossible (the message does not exist there).
//
// The submenu lists the other accounts grouped by server. Picking one
// shows that account's chat picker; picking a chat sends the copy right
// away from that account, in the background, like a regular Forward: the
// user stays in the current account and gets a "Sent to ..." toast.
// The copy is the message text (formatting kept, custom emoji dropped
// since their ids are server-specific), then an empty line and a link to
// the original post when it has one (channels and supergroups:
// t.me/name/ID or t.me/c/ID/ID). Photos and files are downloaded in the
// source account if needed and sent as an album with that text as the
// caption (a text too long for a caption goes as its own message first).
//
// Adds nothing when there is no other signed-in account or the message
// can't be forwarded or saved. Returns whether the item was added.
bool AddForwardToAccountAction(
	not_null<Ui::PopupMenu*> menu,
	std::shared_ptr<Ui::Show> show,
	not_null<HistoryItem*> item);

// "Copyright Check >" in the message context menu: the same copy as
// "Forward To", but only for protected posts (forwarding and saving
// restricted by the channel) and only into accounts whose server marks them
// as copyright validators (owpengram_copyright_validator in the app config:
// an account linked to a server operator with the copyright.validate right).
// Lets a server's operators receive such a post to check who owns it.
//
// Adds nothing when the message is not protected or no other signed-in
// account is a validator. Returns whether the item was added.
bool AddCopyrightCheckAction(
	not_null<Ui::PopupMenu*> menu,
	std::shared_ptr<Ui::Show> show,
	not_null<HistoryItem*> item);

} // namespace Owpengram
