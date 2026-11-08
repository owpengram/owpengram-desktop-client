/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/core_types.h"

namespace MTP {
class Instance;
} // namespace MTP

namespace MTP::details {

// One app can talk to several servers, and their clocks can disagree: a
// self-hosted server without NTP is easily hours off. base::unixtime keeps a
// single process-wide shift, so whichever server synced last used to move
// every account's dates, "last seen" and new message times with it.
//
// Here every Instance keeps the shift its own server reported. That shift is
// used for the instance's msg_ids and anything the server checks against its
// clock (the server rejects msg_ids outside -300s..+30s of its time).
//
// The app-wide base::unixtime shift, used for what the user sees, is chosen
// across all of them:
//  1. a Telegram server, if there is one;
//  2. otherwise the biggest group of servers that agree with each other,
//     counting the local clock as one more vote, if at least two servers are
//     in it and the local clock is not;
//  3. otherwise the local clock.

// Telegram's own servers are trusted for the app-wide time.
void SetServerTimeTrusted(not_null<const Instance*> instance, bool trusted);

// A time the instance's server reported. Without force only the first
// report of an instance counts, like base::unixtime::update without force.
void ReportServerTime(
	not_null<const Instance*> instance,
	TimeId serverTime,
	bool force = false);

void ForgetServerTime(not_null<const Instance*> instance);

// "Now" by the instance's own server clock.
[[nodiscard]] TimeId ServerNow(not_null<const Instance*> instance);

// A fresh msg_id in the instance's own server time.
[[nodiscard]] mtpMsgId NextMsgId(not_null<const Instance*> instance);

} // namespace MTP::details
